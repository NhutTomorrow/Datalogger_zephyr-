/**
 * @file driver_registry.c
 * @brief Hiện thực bảng tra cứu và kết nối Driver theo Mode trên BKIT STM32F407.
 */

#include "driver_registry.h"
#include "channel_hw_caps.h"
#include "bsp_adc.h"
#include "bsp_dout.h"
#include "bsp_din.h"
#include "bsp_rs485.h"
#include <stddef.h>
#include <errno.h>

const channel_driver_ops_t *driver_registry_get_ops(uint8_t channel_id, channel_interface_mode_t mode)
{
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return NULL;
    }

    /* Kiểm tra xem cổng này có hỗ trợ mode phần cứng đó không */
    if (!channel_hw_caps_is_mode_supported(channel_id, mode)) {
        return NULL;
    }

    switch (mode) {
    case MODE_ADC_SINGLE:
        return bsp_adc_get_ops();

    case MODE_DIGITAL_IN:
    case MODE_PULSE_COUNTER:
        return bsp_din_get_ops();

    case MODE_OUT_CONTROL:
        return bsp_dout_get_ops();

    case MODE_RS485:
        return bsp_rs485_get_ops();

    case MODE_DISABLED:
    default:
        return NULL;
    }
}

int driver_registry_init_channel_hw(uint8_t channel_id, channel_interface_mode_t mode)
{
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return -EINVAL;
    }

    if (!channel_hw_caps_is_mode_supported(channel_id, mode)) {
        return -ENOTSUP;
    }

    switch (mode) {
    case MODE_ADC_SINGLE:
        return bsp_adc_init_channel(channel_id);

    case MODE_DIGITAL_IN:
    case MODE_PULSE_COUNTER:
        return bsp_din_init_channel(channel_id);

    case MODE_OUT_CONTROL:
        return bsp_dout_init_channel(channel_id);

    case MODE_RS485:
        return bsp_rs485_init_channel(channel_id);

    case MODE_DISABLED:
        return 0;

    default:
        return -ENOTSUP;
    }
}

int driver_registry_deinit_channel_hw(uint8_t channel_id)
{
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return -EINVAL;
    }

    /* Giải phóng cấu hình phần cứng trên tất cả các domain */
    bsp_adc_deinit_channel(channel_id);
    bsp_din_deinit_channel(channel_id);
    bsp_dout_deinit_channel(channel_id);
    bsp_rs485_deinit_channel(channel_id);

    return 0;
}
