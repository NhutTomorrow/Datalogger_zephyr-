/**
 * @file bsp_lcd.h
 * @brief Driver màn hình LCD FSMC tương thích Zephyr RTOS trên bo mạch BKIT STM32F407.
 */

#ifndef BSP_LCD_H_
#define BSP_LCD_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "sensor_manager.h"

/* Hướng quét hiển thị */
#define DFT_SCAN_DIR  L2R_U2D

#define L2R_U2D  0x00
#define L2R_D2U  0x80
#define R2L_U2D  0x40
#define R2L_D2U  0xc0

#define U2D_L2R  0x20
#define U2D_R2L  0x60
#define D2U_L2R  0xa0
#define D2U_R2L  0xe0

/* Địa chỉ truy cập FSMC SRAM Bank 1 */
#define LCD_BASE        ((uint32_t)(0x60000000 | 0x000ffffe))
#define LCD             ((LCD_TypeDef *) LCD_BASE)

/* Bảng màu RGB565 */
#define WHITE            0xFFFF
#define BLACK            0x0000
#define BLUE             0x001F
#define BRED             0XF81F
#define GRED             0XFFE0
#define GBLUE            0X07FF
#define RED              0xF800
#define MAGENTA          0xF81F
#define GREEN            0x07E0
#define CYAN             0x7FFF
#define YELLOW           0xFFE0
#define BROWN            0XBC40
#define BRRED            0XFC07
#define GRAY             0X8430
#define DARKBLUE         0X01CF
#define LIGHTBLUE        0X7D7C
#define GRAYBLUE         0X5458
#define LIGHTGREEN       0X841F
#define LIGHTGRAY        0XEF5B
#define LGRAY            0XC618
#define LGRAYBLUE        0XA651
#define LBBLUE           0X2B12
#define NAVY             0x000F

/* Cấu trúc quản lý kích thước màn hình */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t id;
} _lcd_dev;

/* Cấu trúc thanh ghi điều khiển LCD qua FSMC */
typedef struct {
    volatile uint16_t LCD_REG;
    volatile uint16_t LCD_RAM;
} LCD_TypeDef;

extern _lcd_dev lcddev;

/* Các hàm đồ họa cơ bản */
void lcd_set_cursor(uint16_t x, uint16_t y);
void lcd_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void lcd_set_display_on(void);
void lcd_set_display_off(void);
uint16_t lcd_read_point(uint16_t x, uint16_t y);
void lcd_clear(uint16_t color);

void lcd_fill(uint16_t xsta, uint16_t ysta, uint16_t xend, uint16_t yend, uint16_t color);
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color);
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t color);
void lcd_show_char(uint16_t x, uint16_t y, uint8_t character, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);
void lcd_show_int_num(uint16_t x, uint16_t y, uint16_t num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t sizey);
void lcd_show_float_num(uint16_t x, uint16_t y, float num, uint8_t len, uint16_t fc, uint16_t bc, uint8_t sizey);
void lcd_show_picture(uint16_t x, uint16_t y, uint16_t length, uint16_t width, const uint8_t pic[]);
void lcd_set_direction(uint8_t dir);
void lcd_init(void);
void lcd_draw_circle(int xc, int yc, uint16_t c, int r, int fill);
void lcd_show_string(uint16_t x, uint16_t y, char *str, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);
void lcd_show_string_center(uint16_t x, uint16_t y, char *str, uint16_t fc, uint16_t bc, uint8_t sizey, uint8_t mode);

/* API điều khiển hiển thị LCD trực tiếp */
void bsp_lcd_fsmc_set_backlight(bool enable);
void bsp_lcd_show_boot_screen(const char *title, const char *version);
void bsp_lcd_show_telemetry(const sensor_telemetry_snapshot_t *snapshot);
void bsp_lcd_show_fault_alert(uint16_t fault_code, const char *code_str, const char *msg);
void bsp_lcd_show_status_bar(const char *fsm_state_str, const char *ai_link_str, uint8_t active_fault_count);
void bsp_lcd_show_fault_list(const uint16_t *fault_codes, uint8_t count, int8_t highlight_idx);
void bsp_lcd_show_error_banner_toggle(void);
void bsp_lcd_debug_show_button(int bit_i, uint16_t raw_spi, uint8_t mapped_id);
void bsp_lcd_update_uart_packet_log(uint8_t cmd, uint8_t seq, uint16_t len);
void bsp_lcd_on_uart_byte_received(uint8_t byte);
void bsp_lcd_log_uart_string(const char *msg);
void bsp_lcd_draw_uart_log_box(void);

#endif /* BSP_LCD_H_ */
