/*
 * bsp_lcd_zephyr.c
 *
 *  Created on: Sep 30, 2026
 *      Author: ASUS
 */


/*
 * lcd.c
 */

#include <lcd_font.h>
#include "bsp_lcd.h"
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <stm32f4xx_hal.h>
#include <stm32f4xx_ll_fsmc.h>

#define FSMC_RES_NODE DT_NODELABEL(fsmc_res)
#define LCD_BLK_NODE  DT_NODELABEL(lcd_blk)

static const struct gpio_dt_spec s_lcd_res = GPIO_DT_SPEC_GET(FSMC_RES_NODE, gpios);
static const struct gpio_dt_spec s_lcd_blk = GPIO_DT_SPEC_GET(LCD_BLK_NODE, gpios);

static SRAM_HandleTypeDef s_hsram1;

static void bsp_fsmc_hardware_init(void)
{
    /* 1. Bật Clock cho FSMC, GPIOD, GPIOE */
    __HAL_RCC_FSMC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /* 2. Cấu hình GPIO cho FSMC Alternate Function AF12 */
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Mode = GPIO_MODE_AF_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Alternate = GPIO_AF12_FSMC;

    /* PE3 (A19), PE7..PE15 (D4..D12) */
    gpio_init.Pin = GPIO_PIN_3 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9
                  | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13
                  | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gpio_init);

    /* PD0..PD1 (D2..D3), PD4 (NOE), PD5 (NWE), PD7 (NE1), PD8..PD10 (D13..D15), PD14..PD15 (D0..D1) */
    gpio_init.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4 | GPIO_PIN_5
                  | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10
                  | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gpio_init);

    /* 3. Cấu hình FSMC NOR/SRAM Bank 1 */
    FSMC_NORSRAM_TimingTypeDef Timing = {0};
    FSMC_NORSRAM_TimingTypeDef ExtTiming = {0};

    s_hsram1.Instance = FSMC_NORSRAM_DEVICE;
    s_hsram1.Extended = FSMC_NORSRAM_EXTENDED_DEVICE;
    s_hsram1.Init.NSBank = FSMC_NORSRAM_BANK1;
    s_hsram1.Init.DataAddressMux = FSMC_DATA_ADDRESS_MUX_DISABLE;
    s_hsram1.Init.MemoryType = FSMC_MEMORY_TYPE_SRAM;
    s_hsram1.Init.MemoryDataWidth = FSMC_NORSRAM_MEM_BUS_WIDTH_16;
    s_hsram1.Init.BurstAccessMode = FSMC_BURST_ACCESS_MODE_DISABLE;
    s_hsram1.Init.WaitSignalPolarity = FSMC_WAIT_SIGNAL_POLARITY_LOW;
    s_hsram1.Init.WrapMode = FSMC_WRAP_MODE_DISABLE;
    s_hsram1.Init.WaitSignalActive = FSMC_WAIT_TIMING_BEFORE_WS;
    s_hsram1.Init.WriteOperation = FSMC_WRITE_OPERATION_ENABLE;
    s_hsram1.Init.WaitSignal = FSMC_WAIT_SIGNAL_DISABLE;
    s_hsram1.Init.ExtendedMode = FSMC_EXTENDED_MODE_ENABLE;
    s_hsram1.Init.AsynchronousWait = FSMC_ASYNCHRONOUS_WAIT_DISABLE;
    s_hsram1.Init.WriteBurst = FSMC_WRITE_BURST_DISABLE;
    s_hsram1.Init.PageSize = FSMC_PAGE_SIZE_NONE;

    /* Timing */
    Timing.AddressSetupTime = 15;
    Timing.AddressHoldTime = 15;
    Timing.DataSetupTime = 60;
    Timing.BusTurnAroundDuration = 0;
    Timing.CLKDivision = 16;
    Timing.DataLatency = 17;
    Timing.AccessMode = FSMC_ACCESS_MODE_A;

    /* ExtTiming */
    ExtTiming.AddressSetupTime = 8;
    ExtTiming.AddressHoldTime = 15;
    ExtTiming.DataSetupTime = 9;
    ExtTiming.BusTurnAroundDuration = 0;
    ExtTiming.CLKDivision = 16;
    ExtTiming.DataLatency = 17;
    ExtTiming.AccessMode = FSMC_ACCESS_MODE_A;

    HAL_SRAM_Init(&s_hsram1, &Timing, &ExtTiming);
}

unsigned char s[50];

_lcd_dev lcddev;

static void LCD_WR_DATA(uint16_t data);
static uint16_t LCD_RD_DATA(void);
static uint32_t mypow(uint8_t m, uint8_t n);

void LCD_WR_REG(uint16_t reg) {
	LCD->LCD_REG = reg;
}

void LCD_WR_DATA(uint16_t data) {
	LCD->LCD_RAM = data;
}

uint16_t LCD_RD_DATA(void) {
	__IO uint16_t ram;
	ram = LCD->LCD_RAM;
	return ram;
}


void lcd_set_address(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
	LCD_WR_REG(0x2a);
	LCD_WR_DATA(x1 >> 8);
	LCD_WR_DATA(x1 & 0xff);
	LCD_WR_DATA(x2 >> 8);
	LCD_WR_DATA(x2 & 0xff);
	LCD_WR_REG(0x2b);
	LCD_WR_DATA(y1 >> 8);
	LCD_WR_DATA(y1 & 0xff);
	LCD_WR_DATA(y2 >> 8);
	LCD_WR_DATA(y2 & 0xff);
	LCD_WR_REG(0x2c);
}

void lcd_set_cursor(uint16_t x, uint16_t y) {
	LCD_WR_REG(0x2a);
	LCD_WR_DATA(x >> 8);
	LCD_WR_DATA(x & 0xff);
	LCD_WR_REG(0x2b);
	LCD_WR_DATA(y >> 8);
	LCD_WR_DATA(y & 0xff);
}

void lcd_set_display_on(void) {
	LCD_WR_REG(0X29);
}

void lcd_set_display_off(void) {
	LCD_WR_REG(0X28);
}

