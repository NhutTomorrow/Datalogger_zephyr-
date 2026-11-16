/**
 * @file main.c
 * @brief Điểm khởi tạo hệ thống Datalogger trên Zephyr RTOS (BKIT STM32F407).
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "channel_manager.h"
#include "fault_manager.h"
#include "app_fsm.h"
#include "bsp_uart.h"
#include "bsp_button.h"
#include "bsp_lcd.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Context toàn cục của hệ thống */
static app_context_t g_app;
static channel_manager_t g_channel_mgr;
static fault_manager_t g_fault_mgr;

/* Node LED debug trên bo mạch (PE4) */
#define DEBUG_LED_NODE DT_NODELABEL(debug_led)
static const struct gpio_dt_spec s_debug_led = GPIO_DT_SPEC_GET(DEBUG_LED_NODE, gpios);

int main(void)
{
    LOG_INF("==============================================");
    LOG_INF("  DATALOGGER BKIT STM32F407 - ZEPHYR RTOS     ");
    LOG_INF("==============================================");

    /* 1. Cấu hình GPIO Debug LED */
    if (gpio_is_ready_dt(&s_debug_led)) {
        gpio_pin_configure_dt(&s_debug_led, GPIO_OUTPUT_INACTIVE);
    }

    /* 2. Khởi tạo Fault Manager */
    fault_manager_init(&g_fault_mgr);

    /* 3. Khởi tạo Channel Manager (Mount Flash NVS, nạp cấu hình đã lưu) */
    channel_manager_init(&g_channel_mgr, &g_fault_mgr);

    // /* Cấu hình 2 kênh mặc định (Nhiệt độ CH_01 và Dòng 4-20mA CH_04) */
    // ChannelConfig_t cfg_temp = {
    //     .channel_id = 1,
    //     .interface_mode = MODE_ADC_SINGLE,
    //     .is_enabled = 1,
    //     .sample_interval_ms = 1000,
    //     .metric_type = METRIC_TEMPERATURE,
    //     .unit_type = UNIT_CELSIUS,
    //     .calib_type = CALIB_NONE
    // };
    // channel_manager_configure(&g_channel_mgr, &cfg_temp, "NhietDo", mock_sensor_get_temperature_ops());

    // ChannelConfig_t cfg_curr = {
    //     .channel_id = 4,
    //     .interface_mode = MODE_ADC_SINGLE,
    //     .is_enabled = 1,
    //     .sample_interval_ms = 1000,
    //     .metric_type = METRIC_CURRENT_4_20MA,
    //     .unit_type = UNIT_MILLIAMP,
    //     .calib_type = CALIB_NONE
    // };
    // channel_manager_configure(&g_channel_mgr, &cfg_curr, "Dong4_20mA", mock_sensor_get_current_loop_ops());

    /* Cấu hình kênh CH_15 đo Nhiệt độ qua RS485 (Đầu dò Modbus RTU) */
    ChannelConfig_t cfg_rs485_temp = {
        .channel_id = 15,
        .interface_mode = MODE_RS485,
        .is_enabled = 1,
        .sample_interval_ms = 2000,
        .metric_type = METRIC_TEMPERATURE,
        .unit_type = UNIT_CELSIUS,
        .calib_type = CALIB_MODBUS_RTU,
        .calib.modbus = {
            .slave_id = 1,
            .function_code = 0x03,
            .register_addr = 0x0000,
            .byte_swap_mode = 0
        }
    };
    channel_manager_configure(&g_channel_mgr, &cfg_rs485_temp, "Temp", NULL);

    /* 4. Khởi tạo ngoại vi UART với Zephyr k_msgq */
    bsp_uart_init();

    /* 5. Khởi tạo FSM với các driver BSP trực tiếp */
    app_dependencies_t deps = {
        .channel_mgr  = &g_channel_mgr,
        .fault_mgr    = &g_fault_mgr,
        .button_count = 16
    };
    app_init(&g_app, &deps);

    LOG_INF("He thong da khoi dong thanh cong. Bat dau FSM loop...");

    /* 6. Vòng lặp điều phối chính (FSM Step + Debug LED) */
    uint32_t last_led_toggle_ms = 0;

    while (1) {
        uint32_t now_ms = k_uptime_get_32();

        /* Thực thi một chu kỳ của App FSM (kèm xử lý byte UART từ k_msgq trong Thread Mode) */
        app_run_step(&g_app, now_ms);

        /* Nhấp nháy Debug LED chu kỳ 500ms */
        if (now_ms - last_led_toggle_ms >= 500) {
            last_led_toggle_ms = now_ms;
            if (gpio_is_ready_dt(&s_debug_led)) {
                gpio_pin_toggle_dt(&s_debug_led);
            }
        }

        /* Nhường CPU cho các luồng hệ thống và ISR */
        k_msleep(10);
    }

    return 0;
}
