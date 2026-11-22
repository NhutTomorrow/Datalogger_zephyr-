/**
 * @file bsp_rs485.h
 * @brief Driver giao tiếp RS485 Modbus RTU Master trên cổng USART3 (PC10/PC11) + PA15 (DE/RE).
 */

#ifndef BSP_RS485_H
#define BSP_RS485_H

#include <stdint.h>
#include <stdbool.h>
#include "channel_driver_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo ngoại vi USART3 và chân GPIO PA15 (RS485 DE/RE).
 * @return true nếu khởi tạo thành công, false nếu thiết bị chưa sẵn sàng.
 */
bool bsp_rs485_init(void);

/**
 * @brief Khởi tạo kênh phần cứng RS485.
 * @param channel_id ID cổng vật lý (1 .. 18)
 * @return 0 nếu thành công, mã lỗi âm nếu thất bại.
 */
int bsp_rs485_init_channel(uint8_t channel_id);

/**
 * @brief Giải phóng cấu hình kênh RS485.
 * @param channel_id ID cổng vật lý (1 .. 18)
 * @return 0 nếu thành công.
 */
int bsp_rs485_deinit_channel(uint8_t channel_id);

/**
 * @brief Đọc giá trị đo từ cảm biến Modbus RTU qua RS485.
 * @param channel_id ID cổng vật lý
 * @param cfg Cấu hình kênh chứa tham số Modbus (slave_id, reg_addr, reg_count, data_type, scale...)
 * @param out_value Con trỏ nhận giá trị đo (float)
 * @return true nếu đọc và xác thực CRC thành công, false nếu timeout hoặc lỗi.
 */
bool bsp_rs485_read(uint8_t channel_id, const ChannelConfig_t *cfg, float *out_value);

/**
 * @brief Kiểm tra sức khỏe đường truyền RS485 (phát hiện mất kết nối/lỗi CRC).
 * @param channel_id ID cổng vật lý
 * @param out_fault_code Con trỏ nhận mã lỗi nếu có
 * @return true nếu kết nối tốt, false nếu có lỗi.
 */
bool bsp_rs485_health_check(uint8_t channel_id, uint16_t *out_fault_code);

/**
 * @brief Ép sinh lỗi đường truyền RS485 phục vụ Demo AI / Fault Diagnostics.
 */
void bsp_rs485_inject_fault(uint8_t channel_id, uint16_t fault_code, bool en);

/**
 * @brief Lấy bảng hàm driver RS485 chuẩn channel_driver_ops_t.
 */
const channel_driver_ops_t *bsp_rs485_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_RS485_H */