uint16_t lcd_read_point(uint16_t x, uint16_t y) {
	uint16_t r = 0, g = 0, b = 0;
	lcd_set_cursor(x, y);
	LCD_WR_REG(0X2E);
	r = LCD_RD_DATA();
	r = LCD_RD_DATA();
	b = LCD_RD_DATA();
	g = r & 0XFF;
	g <<= 8;
	return (((r >> 11) << 11) | ((g >> 10) << 5) | (b >> 11));
}

/**
 * @brief  Fill all pixels with a color
 * @param  color Color to fill the screen
 * @retval None
 */
void lcd_clear(uint16_t color) {
	uint16_t i, j;
	lcd_set_address(0, 0, lcddev.width - 1, lcddev.height - 1);
	for (i = 0; i < lcddev.width; i++) {
		for (j = 0; j < lcddev.height; j++) {
			LCD_WR_DATA(color);
		}
	}
}

/**
 * @brief  Fill a group of pixels with a color
 * @param  xsta	Start column
 * @param  ysta	Start row
 * @param  xend	End column
 * @param  yend	End row
 * @param  color Color to fill
 * @retval None
 */
void lcd_fill(uint16_t xsta, uint16_t ysta, uint16_t xend, uint16_t yend,
		uint16_t color) {
	uint16_t i, j;
	lcd_set_address(xsta, ysta, xend - 1, yend - 1);
	for (i = ysta; i < yend; i++) {
		for (j = xsta; j < xend; j++) {
			LCD_WR_DATA(color);
		}
	}
}

/**
 * @brief  Fill a pixel with a color
 * @param  x X coordinate
 * @param  y Y coordinate
 * @param  color Color to fill
 * @retval None
 */
void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color) {
	lcd_set_address(x, y, x, y);
	LCD_WR_DATA(color);
}

/**
 * @brief  Draw a line with a color
 * @param  x1 X coordinate of start point
 * @param  y1 Y coordinate of start point
 * @param  x2 X coordinate of end point
 * @param  y2 Y coordinate of end point
 * @param  color Color to fill
 * @retval None
 */
void lcd_draw_line(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
		uint16_t color) {
	uint16_t t;
	int xerr = 0, yerr = 0, delta_x, delta_y, distance;
	int incx, incy, uRow, uCol;
	delta_x = x2 - x1;
	delta_y = y2 - y1;
	uRow = x1;
	uCol = y1;
	if (delta_x > 0)
		incx = 1;
	else if (delta_x == 0)
		incx = 0;
	else {
		incx = -1;
		delta_x = -delta_x;
	}
	if (delta_y > 0)
		incy = 1;
	else if (delta_y == 0)
		incy = 0;
	else {
		incy = -1;
		delta_y = -delta_y;
	}
	if (delta_x > delta_y)
		distance = delta_x;
	else
		distance = delta_y;
	for (t = 0; t < distance + 1; t++) {
		lcd_draw_point(uRow, uCol, color);
		xerr += delta_x;
		yerr += delta_y;
		if (xerr > distance) {
			xerr -= distance;
			uRow += incx;
		}
		if (yerr > distance) {
			yerr -= distance;
			uCol += incy;
		}
	}
}

void lcd_draw_rectangle(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2,
		uint16_t color) {
	lcd_draw_line(x1, y1, x2, y1, color);
	lcd_draw_line(x1, y1, x1, y2, color);
	lcd_draw_line(x1, y2, x2, y2, color);
	lcd_draw_line(x2, y1, x2, y2, color);
}

void lcd_show_char(uint16_t x, uint16_t y, uint8_t character, uint16_t fc,
		uint16_t bc, uint8_t sizey, uint8_t mode) {
	uint8_t temp = 0, sizex, t, m = 0;
	uint16_t i, TypefaceNum;
	uint16_t x0 = x;
	sizex = sizey / 2;
	TypefaceNum = (sizex / 8 + ((sizex % 8) ? 1 : 0)) * sizey;
	character = character - ' ';
	lcd_set_address(x, y, x + sizex - 1, y + sizey - 1);
	for (i = 0; i < TypefaceNum; i++) {
		if (sizey == 12)
			;
		else if (sizey == 16)
			temp = ascii_1608[character][i];
		else if (sizey == 24)
			temp = ascii_2412[character][i];
		else if (sizey == 32)
			temp = ascii_3216[character][i];
		else
			return;
		for (t = 0; t < 8; t++) {
			if (!mode) {
				if (temp & (0x01 << t))
					LCD_WR_DATA(fc);
				else
					LCD_WR_DATA(bc);
				m++;
				if (m % sizex == 0) {
					m = 0;
					break;
				}
			} else {
				if (temp & (0x01 << t))
					lcd_draw_point(x, y, fc);
				x++;
				if ((x - x0) == sizex) {
					x = x0;
					y++;
					break;
				}
			}
		}
	}
}

uint32_t mypow(uint8_t m, uint8_t n) {
	uint32_t result = 1;
	while (n--)
		result *= m;
	return result;
}

void lcd_show_int_num(uint16_t x, uint16_t y, uint16_t num, uint8_t len,
		uint16_t fc, uint16_t bc, uint8_t sizey) {
	uint8_t t, temp;
	uint8_t enshow = 0;
	uint8_t sizex = sizey / 2;
	for (t = 0; t < len; t++) {
		temp = (num / mypow(10, len - t - 1)) % 10;
		if (enshow == 0 && t < (len - 1)) {
			if (temp == 0) {
				lcd_show_char(x + t * sizex, y, ' ', fc, bc, sizey, 0);
				continue;
			} else
				enshow = 1;

		}
		lcd_show_char(x + t * sizex, y, temp + 48, fc, bc, sizey, 0);
	}
}

