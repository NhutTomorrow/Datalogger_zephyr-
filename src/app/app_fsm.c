/**
 * @file app_fsm.c
 * @brief Hiện thực máy trạng thái hữu hạn cho Datalogger công nghiệp trên Zephyr RTOS.
 */

#include "app_fsm.h"
#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/reboot.h>
#include "bsp_lcd.h"
#include "bsp_uart.h"
#include "bsp_button.h"
#include "bsp_dout.h"
#include "../services/channel_hw_caps.h"

#define BOOT_SCREEN_DURATION_MS 5000
#define ERROR_TOGGLE_INTERVAL_MS 2000
#define DIAGNOSTIC_AUTO_EXIT_MS 30000

/* Bảng hàm kết nối driver nút bấm 74HC165 với button_manager */
static const button_driver_ops_t s_button_ops = {
    .init           = bsp_button_74hc165_init,
    .read_raw_state = bsp_button_74hc165_read_single,
    .button_scan    = bsp_button_74hc165_read_all_raw,
};

/* ========================================================================= */
/* HÀM TRỢ GIÚP TRUYỀN GÓI UART                                              */
/* ========================================================================= */

static void app_send_uart_response(app_context_t *app,
                                   uint8_t cmd,
                                   uint8_t seq,
                                   const void *payload,
                                   uint16_t payload_len)
{
    ARG_UNUSED(app);
    uint8_t tx_frame[COMM_MAX_FRAME_LEN];
    uint16_t actual_len = 0;

    if (comm_packet_build(cmd, seq, payload, payload_len, tx_frame, sizeof(tx_frame), &actual_len))
    {
        bsp_uart_send_bytes(tx_frame, actual_len);
    }
}

/* ========================================================================= */
/* CALLBACK XỬ LÝ GÓI TIN UART TỪ ESP32 AI GATEWAY                          */
/* ========================================================================= */

