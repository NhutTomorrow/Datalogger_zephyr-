/**
 * @file bsp_adc.h
 * @brief Driver ADC mức phần cứng cho các cổng Analog (CH_01 .. CH_07) trên bo mạch BKIT STM32F407.
 */

#ifndef BSP_ADC_H
#define BSP_ADC_H

#include <stdint.h>
#include <stdbool.h>
#include "channel_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo hệ thống con ADC.
 */
bool bsp_adc_init(void);

/**
 * @brief Đọc giá trị điện áp (Volt) từ kênh ADC tương ứng với channel_id.
 * @param channel_id Cổng vật lý (1 .. 7)
 * @param cfg Cấu hình kênh
 * @param out_value Con trỏ nhận giá trị điện áp thô (Volt)
 * @return true nếu đọc thành công, false nếu lỗi
 */
bool bsp_adc_read(uint8_t channel_id, const ChannelConfig_t *cfg, float *out_value);

/**
 * @brief Kiểm tra sức khỏe đường truyền analog (phát hiện dây đứt, clipping).
 * @param channel_id Cổng vật lý (1 .. 7)
 * @param out_fault_code Mã lỗi nếu có
 * @return true nếu bình thường, false nếu có lỗi
 */
bool bsp_adc_health_check(uint8_t channel_id, uint16_t *out_fault_code);

/**
 * @brief Giả lập ép lỗi kênh ADC phục vụ Demo AI.
 */
void bsp_adc_inject_fault(uint8_t channel_id, uint16_t fault_code, bool en);

/**
 * @brief Khởi tạo phần cứng riêng cho 1 cổng ADC (Pinmux / ADC Channel Setup).
 */
int bsp_adc_init_channel(uint8_t channel_id);

/**
 * @brief Giải phóng cổng ADC khi chuyển mode hoặc disable.
 */
int bsp_adc_deinit_channel(uint8_t channel_id);

/**
 * @brief Lấy bảng channel_driver_ops_t của Generic ADC.
 */
const channel_driver_ops_t *bsp_adc_get_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ADC_H */
