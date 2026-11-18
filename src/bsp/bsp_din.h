/**
 * @file bsp_din.h
 * @brief Driver Digital Input (Opto cách ly PA5, PA6, PC4, PC5) trên bo mạch BKIT STM32F407.
 */

#ifndef BSP_DIN_H
#define BSP_DIN_H

#include <stdint.h>
#include <stdbool.h>
#include "channel_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo hệ thống con Digital Input.
 */
bool bsp_din_init(void);

/**
 * @brief Đọc giá trị mức logic (0.0f hoặc 1.0f) của cổng ngõ vào số.
 */
bool bsp_din_read(uint8_t channel_id, const ChannelConfig_t *cfg, float *out_value);

/**
 * @brief Kiểm tra sức khỏe ngõ vào số.
 */
bool bsp_din_health_check(uint8_t channel_id, uint16_t *out_fault_code);

/**
 * @brief Giả lập ép lỗi ngõ vào phục vụ Demo AI.
 */
void bsp_din_inject_fault(uint8_t channel_id, uint16_t fault_code, bool en);

/**
 * @brief Cấu hình GPIO Input cho cổng vật lý (PA5, PA6, PC4, PC5).
 */
int bsp_din_init_channel(uint8_t channel_id);

/**
 * @brief Giải phóng cấu hình GPIO khi chuyển mode hoặc disable cổng.
 */
int bsp_din_deinit_channel(uint8_t channel_id);

/**
 * @brief Lấy bảng channel_driver_ops_t của Generic Digital Input.
 */
const channel_driver_ops_t *bsp_din_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DIN_H */