static void on_uart_packet_received(const comm_packet_t *packet, void *user_ctx)
{
    app_context_t *app = (app_context_t *)user_ctx;
    if (app == NULL || packet == NULL)
        return;

    app->ai_connected = true;
    bsp_lcd_update_uart_packet_log(packet->cmd, packet->seq, packet->len);

    switch (packet->cmd)
    {
    case COMM_CMD_PING:
    {
        app_send_uart_response(app, COMM_CMD_PONG, packet->seq, NULL, 0);
        break;
    }

    case COMM_CMD_READ_CHANNEL:
    {
        if (packet->len < 1)
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_INVALID_PARAM};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
            break;
        }

        uint8_t ch_id = packet->payload[0];
        comm_payload_read_channel_resp_t resp;
        if (channel_manager_read_single(app->channel_mgr, ch_id, &resp))
        {
            app_send_uart_response(app, COMM_CMD_RESP_CHANNEL, packet->seq, &resp, sizeof(resp));
        }
        else
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_CHANNEL_NOT_FOUND};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
        }
        break;
    }

    case COMM_CMD_GET_FAULTS:
    {
        /* 1. Đóng gói danh sách mã lỗi trả lời cho AI */
        comm_payload_faults_t faults_payload;
        faults_payload.fault_count = fault_manager_get_list(app->fault_mgr, faults_payload.fault_codes, 8);
        app_send_uart_response(app, COMM_CMD_RESP_FAULTS, packet->seq, &faults_payload, sizeof(faults_payload));

        /* 2. CHUYỂN NGAY MÀN HÌNH SANG DIAGNOSTIC LIST */
        app->prev_state = app->state;
        app->state = FSM_STATE_DIAGNOSTIC;
        app->diagnostic_highlight = -1;
        app->diagnostic_timeout_tick = app->state_timer + DIAGNOSTIC_AUTO_EXIT_MS;

        bsp_lcd_show_fault_list(faults_payload.fault_codes, faults_payload.fault_count, -1);
        break;
    }

    case COMM_CMD_CLEAR_FAULTS:
    {
        fault_manager_clear_all(app->fault_mgr);
        if (app->state == FSM_STATE_ERROR || app->state == FSM_STATE_DIAGNOSTIC)
        {
            app->state = FSM_STATE_IDLE;
        }
        comm_payload_ack_t ack = {.acked_cmd = packet->cmd, .status = COMM_ERR_OK};
        app_send_uart_response(app, COMM_CMD_ACK, packet->seq, &ack, sizeof(ack));
        break;
    }

    case COMM_CMD_SET_LOGGING_MODE:
    {
        if (packet->len >= sizeof(comm_payload_logging_t))
        {
            const comm_payload_logging_t *log_cmd = (const comm_payload_logging_t *)packet->payload;
            app->logging_enabled = (log_cmd->enable != 0);
            if (log_cmd->sample_interval_ms > 0)
            {
                app->logging_interval_ms = log_cmd->sample_interval_ms;
            }
            if (app->state != FSM_STATE_ERROR)
            {
                app->state = app->logging_enabled ? FSM_STATE_LOGGING : FSM_STATE_IDLE;
            }
            comm_payload_ack_t ack = {.acked_cmd = packet->cmd, .status = COMM_ERR_OK};
            app_send_uart_response(app, COMM_CMD_ACK, packet->seq, &ack, sizeof(ack));
        }
        break;
    }

    case COMM_CMD_INJECT_FAULT_TEST:
    {
        if (packet->len >= sizeof(comm_payload_inject_fault_t))
        {
            const comm_payload_inject_fault_t *inj = (const comm_payload_inject_fault_t *)packet->payload;
            if (inj->action != 0)
            {
                fault_manager_set(app->fault_mgr, inj->fault_code);
            }
            else
            {
                fault_manager_clear(app->fault_mgr, inj->fault_code);
            }
            comm_payload_ack_t ack = {.acked_cmd = packet->cmd, .status = COMM_ERR_OK};
            app_send_uart_response(app, COMM_CMD_ACK, packet->seq, &ack, sizeof(ack));
        }
        break;
    }
    case COMM_CMD_SET_BACKLIGHT:
    {
        if (packet->len >= 1)
        {
            uint8_t enable = packet->payload[0];
            bsp_lcd_fsmc_set_backlight(enable != 0);

            /* Phản hồi ACK về cho ESP32 */
            comm_payload_ack_t ack = {.acked_cmd = packet->cmd, .status = COMM_ERR_OK};
            app_send_uart_response(app, COMM_CMD_ACK, packet->seq, &ack, sizeof(ack));
        }
        break;
    }
    case COMM_CMD_RESET_MCU:
    {
        /* 1. Gửi phản hồi ACK về cho ESP32 biết lệnh đã nhận thành công trước */
        comm_payload_ack_t ack = {.acked_cmd = packet->cmd, .status = COMM_ERR_OK};
        app_send_uart_response(app, COMM_CMD_ACK, packet->seq, &ack, sizeof(ack));
        /* 2. Delay nhỏ (50ms) để chân TX kịp đẩy hết các byte UART ACK sang ESP32 */
        k_msleep(50);
        /* 3. Kích hoạt Reset toàn bộ chip STM32 */
        sys_reboot(SYS_REBOOT_COLD);
        break;
    }

    case COMM_CMD_CONFIG_CHANNEL:
    {
        if (packet->len < sizeof(ChannelConfig_t))
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_INVALID_PARAM};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
            break;
        }

        ChannelConfig_t cfg;
        memcpy(&cfg, packet->payload, sizeof(ChannelConfig_t));

        int ret = channel_manager_configure(app->channel_mgr, &cfg, NULL, NULL);
        if (ret == 0)
        {
            comm_payload_ack_t ack = {.acked_cmd = packet->cmd, .status = COMM_ERR_OK};
            app_send_uart_response(app, COMM_CMD_ACK, packet->seq, &ack, sizeof(ack));
        }
        else
        {
            uint8_t err_code;
            switch (ret)
            {
            case -EBUSY:
                err_code = COMM_ERR_PIN_CONFLICT;
                break;
            case -ENOTSUP:
                err_code = COMM_ERR_UNSUPPORTED_MODE;
                break;
            case -EINVAL:
                err_code = COMM_ERR_INVALID_PARAM;
                break;
            case -ENOSPC:
                err_code = COMM_ERR_STORAGE_FAIL;
                break;
            default:
                err_code = COMM_ERR_HARDWARE_FAILURE;
                break;
            }
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = err_code};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
        }
        break;
    }

    case COMM_CMD_DISABLE_CHANNEL:
    {
        if (packet->len < 1)
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_INVALID_PARAM};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
            break;
        }

        uint8_t ch_id = packet->payload[0];
        int ret = channel_manager_disable(app->channel_mgr, ch_id);
        if (ret == 0)
        {
            comm_payload_ack_t ack = {.acked_cmd = packet->cmd, .status = COMM_ERR_OK};
            app_send_uart_response(app, COMM_CMD_ACK, packet->seq, &ack, sizeof(ack));
        }
        else
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_CHANNEL_NOT_FOUND};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
        }
        break;
    }

    case COMM_CMD_GET_CHANNEL_CONFIG:
    {
        if (packet->len < 1)
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_INVALID_PARAM};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
            break;
        }

        uint8_t ch_id = packet->payload[0];
        ChannelConfig_t cfg;
        int ret = channel_manager_get_channel_config(app->channel_mgr, ch_id, &cfg);
        if (ret == 0)
        {
            app_send_uart_response(app, COMM_CMD_RESP_CHANNEL_CONFIG, packet->seq, &cfg, sizeof(cfg));
        }
        else
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_CHANNEL_NOT_FOUND};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
        }
        break;
    }

    case COMM_CMD_GET_CONFIG_VERSION:
    {
        config_table_header_t hdr;
        channel_manager_get_config_table(app->channel_mgr, &hdr, NULL, 0);

        comm_payload_config_version_t resp;
        resp.config_version = hdr.config_version;
        resp.crc32 = hdr.crc32;
        resp.active_channels_count = channel_manager_get_active_count(app->channel_mgr);
        resp.is_dirty = (app->channel_mgr != NULL && app->channel_mgr->is_dirty) ? 1 : 0;

        app_send_uart_response(app, COMM_CMD_RESP_CONFIG_VERSION, packet->seq, &resp, sizeof(resp));
        break;
    }

    case COMM_CMD_FLUSH_CONFIG_NVS:
    {
        int ret = channel_manager_flush_nvs(app->channel_mgr);
        if (ret == 0)
        {
            comm_payload_ack_t ack = {.acked_cmd = packet->cmd, .status = COMM_ERR_OK};
            app_send_uart_response(app, COMM_CMD_ACK, packet->seq, &ack, sizeof(ack));
        }
        else
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_STORAGE_FAIL};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
        }
        break;
    }

    case COMM_CMD_FACTORY_RESET:
    {
        int ret = channel_manager_factory_reset(app->channel_mgr);
        if (ret == 0)
        {
            comm_payload_ack_t ack = {.acked_cmd = packet->cmd, .status = COMM_ERR_OK};
            app_send_uart_response(app, COMM_CMD_ACK, packet->seq, &ack, sizeof(ack));
        }
        else
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_STORAGE_FAIL};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
        }
        break;
    }

    case COMM_CMD_SET_OUTPUT:
    {
        if (packet->len < sizeof(comm_payload_set_output_t))
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_INVALID_PARAM};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
            break;
        }

        const comm_payload_set_output_t *cmd_out = (const comm_payload_set_output_t *)packet->payload;
        if (!channel_hw_caps_is_mode_supported(cmd_out->channel_id, MODE_OUT_CONTROL))
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_UNSUPPORTED_MODE};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
            break;
        }

        bsp_dout_set(cmd_out->channel_id, cmd_out->output_state);
        if (app->channel_mgr != NULL && cmd_out->channel_id >= 1 && cmd_out->channel_id <= CHANNEL_MAX_COUNT)
        {
            channel_runtime_t *ch = &app->channel_mgr->channels[cmd_out->channel_id - 1];
            ch->last_value = (float)cmd_out->output_state;
            ch->is_valid = true;
        }

        comm_payload_ack_t ack = {.acked_cmd = packet->cmd, .status = COMM_ERR_OK};
        app_send_uart_response(app, COMM_CMD_ACK, packet->seq, &ack, sizeof(ack));
        break;
    }

    case COMM_CMD_GET_OUTPUT_STATE:
    {
        if (packet->len < 1)
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_INVALID_PARAM};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
            break;
        }

        uint8_t ch_id = packet->payload[0];
        if (!channel_hw_caps_is_mode_supported(ch_id, MODE_OUT_CONTROL))
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_UNSUPPORTED_MODE};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
            break;
        }

        comm_payload_output_state_t resp;
        resp.channel_id = ch_id;
        resp.output_state = bsp_dout_get(ch_id);

        app_send_uart_response(app, COMM_CMD_RESP_OUTPUT_STATE, packet->seq, &resp, sizeof(resp));
        break;
    }

    case COMM_CMD_GET_DYNAMIC_TELEMETRY:
    {
        comm_payload_dynamic_telemetry_t telem;
        uint16_t actual_len = 0;
        uint8_t fault_count = fault_manager_get_count(app->fault_mgr);

        channel_manager_get_dynamic_telemetry(app->channel_mgr, fault_count, app->logging_enabled ? 1 : 0, &telem, &actual_len);
        app_send_uart_response(app, COMM_CMD_RESP_DYNAMIC_TELEMETRY, packet->seq, &telem, actual_len);
        break;
    }
    default:
    {
        comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_UNKNOWN_CMD};
        app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
        break;
    }
    }
}

