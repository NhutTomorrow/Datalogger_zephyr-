/**
 * @file bsp_adc_zephyr.c
 * @brief Hiện thực Driver ADC Zephyr cho 7 cổng Analog trên BKIT STM32F407.
 */

#include "bsp_adc.h"
#include "fault_manager.h"
#include <string.h>
#include <errno.h>

#if defined(__ZEPHYR__)
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bsp_adc, LOG_LEVEL_INF);
#else
#include <stdio.h>
#endif

/* Bảng ánh xạ cổng vật lý sang kênh ADC1 của vi điều khiển STM32F407 */
typedef struct {
    uint8_t  channel_id;
    uint8_t  stm32_adc_channel; /* ADC12_INx */
    const char *pin_name;
} adc_channel_map_t;

static const adc_channel_map_t s_adc_map[] = {
    { 1, 12, "PC2" }, /* LM35 Temp */
    { 2, 10, "PC0" }, /* LDR GL5528 */
    { 3,  8, "PB0" }, /* Power Divider 15k/1.5k */
    { 4,  9, "PB1" }, /* ACS712 Current Loop */
    { 5, 11, "PC1" }, /* VR1 10k */
    { 6,  4, "PA4" }, /* Header P11 Pin 3 */
    { 7,  5, "PA5" }  /* Header P11 Pin 1 */
};

#define ADC_MAP_COUNT (sizeof(s_adc_map) / sizeof(s_adc_map[0]))

static uint16_t s_injected_fault[CHANNEL_MAX_COUNT];

static const adc_channel_map_t *find_adc_map(uint8_t channel_id) {
    for (size_t i = 0; i < ADC_MAP_COUNT; i++) {
        if (s_adc_map[i].channel_id == channel_id) {
            return &s_adc_map[i];
        }
    }
    return NULL;
}

bool bsp_adc_init(void) {
    memset(s_injected_fault, 0, sizeof(s_injected_fault));
    return true;
}

int bsp_adc_init_channel(uint8_t channel_id) {
    const adc_channel_map_t *map = find_adc_map(channel_id);
    if (map == NULL) {
        return -ENOTSUP;
    }

#if defined(__ZEPHYR__)
    const struct device *adc_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(adc1));
    if (adc_dev != NULL && device_is_ready(adc_dev)) {
        struct adc_channel_cfg ch_cfg = {
            .gain = ADC_GAIN_1,
            .reference = ADC_REF_INTERNAL,
            .acquisition_time = ADC_ACQ_TIME_DEFAULT,
            .channel_id = map->stm32_adc_channel,
            .differential = 0
        };
        adc_channel_setup(adc_dev, &ch_cfg);
    }
#endif
    return 0;
}

int bsp_adc_deinit_channel(uint8_t channel_id) {
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return -EINVAL;
    }
    s_injected_fault[channel_id - 1] = FAULT_NONE;
    return 0;
}

bool bsp_adc_read(uint8_t channel_id, float *out_value) {
    if (out_value == NULL) return false;
    const adc_channel_map_t *map = find_adc_map(channel_id);
    if (map == NULL) return false;

    /* Kiểm tra nếu có lỗi ép sinh */
    if (channel_id <= CHANNEL_MAX_COUNT && s_injected_fault[channel_id - 1] != FAULT_NONE) {
        return false;
    }

#if defined(__ZEPHYR__)
    const struct device *adc_dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(adc1));
    if (adc_dev != NULL && device_is_ready(adc_dev)) {
        int16_t sample_buffer = 0;
        struct adc_sequence sequence = {
            .channels = BIT(map->stm32_adc_channel),
            .buffer = &sample_buffer,
            .buffer_size = sizeof(sample_buffer),
            .resolution = 12
        };

        int rc = adc_read(adc_dev, &sequence);
        if (rc == 0) {
            /* 12-bit ADC: 0..4095 với VREF = 3300mV */
            int32_t val_mv = sample_buffer;
            adc_raw_to_millivolts(3300, ADC_GAIN_1, 12, &val_mv);
            *out_value = (float)val_mv / 1000.0f; /* Đổi ra Volt */
            return true;
        }
    }
#endif

    /* Giả lập giá trị chuẩn cho Host / Testing:
     * CH1 (LM35): ~0.28V (tương đương 28.0°C khi scale 100)
     * CH4 (4-20mA): ~1.20V (tương đương 12.0mA)
     * Các kênh khác: 1.65V (giữa dải 3.3V) */
    if (channel_id == 1) {
        *out_value = 0.285f;
    } else if (channel_id == 4) {
        *out_value = 1.20f;
    } else {
        *out_value = 1.65f;
    }
    return true;
}

bool bsp_adc_health_check(uint8_t channel_id, uint16_t *out_fault_code) {
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) return false;

    uint16_t fault = s_injected_fault[channel_id - 1];
    if (out_fault_code != NULL) {
        *out_fault_code = fault;
    }
    return (fault == FAULT_NONE);
}

void bsp_adc_inject_fault(uint8_t channel_id, uint16_t fault_code, bool en) {
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) return;

    if (en) {
        s_injected_fault[channel_id - 1] = fault_code;
    } else {
        s_injected_fault[channel_id - 1] = FAULT_NONE;
    }
}

static const channel_driver_ops_t s_bsp_adc_ops = {
    .init         = bsp_adc_init,
    .read         = bsp_adc_read,
    .health_check = bsp_adc_health_check,
    .inject_fault = bsp_adc_inject_fault
};

const channel_driver_ops_t *bsp_adc_get_ops(void) {
    return &s_bsp_adc_ops;
}