void lcd_show_float_num(uint16_t x, uint16_t y, float num, uint8_t len,
		uint16_t fc, uint16_t bc, uint8_t sizey) {
	uint8_t t, temp, sizex;
	uint16_t num1;
	sizex = sizey / 2;
	num1 = num * 100;
	for (t = 0; t < len; t++) {
		temp = (num1 / mypow(10, len - t - 1)) % 10;
		if (t == (len - 2)) {
			lcd_show_char(x + (len - 2) * sizex, y, '.', fc, bc, sizey, 0);
			t++;
			len += 1;
		}
		lcd_show_char(x + t * sizex, y, temp + 48, fc, bc, sizey, 0);
	}
}

void lcd_show_picture(uint16_t x, uint16_t y, uint16_t length, uint16_t width,
		const uint8_t pic[]) {
	uint8_t picH, picL;
	uint16_t i, j;
	uint32_t k = 0;
	lcd_set_address(x, y, x + length - 1, y + width - 1);
	for (i = 0; i < length; i++) {
		for (j = 0; j < width; j++) {
			picH = pic[k * 2];
			picL = pic[k * 2 + 1];
			LCD_WR_DATA(picH << 8 | picL);
			k++;
		}
	}
}

void lcd_set_direction(uint8_t dir) {
	if ((dir >> 4) % 4) {
		lcddev.width = 320;
		lcddev.height = 240;
	} else {
		lcddev.width = 240;
		lcddev.height = 320;
	}
}

void lcd_init(void) {
	bsp_fsmc_hardware_init();

	if (gpio_is_ready_dt(&s_lcd_res)) {
		gpio_pin_configure_dt(&s_lcd_res, GPIO_OUTPUT_ACTIVE);
		gpio_pin_set_dt(&s_lcd_res, 0);
		k_msleep(50);
		gpio_pin_set_dt(&s_lcd_res, 1);
		k_msleep(50);
	}
	if (gpio_is_ready_dt(&s_lcd_blk)) {
		gpio_pin_configure_dt(&s_lcd_blk, GPIO_OUTPUT_INACTIVE);
	}
	lcd_set_direction(DFT_SCAN_DIR);
	LCD_WR_REG(0XD3);
	lcddev.id = LCD_RD_DATA();
	lcddev.id = LCD_RD_DATA();
	lcddev.id = LCD_RD_DATA();
	lcddev.id <<= 8;
	lcddev.id |= LCD_RD_DATA();

	LCD_WR_REG(0xCF);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0xC1);
	LCD_WR_DATA(0X30);
	LCD_WR_REG(0xED);
	LCD_WR_DATA(0x64);
	LCD_WR_DATA(0x03);
	LCD_WR_DATA(0X12);
	LCD_WR_DATA(0X81);
	LCD_WR_REG(0xE8);
	LCD_WR_DATA(0x85);
	LCD_WR_DATA(0x10);
	LCD_WR_DATA(0x7A);
	LCD_WR_REG(0xCB);
	LCD_WR_DATA(0x39);
	LCD_WR_DATA(0x2C);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x34);
	LCD_WR_DATA(0x02);
	LCD_WR_REG(0xF7);
	LCD_WR_DATA(0x20);
	LCD_WR_REG(0xEA);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_REG(0xC0);    //Power control
	LCD_WR_DATA(0x1B);   //VRH[5:0]
	LCD_WR_REG(0xC1);    //Power control
	LCD_WR_DATA(0x01);   //SAP[2:0];BT[3:0]
	LCD_WR_REG(0xC5);    //VCM control
	LCD_WR_DATA(0x30); 	 //3F
	LCD_WR_DATA(0x30); 	 //3C
	LCD_WR_REG(0xC7);    //VCM control2
	LCD_WR_DATA(0XB7);
	LCD_WR_REG(0x36);    // Memory Access Control

	LCD_WR_DATA(0x08 | DFT_SCAN_DIR);
	LCD_WR_REG(0x3A);
	LCD_WR_DATA(0x55);
	LCD_WR_REG(0xB1);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x1A);
	LCD_WR_REG(0xB6);    // Display Function Control
	LCD_WR_DATA(0x0A);
	LCD_WR_DATA(0xA2);
	LCD_WR_REG(0xF2);    // 3Gamma Function Disable
	LCD_WR_DATA(0x00);
	LCD_WR_REG(0x26);    //Gamma curve selected
	LCD_WR_DATA(0x01);
	LCD_WR_REG(0xE0);    //Set Gamma
	LCD_WR_DATA(0x0F);
	LCD_WR_DATA(0x2A);
	LCD_WR_DATA(0x28);
	LCD_WR_DATA(0x08);
	LCD_WR_DATA(0x0E);
	LCD_WR_DATA(0x08);
	LCD_WR_DATA(0x54);
	LCD_WR_DATA(0XA9);
	LCD_WR_DATA(0x43);
	LCD_WR_DATA(0x0A);
	LCD_WR_DATA(0x0F);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_REG(0XE1);    //Set Gamma
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x15);
	LCD_WR_DATA(0x17);
	LCD_WR_DATA(0x07);
	LCD_WR_DATA(0x11);
	LCD_WR_DATA(0x06);
	LCD_WR_DATA(0x2B);
	LCD_WR_DATA(0x56);
	LCD_WR_DATA(0x3C);
	LCD_WR_DATA(0x05);
	LCD_WR_DATA(0x10);
	LCD_WR_DATA(0x0F);
	LCD_WR_DATA(0x3F);
	LCD_WR_DATA(0x3F);
	LCD_WR_DATA(0x0F);
	LCD_WR_REG(0x2B);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x01);
	LCD_WR_DATA(0x3f);
	LCD_WR_REG(0x2A);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0x00);
	LCD_WR_DATA(0xef);
	LCD_WR_REG(0x11); // Exit Sleep
	k_msleep(120);
	LCD_WR_REG(0x29); // Display on
	if (gpio_is_ready_dt(&s_lcd_blk)) {
		gpio_pin_set_dt(&s_lcd_blk, 1);
	}
}

static void _draw_circle_8(int xc, int yc, int x, int y, uint16_t c) {
	lcd_draw_point(xc + x, yc + y, c);

	lcd_draw_point(xc - x, yc + y, c);

	lcd_draw_point(xc + x, yc - y, c);

	lcd_draw_point(xc - x, yc - y, c);

	lcd_draw_point(xc + y, yc + x, c);

	lcd_draw_point(xc - y, yc + x, c);

	lcd_draw_point(xc + y, yc - x, c);

	lcd_draw_point(xc - y, yc - x, c);
}

