/**
 * @file bsp_button_zephyr.c
 * @brief Hiện thực driver 74HC165 qua SPI1 trên Zephyr RTOS bo mạch BKIT STM32F407.
 */

#include "bsp_button.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <string.h>

/* Variables */
uint16_t button_count[16] = {0};
static uint16_t button_spi_buffer = 0x0000;

#define BTN_LOAD_NODE DT_NODELABEL(btn_load)
static const struct gpio_dt_spec s_btn_load = GPIO_DT_SPEC_GET(BTN_LOAD_NODE, gpios);

#define SPI_DEV_NODE DT_NODELABEL(spi1)
static const struct device *s_spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);

static const struct spi_config s_spi_cfg = {
    .frequency = 1000000,
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
};

bool bsp_button_74hc165_init(void)
{
    if (!gpio_is_ready_dt(&s_btn_load)) {
        return false;
    }
    gpio_pin_configure_dt(&s_btn_load, GPIO_OUTPUT_ACTIVE);

    if (!device_is_ready(s_spi_dev)) {
        return false;
    }

    return true;
}

uint16_t bsp_button_74hc165_read_all_raw(void)
{
    if (!device_is_ready(s_spi_dev) || !gpio_is_ready_dt(&s_btn_load)) {
        return 0;
    }

    /* 1. Tạo xung chốt nạp dữ liệu song song vào 74HC165 (PL Low rồi High) */
    gpio_pin_set_dt(&s_btn_load, 0);
    k_busy_wait(2);
    gpio_pin_set_dt(&s_btn_load, 1);

    /* 2. Đọc 2 byte (16 bit) nối tiếp qua SPI1 */
    uint8_t rx_raw[2] = {0};
    const struct spi_buf rx_buf = {
        .buf = rx_raw,
        .len = sizeof(rx_raw),
    };
    const struct spi_buf_set rx_set = {
        .buffers = &rx_buf,
        .count = 1,
    };

    if (spi_read(s_spi_dev, &s_spi_cfg, &rx_set) == 0) {
        memcpy(&button_spi_buffer, rx_raw, sizeof(button_spi_buffer));
    }

    /* 3. Giải mã 16 bit thành ma trận phím */
    int button_index = 0;
    uint16_t mask = 0x8000;
    uint16_t new_bitmap = 0;

    for (int i = 0; i < 16; i++)
    {
        if (i >= 0 && i <= 3) {
            button_index = i + 4;   /* Hàng 2: ID 4..7 */
        } else if (i >= 4 && i <= 7) {
            button_index = 7 - i;   /* Hàng 1: ID 3..0 (đảo ngược) */
        } else if (i >= 8 && i <= 11) {
            button_index = i + 4;   /* Hàng 4: ID 12..15 */
        } else {
            button_index = 23 - i;  /* Hàng 3: ID 11..8 (đảo ngược) */
        }

        /* Phím nhấn: nối GND (mức 0). Phím nhả: kéo trở lên 3V3 (mức 1) */
        if (button_spi_buffer & mask)
        {
            button_count[button_index] = 0;
        }
        else
        {
            button_count[button_index]++;
            new_bitmap |= (1U << button_index);
        }

        mask >>= 1;
    }

    return new_bitmap;
}

bool bsp_button_74hc165_read_single(uint8_t btn_id)
{
    if (btn_id >= 16)
        return false;
    return (button_count[btn_id] > 0);
}

