/**
 * @file app_fsm.h
 * @brief Máy trạng thái hữu hạn (FSM) điều phối vòng đời của trạm Datalogger.
 */

#ifndef APP_FSM_H
#define APP_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include "data_manager.h"
#include "fault_manager.h"
#include "com_protocol.h"
#include "button_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* CÁC TRẠNG THÁI FSM (STATES)                                               */
/* ========================================================================= */

typedef enum {
    FSM_STATE_INIT = 0,         /**< Khởi tạo, hiển thị màn hình chào 2s */
    FSM_STATE_IDLE,             /**< Chờ, giám sát đo lường, không ghi log SD */
    FSM_STATE_LOGGING,          /**< Đang trong chế độ ghi log dữ liệu định kỳ */
    FSM_STATE_ERROR,            /**< Có sự cố nghiêm trọng, chớp màn hình ERROR - VUI LONG HOI AI */
    FSM_STATE_DIAGNOSTIC        /**< Màn hình danh sách mã lỗi (khi AI hỏi hoặc user tra cứu) */
} fsm_state_t;

/* ========================================================================= */
/* BỐ CỤC BỘ KHỞI TẠO NỘI BỘ (DEPENDENCIES CONTAINER)                        */
/* ========================================================================= */

typedef struct {
    sensor_manager_t *sensor_mgr;
    fault_manager_t  *fault_mgr;
    uint8_t           button_count;
} app_dependencies_t;

/* ========================================================================= */
/* NGỮ CẢNH TOÀN TRẠM (APPLICATION CONTEXT)                                 */
/* ========================================================================= */

typedef struct {
    fsm_state_t       state;
    fsm_state_t       prev_state;       /**< Lưu trạng thái trước khi vào Diagnostic */
    data_manager_t    data_mgr;
    fault_manager_t  *fault_mgr;
    button_manager_t  button_mgr;
    comm_parser_t     uart_parser;

    /* Các bộ đếm thời gian non-blocking */
    uint32_t          state_timer;
    uint32_t          last_screen_toggle_tick;
    uint32_t          diagnostic_timeout_tick;

    /* Cờ trạng thái hiển thị */
    bool              error_toggle_phase;   /**< false: Hiện Dashboard, true: Hiện ERROR Prompt */
    int8_t            diagnostic_highlight; /**< Vị trí mã lỗi đang highlight (-1 nếu không) */
    bool              ai_connected;
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
