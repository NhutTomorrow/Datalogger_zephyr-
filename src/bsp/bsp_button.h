/**
 * @file bsp_button.h
 * @brief Driver đọc 16 nút nhấn qua 2 IC 74HC165 nối tiếp trên Zephyr RTOS (STM32F407).
 */

#ifndef BSP_BUTTON_74HC165_H
#define BSP_BUTTON_74HC165_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t button_count[16];

/**
 * @brief Khởi tạo phần cứng GPIO (chân BTN_LOAD) và ngoại vi SPI1.
 */
bool bsp_button_74hc165_init(void);

/**
 * @brief Đọc trạng thái thô của toàn bộ 16 nút nhấn trong 1 chu kỳ dịch.
 */
uint16_t bsp_button_74hc165_read_all_raw(void);

/**
 * @brief Đọc trạng thái 1 nút bấm cụ thể (0 đến 15).
 */
bool bsp_button_74hc165_read_single(uint8_t btn_id);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BUTTON_74HC165_H */
