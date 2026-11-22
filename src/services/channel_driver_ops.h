/**
 * @file channel_driver_ops.h
 * @brief Hợp đồng giao tiếp trừu tượng (Hardware Driver Interface Vtable) giữa BSP và Middleware.
 * Decoupled hoàn toàn khỏi channel_manager_t, an toàn cho cả BSP Drivers và Services sử dụng.
 */

#ifndef CHANNEL_DRIVER_OPS_H
#define CHANNEL_DRIVER_OPS_H

#include <stdint.h>
#include <stdbool.h>
#include "channel_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bảng con trỏ hàm (vtable) điều khiển phần cứng cho một cổng/kênh cảm biến.
 */
typedef struct {
    bool (*init)(void);                                                    /**< Khởi tạo ngoại vi / cảm biến */
    bool (*read)(uint8_t channel_id, const ChannelConfig_t *cfg, float *out_value); /**< Đọc giá trị từ phần cứng dựa trên config */
    bool (*health_check)(uint8_t channel_id, uint16_t *out_fault_code);    /**< Tự chẩn đoán đứt dây / lỗi bus */
    void (*inject_fault)(uint8_t channel_id, uint16_t fault_code, bool en);/**< Ép sinh lỗi phục vụ Demo AI */
} channel_driver_ops_t;

/* Tên định danh tương thích ngược */
typedef channel_driver_ops_t sensor_driver_ops_t;

#ifdef __cplusplus
}
#endif

#endif /* CHANNEL_DRIVER_OPS_H */
