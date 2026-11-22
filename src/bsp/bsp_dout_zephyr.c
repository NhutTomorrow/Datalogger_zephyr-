/**
 * @file bsp_dout_zephyr.c
 * @brief Hiện thực Driver Digital Output Zephyr cho PE5, PE6, PF8 trên BKIT STM32F407.
 */

#include "bsp_dout.h"
#include "fault_codes.h"
#include <string.h>
#include <errno.h>

#if defined(__ZEPHYR__)
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bsp_dout, LOG_LEVEL_INF);
#endif

typedef struct {
    uint8_t channel_id;
    const char *gpio_dev_name;
    uint8_t pin;
    uint8_t state;
} dout_channel_map_t;

static dout_channel_map_t s_dout_map[] = {
    { 12, "GPIOE", 5, 0 }, /* PE5 - MOSFET Y0 */
    { 13, "GPIOE", 6, 0 }, /* PE6 - MOSFET Y1 */
    { 14, "GPIOF", 8, 0 }  /* PF8 - Buzzer */
};

#define DOUT_MAP_COUNT (sizeof(s_dout_map) / sizeof(s_dout_map[0]))

static uint16_t s_dout_fault[CHANNEL_MAX_COUNT];

static dout_channel_map_t *find_dout_map(uint8_t channel_id) {
    for (size_t i = 0; i < DOUT_MAP_COUNT; i++) {
        if (s_dout_map[i].channel_id == channel_id) {
            return &s_dout_map[i];
        }
    }
    return NULL;
}

bool bsp_dout_init(void) {
    memset(s_dout_fault, 0, sizeof(s_dout_fault));
    return true;
}

int bsp_dout_init_channel(uint8_t channel_id) {
    dout_channel_map_t *map = find_dout_map(channel_id);
    if (map == NULL) return -ENOTSUP;

    map->state = 0;

#if defined(__ZEPHYR__)
    const struct device *gpio_dev = device_get_binding(map->gpio_dev_name);
    if (gpio_dev != NULL && device_is_ready(gpio_dev)) {
        gpio_pin_configure(gpio_dev, map->pin, GPIO_OUTPUT_INACTIVE);
    }
#endif
    return 0;
}

int bsp_dout_deinit_channel(uint8_t channel_id) {
    dout_channel_map_t *map = find_dout_map(channel_id);
    if (map == NULL) return -EINVAL;

    bsp_dout_set(channel_id, 0); /* Đảm bảo ngắt an toàn ngõ ra */
    s_dout_fault[channel_id - 1] = FAULT_NONE;
    return 0;
}

int bsp_dout_set(uint8_t channel_id, uint8_t state) {
    dout_channel_map_t *map = find_dout_map(channel_id);
    if (map == NULL) return -EINVAL;

    map->state = state ? 1 : 0;

#if defined(__ZEPHYR__)
    const struct device *gpio_dev = device_get_binding(map->gpio_dev_name);
    if (gpio_dev != NULL && device_is_ready(gpio_dev)) {
        gpio_pin_set(gpio_dev, map->pin, map->state);
    }
#endif
    return 0;
}

uint8_t bsp_dout_get(uint8_t channel_id) {
    dout_channel_map_t *map = find_dout_map(channel_id);
    if (map == NULL) return 0;
    return map->state;
}

bool bsp_dout_read(uint8_t channel_id, const ChannelConfig_t *cfg, float *out_value) {
    (void)cfg;
    if (out_value == NULL) return false;
    dout_channel_map_t *map = find_dout_map(channel_id);
    if (map == NULL) return false;

    if (channel_id <= CHANNEL_MAX_COUNT && s_dout_fault[channel_id - 1] != FAULT_NONE) {
        return false;
    }

    *out_value = (float)map->state;
    return true;
}

bool bsp_dout_health_check(uint8_t channel_id, uint16_t *out_fault_code) {
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) return false;
    uint16_t fault = s_dout_fault[channel_id - 1];
    if (out_fault_code != NULL) *out_fault_code = fault;
    return (fault == FAULT_NONE);
}

void bsp_dout_inject_fault(uint8_t channel_id, uint16_t fault_code, bool en) {
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) return;
    s_dout_fault[channel_id - 1] = en ? fault_code : FAULT_NONE;
}

static const channel_driver_ops_t s_bsp_dout_ops = {
    .init         = bsp_dout_init,
    .read         = bsp_dout_read,
    .health_check = bsp_dout_health_check,
    .inject_fault = bsp_dout_inject_fault
};

const channel_driver_ops_t *bsp_dout_get_ops(void) {
    return &s_bsp_dout_ops;
}
