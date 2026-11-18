/**
 * @file app_fsm.h
 * @brief Máy trạng thái hữu hạn (FSM) điều phối vòng đời của trạm Datalogger.
 */

#ifndef APP_FSM_H
#define APP_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include "channel_manager.h"
#include "fault_manager.h"
#include "com_protocol.h"
#include "button_manager.h"
#include "keypad_multitap.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* CÁC TRẠNG THÁI FSM PHÂN TẦNG (HIERARCHICAL FSM STATES)                    */
/* ========================================================================= */

typedef enum {
    FSM_STATE_INIT = 0,             /**< Khởi tạo, hiển thị Logo/Boot screen 2s */
    FSM_STATE_HOME_DASHBOARD,       /**< Màn hình chính quan trắc phân trang (thay thế IDLE/LOGGING) */
    FSM_STATE_AUTH_LOGIN,           /**< Nhập mã PIN bảo vệ (4-6 số) */
    FSM_STATE_MAIN_MENU,            /**< Danh sách 7 phân hệ chính */
    FSM_STATE_SUBMENU_LIST,         /**< Danh sách kênh / thông số con */
    FSM_STATE_EDIT_ENUM,            /**< Chỉnh sửa chọn lựa [ON/OFF, Baudrate, Metric, Unit] */
    FSM_STATE_EDIT_NUMERIC,         /**< Chỉnh sửa nhập số (Gain, Offset, Interval) */
    FSM_STATE_SAVE_CONFIRM,         /**< Hộp thoại hỏi lưu Flash: Save changes? [YES/NO] */
    FSM_STATE_ALARM_POPUP,          /**< Cảnh báo khẩn cấp đè màn hình */
    FSM_STATE_AI_PROPOSAL,          /**< Popup AI đề xuất đổi cấu hình kênh [ENTER: OK, BACK: Cancel] */
    FSM_STATE_DIAGNOSTICS_LIVE,     /**< Chẩn đoán trực tiếp (Raw vs Calib) */
    FSM_STATE_SCREENSAVER_SLEEP,    /**< Tiết kiệm năng lượng LCD sau 5 phút */

    /* Alias tương thích ngược */
    FSM_STATE_IDLE          = FSM_STATE_HOME_DASHBOARD,
    FSM_STATE_LOGGING       = FSM_STATE_HOME_DASHBOARD,
    FSM_STATE_ERROR         = FSM_STATE_ALARM_POPUP,
    FSM_STATE_DIAGNOSTIC    = FSM_STATE_DIAGNOSTICS_LIVE,
    FSM_STATE_AUTHENTICATION = FSM_STATE_AUTH_LOGIN,
    FSM_STATE_MENU          = FSM_STATE_MAIN_MENU
} fsm_state_t;

/* ========================================================================= */
/* PHÂN HỆ MENU VÀ ĐÍCH BIÊN TẬP (MENU & EDIT TARGETS)                      */
/* ========================================================================= */

typedef enum {
    SUBMENU_NONE = 0,
    SUBMENU_CHANNEL_LIST,
    SUBMENU_CHANNEL_PARAMS,
    SUBMENU_CLOUD_UPLINK,
    SUBMENU_LOGGING_STORAGE,
    SUBMENU_COMMUNICATION,
    SUBMENU_ALARM_THRESHOLD,
    SUBMENU_SYSTEM_RTC
} submenu_type_t;

typedef enum {
    EDIT_TARGET_NONE = 0,
    EDIT_TARGET_CH_ENABLED,
    EDIT_TARGET_CH_MODE,
    EDIT_TARGET_CH_METRIC,
    EDIT_TARGET_CH_UNIT,
    EDIT_TARGET_CH_SCALE_A,
    EDIT_TARGET_CH_OFFSET_B,
    EDIT_TARGET_CH_INTERVAL,
    EDIT_TARGET_LOG_ENABLE,
    EDIT_TARGET_LOG_INTERVAL,
    EDIT_TARGET_RS485_BAUD
} edit_target_t;