void lcd_draw_circle(int xc, int yc, uint16_t c, int r, int fill)
{
	int x = 0, y = r, yi, d;

	d = 3 - 2 * r;

	if (fill) {
		while (x <= y) {
			for (yi = x; yi <= y; yi++)
				_draw_circle_8(xc, yc, x, yi, c);

			if (d < 0) {
				d = d + 4 * x + 6;
			} else {
				d = d + 4 * (x - y) + 10;
				y--;
			}
			x++;
		}
	} else {
		while (x <= y) {
			_draw_circle_8(xc, yc, x, y, c);
			if (d < 0) {
				d = d + 4 * x + 6;
			} else {
				d = d + 4 * (x - y) + 10;
				y--;
			}
			x++;
		}
	}
}

void lcd_show_string(uint16_t x, uint16_t y, char *str, uint16_t fc, uint16_t bc,
		uint8_t sizey, uint8_t mode) {
	uint16_t x0 = x;
	uint8_t bHz = 0;
	while (*str != 0) {
		if (!bHz) {
			if (x > (lcddev.width - sizey / 2) || y > (lcddev.height - sizey))
				return;
			if (*str > 0x80)
				bHz = 1;
			else {
				if (*str == 0x0D) {
					y += sizey;
					x = x0;
					str++;
				} else {
					lcd_show_char(x, y, *str, fc, bc, sizey, mode);
					x += sizey / 2;
				}
				str++;
			}
		}
	}
}

void lcd_show_string_center(uint16_t x, uint16_t y, char *str, uint16_t fc, uint16_t bc,
		uint8_t sizey, uint8_t mode) {
	uint16_t len = strlen((const char*) str);
	uint16_t x1 = (lcddev.width - len * 8) / 2;
	lcd_show_string(x + x1, y, str, fc, bc, sizey, mode);
}
void bsp_lcd_fsmc_set_backlight(bool enable){
	if (gpio_is_ready_dt(&s_lcd_blk)) {
		gpio_pin_set_dt(&s_lcd_blk, enable ? 1 : 0);
	}
}
/**
 * @brief 1. Màn hình khởi động hệ thống (Boot Screen)
 */
void bsp_lcd_show_boot_screen(const char *title, const char *version) {
    /* Xóa nền màu xanh NAVY */
    lcd_clear(NAVY);
    /* Khung Banner tiêu đề */
    lcd_fill(0, 20, lcddev.width - 1, 65, DARKBLUE);
    lcd_draw_rectangle(0, 20, lcddev.width - 1, 65, GRAY);
    /* Tiêu đề chính căn giữa (font 24, chữ vàng trên nền xanh đậm) */
    lcd_show_string(30, 30, (char *)(title ? title : "DATALOGGER AI STATION"), YELLOW, DARKBLUE, 24, 0);
    /* Thông tin cấu hình trạm Datalogger (font 16) */
    lcd_show_string(25, 80,  "Board: BKIT ARM STM32F407ZGT6", CYAN, NAVY, 16, 0);
    lcd_show_string(25, 105, "Edge AI Link : ESP32 UART", GREEN, NAVY, 16, 0);
    lcd_show_string(25, 130, "Sensor In    : 4-20mA / LM35 / Mock", WHITE, NAVY, 16, 0);
    /* Phiên bản Firmware */
    char ver_buf[32];
    snprintf(ver_buf, sizeof(ver_buf), "Firmware     : %s", version ? version : "v1.0.0");
    lcd_show_string(25, 155, ver_buf, YELLOW, NAVY, 16, 0);
    /* Dòng trạng thái khởi động */
    lcd_show_string_center(0, 195, "System Initializing...", WHITE, NAVY, 16, 0);
}
/**
 * @brief 2. Hiển thị thông số quan trắc các kênh đo (Telemetry Screen)
 * @note Không dùng lcd_clear() để tránh bị nhấp nháy màn hình (flicker) mỗi chu kỳ 1s
 */
void bsp_lcd_show_telemetry(const sensor_telemetry_snapshot_t *snapshot) {
    if (snapshot == NULL) return;
    /* Thanh Header cố định ở đỉnh màn hình */
    lcd_clear(BLACK);
    lcd_fill(0, 0, lcddev.width - 1, 26, DARKBLUE);
    lcd_show_string(12, 5, "DATALOGGER TELEMETRY [BKIT]", WHITE, DARKBLUE, 16, 0);
    uint16_t start_y = 34;
    /* Vẽ danh sách từng kênh đo */
    for (uint8_t i = 0; i < snapshot->total_channels; i++) {
        const sensor_channel_t *ch = &snapshot->channels[i];
        char line_buf[48];
        uint16_t row_y = start_y + (i * 26);
        if (row_y + 24 > lcddev.height - 26) break; /* Chừa chỗ cho Status bar */
        /* Nếu kênh bình thường -> Nền đen viền xám; Nếu lỗi -> Nền đỏ viền vàng */
        uint16_t bg_color = ch->is_valid ? BLACK : RED;
        uint16_t border_color = ch->is_valid ? GRAY : YELLOW;
        lcd_fill(8, row_y, lcddev.width - 9, row_y + 22, bg_color);
        lcd_draw_rectangle(8, row_y, lcddev.width - 9, row_y + 22, border_color);
        if (ch->is_valid) {
            snprintf(line_buf, sizeof(line_buf), "CH_%u [%-6s]: %6.2f %s",
                     ch->channel_id, ch->name, ch->value, sensor_unit_to_str(ch->unit));
            lcd_show_string(14, row_y + 3, line_buf, GREEN, bg_color, 16, 0);
        } else {
            snprintf(line_buf, sizeof(line_buf), "CH_%u [%-10s]: FAULT [0x%04X]",
                     ch->channel_id, ch->name, ch->fault_code);
            lcd_show_string(14, row_y + 3, line_buf, WHITE, bg_color, 16, 0);
        }
    }

    /* Vẽ lại khung Debug Button nếu đã từng nhấn phím (tránh bị lcd_clear xóa) */
    //bsp_lcd_debug_show_button(-1, 0, 0);

    /* Vẽ lại khung Debug UART Log từ ESP32 */
    //bsp_lcd_draw_uart_log_box();
}
/**
 * @brief 3. Hiển thị hộp cảnh báo khi có sự cố / mã lỗi
 */
