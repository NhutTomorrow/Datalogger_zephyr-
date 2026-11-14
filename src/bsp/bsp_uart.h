/**
 * @file bsp_uart.h
 * @brief Driver ngoại vi UART cho Zephyr RTOS trên STM32F407 (Giao tiếp ESP32 Gateway).
 */

#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo driver UART trên Zephyr với ngắt nhận đẩy vào k_msgq.
 */
bool bsp_uart_init(void);

/**
 * @brief Gửi một khối byte ra đường truyền UART.
 */
bool bsp_uart_send_bytes(const uint8_t *data, uint16_t length);

/**
 * @brief Rút 1 byte từ hàng đợi nhận k_msgq (Non-blocking).
 * @param[out] byte Con trỏ lưu byte nhận được
 * @return true nếu lấy được byte, false nếu hàng đợi rỗng
 */
bool bsp_uart_get_byte(uint8_t *byte);

/**
 * @brief Kiểm tra xem đường truyền UART có đang bận gửi không.
 */
bool bsp_uart_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_H */