/* ========================================================================= */
/* CALLBACK XỬ LÝ SỰ KIỆN NÚT BẤM (74HC165)                                 */
/* ========================================================================= */

static void on_button_event_received(button_id_t btn_id, button_event_t evt, void *user_data)
{
    app_context_t *app = (app_context_t *)user_data;
    if (app == NULL)
        return;

    if (evt == BUTTON_EVT_CLICK)
    {
        switch (btn_id)
        {
        case BTN_ID_1:
        {
            /* Trong chế độ Diagnostic: Nút 1 để duyệt chuyển highlight mã lỗi tiếp theo */
            if (app->state == FSM_STATE_DIAGNOSTIC)
            {
                uint16_t codes[8];
                uint8_t count = fault_manager_get_list(app->fault_mgr, codes, 8);
                if (count > 0)
                {
                    app->diagnostic_highlight = (app->diagnostic_highlight + 1) % count;
                    bsp_lcd_show_fault_list(codes, count, app->diagnostic_highlight);
                }
            }
            break;
        }
        case BTN_ID_2: //check những lỗi hiện có trên stm32 
        {


        }

        case BTN_ID_3:
        {
            /* Nút 3: Bật / Tắt chế độ Logging (IDLE <-> LOGGING) */
            if (app->state == FSM_STATE_IDLE)
            {
                app->logging_enabled = true;
                app->state = FSM_STATE_LOGGING;
            }
            else if (app->state == FSM_STATE_LOGGING)
            {
                app->logging_enabled = false;
                app->state = FSM_STATE_IDLE;
            }
            break;
        }

        case BTN_ID_4:
        {
          
          
        }

        default:
            break;
        }
    }
}