void bsp_lcd_show_fault_alert(uint16_t fault_code, const char *code_str, const char *msg) {
    uint16_t box_y = 145;
    uint16_t box_h = 60;
    /* Nếu không có lỗi (0x0000) -> Xóa sạch hộp cảnh báo trả về nền đen */
    if (fault_code == 0) {
        lcd_fill(8, box_y, lcddev.width - 9, box_y + box_h, BLACK);
        return;
    }
    /* Khi có lỗi -> Hộp cảnh báo màu ĐỎ nổi bật */
    lcd_fill(8, box_y, lcddev.width - 9, box_y + box_h, RED);
    lcd_draw_rectangle(8, box_y, lcddev.width - 9, box_y + box_h, YELLOW);
    /* Dòng 1: Mã lỗi và tên lỗi */
    char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "! CANH BAO: %s [0x%04X]",
             code_str ? code_str : "ERR", fault_code);
    lcd_show_string(14, box_y + 6, title_buf, YELLOW, RED, 16, 0);
    /* Dòng 2: Nguyên nhân / hướng dẫn ngắn */
    lcd_show_string(14, box_y + 30, (char *)(msg ? msg : "Su co he thong quan trac"), WHITE, RED, 16, 0);
}
/**
 * @brief 4. Thanh trạng thái hệ thống ở đáy màn hình (Bottom Status Bar)
 */
void bsp_lcd_show_status_bar(const char *fsm_state_str, const char *ai_link_str, uint8_t active_fault_count) {
    uint16_t bar_y = lcddev.height - 24;
    uint16_t bar_bg = (active_fault_count > 0) ? RED : DARKBLUE;
    /* Vẽ dải màu nền trạng thái */
    lcd_fill(0, bar_y, lcddev.width - 1, lcddev.height - 1, bar_bg);
    lcd_draw_line(0, bar_y, lcddev.width - 1, bar_y, GRAY);
    /* Format nội dung: [Trạng thái FSM] | [Liên kết AI] | [Số lỗi active] */
    char status_buf[64];
    snprintf(status_buf, sizeof(status_buf), "[%s] | %s | ERR: %u",
             fsm_state_str ? fsm_state_str : "IDLE",
             ai_link_str ? ai_link_str : "AI: OK",
             active_fault_count);
    lcd_show_string(10, bar_y + 4, status_buf, WHITE, bar_bg, 16, 0);
}

void bsp_lcd_show_fault_list(const uint16_t *fault_codes, uint8_t count, int8_t highlight_idx){
	lcd_fill(0, 0, lcddev.width , 26, DARKBLUE);
	lcd_show_string(10, 5, "DIAGNOSTIC LIST", WHITE, DARKBLUE, 16, 0);
	lcd_fill(0, 26, lcddev.width , lcddev.height - 50, BLACK);

	uint16_t start_y = 38;
	    for (uint8_t i = 0; i < count; i++) {
	        uint16_t row_y = start_y + (i * 26);
	        if (row_y + 22 > lcddev.height - 28) break;

	        char code_buf[32];
	        bool is_highlight = (highlight_idx >= 0 && i == (uint8_t)highlight_idx);
	        uint16_t bg_color = is_highlight ? DARKBLUE : BLACK;
	        uint16_t text_color = is_highlight ? YELLOW : RED;

	        lcd_fill(15, row_y, lcddev.width - 30, 22, bg_color);
	        snprintf(code_buf, sizeof(code_buf), "%s ERR_%04X", is_highlight ? ">>" : "  ", fault_codes[i]);
	        lcd_show_string(20, row_y + 5, code_buf, text_color, bg_color, 24, 0);
	    }

	    /* Thanh trạng thái đáy */
	    lcd_fill(0, lcddev.height - 24, lcddev.width, 24, GRAY);
	    lcd_show_string(70, lcddev.height - 40, "[DIAGNOSTIC]", WHITE, GRAY, 16, 0);
	    lcd_show_string(10, lcddev.height - 20, "Hoi AI de duoc huong dan sua", WHITE, GRAY, 16, 0);
}
void bsp_lcd_show_error_banner_toggle(void){

	lcd_clear(BLACK);
//		lcd_fill(15, 60, lcddev.width - 30, 110, RED);

		lcd_show_string(80, 75, "ERROR", RED, BLACK, 32, 0);
		lcd_show_string(30, 120, "HE THONG GHI NHAN LOI", WHITE, BLACK, 16, 0);
		lcd_show_string(30, 140, "NHAN NUT HOAC HOI AI", WHITE, BLACK, 16, 0);

}

static int g_last_btn_bit = -1;
static uint16_t g_last_btn_spi = 0;
static uint8_t g_last_btn_id = 0;

void bsp_lcd_debug_show_button(int bit_i, uint16_t raw_spi, uint8_t mapped_id) {
    if (bit_i >= 0) {
        g_last_btn_bit = bit_i;
        g_last_btn_spi = raw_spi;
        g_last_btn_id = mapped_id;
    }

    if (g_last_btn_bit < 0) return;

    uint16_t box_y = 100;
    uint16_t box_h = 68;
    lcd_fill(8, box_y, lcddev.width - 9, box_y + box_h, NAVY);
    lcd_draw_rectangle(8, box_y, lcddev.width - 9, box_y + box_h, YELLOW);

    char buf[32];
    lcd_show_string(14, box_y + 4, "[KEYPAD TESTER]", WHITE, NAVY, 16, 0);

    snprintf(buf, sizeof(buf), "BIT i: [ %2d ]", g_last_btn_bit);
    lcd_show_string(14, box_y + 22, buf, YELLOW, NAVY, 24, 0);

    snprintf(buf, sizeof(buf), "SPI:0x%04X -> ID:%u", g_last_btn_spi, g_last_btn_id);
    lcd_show_string(14, box_y + 48, buf, CYAN, NAVY, 16, 0);
}

