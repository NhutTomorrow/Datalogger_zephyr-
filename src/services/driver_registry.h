/**
 * @file driver_registry.h
 * @brief Tầng tra cứu và kết nối Driver (Driver Registry) theo Mode và Cổng vật lý.
 */

#ifndef DRIVER_REGISTRY_H
#define DRIVER_REGISTRY_H

#include <stdint.h>
#include <stdbool.h>
#include "channel_config.h"
#include "channel_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tra cứu bảng hàm driver thao tác phần cứng phù hợp với cấu hình kênh.
 * @param channel_id ID cổng vật lý (1 .. 18)
 * @param mode Chế độ giao tiếp (channel_interface_mode_t)
 * @return Con trỏ tới channel_driver_ops_t, hoặc NULL nếu mode chưa hỗ trợ hoặc sai cổng.
 */
const channel_driver_ops_t *driver_registry_get_ops(uint8_t channel_id, channel_interface_mode_t mode);

/**
 * @brief Khởi tạo phần cứng cấp thấp cho cổng vật lý (GPIO pinmux, ADC channel setup, v.v.).
 * @param channel_id ID cổng vật lý (1 .. 18)
 * @param mode Chế độ giao tiếp
 * @return 0 nếu thành công, mã lỗi âm nếu thất bại.
 */
int driver_registry_init_channel_hw(uint8_t channel_id, channel_interface_mode_t mode);

/**
 * @brief Giải phóng cấu hình phần cứng khi vô hiệu hóa hoặc chuyển mode cổng (an toàn chân pin).
 * @param channel_id ID cổng vật lý (1 .. 18)
 * @return 0 nếu thành công, mã lỗi âm nếu thất bại.
 */
int driver_registry_deinit_channel_hw(uint8_t channel_id);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_REGISTRY_H */