/* ========================================================================= */
/* API CÔNG KHAI CỦA FSM                                                     */
/* ========================================================================= */

void app_init(app_context_t *app, const app_dependencies_t *deps)
{
    if (app == NULL || deps == NULL)
        return;

    memset(app, 0, sizeof(app_context_t));

    app->state = FSM_STATE_INIT;
    app->prev_state = FSM_STATE_IDLE;
    app->fault_mgr = deps->fault_mgr;
    app->diagnostic_highlight = -1;
    app->ai_connected = false;

    /* Cấu hình Channel Manager & Logging state */
    app->channel_mgr = deps->channel_mgr;
    app->logging_enabled = false;
    app->logging_interval_ms = 1000;
    app->logged_records_count = 0;
    app->last_ui_update_tick = 0;

    /* Khởi tạo Button Manager trực tiếp với driver 74HC165 */
    button_manager_init(&app->button_mgr,
                        &s_button_ops,
                        deps->button_count,
                        on_button_event_received,
                        app);

    /* Khởi tạo UART Stream Parser */
    comm_parser_init(&app->uart_parser, on_uart_packet_received, app);

    /* Khởi tạo Màn hình LCD và hiển thị Boot Screen */
    lcd_init();
    bsp_lcd_show_boot_screen("DATALOGGER BKIT", "v1.0.0");
}