/* ========================================================================= */
/* UART DEBUG LOG TRÊN MÀN HÌNH LCD                                          */
/* ========================================================================= */

static uint32_t g_uart_rx_bytes = 0;
static uint32_t g_uart_rx_packets = 0;
static uint8_t  g_uart_last_cmd = 0;
static uint8_t  g_uart_last_seq = 0;
static uint16_t g_uart_last_len = 0;
static bool     g_uart_has_packet = false;
static uint8_t  g_uart_recent_bytes[4] = {0};
static char     g_uart_custom_msg[36] = {0};

static const char *comm_cmd_name_str(uint8_t cmd) {
    switch (cmd) {
    case 0x01: return "PING";
    case 0x81: return "PONG";
    case 0x02: return "GET_TELEM";
    case 0x82: return "RESP_TELEM";
    case 0x03: return "GET_FAULTS";
    case 0x83: return "RESP_FAULTS";
    case 0x04: return "CLR_FAULTS";
    case 0x05: return "SET_LOG";
    case 0x06: return "INJ_FAULT";
    case 0x80: return "ACK";
    case 0xFF: return "NACK";
    case 0x07: return "BACKLIGHT";
    case 0x08: return "RESET_MCU";

    default:   return "CMD_UNKNOWN";
    }
}

void bsp_lcd_on_uart_byte_received(uint8_t byte) {
    g_uart_rx_bytes++;
    g_uart_recent_bytes[0] = g_uart_recent_bytes[1];
    g_uart_recent_bytes[1] = g_uart_recent_bytes[2];
    g_uart_recent_bytes[2] = g_uart_recent_bytes[3];
    g_uart_recent_bytes[3] = byte;
}

void bsp_lcd_update_uart_packet_log(uint8_t cmd, uint8_t seq, uint16_t len) {
    g_uart_rx_packets++;
    g_uart_last_cmd = cmd;
    g_uart_last_seq = seq;
    g_uart_last_len = len;
    g_uart_has_packet = true;
    bsp_lcd_draw_uart_log_box();
}

void bsp_lcd_log_uart_string(const char *msg) {
    if (msg != NULL) {
        strncpy(g_uart_custom_msg, msg, sizeof(g_uart_custom_msg) - 1);
        g_uart_custom_msg[sizeof(g_uart_custom_msg) - 1] = '\0';
    }
    bsp_lcd_draw_uart_log_box();
}

void bsp_lcd_draw_uart_log_box(void) {
    uint16_t box_y = 180;
    uint16_t box_h = 68;
    uint16_t border_color = g_uart_has_packet ? GREEN : CYAN;
    uint16_t bg_color = DARKBLUE;

    lcd_fill(8, box_y, lcddev.width - 9, box_y + box_h, bg_color);
    lcd_draw_rectangle(8, box_y, lcddev.width - 9, box_y + box_h, border_color);

    char title_buf[36];
    snprintf(title_buf, sizeof(title_buf), "[ESP32 UART3] %s", g_uart_has_packet ? "ONLINE" : "WAITING");
    lcd_show_string(14, box_y + 4, title_buf, g_uart_has_packet ? GREEN : CYAN, bg_color, 16, 0);

    char line2[36];
    if (g_uart_custom_msg[0] != '\0') {
        snprintf(line2, sizeof(line2), "%-24s", g_uart_custom_msg);
        lcd_show_string(14, box_y + 24, line2, YELLOW, bg_color, 16, 0);
    } else if (g_uart_has_packet) {
        snprintf(line2, sizeof(line2), "RX:%s (0x%02X) #%u",
                 comm_cmd_name_str(g_uart_last_cmd), g_uart_last_cmd, g_uart_last_seq);
        lcd_show_string(14, box_y + 24, line2, YELLOW, bg_color, 16, 0);
    } else {
        if (g_uart_rx_bytes > 0) {
            snprintf(line2, sizeof(line2), "Hex:[%02X %02X %02X %02X]",
                     g_uart_recent_bytes[0], g_uart_recent_bytes[1], g_uart_recent_bytes[2], g_uart_recent_bytes[3]);
            lcd_show_string(14, box_y + 24, line2, WHITE, bg_color, 16, 0);
        } else {
            lcd_show_string(14, box_y + 24, "No UART data yet...", GRAY, bg_color, 16, 0);
        }
    }

    char line3[36];
    snprintf(line3, sizeof(line3), "Pkts: %lu | Bytes: %lu",
             (unsigned long)g_uart_rx_packets, (unsigned long)g_uart_rx_bytes);
    lcd_show_string(14, box_y + 46, line3, WHITE, bg_color, 16, 0);
}

void bsp_lcd_show_auth_screen(const char *input_str, char preview_char, bool masked)
{
    /* Tiêu đề */
    lcd_fill(0, 0, lcddev.width, 30, DARKBLUE);
    lcd_show_string(20, 6, "XAC THUC DANG NHAP", WHITE, DARKBLUE, 16, 0);

    /* Vùng thân hộp thoại */
    lcd_fill(0, 30, lcddev.width, lcddev.height - 30, BLACK);
    lcd_show_string(30, 60, "Nhap ma PIN / Password:", WHITE, BLACK, 16, 0);

    /* Hộp nhập mật khẩu */
    uint16_t box_x = 30;
    uint16_t box_y = 90;
    uint16_t box_w = lcddev.width - 60;
    uint16_t box_h = 36;
    lcd_draw_rectangle(box_x, box_y, box_x + box_w, box_y + box_h, CYAN);
    lcd_fill(box_x + 1, box_y + 1, box_x + box_w - 1, box_y + box_h - 1, LGRAY);

    /* Format hiển thị chuỗi */
    char display_buf[32] = {0};
    uint8_t len = input_str ? (uint8_t)strlen(input_str) : 0;
    if (len > 24) len = 24;

    for (uint8_t i = 0; i < len; i++) {
        display_buf[i] = masked ? '*' : input_str[i];
    }
    if (preview_char != '\0' && len < 24) {
        display_buf[len] = preview_char;
        display_buf[len + 1] = '\0';
    } else {
        display_buf[len] = '\0';
    }

    lcd_show_string(box_x + 10, box_y + 8, display_buf, BLACK, LGRAY, 24, 0);

    /* Gợi ý phím thao tác */
    lcd_show_string(20, 150, "1..9, 0: Nhap ky tu", GRAY, BLACK, 16, 0);
    lcd_show_string(20, 175, "*: Doi Mode | #: +/-", GRAY, BLACK, 16, 0);
    lcd_show_string(20, 200, "BACK: Xoa | ENTER: Chot", YELLOW, BLACK, 16, 0);
}

