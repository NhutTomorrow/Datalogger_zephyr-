/**
 * @file keypad_multitap.c
 * @brief Hiện thực bộ giải mã bàn phím 16 nút chuẩn điện thoại (Multi-tap T9 & Navigation).
 */

#include "keypad_multitap.h"
#include <string.h>

/* ========================================================================= */
/* BẢNG TRA CỨU KÝ TỰ MULTI-TAP (LUT TRONG FLASH)                           */
/* ========================================================================= */

static const char *s_multitap_lower[KEYPAD_KEY_MAX] = {
    [KEYPAD_KEY_1] = ".,?!1",
    [KEYPAD_KEY_2] = "abc2",
    [KEYPAD_KEY_3] = "def3",
    [KEYPAD_KEY_4] = "ghi4",
    [KEYPAD_KEY_5] = "jkl5",
    [KEYPAD_KEY_6] = "mno6",
    [KEYPAD_KEY_7] = "pqrs7",
    [KEYPAD_KEY_8] = "tuv8",
    [KEYPAD_KEY_9] = "wxyz9",
    [KEYPAD_KEY_0] = " 0",
};

static const char *s_multitap_upper[KEYPAD_KEY_MAX] = {
    [KEYPAD_KEY_1] = ".,?!1",
    [KEYPAD_KEY_2] = "ABC2",
    [KEYPAD_KEY_3] = "DEF3",
    [KEYPAD_KEY_4] = "GHI4",
    [KEYPAD_KEY_5] = "JKL5",
    [KEYPAD_KEY_6] = "MNO6",
    [KEYPAD_KEY_7] = "PQRS7",
    [KEYPAD_KEY_8] = "TUV8",
    [KEYPAD_KEY_9] = "WXYZ9",
    [KEYPAD_KEY_0] = " 0",
};

/* ========================================================================= */
/* ÁNH XẠ PHẦN CỨNG 74HC165 -> KEYPAD LOGIC                                 */
/* ========================================================================= */

keypad_key_t keypad_map_hardware_button(button_id_t btn_id)
{
    switch (btn_id)
    {
    case BTN_ID_1:  return KEYPAD_KEY_1;      /* Hàng 1, Cột 1: [ 1 ] */
    case BTN_ID_2:  return KEYPAD_KEY_2;      /* Hàng 1, Cột 2: [ 2 abc ] */
    case BTN_ID_3:  return KEYPAD_KEY_3;      /* Hàng 1, Cột 3: [ 3 def ] */
    case BTN_ID_4:  return KEYPAD_KEY_UP;     /* Hàng 1, Cột 4: [ UP ] */

    case BTN_ID_5:  return KEYPAD_KEY_4;      /* Hàng 2, Cột 1: [ 4 ghi ] */
    case BTN_ID_6:  return KEYPAD_KEY_5;      /* Hàng 2, Cột 2: [ 5 jkl ] */
    case BTN_ID_7:  return KEYPAD_KEY_6;      /* Hàng 2, Cột 3: [ 6 mno ] */
    case BTN_ID_8:  return KEYPAD_KEY_DOWN;   /* Hàng 2, Cột 4: [ DOWN ] */

    case BTN_ID_9:  return KEYPAD_KEY_7;      /* Hàng 3, Cột 1: [ 7 pqrs ] */
    case BTN_ID_10: return KEYPAD_KEY_8;      /* Hàng 3, Cột 2: [ 8 tuv ] */
    case BTN_ID_11: return KEYPAD_KEY_9;      /* Hàng 3, Cột 3: [ 9 wxyz ] */
    case BTN_ID_12: return KEYPAD_KEY_BACK;   /* Hàng 3, Cột 4: [ BACK / DEL ] */

    case BTN_ID_13: return KEYPAD_KEY_STAR;   /* Hàng 4, Cột 1: [ * / Mode ] */
    case BTN_ID_14: return KEYPAD_KEY_0;      /* Hàng 4, Cột 2: [ 0 Space ] */
    case BTN_ID_15: return KEYPAD_KEY_HASH;   /* Hàng 4, Cột 3: [ # / +/- ] */
    case BTN_ID_16: return KEYPAD_KEY_ENTER;  /* Hàng 4, Cột 4: [ ENTER / OK ] */

    default:        return KEYPAD_KEY_NONE;
    }
}

