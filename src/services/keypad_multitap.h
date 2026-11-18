/**
 * @file keypad_multitap.h
 * @brief Bộ giải mã bàn phím 16 nút ma trận 4x4 theo chuẩn điện thoại (Multi-tap / T9 & Navigation).
 * @note Thuần C99, độc lập phần cứng, thiết kế tương thích với button_manager và Zephyr RTOS.
 */

#ifndef KEYPAD_MULTITAP_H
#define KEYPAD_MULTITAP_H

#include <stdint.h>
#include <stdbool.h>
#include "button_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* ĐỊNH NGHĨA MÃ PHÍM VÀ CHẾ ĐỘ NHẬP LIỆU                                   */
/* ========================================================================= */

#define KEYPAD_DEFAULT_COMMIT_TIMEOUT_MS  700   /**< Trễ chốt ký tự Multi-tap (ms) */
#define KEYPAD_MAX_BUFFER_LEN             64    /**< Kích thước tối đa của bộ đệm nhập liệu */

/**
 * @brief Định danh logic các phím trên bàn phím chuẩn điện thoại (16 phím).
 */
typedef enum {
    KEYPAD_KEY_NONE = 0,
    KEYPAD_KEY_1,
    KEYPAD_KEY_2,
    KEYPAD_KEY_3,
    KEYPAD_KEY_4,
    KEYPAD_KEY_5,
    KEYPAD_KEY_6,
    KEYPAD_KEY_7,
    KEYPAD_KEY_8,
    KEYPAD_KEY_9,
    KEYPAD_KEY_0,
    KEYPAD_KEY_STAR,    /**< Phím *: Đổi chế độ nhập / dấu '.' */
    KEYPAD_KEY_HASH,    /**< Phím #: Đổi dấu +/- hoặc ký tự đặc biệt */
    KEYPAD_KEY_UP,      /**< Phím điều hướng Lên */
    KEYPAD_KEY_DOWN,    /**< Phím điều hướng Xuống */
    KEYPAD_KEY_BACK,    /**< Phím Xóa lùi (Backspace) hoặc Thoát */
    KEYPAD_KEY_ENTER,   /**< Phím Xác nhận / Chốt dữ liệu */
    KEYPAD_KEY_MAX
} keypad_key_t;

/**
 * @brief Chế độ hoạt động của bàn phím tùy theo ngữ cảnh FSM.
 */
typedef enum {
    KEYPAD_MODE_NAV = 0,       /**< Chế độ điều hướng: 1..9 là hotkey, 4 phím chức năng điều hướng */
    KEYPAD_MODE_NUMERIC,       /**< Chế độ số: Bấm là ăn ngay (Zero-latency), * là dấu chấm '.' */
    KEYPAD_MODE_TEXT_LOWER,    /**< Chế độ chữ thường: a, b, c, 2... (Multi-tap với timeout) */
    KEYPAD_MODE_TEXT_UPPER     /**< Chế độ chữ hoa: A, B, C, 2... */
} keypad_mode_t;

/* ========================================================================= */
/* CALLBACKS PHÁT SINH SỰ KIỆN                                               */
/* ========================================================================= */

/**
 * @brief Callback khi một ký tự đã được chốt (committed) vào buffer.
 */
typedef void (*keypad_commit_cb_t)(char committed_char, const char *current_str, uint8_t cursor_pos, void *user_data);

/**
 * @brief Callback khi đang xoay vòng ký tự (preview ký tự trước khi commit).
 */
typedef void (*keypad_preview_cb_t)(char preview_char, uint8_t cursor_pos, void *user_data);

/**
 * @brief Callback khi người dùng xóa lùi 1 ký tự (Backspace).
 */
typedef void (*keypad_backspace_cb_t)(const char *current_str, uint8_t cursor_pos, void *user_data);

/**
 * @brief Callback khi phát sinh sự kiện điều hướng hoặc Hotkey (trong MODE_NAV).
 */
typedef void (*keypad_nav_action_cb_t)(keypad_key_t key, button_event_t evt, void *user_data);

/* ========================================================================= */
/* CẤU TRÚC DỮ LIỆU CONTEXT                                                 */
/* ========================================================================= */

typedef struct {
    keypad_mode_t          mode;
    uint32_t               commit_timeout_ms;

    /* Bộ đệm chuỗi đang nhập */
    char                  *buffer;
    uint8_t                buffer_size;
    uint8_t                cursor_pos;

    /* Trạng thái Multi-tap đang chờ chốt */
    keypad_key_t           pending_key;
    uint8_t                tap_index;
    char                   pending_char;
    uint32_t               last_tap_tick;
    bool                   is_pending;

    /* Callbacks */
    keypad_commit_cb_t     on_commit;
    keypad_preview_cb_t    on_preview;
    keypad_backspace_cb_t  on_backspace;
    keypad_nav_action_cb_t on_nav;
    void                  *user_data;
} keypad_multitap_t;

/* ========================================================================= */
/* API ĐIỀU KHIỂN BÀN PHÍM                                                   */
/* ========================================================================= */

/**
 * @brief Khởi tạo module bàn phím đa năng.
 */
void keypad_multitap_init(keypad_multitap_t *keypad, void *user_data);

/**
 * @brief Đăng ký các hàm callback nhận diện sự kiện.
 */
void keypad_multitap_set_callbacks(keypad_multitap_t *keypad,
                                  keypad_commit_cb_t on_commit,
                                  keypad_preview_cb_t on_preview,
                                  keypad_backspace_cb_t on_backspace,
                                  keypad_nav_action_cb_t on_nav);

/**
 * @brief Gắn bộ đệm chuỗi cần nhập liệu (khi FSM chuyển sang chế độ EDIT).
 */
void keypad_multitap_bind_buffer(keypad_multitap_t *keypad, char *buf, uint8_t max_size);

/**
 * @brief Thay đổi chế độ nhập liệu (NAV, NUMERIC, TEXT_LOWER, TEXT_UPPER).
 */
void keypad_multitap_set_mode(keypad_multitap_t *keypad, keypad_mode_t mode);

/**
 * @brief Nạp sự kiện nút bấm từ button_manager vào module giải mã bàn phím.
 */
void keypad_multitap_handle_button_event(keypad_multitap_t *keypad, button_id_t btn_id, button_event_t evt, uint32_t now_ms);

/**
 * @brief Hàm cập nhật định kỳ (gọi trong app_run_step) để xử lý timeout chốt ký tự.
 */
void keypad_multitap_tick(keypad_multitap_t *keypad, uint32_t now_ms);

/**
 * @brief Cưỡng chế chốt ngay lập tức ký tự đang preview (nếu có).
 */
void keypad_multitap_force_commit(keypad_multitap_t *keypad);

/**
 * @brief Hủy bỏ ký tự đang preview và giải phóng trạng thái (dùng trong FSM Exit Action).
 */
void keypad_multitap_abort(keypad_multitap_t *keypad);

/**
 * @brief Ánh xạ từ chỉ số button phần cứng 74HC165 sang mã phím logic điện thoại.
 */
keypad_key_t keypad_map_hardware_button(button_id_t btn_id);

#ifdef __cplusplus
}
#endif

#endif /* KEYPAD_MULTITAP_H */