void bsp_lcd_show_menu(const char *title, const char *items[], uint8_t count, uint8_t selected_idx)
{
    /* Tiêu đề Menu */
    lcd_fill(0, 0, lcddev.width, 30, DARKBLUE);
    lcd_show_string(20, 6, (char *)(title ? title : "MENU HE THONG"), WHITE, DARKBLUE, 16, 0);

    /* Xóa vùng danh sách */
    lcd_fill(0, 30, lcddev.width, lcddev.height - 30, BLACK);

    uint16_t start_y = 40;
    for (uint8_t i = 0; i < count; i++) {
        uint16_t row_y = start_y + (i * 26);
        if (row_y + 24 > lcddev.height - 30) break;

        bool is_selected = (i == selected_idx);
        uint16_t bg = is_selected ? DARKBLUE : BLACK;
        uint16_t fg = is_selected ? YELLOW : WHITE;

        lcd_fill(10, row_y, lcddev.width - 20, 24, bg);

        char item_buf[40];
        snprintf(item_buf, sizeof(item_buf), "%s %d. %s",
                 is_selected ? ">>" : "  ",
                 i + 1,
                 items[i] ? items[i] : "");
        lcd_show_string(15, row_y + 4, item_buf, fg, bg, 16, 0);
    }

    /* Thanh trạng thái điều hướng */
    uint16_t bar_y = lcddev.height - 24;
    lcd_fill(0, bar_y, lcddev.width, 24, GRAY);
    lcd_show_string(10, bar_y + 4, "UP/DOWN: Chon | ENTER: Vao | BACK: Ra", BLACK, GRAY, 16, 0);
}

void bsp_lcd_show_dashboard_paged(const sensor_telemetry_snapshot_t *snapshot,
                                  uint8_t page,
                                  bool logging_active,
                                  const char *ai_str,
                                  uint8_t fault_count)
{
    /* 1. Header */
    lcd_fill(0, 0, lcddev.width, 28, DARKBLUE);
    char header_buf[48];
    uint8_t total_pages = 1;
    if (snapshot != NULL && snapshot->total_channels > 0) {
        total_pages = (snapshot->total_channels + 3) / 4;
    }
    snprintf(header_buf, sizeof(header_buf), "DATALOGGER [%s] P%u/%u",
             logging_active ? "REC" : "STOP", page + 1, total_pages);
    lcd_show_string(10, 6, header_buf, logging_active ? GREEN : YELLOW, DARKBLUE, 16, 0);

    /* 2. Thân hiển thị 4 kênh đo */
    lcd_fill(0, 28, lcddev.width, lcddev.height - 28, BLACK);

    uint8_t start_ch = page * 4;
    uint16_t start_y = 36;

    if (snapshot != NULL) {
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t ch_idx = start_ch + i;
            if (ch_idx >= snapshot->total_channels) break;

            const sensor_channel_t *ch = &snapshot->channels[ch_idx];
            uint16_t row_y = start_y + (i * 38);
            if (row_y + 34 > lcddev.height - 26) break;

            uint16_t bg = ch->is_valid ? BLACK : RED;
            uint16_t border = ch->is_valid ? GRAY : YELLOW;
            lcd_fill(8, row_y, lcddev.width - 9, row_y + 34, bg);
            lcd_draw_rectangle(8, row_y, lcddev.width - 9, row_y + 34, border);

            char line1[36], line2[36];
            snprintf(line1, sizeof(line1), "CH_%u: %-12s", ch->channel_id, ch->name);
            lcd_show_string(14, row_y + 4, line1, WHITE, bg, 16, 0);

            if (ch->is_valid) {
                snprintf(line2, sizeof(line2), "VAL: %6.2f %s", ch->value, sensor_unit_to_str(ch->unit));
                lcd_show_string(14, row_y + 18, line2, GREEN, bg, 16, 0);
            } else {
                snprintf(line2, sizeof(line2), "FAULT: [0x%04X]", ch->fault_code);
                lcd_show_string(14, row_y + 18, line2, YELLOW, bg, 16, 0);
            }
        }
    }

    /* 3. Thanh trạng thái đáy */
    uint16_t bar_y = lcddev.height - 24;
    uint16_t bar_bg = (fault_count > 0) ? RED : DARKBLUE;
    lcd_fill(0, bar_y, lcddev.width, 24, bar_bg);
    char status_buf[64];
    snprintf(status_buf, sizeof(status_buf), "%s | ERR:%u | UP/DN:Trang | E:Menu",
             ai_str ? ai_str : "AI:--", fault_count);
    lcd_show_string(8, bar_y + 4, status_buf, WHITE, bar_bg, 16, 0);
}