/* ========================================================================= */
/* HÀM NỘI BỘ (INTERNAL HELPERS)                                             */
/* ========================================================================= */

static bool is_number_key(keypad_key_t key)
{
    return (key >= KEYPAD_KEY_1 && key <= KEYPAD_KEY_9) || (key == KEYPAD_KEY_0);
}

static const char *get_key_chars(const keypad_multitap_t *kp, keypad_key_t key)
{
    if (kp->mode == KEYPAD_MODE_TEXT_UPPER) {
        return s_multitap_upper[key];
    }
    return s_multitap_lower[key];
}

static void commit_char_internal(keypad_multitap_t *kp, char c)
{
    if (c == '\0') {
        return;
    }

    if (kp->buffer != NULL && kp->cursor_pos < (kp->buffer_size - 1)) {
        kp->buffer[kp->cursor_pos++] = c;
        kp->buffer[kp->cursor_pos] = '\0';
    }

    kp->is_pending = false;
    kp->pending_key = KEYPAD_KEY_NONE;
    kp->pending_char = '\0';

    if (kp->on_commit != NULL) {
        kp->on_commit(c, kp->buffer, kp->cursor_pos, kp->user_data);
    }
}

/* ========================================================================= */
/* API CÔNG KHAI CỦA KEYPAD_MULTITAP                                         */
/* ========================================================================= */

void keypad_multitap_init(keypad_multitap_t *keypad, void *user_data)
{
    if (keypad == NULL) return;

    memset(keypad, 0, sizeof(keypad_multitap_t));
    keypad->mode = KEYPAD_MODE_NAV;
    keypad->commit_timeout_ms = KEYPAD_DEFAULT_COMMIT_TIMEOUT_MS;
    keypad->user_data = user_data;
}

void keypad_multitap_set_callbacks(keypad_multitap_t *keypad,
                                  keypad_commit_cb_t on_commit,
                                  keypad_preview_cb_t on_preview,
                                  keypad_backspace_cb_t on_backspace,
                                  keypad_nav_action_cb_t on_nav)
{
    if (keypad == NULL) return;

    keypad->on_commit = on_commit;
    keypad->on_preview = on_preview;
    keypad->on_backspace = on_backspace;
    keypad->on_nav = on_nav;
}

void keypad_multitap_bind_buffer(keypad_multitap_t *keypad, char *buf, uint8_t max_size)
{
    if (keypad == NULL) return;

    /* Chốt ký tự cũ trước khi trỏ sang buffer mới */
    keypad_multitap_force_commit(keypad);

    keypad->buffer = buf;
    keypad->buffer_size = max_size;
    keypad->cursor_pos = (buf != NULL) ? (uint8_t)strlen(buf) : 0;
}

void keypad_multitap_set_mode(keypad_multitap_t *keypad, keypad_mode_t mode)
{
    if (keypad == NULL) return;

    /* Khi đổi mode, tự động chốt ký tự đang dở */
    keypad_multitap_force_commit(keypad);
    keypad->mode = mode;
}

void keypad_multitap_force_commit(keypad_multitap_t *keypad)
{
    if (keypad == NULL || !keypad->is_pending) return;

    commit_char_internal(keypad, keypad->pending_char);
}

void keypad_multitap_abort(keypad_multitap_t *keypad)
{
    if (keypad == NULL) return;

    /* Hủy bỏ hoàn toàn ký tự đang xoay vòng mà không lưu vào buffer */
    keypad->is_pending = false;
    keypad->pending_key = KEYPAD_KEY_NONE;
    keypad->pending_char = '\0';
    keypad->tap_index = 0;
}

