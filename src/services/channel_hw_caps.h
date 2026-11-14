/**
 * @file channel_hw_caps.h
 * @brief Bảng ràng buộc phần cứng và kiểm tra xung đột chân cho 18 cổng Datalogger.
 * @note File thuần C99, độc lập phần cứng.
 */

#ifndef CHANNEL_HW_CAPS_H
#define CHANNEL_HW_CAPS_H

#include <stdint.h>
#include <stdbool.h>
#include "channel_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* NHÓM XUNG ĐỘT CHÂN VẬT LÝ                                                 */
/* ========================================================================= */

#define CONFLICT_GROUP_NONE     0  /**< Không xung đột với cổng nào */
#define CONFLICT_GROUP_PA5      1  /**< CH_07 (ADC12_IN5) và CH_08 (Opto HCPL-0630) cùng nối PA5 */

/* ========================================================================= */
/* CẤU TRÚC DESCRIPTOR NĂNG LỰC PHẦN CỨNG                                    */
/* ========================================================================= */

/**
 * @brief Định nghĩa khả năng phần cứng cố định của 1 cổng trên bo mạch.
 */
typedef struct {
    uint8_t     channel_id;         /**< 1 .. 18 */
    uint16_t    supported_modes_bm; /**< Bitmask các mode được phép: (1 << mode) */
    uint8_t     conflict_group;     /**< ID nhóm xung đột phần cứng */
    const char *port_name;          /**< Tên chân MCU để debug / phản hồi AI */
    const char *description;        /**< Mô tả linh kiện / mạch đệm trên bo mạch */
} channel_hw_cap_entry_t;

/* ========================================================================= */
/* FUNCTION PROTOTYPES                                                       */
/* ========================================================================= */

/**
 * @brief Kiểm tra xem một mode có được cổng này hỗ trợ về mặt điện học không.
 * @param channel_id ID cổng (1 .. 18)
 * @param mode Chế độ muốn cấu hình
 * @return true nếu tương thích phần cứng, false nếu không được phép
 */
bool channel_hw_caps_is_mode_supported(uint8_t channel_id, channel_interface_mode_t mode);

/**
 * @brief Kiểm tra xung đột chân vật lý với các kênh đang active khác trong bảng.
 * @param channel_id ID cổng muốn kích hoạt
 * @param current_table Mảng cấu hình 18 kênh hiện tại
 * @param out_conflicting_id Con trỏ nhận ID kênh đang xung đột (nếu có, có thể NULL)
 * @return true nếu an toàn (không xung đột), false nếu có xung đột phần cứng
 */
bool channel_hw_caps_check_conflict(uint8_t channel_id,
                                    const ChannelConfig_t current_table[CHANNEL_MAX_COUNT],
                                    uint8_t *out_conflicting_id);

/**
 * @brief Lấy thông tin descriptor năng lực phần cứng của cổng.
 * @param channel_id ID cổng (1 .. 18)
 * @return Con trỏ const đến descriptor, hoặc NULL nếu ID không hợp lệ
 */
const channel_hw_cap_entry_t *channel_hw_caps_get(uint8_t channel_id);

#ifdef __cplusplus
}
#endif

#endif /* CHANNEL_HW_CAPS_H */