void bsp_lcd_show_edit_enum(const char *param_name, const char *option_str, uint8_t idx, uint8_t total)
{
    /* Header */
    lcd_fill(0, 0, lcddev.width, 28, DARKBLUE);
    lcd_show_string(10, 6, "CHINH SUA TU CHON", WHITE, DARKBLUE, 16, 0);

    /* Thân */
    lcd_fill(0, 28, lcddev.width, lcddev.height - 28, BLACK);

    char name_buf[48];
    snprintf(name_buf, sizeof(name_buf), "Tham so: %s", param_name ? param_name : "");
    lcd_show_string(20, 50, name_buf, WHITE, BLACK, 16, 0);

    /* Hộp hiển thị lựa chọn */
    uint16_t box_y = 85;
    lcd_draw_rectangle(20, box_y, lcddev.width - 20, box_y + 40, CYAN);
    lcd_fill(21, box_y + 1, lcddev.width - 21, box_y + 39, DARKBLUE);

    char opt_buf[48];
    snprintf(opt_buf, sizeof(opt_buf), "<  %s  >", option_str ? option_str : "");
    lcd_show_string(30, box_y + 12, opt_buf, YELLOW, DARKBLUE, 24, 0);

    char count_buf[32];
    snprintf(count_buf, sizeof(count_buf), "(Lua chon %u/%u)", idx + 1, total);
    lcd_show_string(30, 140, count_buf, GRAY, BLACK, 16, 0);

    /* Hint */
    uint16_t bar_y = lcddev.height - 24;
    lcd_fill(0, bar_y, lcddev.width, 24, GRAY);
    lcd_show_string(10, bar_y + 4, "UP/DOWN: Chon | ENTER: Luu | BACK: Huy", BLACK, GRAY, 16, 0);
}

void bsp_lcd_show_edit_numeric(const char *param_name, const char *val_str, const char *unit_str)
{
    /* Header */
    lcd_fill(0, 0, lcddev.width, 28, DARKBLUE);
    lcd_show_string(10, 6, "CHINH SUA SO LIEU", WHITE, DARKBLUE, 16, 0);

    /* Thân */
    lcd_fill(0, 28, lcddev.width, lcddev.height - 28, BLACK);

    char name_buf[48];
    snprintf(name_buf, sizeof(name_buf), "Tham so: %s", param_name ? param_name : "");
    lcd_show_string(20, 50, name_buf, WHITE, BLACK, 16, 0);

    /* Hộp nhập số */
    uint16_t box_y = 85;
    lcd_draw_rectangle(20, box_y, lcddev.width - 20, box_y + 40, GREEN);
    lcd_fill(21, box_y + 1, lcddev.width - 21, box_y + 39, LGRAY);

    char val_buf[48];
    snprintf(val_buf, sizeof(val_buf), "%s %s", val_str ? val_str : "", unit_str ? unit_str : "");
    lcd_show_string(30, box_y + 10, val_buf, BLACK, LGRAY, 24, 0);

    /* Hướng dẫn phím */
    lcd_show_string(20, 140, "Phim 0..9: Nhap so", GRAY, BLACK, 16, 0);
    lcd_show_string(20, 160, "*: Dau cham . | #: Dau +/-", GRAY, BLACK, 16, 0);

    /* Hint */
    uint16_t bar_y = lcddev.height - 24;
    lcd_fill(0, bar_y, lcddev.width, 24, GRAY);
    lcd_show_string(10, bar_y + 4, "BACK: Xoa | ENTER: Xac nhan", BLACK, GRAY, 16, 0);
}

void bsp_lcd_show_save_confirm(void)
{
    /* Header */
    lcd_fill(0, 0, lcddev.width, 28, DARKBLUE);
    lcd_show_string(10, 6, "XAC NHAN LUU CAU HINH", WHITE, DARKBLUE, 16, 0);

    /* Thân */
    lcd_fill(0, 28, lcddev.width, lcddev.height - 28, BLACK);

    lcd_show_string(30, 60, "Co thay doi cau hinh!", YELLOW, BLACK, 16, 0);
    lcd_show_string(30, 85, "Luu vao Flash (Rolling Slot)?", WHITE, BLACK, 16, 0);

    /* Nút chọn */
    lcd_fill(30, 130, lcddev.width - 30, 165, DARKBLUE);
    lcd_draw_rectangle(30, 130, lcddev.width - 30, 165, GREEN);
    lcd_show_string(45, 140, "[ENTER] : DONG Y LUU", GREEN, DARKBLUE, 16, 0);

    lcd_fill(30, 180, lcddev.width - 30, 215, DARKBLUE);
    lcd_draw_rectangle(30, 180, lcddev.width - 30, 215, RED);
    lcd_show_string(45, 190, "[BACK]  : HUY BO THAY DOI", RED, DARKBLUE, 16, 0);
}

void bsp_lcd_show_ai_proposal(uint8_t ch_id, const char *mode_str, const char *metric_str)
{
    /* Header */
    lcd_fill(0, 0, lcddev.width, 28, DARKBLUE);
    lcd_show_string(10, 6, "AI CONFIG PROPOSAL", YELLOW, DARKBLUE, 16, 0);

    /* Body */
    lcd_fill(0, 28, lcddev.width, lcddev.height - 28, BLACK);

    char buf[40];
    snprintf(buf, sizeof(buf), "AI de xuat cau hinh CH_%02u:", ch_id);
    lcd_show_string(20, 50, buf, CYAN, BLACK, 16, 0);

    snprintf(buf, sizeof(buf), "Mode  : %s", mode_str ? mode_str : "UNKNOWN");
    lcd_show_string(30, 80, buf, WHITE, BLACK, 16, 0);

    snprintf(buf, sizeof(buf), "Metric: %s", metric_str ? metric_str : "UNKNOWN");
    lcd_show_string(30, 105, buf, WHITE, BLACK, 16, 0);

    /* Decision boxes */
    lcd_fill(20, 140, lcddev.width - 20, 175, DARKBLUE);
    lcd_draw_rectangle(20, 140, lcddev.width - 20, 175, GREEN);
    lcd_show_string(35, 150, "[ENTER] : DONG Y AP DUNG", GREEN, DARKBLUE, 16, 0);

    lcd_fill(20, 190, lcddev.width - 20, 225, DARKBLUE);
    lcd_draw_rectangle(20, 190, lcddev.width - 20, 225, RED);
    lcd_show_string(35, 200, "[BACK]  : TU CHOI / CANCEL", RED, DARKBLUE, 16, 0);
}