/* ========================================================================= */
/* BỐ CỤC BỘ KHỞI TẠO NỘI BỘ (DEPENDENCIES CONTAINER)                        */
/* ========================================================================= */

typedef struct {
    channel_manager_t *channel_mgr;
    fault_manager_t   *fault_mgr;
    uint8_t            button_count;
} app_dependencies_t;

/* ========================================================================= */
/* NGỮ CẢNH TOÀN TRẠM (APPLICATION CONTEXT)                                 */
/* ========================================================================= */

typedef struct {
    fsm_state_t        state;
    fsm_state_t        prev_state;          /**< Lưu trạng thái trước khi vào Diagnostic */
    channel_manager_t *channel_mgr;         /**< Quản lý kênh và lấy mẫu độc lập */
    fault_manager_t   *fault_mgr;
    button_manager_t   button_mgr;
    keypad_multitap_t  keypad;
    comm_parser_t      uart_parser;

    /* Bộ đệm nhập liệu bàn phím (Password / IP / Number) */
    char               input_buffer[KEYPAD_MAX_BUFFER_LEN];

    /* Trạng thái ghi dữ liệu (Logging) */
    bool               logging_enabled;     /**< true: Đang ghi log, false: Dừng */
    uint16_t           logging_interval_ms; /**< Chu kỳ cập nhật UI / ghi log */
    uint32_t           logged_records_count;/**< Số bản ghi */
    uint32_t           last_ui_update_tick; /**< Mốc thời gian cập nhật LCD gần nhất */

    /* Các bộ đếm thời gian non-blocking */
    uint32_t          state_timer;
    uint32_t          last_screen_toggle_tick;
    uint32_t          diagnostic_timeout_tick;

    /* Cờ trạng thái hiển thị */
    bool              error_toggle_phase;   /**< false: Hiện Dashboard, true: Hiện ERROR Prompt */
    int8_t            diagnostic_highlight; /**< Vị trí mã lỗi đang highlight (-1 nếu không) */
    bool              ai_connected;

    /* Điều khiển hiển thị & Menu Stack */
    uint8_t           dashboard_page;
    submenu_type_t    active_submenu;
    uint8_t           submenu_selected_idx;
    uint8_t           selected_channel_id;
    edit_target_t     edit_target;
    uint8_t           edit_enum_idx;
    ChannelConfig_t   staging_cfg;
    bool              config_dirty;
    bool              ui_dirty;

    /* Đề xuất cấu hình từ Edge AI (Human-in-the-loop safety) */
    ChannelConfig_t   ai_proposal_cfg;
    uint8_t           ai_proposal_seq;
    bool              ai_proposal_pending;

    /* Bộ đếm an toàn & Tiết kiệm năng lượng */
    uint32_t          last_user_activity_tick;
    fsm_state_t       pre_screensaver_state;
    fsm_state_t       pre_popup_state;
} app_context_t;

/* ========================================================================= */
/* FUNCTION PROTOTYPES                                                       */
/* ========================================================================= */

/**
 * @brief Khởi tạo toàn bộ ứng dụng Datalogger và máy trạng thái FSM.
 */
void app_init(app_context_t *app, const app_dependencies_t *deps);

/**
 * @brief Bước thực thi vòng lặp chính của FSM (gọi trong while(1)).
 * @param current_tick_ms Thời gian hệ thống (k_uptime_get_32())
 */
void app_run_step(app_context_t *app, uint32_t current_tick_ms);

/**
 * @brief Đưa 1 byte nhận được từ UART của ESP32 vào FSM giải mã (gọi trong Thread Context).
 */
void app_on_uart_byte_received(app_context_t *app, uint8_t byte);

/**
 * @brief Lấy chuỗi mô tả trạng thái FSM hiện tại.
 */
const char *app_fsm_state_to_str(fsm_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* APP_FSM_H */
