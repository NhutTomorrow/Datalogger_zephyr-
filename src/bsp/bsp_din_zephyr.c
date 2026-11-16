/**
 * @file bsp_din_zephyr.c
 * @brief Hiện thực Driver Digital Input Zephyr cho Opto cách ly trên BKIT STM32F407.
 */

#include "bsp_din.h"
#include "fault_manager.h"
#include <string.h>
#include <errno.h>

#if defined(__ZEPHYR__)
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(bsp_din, LOG_LEVEL_INF);
#endif

typedef struct {
    uint8_t channel_id;
    const char *gpio_dev_name;
    uint8_t pin;
} din_channel_map_t;

static const din_channel_map_t s_din_map[] = {
    {  8, "GPIOA", 5 }, /* PA5 - HCPL-0630 X0 */
    {  9, "GPIOA", 6 }, /* PA6 - HCPL-0630 X1 */
    { 10, "GPIOC", 4 }, /* PC4 - EL3H7 X2 */
    { 11, "GPIOC", 5 }  /* PC5 - EL3H7 X3 */
};

#define DIN_MAP_COUNT (sizeof(s_din_map) / sizeof(s_din_map[0]))

static uint16_t s_din_fault[CHANNEL_MAX_COUNT];

static const din_channel_map_t *find_din_map(uint8_t channel_id) {
    for (size_t i = 0; i < DIN_MAP_COUNT; i++) {
        if (s_din_map[i].channel_id == channel_id) {
            return &s_din_map[i];
        }
    }
    return NULL;
}

bool bsp_din_init(void) {
    memset(s_din_fault, 0, sizeof(s_din_fault));
    return true;
}

int bsp_din_init_channel(uint8_t channel_id) {
    const din_channel_map_t *map = find_din_map(channel_id);
    if (map == NULL) return -ENOTSUP;

#if defined(__ZEPHYR__)
    const struct device *gpio_dev = device_get_binding(map->gpio_dev_name);
    if (gpio_dev != NULL && device_is_ready(gpio_dev)) {
        gpio_pin_configure(gpio_dev, map->pin, GPIO_INPUT | GPIO_PULL_UP);
    }
#endif
    return 0;
}

int bsp_din_deinit_channel(uint8_t channel_id) {
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) return -EINVAL;
    s_din_fault[channel_id - 1] = FAULT_NONE;
    return 0;
}

bool bsp_din_read(uint8_t channel_id, float *out_value) {
    if (out_value == NULL) return false;
    const din_channel_map_t *map = find_din_map(channel_id);
    if (map == NULL) return false;

    if (channel_id <= CHANNEL_MAX_COUNT && s_din_fault[channel_id - 1] != FAULT_NONE) {
        return false;
    }

#if defined(__ZEPHYR__)
    const struct device *gpio_dev = device_get_binding(map->gpio_dev_name);
    if (gpio_dev != NULL && device_is_ready(gpio_dev)) {
        int val = gpio_pin_get(gpio_dev, map->pin);
        *out_value = (val > 0) ? 1.0f : 0.0f;
        return true;
    }
#endif

    /* Giả lập cho Host: trả về 1.0f (Opto kéo trở pull-up) */
    *out_value = 1.0f;
    return true;
}

bool bsp_din_health_check(uint8_t channel_id, uint16_t *out_fault_code) {
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) return false;
    uint16_t fault = s_din_fault[channel_id - 1];
    if (out_fault_code != NULL) *out_fault_code = fault;
    return (fault == FAULT_NONE);
}

void bsp_din_inject_fault(uint8_t channel_id, uint16_t fault_code, bool en) {
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) return;
    s_din_fault[channel_id - 1] = en ? fault_code : FAULT_NONE;
}

static const channel_driver_ops_t s_bsp_din_ops = {
    .init         = bsp_din_init,
    .read         = bsp_din_read,
    .health_check = bsp_din_health_check,
    .inject_fault = bsp_din_inject_fault
};

const channel_driver_ops_t *bsp_din_get_ops(void) {
    return &s_bsp_din_ops;
}
