/**
 * @file bsp_dout.h
 * @brief Driver Digital Output (MOSFET PE5, PE6 và Buzzer PF8) trên bo mạch BKIT STM32F407.
 */

#ifndef BSP_DOUT_H
#define BSP_DOUT_H

#include <stdint.h>
#include <stdbool.h>
#include "channel_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo hệ thống con Digital Output.
 */
bool bsp_dout_init(void);

/**
 * @brief Đọc trạng thái logic hiện tại (0.0f hoặc 1.0f) của cổng Output.
 */
bool bsp_dout_read(uint8_t channel_id, const ChannelConfig_t *cfg, float *out_value);

/**
 * @brief Kiểm tra sức khỏe cổng Output.
 */
bool bsp_dout_health_check(uint8_t channel_id, uint16_t *out_fault_code);

/**
 * @brief Giả lập ép lỗi ngõ ra phục vụ Demo AI.
 */
void bsp_dout_inject_fault(uint8_t channel_id, uint16_t fault_code, bool en);

/**
 * @brief Cấu hình GPIO Output cho cổng vật lý (PE5, PE6, PF8).
 */
int bsp_dout_init_channel(uint8_t channel_id);

/**
 * @brief Giải phóng GPIO Output khi chuyển mode hoặc disable (trả về LOW/ngắt an toàn).
 */
int bsp_dout_deinit_channel(uint8_t channel_id);

/**
 * @brief Điều khiển bật/tắt trực tiếp ngõ ra (MOSFET / Buzzer).
 * @param channel_id Cổng điều khiển (12, 13, 14)
 * @param state 0: Tắt (Open/LOW), 1: Bật (Close/HIGH)
 * @return 0 nếu thành công, -EINVAL nếu channel_id không hợp lệ
 */
int bsp_dout_set(uint8_t channel_id, uint8_t state);

/**
 * @brief Đọc trạng thái ngõ ra hiện tại (0 hoặc 1).
 */
uint8_t bsp_dout_get(uint8_t channel_id);

/**
 * @brief Lấy bảng channel_driver_ops_t của Generic Digital Output.
 */
const channel_driver_ops_t *bsp_dout_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DOUT_H */
