/**
 * @file button_if.h
 * @brief Giao diện trừu tượng cho nút bấm (Button Interface & Event Manager).
 * @note File thuần C99, độc lập phần cứng.
 *       Hỗ trợ cả nút bấm trực tiếp từ GPIO hoặc đọc qua IC mở rộng 74HC165 (như trên board BKIT).
 */

#ifndef BUTTON_IF_H
#define BUTTON_IF_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* CẤU HÌNH & ĐỊNH DANH NÚT BẤM                                             */
/* ========================================================================= */

#define BUTTON_MAX_COUNT            16      /**< Hỗ trợ tối đa 16 nút (phù hợp cụm 74HC165 trên board) */
#define BUTTON_DEBOUNCE_TIME_MS     20      /**< Thời gian chống rung phím (ms) */
#define BUTTON_LONG_PRESS_TIME_MS   1500    /**< Ngưỡng nhận diện nhấn giữ (ms) */

/* Định danh các nút cơ bản phục vụ Datalogger */
typedef enum {
    BTN_ID_1 = 0,   /**< Phím 1: UP / Xem thông số kế tiếp */
    BTN_ID_2,       /**< Phím 2: DOWN / Xem thông số trước */
    BTN_ID_3,       /**< Phím 3: ENTER / Chuyển chế độ Logging */
    BTN_ID_4,       /**< Phím 4: BACK / Xóa lỗi / Reset cảnh báo */
    BTN_ID_5,
    BTN_ID_6,
    BTN_ID_7,
    BTN_ID_8,
    BTN_ID_9,
    BTN_ID_10,
    BTN_ID_11,
    BTN_ID_12,
    BTN_ID_13,
    BTN_ID_14,
    BTN_ID_15,
    BTN_ID_16
} button_id_t;

/* Sự kiện nút bấm phát sinh */
typedef enum {
    BUTTON_EVT_NONE = 0,
    BUTTON_EVT_CLICK,           /**< Nhấn nhả nhanh (Short click) */
    BUTTON_EVT_LONG_PRESS,      /**< Nhấn giữ > 1.5 giây */
    BUTTON_EVT_HOLD             /**< Đang tiếp tục giữ nút */
} button_event_t;

/* Callback xử lý sự kiện nút bấm */
typedef void (*button_event_handler_t)(button_id_t btn_id, button_event_t evt, void *user_data);

/* ========================================================================= */
/* BẢNG HÀM DRIVER PHẦN CỨNG NÚT BẤM (BUTTON DRIVER OPS)                     */
/* ========================================================================= */

typedef struct {
    /**
     * @brief Khởi tạo phần cứng (cấu hình GPIO hoặc SPI1 đọc 74HC165).
     */
    bool (*init)(void);

    /**
     * @brief Đọc mức logic thô hiện tại của 1 nút bấm.
     * @param btn_id Chỉ số nút bấm (0 đến 15)
     * @return true nếu nút ĐANG BỊ NHẤN (Active), false nếu nhả
     */
    bool (*read_raw_state)(uint8_t btn_id);
    uint16_t (*button_scan)(void);
} button_driver_ops_t;

/* ========================================================================= */
/* CONTEXT BỘ QUẢN LÝ NÚT BẤM (DEBOUNCE & EVENT DISPATCHER)                  */
/* ========================================================================= */

typedef struct {
    bool     prev_state;
    bool     stable_state;
    uint32_t press_start_tick;
    bool     long_press_fired;
} button_state_t;

typedef struct {
    const button_driver_ops_t *ops;
    uint8_t                    button_count;
    button_state_t             states[BUTTON_MAX_COUNT];
    button_event_handler_t     event_cb;
    void                      *user_data;
} button_manager_t;

/* ========================================================================= */
/* API CHO ỨNG DỤNG & FSM                                                   */
/* ========================================================================= */

/**
 * @brief Khởi tạo bộ quản lý nút bấm với driver cụ thể.
 *
 * @param mgr          Con trỏ button_manager
 * @param ops          Bảng hàm đọc phần cứng (GPIO hoặc 74HC165)
 * @param button_count Số nút cần quét (tối đa 16)
 * @param event_cb     Hàm callback được gọi khi có sự kiện nút bấm
 * @param user_data    Con trỏ dữ liệu tùy biến truyền vào callback
 */
void button_manager_init(button_manager_t *mgr,
                         const button_driver_ops_t *ops,
                         uint8_t button_count,
                         button_event_handler_t event_cb,
                         void *user_data);

/**
 * @brief Hàm cập nhật trạng thái nút bấm (gọi định kỳ mỗi 10ms - 20ms trong vòng lặp FSM).
 *
 * @param mgr              Con trỏ button_manager
 * @param current_tick_ms  Mốc thời gian hiện tại (lấy từ HAL_GetTick() hoặc phần mềm timer)
 */
void button_manager_tick(button_manager_t *mgr, uint32_t current_tick_ms);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_IF_H */
