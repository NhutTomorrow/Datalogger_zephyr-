/**
 * @file bsp_uart_zephyr.c
 * @brief Hiện thực driver ngoại vi UART cho Zephyr RTOS trên bo mạch BKIT STM32F407.
 *        Sử dụng k_msgq để chuyển byte từ ISR sang Thread Context.
 */

#include "bsp_uart.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>

#define UART_NODE DT_NODELABEL(usart6)

static const struct device *s_uart_dev = DEVICE_DT_GET(UART_NODE);

/* Hàng đợi 128 byte nhận từ UART trong ISR */
K_MSGQ_DEFINE(s_uart_rx_msgq, sizeof(uint8_t), 128, 4);

static void uart_isr_callback(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    while (uart_irq_update(dev) && uart_irq_is_pending(dev)) {
        if (uart_irq_rx_ready(dev)) {
            uint8_t rx_char;
            while (uart_fifo_read(dev, &rx_char, 1) == 1) {
                /* Đẩy byte vào queue ngay trong ISR, K_NO_WAIT đảm bảo không bao giờ block */
                k_msgq_put(&s_uart_rx_msgq, &rx_char, K_NO_WAIT);
            }
        }
    }
}

bool bsp_uart_init(void)
{
    if (!device_is_ready(s_uart_dev)) {
        return false;
    }

    uart_irq_callback_user_data_set(s_uart_dev, uart_isr_callback, NULL);
    uart_irq_rx_enable(s_uart_dev);

    return true;
}

bool bsp_uart_send_bytes(const uint8_t *data, uint16_t length)
{
    if (!device_is_ready(s_uart_dev) || data == NULL || length == 0) {
        return false;
    }

    for (uint16_t i = 0; i < length; i++) {
        uart_poll_out(s_uart_dev, data[i]);
    }
    return true;
}

bool bsp_uart_get_byte(uint8_t *byte)
{
    if (byte == NULL) {
        return false;
    }
    return (k_msgq_get(&s_uart_rx_msgq, byte, K_NO_WAIT) == 0);
}

bool bsp_uart_is_busy(void)
{
    return false;
}