void app_run_step(app_context_t *app, uint32_t current_tick_ms)
{
    if (app == NULL)
        return;

    app->state_timer = current_tick_ms;

    /* 1. Xử lý các byte nhận từ UART trong Thread Context (lấy từ k_msgq) */
    uint8_t rx_byte;
    while (bsp_uart_get_byte(&rx_byte))
    {
        app_on_uart_byte_received(app, rx_byte);
    }

    /* 2. Cập nhật quét chống rung nút bấm (20ms định kỳ) */
    button_manager_tick(&app->button_mgr, current_tick_ms);

    /* 3. Thực thi State Machine chính */
    switch (app->state)
    {
    case FSM_STATE_INIT:
    {
        
        if (current_tick_ms >= BOOT_SCREEN_DURATION_MS)
        {
            /* Hết thời gian chào -> Chuyển sang IDLE và vẽ UI */
            app->state = FSM_STATE_IDLE;
            app->last_ui_update_tick = current_tick_ms;

            if (app->channel_mgr != NULL)
            {
                channel_snapshot_t snapshot;
                channel_manager_get_snapshot(app->channel_mgr, &snapshot);
                bsp_lcd_show_telemetry(&snapshot);
            }
            bsp_lcd_show_status_bar("IDLE", app->ai_connected ? "AI: OK" : "AI: --", 0);
        }
        break;
    }

    case FSM_STATE_IDLE:
    {
        /* Cập nhật UI & kiểm tra sức khỏe cảm biến định kỳ mỗi logging_interval_ms (mặc định 1s) */
        if (current_tick_ms - app->last_ui_update_tick >= app->logging_interval_ms)
        {
            app->last_ui_update_tick = current_tick_ms;

            /* 1. Tự động kiểm tra sức khỏe cảm biến */
            if (app->channel_mgr != NULL)
            {
                channel_manager_check_health(app->channel_mgr);
            }

            /* 2. Kiểm tra có lỗi nghiêm trọng phát sinh không */
            fault_severity_t max_sev = fault_manager_get_max_severity(app->fault_mgr);
            if (max_sev >= FAULT_SEV_CRITICAL)
            {
                app->state = FSM_STATE_ERROR;
                app->last_screen_toggle_tick = current_tick_ms;
                app->error_toggle_phase = true;
                bsp_lcd_show_error_banner_toggle();
                break;
            }

            /* 3. Cập nhật giao diện Telemetry và thanh trạng thái [IDLE] */
            if (app->channel_mgr != NULL)
            {
                channel_snapshot_t snapshot;
                channel_manager_get_snapshot(app->channel_mgr, &snapshot);
                bsp_lcd_show_telemetry(&snapshot);
            }
            uint8_t err_count = fault_manager_get_count(app->fault_mgr);
            bsp_lcd_show_status_bar("IDLE", app->ai_connected ? "AI: OK" : "AI: --", err_count);
        }
        break;
    }

    case FSM_STATE_LOGGING:
    {
        /* Chu kỳ cập nhật logging & UI */
        if (current_tick_ms - app->last_ui_update_tick >= app->logging_interval_ms)
        {
            app->last_ui_update_tick = current_tick_ms;

            /* Tăng đếm bản ghi nếu logging đang bật */
            if (app->logging_enabled)
            {
                app->logged_records_count++;
            }

            /* Tự động kiểm tra sức khỏe cảm biến */
            if (app->channel_mgr != NULL)
            {
                channel_manager_check_health(app->channel_mgr);
            }

            /* Kiểm tra có lỗi nghiêm trọng phát sinh không */
            fault_severity_t max_sev = fault_manager_get_max_severity(app->fault_mgr);
            if (max_sev >= FAULT_SEV_CRITICAL)
            {
                app->state = FSM_STATE_ERROR;
                app->last_screen_toggle_tick = current_tick_ms;
                app->error_toggle_phase = true;
                bsp_lcd_show_error_banner_toggle();
                break;
            }

            /* Cập nhật giao diện Telemetry bình thường */
            if (app->channel_mgr != NULL)
            {
                channel_snapshot_t snapshot;
                channel_manager_get_snapshot(app->channel_mgr, &snapshot);
                bsp_lcd_show_telemetry(&snapshot);
            }
            uint8_t err_count = fault_manager_get_count(app->fault_mgr);
            bsp_lcd_show_status_bar("LOGGING", app->ai_connected ? "AI: OK" : "AI: --", err_count);
        }
        break;
    }

    case FSM_STATE_ERROR:
    {
        /* Kiểm tra định kỳ xem lỗi đã hết chưa */
        if (current_tick_ms - app->last_ui_update_tick >= app->logging_interval_ms)
        {
            app->last_ui_update_tick = current_tick_ms;
            if (app->channel_mgr != NULL)
            {
                channel_manager_check_health(app->channel_mgr);
            }
        }

        /* Kiểm tra xem lỗi đã hết chưa */
        fault_severity_t max_sev = fault_manager_get_max_severity(app->fault_mgr);
        if (max_sev < FAULT_SEV_CRITICAL)
        {
            app->state = FSM_STATE_IDLE;
            break;
        }

        /* MÀN HÌNH CHỚP XEN KẼ MỖI 2 GIÂY:
         * Phase true:  Hiện "ERROR - VUI LONG HOI AI"
         * Phase false: Hiện Dashboard Telemetry (Status bar màu đỏ) */
        if (current_tick_ms - app->last_screen_toggle_tick >= ERROR_TOGGLE_INTERVAL_MS)
        {
            app->last_screen_toggle_tick = current_tick_ms;
            app->error_toggle_phase = !app->error_toggle_phase;

            if (app->error_toggle_phase)
            {
                bsp_lcd_show_error_banner_toggle();
            }
            else
            {
                if (app->channel_mgr != NULL)
                {
                    channel_snapshot_t snapshot;
                    channel_manager_get_snapshot(app->channel_mgr, &snapshot);
                    bsp_lcd_show_telemetry(&snapshot);
                }
                uint8_t count = fault_manager_get_count(app->fault_mgr);
                bsp_lcd_show_status_bar("ERROR", app->ai_connected ? "AI: OK" : "AI: --", count);
            }
        }
        break;
    }

    case FSM_STATE_DIAGNOSTIC:
    {
        /* Định kỳ kiểm tra sức khỏe cảm biến */
        if (current_tick_ms - app->last_ui_update_tick >= app->logging_interval_ms)
        {
            app->last_ui_update_tick = current_tick_ms;
            if (app->channel_mgr != NULL)
            {
                channel_manager_check_health(app->channel_mgr);
            }
        }

        /* Hết thời gian chờ (15s) -> Tự động quay về màn hình trước */
        if (current_tick_ms >= app->diagnostic_timeout_tick)
        {
            fault_severity_t sev = fault_manager_get_max_severity(app->fault_mgr);
            app->state = (sev >= FAULT_SEV_CRITICAL) ? FSM_STATE_ERROR : FSM_STATE_IDLE;
        }
        break;
    }

    default:
        app->state = FSM_STATE_IDLE;
        break;
    }
}

void app_on_uart_byte_received(app_context_t *app, uint8_t byte)
{
    if (app == NULL)
        return;
    bsp_lcd_on_uart_byte_received(byte);
    comm_parser_feed_byte(&app->uart_parser, byte);
}

const char *app_fsm_state_to_str(fsm_state_t state)
{
    switch (state)
    {
    case FSM_STATE_INIT:
        return "INIT";
    case FSM_STATE_IDLE:
        return "IDLE";
    case FSM_STATE_LOGGING:
        return "LOGGING";
    case FSM_STATE_ERROR:
        return "ERROR";
    case FSM_STATE_DIAGNOSTIC:
        return "DIAGNOSTIC";
    default:
        return "UNKNOWN";
    }
}