void keypad_multitap_tick(keypad_multitap_t *keypad, uint32_t now_ms)
{
    if (keypad == NULL || !keypad->is_pending) return;

    /* Kiểm tra quá hạn timeout -> Tự động chốt ký tự */
    if ((now_ms - keypad->last_tap_tick) >= keypad->commit_timeout_ms) {
        commit_char_internal(keypad, keypad->pending_char);
    }
}

void keypad_multitap_handle_button_event(keypad_multitap_t *keypad,
                                         button_id_t btn_id,
                                         button_event_t evt,
                                         uint32_t now_ms)
{
    if (keypad == NULL) return;

    keypad_key_t key = keypad_map_hardware_button(btn_id);
    if (key == KEYPAD_KEY_NONE) return;

    /* ===================================================================== */
    /* 1. NGỮ CẢNH: CHẾ ĐỘ ĐIỀU HƯỚNG MENU / DASHBOARD                      */
    /* ===================================================================== */
    if (keypad->mode == KEYPAD_MODE_NAV) {
        if (keypad->on_nav != NULL) {
            keypad->on_nav(key, evt, keypad->user_data);
        }
        return;
    }

    /* Các chế độ gõ chữ/số chỉ xử lý sự kiện CLICK */
    if (evt != BUTTON_EVT_CLICK) {
        return;
    }

    /* ===================================================================== */
    /* 2. NGỮ CẢNH: CHẾ ĐỘ SỐ THUẦN (ZERO-LATENCY NUMERIC)                  */
    /* ===================================================================== */
    if (keypad->mode == KEYPAD_MODE_NUMERIC) {
        if (is_number_key(key)) {
            char digit = (key == KEYPAD_KEY_0) ? '0' : ('0' + (key - KEYPAD_KEY_1 + 1));
            commit_char_internal(keypad, digit);
            return;
        }

        switch (key) {
        case KEYPAD_KEY_STAR:
            /* Dấu chấm thập phân */
            commit_char_internal(keypad, '.');
            break;

        case KEYPAD_KEY_HASH:
            /* Đổi dấu âm/dương (+/-) */
            if (keypad->buffer != NULL && keypad->buffer_size > 2) {
                if (keypad->buffer[0] == '-') {
                    memmove(&keypad->buffer[0], &keypad->buffer[1], strlen(keypad->buffer));
                    if (keypad->cursor_pos > 0) keypad->cursor_pos--;
                } else if (keypad->cursor_pos < (keypad->buffer_size - 1)) {
                    memmove(&keypad->buffer[1], &keypad->buffer[0], strlen(keypad->buffer) + 1);
                    keypad->buffer[0] = '-';
                    keypad->cursor_pos++;
                }
                if (keypad->on_commit != NULL) {
                    keypad->on_commit('\0', keypad->buffer, keypad->cursor_pos, keypad->user_data);
                }
            }
            break;

        case KEYPAD_KEY_BACK:
            /* Xóa lùi 1 ký tự số */
            if (keypad->buffer != NULL && keypad->cursor_pos > 0) {
                keypad->cursor_pos--;
                keypad->buffer[keypad->cursor_pos] = '\0';
                if (keypad->on_backspace != NULL) {
                    keypad->on_backspace(keypad->buffer, keypad->cursor_pos, keypad->user_data);
                }
            } else {
                /* Bộ đệm đã rỗng -> Phím BACK chuyển vai trò thành phím Điều hướng Thoát */
                if (keypad->on_nav != NULL) {
                    keypad->on_nav(KEYPAD_KEY_BACK, evt, keypad->user_data);
                }
            }
            break;

        case KEYPAD_KEY_ENTER:
        case KEYPAD_KEY_UP:
        case KEYPAD_KEY_DOWN:
            /* Điều hướng trong lúc nhập số */
            if (keypad->on_nav != NULL) {
                keypad->on_nav(key, evt, keypad->user_data);
            }
            break;

        default:
            break;
        }
        return;
    }

    /* ===================================================================== */
    /* 3. NGỮ CẢNH: CHẾ ĐỘ NHẬP VĂN BẢN (MULTI-TAP TEXT LOWER / UPPER)       */
    /* ===================================================================== */
    if (is_number_key(key)) {
        const char *chars = get_key_chars(keypad, key);
        if (chars == NULL || chars[0] == '\0') return;

        uint8_t num_chars = (uint8_t)strlen(chars);

        /* Nếu bấm tiếp cùng một phím trong khi đang chờ timeout -> Xoay vòng ký tự */
        if (keypad->is_pending && keypad->pending_key == key) {
            keypad->tap_index = (keypad->tap_index + 1) % num_chars;
            keypad->pending_char = chars[keypad->tap_index];
            keypad->last_tap_tick = now_ms;

            if (keypad->on_preview != NULL) {
                keypad->on_preview(keypad->pending_char, keypad->cursor_pos, keypad->user_data);
            }
        }
        else {
            /* Nếu bấm sang phím số khác -> Chốt ký tự cũ trước */
            if (keypad->is_pending) {
                commit_char_internal(keypad, keypad->pending_char);
            }

            /* Bắt đầu chu trình chọn ký tự mới */
            keypad->is_pending = true;
            keypad->pending_key = key;
            keypad->tap_index = 0;
            keypad->pending_char = chars[0];
            keypad->last_tap_tick = now_ms;

            if (keypad->on_preview != NULL) {
                keypad->on_preview(keypad->pending_char, keypad->cursor_pos, keypad->user_data);
            }
        }
        return;
    }

    /* Xử lý các phím chức năng trong chế độ gõ Text */
    switch (key) {
    case KEYPAD_KEY_STAR:
        /* Chốt ký tự đang dở và xoay vòng Mode: Lower -> Upper -> Numeric -> Lower */
        keypad_multitap_force_commit(keypad);
        if (keypad->mode == KEYPAD_MODE_TEXT_LOWER) {
            keypad->mode = KEYPAD_MODE_TEXT_UPPER;
        } else if (keypad->mode == KEYPAD_MODE_TEXT_UPPER) {
            keypad->mode = KEYPAD_MODE_NUMERIC;
        } else {
            keypad->mode = KEYPAD_MODE_TEXT_LOWER;
        }
        break;

    case KEYPAD_KEY_BACK:
        /* Nếu đang có ký tự preview -> Hủy preview */
        if (keypad->is_pending) {
            keypad->is_pending = false;
            keypad->pending_char = '\0';
            if (keypad->on_preview != NULL) {
                keypad->on_preview('\0', keypad->cursor_pos, keypad->user_data);
            }
        }
        /* Nếu không có ký tự preview -> Xóa lùi ký tự đã chốt trong buffer */
        else if (keypad->buffer != NULL && keypad->cursor_pos > 0) {
            keypad->cursor_pos--;
            keypad->buffer[keypad->cursor_pos] = '\0';
            if (keypad->on_backspace != NULL) {
                keypad->on_backspace(keypad->buffer, keypad->cursor_pos, keypad->user_data);
            }
        }
        else {
            /* Bộ đệm đã rỗng -> Phím BACK chuyển vai trò thành phím Điều hướng Thoát */
            if (keypad->on_nav != NULL) {
                keypad->on_nav(KEYPAD_KEY_BACK, evt, keypad->user_data);
            }
        }
        break;

    case KEYPAD_KEY_ENTER:
        /* Chốt ký tự pending trước khi phát lệnh xác nhận */
        keypad_multitap_force_commit(keypad);
        if (keypad->on_nav != NULL) {
            keypad->on_nav(KEYPAD_KEY_ENTER, evt, keypad->user_data);
        }
        break;

    case KEYPAD_KEY_UP:
    case KEYPAD_KEY_DOWN:
        keypad_multitap_force_commit(keypad);
        if (keypad->on_nav != NULL) {
            keypad->on_nav(key, evt, keypad->user_data);
        }
        break;

    default:
        break;
    }
}
