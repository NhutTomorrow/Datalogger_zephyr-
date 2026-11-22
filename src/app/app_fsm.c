/**
 * @file app_fsm.c
 * @brief Hiện thực máy trạng thái hữu hạn cho Datalogger công nghiệp trên Zephyr RTOS.
 */

#include "app_fsm.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
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

    case COMM_CMD_CONFIG_PROPOSAL:
    {
        if (packet->len < sizeof(ChannelConfig_t))
        {
            comm_payload_nack_t nack = {.failed_cmd = packet->cmd, .error_code = COMM_ERR_INVALID_PARAM};
            app_send_uart_response(app, COMM_CMD_NACK, packet->seq, &nack, sizeof(nack));
            break;
        }

        /* Lưu trữ cấu hình AI đề xuất vào bộ đệm chờ người dùng duyệt */
        memcpy(&app->ai_proposal_cfg, packet->payload, sizeof(ChannelConfig_t));
        app->ai_proposal_seq = packet->seq;
        app->ai_proposal_pending = true;

        /* Lưu lại trạng thái trước khi mở Popup (nếu không phải Screensaver/Init) */
        if (app->state != FSM_STATE_SCREENSAVER_SLEEP && app->state != FSM_STATE_INIT) {
            app->pre_popup_state = app->state;
        } else {
            app->pre_popup_state = FSM_STATE_HOME_DASHBOARD;
        }

        /* Đánh thức màn hình nếu đang ngủ */
        if (app->state == FSM_STATE_SCREENSAVER_SLEEP) {
            bsp_lcd_fsmc_set_backlight(true);
        }

        app->state = FSM_STATE_AI_PROPOSAL;
        app->last_user_activity_tick = app->state_timer;
        app->ui_dirty = true;
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
/* CẤU TRÚC DỮ LIỆU CÂY MENU VÀ BẢNG LỰA CHỌN TĨNH                           */
/* ========================================================================= */

static const char *s_main_menu_items[] = {
    "CHANNEL CONFIG",
    "CLOUD & UPLINK",
    "LOGGING & STORAGE",
    "COMMUNICATION",
    "ALARM & THRESHOLD",
    "SYSTEM & RTC",
    "DIAGNOSTIC & I/O"
};
#define MAIN_MENU_COUNT ((uint8_t)(sizeof(s_main_menu_items) / sizeof(s_main_menu_items[0])))
static uint8_t s_main_menu_selected_idx = 0;

static const char *s_cloud_uplink_items[] = {
    "Giao thuc [MQTT]",
    "Broker Host/IP",
    "Cong Server (Port)",
    "Chu ky Uplink (s)"
};
#define CLOUD_UPLINK_COUNT ((uint8_t)(sizeof(s_cloud_uplink_items) / sizeof(s_cloud_uplink_items[0])))

static const char *s_ch_list_items[] = {
    "Kenh 1 (LM35 - PC2)",
    "Kenh 2 (LDR - PC0)",
    "Kenh 3 (Power - PB0)",
    "Kenh 4 (ACS712 - PB1)",
    "Kenh 5 (VR1 - PC1)",
    "Kenh 6 (Header P11-3)",
    "Kenh 7 (Header P11-1)",
    "Kenh 8 (RS485 Modbus)"
};
#define CH_LIST_COUNT ((uint8_t)(sizeof(s_ch_list_items) / sizeof(s_ch_list_items[0])))

static const char *s_ch_params_items[] = {
    "Kich hoat (Enable)",
    "Che do giao tiep",
    "Dai luong do",
    "Don vi do",
    "He so Scale A",
    "Do lech Offset B",
    "Chu ky lay mau (ms)"
};
#define CH_PARAMS_COUNT ((uint8_t)(sizeof(s_ch_params_items) / sizeof(s_ch_params_items[0])))

static const char *s_log_storage_items[] = {
    "Bat/Tat Ghi [REC/STOP]",
    "Chu ky ghi Log (ms)"
};
#define LOG_STORAGE_COUNT ((uint8_t)(sizeof(s_log_storage_items) / sizeof(s_log_storage_items[0])))

static const char *s_comm_items[] = {
    "Baudrate RS485 Modbus",
    "Baudrate UART ESP32 (115200)"
};
#define COMM_COUNT ((uint8_t)(sizeof(s_comm_items) / sizeof(s_comm_items[0])))

static const char *s_alarm_items[] = {
    "Canh bao dut vong 4-20mA",
    "Nguong canh bao qua dong"
};
#define ALARM_COUNT ((uint8_t)(sizeof(s_alarm_items) / sizeof(s_alarm_items[0])))

static const char *s_sys_items[] = {
    "Doi ma PIN he thong",
    "Thoi gian tat den man hinh",
    "Khoi dong lai he thong"
};
#define SYS_COUNT ((uint8_t)(sizeof(s_sys_items) / sizeof(s_sys_items[0])))

/* Các bảng lựa chọn ENUM tĩnh */
static const char *s_enable_opts[] = { "DISABLE", "ENABLE" };
#define ENABLE_OPTS_COUNT 2

static const char *s_mode_opts[] = {
    "DISABLED", "ADC_SINGLE", "DIGITAL_IN", "PULSE_COUNT",
    "OUT_CONTROL", "I2C", "RS485", "RS232", "UART"
};
#define MODE_OPTS_COUNT 9

static const char *s_metric_opts[] = {
    "NONE", "TEMPERATURE", "HUMIDITY", "CURRENT_4_20MA",
    "VOLTAGE", "PH", "SOIL_MOISTURE", "PRESSURE", "PULSE_COUNT", "DIGITAL"
};
#define METRIC_OPTS_COUNT 10

static const char *s_unit_opts[] = {
    "degC", "%RH", "mA", "V", "pH", "Bar", "ppm", "RAW"
};
#define UNIT_OPTS_COUNT 8

static const char *s_baud_opts[] = { "9600", "19200", "38400", "115200" };
#define BAUD_OPTS_COUNT 4

/* ========================================================================= */
/* BỘ VẼ GIAO DIỆN THEO CỜ BẨN (DIRTY FLAG RENDER DISPATCHER)                */
/* ========================================================================= */

static void render_current_screen(app_context_t *app)
{
    if (app == NULL) return;

    switch (app->state)
    {
    case FSM_STATE_HOME_DASHBOARD:
    {
        sensor_telemetry_snapshot_t snapshot;
        memset(&snapshot, 0, sizeof(snapshot));
        if (app->channel_mgr != NULL) {
            channel_manager_get_snapshot(app->channel_mgr, &snapshot);
        }
        uint8_t fault_count = fault_manager_get_count(app->fault_mgr);
        bsp_lcd_show_dashboard_paged(&snapshot,
                                     app->dashboard_page,
                                     app->logging_enabled,
                                     app->ai_connected ? "AI:OK" : "AI:--",
                                     fault_count);
        break;
    }

    case FSM_STATE_AUTH_LOGIN:
    {
        bsp_lcd_show_auth_screen(app->input_buffer, '\0', true);
        break;
    }

    case FSM_STATE_MAIN_MENU:
    {
        bsp_lcd_show_menu("MAIN MENU HE THONG", s_main_menu_items, MAIN_MENU_COUNT, s_main_menu_selected_idx);
        break;
    }

    case FSM_STATE_SUBMENU_LIST:
    {
        switch (app->active_submenu)
        {
        case SUBMENU_CHANNEL_LIST:
            bsp_lcd_show_menu("CHON KENH CAU HINH", s_ch_list_items, CH_LIST_COUNT, app->submenu_selected_idx);
            break;
        case SUBMENU_CHANNEL_PARAMS:
        {
            char title[32];
            snprintf(title, sizeof(title), "THAM SO KENH %u", app->selected_channel_id);
            bsp_lcd_show_menu(title, s_ch_params_items, CH_PARAMS_COUNT, app->submenu_selected_idx);
            break;
        }
        case SUBMENU_CLOUD_UPLINK:
            bsp_lcd_show_menu("CLOUD & UPLINK", s_cloud_uplink_items, CLOUD_UPLINK_COUNT, app->submenu_selected_idx);
            break;
        case SUBMENU_LOGGING_STORAGE:
            bsp_lcd_show_menu("LOGGING & STORAGE", s_log_storage_items, LOG_STORAGE_COUNT, app->submenu_selected_idx);
            break;
        case SUBMENU_COMMUNICATION:
            bsp_lcd_show_menu("COMMUNICATION", s_comm_items, COMM_COUNT, app->submenu_selected_idx);
            break;
        case SUBMENU_ALARM_THRESHOLD:
            bsp_lcd_show_menu("ALARM & THRESHOLD", s_alarm_items, ALARM_COUNT, app->submenu_selected_idx);
            break;
        case SUBMENU_SYSTEM_RTC:
            bsp_lcd_show_menu("SYSTEM & RTC", s_sys_items, SYS_COUNT, app->submenu_selected_idx);
            break;
        default:
            break;
        }
        break;
    }

    case FSM_STATE_EDIT_ENUM:
    {
        const char *name = "";
        const char *opt = "";
        uint8_t total = 1;

        switch (app->edit_target)
        {
        case EDIT_TARGET_CH_ENABLED:
            name = "Kich hoat Kenh";
            total = ENABLE_OPTS_COUNT;
            opt = s_enable_opts[app->edit_enum_idx % total];
            break;
        case EDIT_TARGET_CH_MODE:
            name = "Che do giao tiep";
            total = MODE_OPTS_COUNT;
            opt = s_mode_opts[app->edit_enum_idx % total];
            break;
        case EDIT_TARGET_CH_METRIC:
            name = "Dai luong do";
            total = METRIC_OPTS_COUNT;
            opt = s_metric_opts[app->edit_enum_idx % total];
            break;
        case EDIT_TARGET_CH_UNIT:
            name = "Don vi do";
            total = UNIT_OPTS_COUNT;
            opt = s_unit_opts[app->edit_enum_idx % total];
            break;
        case EDIT_TARGET_LOG_ENABLE:
            name = "Ghi du lieu Log";
            total = ENABLE_OPTS_COUNT;
            opt = s_enable_opts[app->edit_enum_idx % total];
            break;
        case EDIT_TARGET_RS485_BAUD:
            name = "Baudrate Modbus";
            total = BAUD_OPTS_COUNT;
            opt = s_baud_opts[app->edit_enum_idx % total];
            break;
        default:
            break;
        }
        bsp_lcd_show_edit_enum(name, opt, app->edit_enum_idx, total);
        break;
    }

    case FSM_STATE_EDIT_NUMERIC:
    {
        const char *name = "";
        const char *unit = "";
        switch (app->edit_target)
        {
        case EDIT_TARGET_CH_SCALE_A:
            name = "He so Scale A";
            unit = "";
            break;
        case EDIT_TARGET_CH_OFFSET_B:
            name = "Do lech Offset B";
            unit = "";
            break;
        case EDIT_TARGET_CH_INTERVAL:
            name = "Chu ky doc kenh";
            unit = "ms";
            break;
        case EDIT_TARGET_LOG_INTERVAL:
            name = "Chu ky ghi log";
            unit = "ms";
            break;
        default:
            break;
        }
        bsp_lcd_show_edit_numeric(name, app->input_buffer, unit);
        break;
    }

    case FSM_STATE_SAVE_CONFIRM:
    {
        bsp_lcd_show_save_confirm();
        break;
    }

    case FSM_STATE_DIAGNOSTICS_LIVE:
    {
        uint16_t codes[8];
        uint8_t count = fault_manager_get_list(app->fault_mgr, codes, 8);
        bsp_lcd_show_fault_list(codes, count, app->diagnostic_highlight);
        break;
    }

    case FSM_STATE_ALARM_POPUP:
    {
        bsp_lcd_show_error_banner_toggle();
        break;
    }

    case FSM_STATE_AI_PROPOSAL:
    {
        bsp_lcd_show_ai_proposal(
            app->ai_proposal_cfg.channel_id,
            s_mode_opts[(app->ai_proposal_cfg.interface_mode < MODE_OPTS_COUNT) ? app->ai_proposal_cfg.interface_mode : 0],
            s_metric_opts[(app->ai_proposal_cfg.metric_type < METRIC_OPTS_COUNT) ? app->ai_proposal_cfg.metric_type : 0]
        );
        break;
    }

    default:
        break;
    }
}

/* ========================================================================= */
/* CALLBACKS BÀN PHÍM (KEYPAD MULTITAP HANDLERS)                             */
/* ========================================================================= */

static void on_keypad_commit_handler(char committed_char, const char *current_str, uint8_t cursor_pos, void *user_data)
{
    (void)committed_char;
    (void)current_str;
    (void)cursor_pos;
    app_context_t *app = (app_context_t *)user_data;
    if (app == NULL) return;
    app->ui_dirty = true;
}

static void on_keypad_preview_handler(char preview_char, uint8_t cursor_pos, void *user_data)
{
    (void)cursor_pos;
    app_context_t *app = (app_context_t *)user_data;
    if (app == NULL) return;

    if (app->state == FSM_STATE_AUTH_LOGIN) {
        bsp_lcd_show_auth_screen(app->input_buffer, preview_char, true);
    }
}

static void on_keypad_backspace_handler(const char *current_str, uint8_t cursor_pos, void *user_data)
{
    (void)current_str;
    (void)cursor_pos;
    app_context_t *app = (app_context_t *)user_data;
    if (app == NULL) return;
    app->ui_dirty = true;
}

static void on_keypad_nav_handler(keypad_key_t key, button_event_t evt, void *user_data)
{
    app_context_t *app = (app_context_t *)user_data;
    if (app == NULL || evt != BUTTON_EVT_CLICK) return;

    /* Ghi nhận hoạt động người dùng để reset Inactivity và Screensaver */
    app->last_user_activity_tick = app->state_timer;

    /* 1. Xử lý đánh thức màn hình (Wake-up Guard) */
    if (app->state == FSM_STATE_SCREENSAVER_SLEEP)
    {
        bsp_lcd_fsmc_set_backlight(true);
        app->state = FSM_STATE_HOME_DASHBOARD;
        app->ui_dirty = true;
        return; /* Nuốt phím đầu tiên */
    }

    /* 2. Điều phối theo máy trạng thái */
    switch (app->state)
    {
    case FSM_STATE_HOME_DASHBOARD:
    {
        /* Phím UP/DOWN: Cuộn trang kênh */
        if (key == KEYPAD_KEY_UP)
        {
            if (app->dashboard_page > 0) app->dashboard_page--;
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_DOWN)
        {
            app->dashboard_page = (app->dashboard_page + 1) % 2; /* 2 trang (8 kênh) */
            app->ui_dirty = true;
        }
        /* Phím 3: Bật / Tắt Logging */
        else if (key == KEYPAD_KEY_3)
        {
            app->logging_enabled = !app->logging_enabled;
            app->ui_dirty = true;
        }
        /* Phím ENTER: Vào màn hình đăng nhập PIN */
        else if (key == KEYPAD_KEY_ENTER)
        {
            app->prev_state = app->state;
            app->state = FSM_STATE_AUTH_LOGIN;
            memset(app->input_buffer, 0, sizeof(app->input_buffer));
            keypad_multitap_bind_buffer(&app->keypad, app->input_buffer, sizeof(app->input_buffer));
            keypad_multitap_set_mode(&app->keypad, KEYPAD_MODE_NUMERIC);
            app->ui_dirty = true;
        }
        break;
    }

    case FSM_STATE_AUTH_LOGIN:
    {
        if (key == KEYPAD_KEY_BACK)
        {
            keypad_multitap_abort(&app->keypad);
            keypad_multitap_set_mode(&app->keypad, KEYPAD_MODE_NAV);
            app->state = FSM_STATE_HOME_DASHBOARD;
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_ENTER)
        {
            /* Kiểm tra mã PIN (mặc định "1234" hoặc >= 4 ký tự) */
            if (strcmp(app->input_buffer, "1234") == 0 || strlen(app->input_buffer) >= 4)
            {
                app->state = FSM_STATE_MAIN_MENU;
                s_main_menu_selected_idx = 0;
                keypad_multitap_set_mode(&app->keypad, KEYPAD_MODE_NAV);
                app->ui_dirty = true;
            }
            else
            {
                memset(app->input_buffer, 0, sizeof(app->input_buffer));
                keypad_multitap_bind_buffer(&app->keypad, app->input_buffer, sizeof(app->input_buffer));
                bsp_lcd_show_auth_screen("SAI MA PIN!", '\0', false);
            }
        }
        break;
    }

    case FSM_STATE_MAIN_MENU:
    {
        if (key == KEYPAD_KEY_UP)
        {
            if (s_main_menu_selected_idx > 0) s_main_menu_selected_idx--;
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_DOWN)
        {
            if (s_main_menu_selected_idx < MAIN_MENU_COUNT - 1) s_main_menu_selected_idx++;
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_BACK)
        {
            if (app->config_dirty) {
                app->state = FSM_STATE_SAVE_CONFIRM;
            } else {
                app->state = FSM_STATE_HOME_DASHBOARD;
            }
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_ENTER)
        {
            switch (s_main_menu_selected_idx)
            {
            case 0: /* Channel Config */
                app->active_submenu = SUBMENU_CHANNEL_LIST;
                app->submenu_selected_idx = 0;
                app->state = FSM_STATE_SUBMENU_LIST;
                break;
            case 1: /* Cloud & Uplink */
                app->active_submenu = SUBMENU_CLOUD_UPLINK;
                app->submenu_selected_idx = 0;
                app->state = FSM_STATE_SUBMENU_LIST;
                break;
            case 2: /* Logging & Storage */
                app->active_submenu = SUBMENU_LOGGING_STORAGE;
                app->submenu_selected_idx = 0;
                app->state = FSM_STATE_SUBMENU_LIST;
                break;
            case 3: /* Communication */
                app->active_submenu = SUBMENU_COMMUNICATION;
                app->submenu_selected_idx = 0;
                app->state = FSM_STATE_SUBMENU_LIST;
                break;
            case 4: /* Alarm & Threshold */
                app->active_submenu = SUBMENU_ALARM_THRESHOLD;
                app->submenu_selected_idx = 0;
                app->state = FSM_STATE_SUBMENU_LIST;
                break;
            case 5: /* System & RTC */
                app->active_submenu = SUBMENU_SYSTEM_RTC;
                app->submenu_selected_idx = 0;
                app->state = FSM_STATE_SUBMENU_LIST;
                break;
            case 6: /* Diagnostics / I/O */
                app->prev_state = FSM_STATE_MAIN_MENU;
                app->state = FSM_STATE_DIAGNOSTICS_LIVE;
                app->diagnostic_timeout_tick = app->state_timer + 15000;
                app->diagnostic_highlight = 0;
                break;
            default:
                break;
            }
            app->ui_dirty = true;
        }
        break;
    }

    case FSM_STATE_SUBMENU_LIST:
    {
        uint8_t total_items = 1;
        if (app->active_submenu == SUBMENU_CHANNEL_LIST) total_items = CH_LIST_COUNT;
        else if (app->active_submenu == SUBMENU_CHANNEL_PARAMS) total_items = CH_PARAMS_COUNT;
        else if (app->active_submenu == SUBMENU_CLOUD_UPLINK) total_items = CLOUD_UPLINK_COUNT;
        else if (app->active_submenu == SUBMENU_LOGGING_STORAGE) total_items = LOG_STORAGE_COUNT;
        else if (app->active_submenu == SUBMENU_COMMUNICATION) total_items = COMM_COUNT;
        else if (app->active_submenu == SUBMENU_ALARM_THRESHOLD) total_items = ALARM_COUNT;
        else if (app->active_submenu == SUBMENU_SYSTEM_RTC) total_items = SYS_COUNT;

        if (key == KEYPAD_KEY_UP)
        {
            if (app->submenu_selected_idx > 0) app->submenu_selected_idx--;
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_DOWN)
        {
            if (app->submenu_selected_idx < total_items - 1) app->submenu_selected_idx++;
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_BACK)
        {
            if (app->active_submenu == SUBMENU_CHANNEL_PARAMS)
            {
                /* Quay lại danh sách kênh */
                app->active_submenu = SUBMENU_CHANNEL_LIST;
                app->submenu_selected_idx = app->selected_channel_id - 1;
            }
            else
            {
                /* Quay lại Menu chính */
                app->state = FSM_STATE_MAIN_MENU;
            }
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_ENTER)
        {
            if (app->active_submenu == SUBMENU_CHANNEL_LIST)
            {
                app->selected_channel_id = app->submenu_selected_idx + 1;
                if (app->channel_mgr != NULL) {
                    channel_manager_get_channel_config(app->channel_mgr, app->selected_channel_id, &app->staging_cfg);
                }
                app->active_submenu = SUBMENU_CHANNEL_PARAMS;
                app->submenu_selected_idx = 0;
                app->ui_dirty = true;
            }
            else if (app->active_submenu == SUBMENU_CHANNEL_PARAMS)
            {
                switch (app->submenu_selected_idx)
                {
                case 0: /* Enable */
                    app->edit_target = EDIT_TARGET_CH_ENABLED;
                    app->edit_enum_idx = app->staging_cfg.is_enabled;
                    app->state = FSM_STATE_EDIT_ENUM;
                    break;
                case 1: /* Mode */
                    app->edit_target = EDIT_TARGET_CH_MODE;
                    app->edit_enum_idx = app->staging_cfg.interface_mode;
                    app->state = FSM_STATE_EDIT_ENUM;
                    break;
                case 2: /* Metric */
                    app->edit_target = EDIT_TARGET_CH_METRIC;
                    app->edit_enum_idx = app->staging_cfg.metric_type;
                    app->state = FSM_STATE_EDIT_ENUM;
                    break;
                case 3: /* Unit */
                    app->edit_target = EDIT_TARGET_CH_UNIT;
                    app->edit_enum_idx = app->staging_cfg.unit_type;
                    app->state = FSM_STATE_EDIT_ENUM;
                    break;
                case 4: /* Scale A */
                    app->edit_target = EDIT_TARGET_CH_SCALE_A;
                    snprintf(app->input_buffer, sizeof(app->input_buffer), "%.2f", (double)app->staging_cfg.calib.adc.scale_a);
                    keypad_multitap_bind_buffer(&app->keypad, app->input_buffer, sizeof(app->input_buffer));
                    keypad_multitap_set_mode(&app->keypad, KEYPAD_MODE_NUMERIC);
                    app->state = FSM_STATE_EDIT_NUMERIC;
                    break;
                case 5: /* Offset B */
                    app->edit_target = EDIT_TARGET_CH_OFFSET_B;
                    snprintf(app->input_buffer, sizeof(app->input_buffer), "%.2f", (double)app->staging_cfg.calib.adc.offset_b);
                    keypad_multitap_bind_buffer(&app->keypad, app->input_buffer, sizeof(app->input_buffer));
                    keypad_multitap_set_mode(&app->keypad, KEYPAD_MODE_NUMERIC);
                    app->state = FSM_STATE_EDIT_NUMERIC;
                    break;
                case 6: /* Interval */
                    app->edit_target = EDIT_TARGET_CH_INTERVAL;
                    snprintf(app->input_buffer, sizeof(app->input_buffer), "%u", app->staging_cfg.sample_interval_ms);
                    keypad_multitap_bind_buffer(&app->keypad, app->input_buffer, sizeof(app->input_buffer));
                    keypad_multitap_set_mode(&app->keypad, KEYPAD_MODE_NUMERIC);
                    app->state = FSM_STATE_EDIT_NUMERIC;
                    break;
                default:
                    break;
                }
                app->ui_dirty = true;
            }
            else if (app->active_submenu == SUBMENU_LOGGING_STORAGE)
            {
                if (app->submenu_selected_idx == 0) {
                    app->edit_target = EDIT_TARGET_LOG_ENABLE;
                    app->edit_enum_idx = app->logging_enabled ? 1 : 0;
                    app->state = FSM_STATE_EDIT_ENUM;
                } else {
                    app->edit_target = EDIT_TARGET_LOG_INTERVAL;
                    snprintf(app->input_buffer, sizeof(app->input_buffer), "%u", app->logging_interval_ms);
                    keypad_multitap_bind_buffer(&app->keypad, app->input_buffer, sizeof(app->input_buffer));
                    keypad_multitap_set_mode(&app->keypad, KEYPAD_MODE_NUMERIC);
                    app->state = FSM_STATE_EDIT_NUMERIC;
                }
                app->ui_dirty = true;
            }
            else if (app->active_submenu == SUBMENU_COMMUNICATION)
            {
                if (app->submenu_selected_idx == 0) {
                    app->edit_target = EDIT_TARGET_RS485_BAUD;
                    app->edit_enum_idx = 0; /* Default 9600 */
                    app->state = FSM_STATE_EDIT_ENUM;
                    app->ui_dirty = true;
                }
            }
            else if (app->active_submenu == SUBMENU_SYSTEM_RTC)
            {
                if (app->submenu_selected_idx == 2) {
                    /* Reboot MCU */
                    k_msleep(50);
                    sys_reboot(SYS_REBOOT_COLD);
                }
            }
        }
        break;
    }

    case FSM_STATE_EDIT_ENUM:
    {
        uint8_t total_opts = 1;
        switch (app->edit_target) {
        case EDIT_TARGET_CH_ENABLED:
        case EDIT_TARGET_LOG_ENABLE:
            total_opts = ENABLE_OPTS_COUNT;
            break;
        case EDIT_TARGET_CH_MODE:
            total_opts = MODE_OPTS_COUNT;
            break;
        case EDIT_TARGET_CH_METRIC:
            total_opts = METRIC_OPTS_COUNT;
            break;
        case EDIT_TARGET_CH_UNIT:
            total_opts = UNIT_OPTS_COUNT;
            break;
        case EDIT_TARGET_RS485_BAUD:
            total_opts = BAUD_OPTS_COUNT;
            break;
        default:
            break;
        }

        if (key == KEYPAD_KEY_UP)
        {
            app->edit_enum_idx = (app->edit_enum_idx > 0) ? (app->edit_enum_idx - 1) : (total_opts - 1);
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_DOWN)
        {
            app->edit_enum_idx = (app->edit_enum_idx + 1) % total_opts;
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_BACK)
        {
            app->state = FSM_STATE_SUBMENU_LIST;
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_ENTER)
        {
            /* Áp dụng giá trị sửa đổi */
            switch (app->edit_target)
            {
            case EDIT_TARGET_CH_ENABLED:
                app->staging_cfg.is_enabled = app->edit_enum_idx;
                break;
            case EDIT_TARGET_CH_MODE:
                app->staging_cfg.interface_mode = app->edit_enum_idx;
                break;
            case EDIT_TARGET_CH_METRIC:
                app->staging_cfg.metric_type = app->edit_enum_idx;
                break;
            case EDIT_TARGET_CH_UNIT:
                app->staging_cfg.unit_type = app->edit_enum_idx;
                break;
            case EDIT_TARGET_LOG_ENABLE:
                app->logging_enabled = (app->edit_enum_idx != 0);
                break;
            default:
                break;
            }

            if (app->edit_target >= EDIT_TARGET_CH_ENABLED && app->edit_target <= EDIT_TARGET_CH_INTERVAL) {
                if (app->channel_mgr != NULL) {
                    channel_manager_configure(app->channel_mgr, &app->staging_cfg, NULL, NULL);
                }
            }

            app->config_dirty = true;
            app->state = FSM_STATE_SUBMENU_LIST;
            app->ui_dirty = true;
        }
        break;
    }

    case FSM_STATE_EDIT_NUMERIC:
    {
        if (key == KEYPAD_KEY_BACK)
        {
            keypad_multitap_abort(&app->keypad);
            keypad_multitap_set_mode(&app->keypad, KEYPAD_MODE_NAV);
            app->state = FSM_STATE_SUBMENU_LIST;
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_ENTER)
        {
            double val = strtod(app->input_buffer, NULL);
            switch (app->edit_target)
            {
            case EDIT_TARGET_CH_SCALE_A:
                app->staging_cfg.calib.adc.scale_a = (float)val;
                break;
            case EDIT_TARGET_CH_OFFSET_B:
                app->staging_cfg.calib.adc.offset_b = (float)val;
                break;
            case EDIT_TARGET_CH_INTERVAL:
                app->staging_cfg.sample_interval_ms = (uint16_t)val;
                break;
            case EDIT_TARGET_LOG_INTERVAL:
                app->logging_interval_ms = (uint16_t)val;
                break;
            default:
                break;
            }

            if (app->edit_target >= EDIT_TARGET_CH_ENABLED && app->edit_target <= EDIT_TARGET_CH_INTERVAL) {
                if (app->channel_mgr != NULL) {
                    channel_manager_configure(app->channel_mgr, &app->staging_cfg, NULL, NULL);
                }
            }

            app->config_dirty = true;
            keypad_multitap_set_mode(&app->keypad, KEYPAD_MODE_NAV);
            app->state = FSM_STATE_SUBMENU_LIST;
            app->ui_dirty = true;
        }
        break;
    }

    case FSM_STATE_SAVE_CONFIRM:
    {
        if (key == KEYPAD_KEY_ENTER)
        {
            /* Ghi Flash qua Rolling Slot của Channel Manager */
            if (app->channel_mgr != NULL) {
                channel_manager_flush_nvs(app->channel_mgr);
            }
            app->config_dirty = false;
            app->state = FSM_STATE_HOME_DASHBOARD;
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_BACK)
        {
            /* Hủy thay đổi */
            app->config_dirty = false;
            app->state = FSM_STATE_HOME_DASHBOARD;
            app->ui_dirty = true;
        }
        break;
    }

    case FSM_STATE_DIAGNOSTICS_LIVE:
    {
        if (key == KEYPAD_KEY_UP || key == KEYPAD_KEY_1)
        {
            uint16_t codes[8];
            uint8_t count = fault_manager_get_list(app->fault_mgr, codes, 8);
            if (count > 0)
            {
                app->diagnostic_highlight = (app->diagnostic_highlight + 1) % count;
                app->ui_dirty = true;
            }
        }
        else if (key == KEYPAD_KEY_BACK)
        {
            app->state = app->prev_state;
            app->ui_dirty = true;
        }
        break;
    }

    case FSM_STATE_ALARM_POPUP:
    {
        if (key == KEYPAD_KEY_ENTER || key == KEYPAD_KEY_BACK)
        {
            app->state = FSM_STATE_HOME_DASHBOARD;
            app->ui_dirty = true;
        }
        break;
    }

    case FSM_STATE_AI_PROPOSAL:
    {
        if (key == KEYPAD_KEY_ENTER)
        {
            int ret = 0;
            if (app->channel_mgr != NULL) {
                ret = channel_manager_configure(app->channel_mgr, &app->ai_proposal_cfg, NULL, NULL);
                if (ret == 0) {
                    channel_manager_flush_nvs(app->channel_mgr);
                }
            }
            comm_payload_proposal_resp_t resp = {
                .channel_id = app->ai_proposal_cfg.channel_id,
                .accepted = (ret == 0) ? 1 : 0,
                .status_code = (ret == 0) ? COMM_ERR_OK : COMM_ERR_HARDWARE_FAILURE
            };
            app_send_uart_response(app, COMM_CMD_RESP_CONFIG_PROPOSAL, app->ai_proposal_seq, &resp, sizeof(resp));
            app->ai_proposal_pending = false;
            app->state = (app->pre_popup_state != FSM_STATE_AI_PROPOSAL) ? app->pre_popup_state : FSM_STATE_HOME_DASHBOARD;
            app->ui_dirty = true;
        }
        else if (key == KEYPAD_KEY_BACK)
        {
            comm_payload_proposal_resp_t resp = {
                .channel_id = app->ai_proposal_cfg.channel_id,
                .accepted = 0,
                .status_code = COMM_ERR_REJECTED
            };
            app_send_uart_response(app, COMM_CMD_RESP_CONFIG_PROPOSAL, app->ai_proposal_seq, &resp, sizeof(resp));
            app->ai_proposal_pending = false;
            app->state = (app->pre_popup_state != FSM_STATE_AI_PROPOSAL) ? app->pre_popup_state : FSM_STATE_HOME_DASHBOARD;
            app->ui_dirty = true;
        }
        break;
    }

    default:
        break;
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

    /* Điều phối qua bộ giải mã bàn phím 16 nút */
    keypad_multitap_handle_button_event(&app->keypad, btn_id, evt, app->state_timer);
}

/* ========================================================================= */
/* API CÔNG KHAI CỦA FSM                                                     */
/* ========================================================================= */

void app_init(app_context_t *app, const app_dependencies_t *deps)
{
    if (app == NULL || deps == NULL) return;

    memset(app, 0, sizeof(app_context_t));

    app->state = FSM_STATE_INIT;
    app->prev_state = FSM_STATE_HOME_DASHBOARD;
    app->fault_mgr = deps->fault_mgr;
    app->diagnostic_highlight = -1;
    app->ai_connected = false;

    /* Cấu hình Channel Manager & Logging state */
    app->channel_mgr = deps->channel_mgr;
    app->logging_enabled = false;
    app->logging_interval_ms = 1000;
    app->logged_records_count = 0;
    app->last_ui_update_tick = 0;
    app->dashboard_page = 0;
    app->ui_dirty = true;
    app->last_user_activity_tick = 0;

    /* Khởi tạo Button Manager trực tiếp với driver 74HC165 */
    button_manager_init(&app->button_mgr,
                        &s_button_ops,
                        deps->button_count,
                        on_button_event_received,
                        app);

    /* Khởi tạo Bàn phím đa năng (Keypad Multitap) */
    keypad_multitap_init(&app->keypad, app);
    keypad_multitap_set_callbacks(&app->keypad,
                                  on_keypad_commit_handler,
                                  on_keypad_preview_handler,
                                  on_keypad_backspace_handler,
                                  on_keypad_nav_handler);
    keypad_multitap_bind_buffer(&app->keypad, app->input_buffer, sizeof(app->input_buffer));
    keypad_multitap_set_mode(&app->keypad, KEYPAD_MODE_NAV);


    
    /* 4. Khởi tạo ngoại vi UART với Zephyr k_msgq */
    bsp_uart_init();
    /* Khởi tạo UART Stream Parser */
    comm_parser_init(&app->uart_parser, on_uart_packet_received, app);

    /* Khởi tạo Màn hình LCD và hiển thị Boot Screen */
    lcd_init();
    bsp_lcd_show_boot_screen("DATALOGGER BKIT", "v1.0.0");
}

void app_run_step(app_context_t *app, uint32_t current_tick_ms)
{
    if (app == NULL) return;

    app->state_timer = current_tick_ms;

    /* 1. Xử lý các byte nhận từ UART trong Thread Context */
    uint8_t rx_byte;
    while (bsp_uart_get_byte(&rx_byte))
    {
        app_on_uart_byte_received(app, rx_byte);
    }

    /* 2. Cập nhật quét chống rung nút bấm (20ms định kỳ) và bộ đếm gõ phím */
    button_manager_tick(&app->button_mgr, current_tick_ms);
    keypad_multitap_tick(&app->keypad, current_tick_ms);

    /* 3. BẢO VỆ TOÀN CỤC (GLOBAL GUARDS) */
    if (app->state != FSM_STATE_INIT)
    {
        /* Inactivity Timeout (60s) trong chế độ Menu/Edit -> Tự động out về HOME */
        if (app->state >= FSM_STATE_AUTH_LOGIN && app->state <= FSM_STATE_SAVE_CONFIRM)
        {
            if (current_tick_ms - app->last_user_activity_tick >= 60000)
            {
                keypad_multitap_abort(&app->keypad);
                keypad_multitap_set_mode(&app->keypad, KEYPAD_MODE_NAV);
                app->config_dirty = false;
                app->state = FSM_STATE_HOME_DASHBOARD;
                app->ui_dirty = true;
            }
        }

        if (app->state == FSM_STATE_AI_PROPOSAL)
        {
            if (current_tick_ms - app->last_user_activity_tick >= 60000)
            {
                comm_payload_proposal_resp_t resp = {
                    .channel_id = app->ai_proposal_cfg.channel_id,
                    .accepted = 0,
                    .status_code = COMM_ERR_TIMEOUT
                };
                app_send_uart_response(app, COMM_CMD_RESP_CONFIG_PROPOSAL, app->ai_proposal_seq, &resp, sizeof(resp));
                app->ai_proposal_pending = false;
                app->state = FSM_STATE_HOME_DASHBOARD;
                app->ui_dirty = true;
            }
        }

        /* Screensaver Timer (5 phút = 300.000ms không thao tác -> Tắt đèn nền) */
        if (app->state != FSM_STATE_SCREENSAVER_SLEEP)
        {
            if (current_tick_ms - app->last_user_activity_tick >= 300000)
            {
                app->pre_screensaver_state = app->state;
                app->state = FSM_STATE_SCREENSAVER_SLEEP;
                bsp_lcd_fsmc_set_backlight(false);
            }
        }
    }

    /* 4. Thực thi State Machine chính */
    switch (app->state)
    {
    case FSM_STATE_INIT:
    {
        if (current_tick_ms >= BOOT_SCREEN_DURATION_MS)
        {
            app->state = FSM_STATE_HOME_DASHBOARD;
            app->last_ui_update_tick = current_tick_ms;
            app->last_user_activity_tick = current_tick_ms;
            app->ui_dirty = true;
        }
        break;
    }

    case FSM_STATE_HOME_DASHBOARD:
    {
        /* Chu kỳ cập nhật dữ liệu cảm biến định kỳ */
        if (current_tick_ms - app->last_ui_update_tick >= app->logging_interval_ms)
        {
            app->last_ui_update_tick = current_tick_ms;
            if (app->logging_enabled) {
                app->logged_records_count++;
            }
            if (app->channel_mgr != NULL) {
                channel_manager_check_health(app->channel_mgr);
            }

            /* Kiểm tra lỗi nguy cấp */
            fault_severity_t max_sev = fault_manager_get_max_severity(app->fault_mgr);
            if (max_sev >= FAULT_SEV_CRITICAL) {
                app->state = FSM_STATE_ALARM_POPUP;
                app->last_screen_toggle_tick = current_tick_ms;
                app->error_toggle_phase = true;
            }
            app->ui_dirty = true;
        }
        break;
    }

    case FSM_STATE_ALARM_POPUP:
    {
        if (current_tick_ms - app->last_ui_update_tick >= app->logging_interval_ms) {
            app->last_ui_update_tick = current_tick_ms;
            if (app->channel_mgr != NULL) {
                channel_manager_check_health(app->channel_mgr);
            }
        }
        fault_severity_t max_sev = fault_manager_get_max_severity(app->fault_mgr);
        if (max_sev < FAULT_SEV_CRITICAL) {
            app->state = FSM_STATE_HOME_DASHBOARD;
            app->ui_dirty = true;
            break;
        }
        if (current_tick_ms - app->last_screen_toggle_tick >= ERROR_TOGGLE_INTERVAL_MS) {
            app->last_screen_toggle_tick = current_tick_ms;
            app->error_toggle_phase = !app->error_toggle_phase;
            app->ui_dirty = true;
        }
        break;
    }

    case FSM_STATE_DIAGNOSTICS_LIVE:
    {
        if (current_tick_ms >= app->diagnostic_timeout_tick) {
            fault_severity_t sev = fault_manager_get_max_severity(app->fault_mgr);
            app->state = (sev >= FAULT_SEV_CRITICAL) ? FSM_STATE_ALARM_POPUP : FSM_STATE_HOME_DASHBOARD;
            app->ui_dirty = true;
        }
        break;
    }

    default:
        break;
    }

    /* 5. VẼ LẠI MÀN HÌNH NẾU CỜ BẨN ĐƯỢC BẬT (DIRTY FLAG RENDERING) */
    if (app->ui_dirty && app->state != FSM_STATE_SCREENSAVER_SLEEP && app->state != FSM_STATE_INIT)
    {
        render_current_screen(app);
        app->ui_dirty = false;
    }
}

void app_on_uart_byte_received(app_context_t *app, uint8_t byte)
{
    if (app == NULL) return;
    bsp_lcd_on_uart_byte_received(byte);
    comm_parser_feed_byte(&app->uart_parser, byte);
}

const char *app_fsm_state_to_str(fsm_state_t state)
{
    switch (state)
    {
    case FSM_STATE_INIT:             return "INIT";
    case FSM_STATE_HOME_DASHBOARD:   return "DASHBOARD";
    case FSM_STATE_AUTH_LOGIN:       return "AUTH_LOGIN";
    case FSM_STATE_MAIN_MENU:        return "MAIN_MENU";
    case FSM_STATE_SUBMENU_LIST:     return "SUBMENU";
    case FSM_STATE_EDIT_ENUM:        return "EDIT_ENUM";
    case FSM_STATE_EDIT_NUMERIC:     return "EDIT_NUM";
    case FSM_STATE_SAVE_CONFIRM:     return "SAVE_CONFIRM";
    case FSM_STATE_ALARM_POPUP:      return "ALARM_POPUP";
    case FSM_STATE_AI_PROPOSAL:      return "AI_PROPOSAL";
    case FSM_STATE_DIAGNOSTICS_LIVE: return "DIAGNOSTIC";
    case FSM_STATE_SCREENSAVER_SLEEP:return "SLEEP";
    default:                         return "UNKNOWN";
    }
}
