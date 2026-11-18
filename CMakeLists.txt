cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(datalogger_zephyr)

# Đăng ký các thư mục Include (Tất cả đã nằm dưới src/)
target_include_directories(app PRIVATE
    src/services
    src/mock
    src/app
    src/bsp
)

# Đăng ký các file mã nguồn C
target_sources(app PRIVATE
    src/main.c
    
    
    # Services (Pure C99)
    src/services/com_protocol.c
    src/services/fault_manager.c
    src/services/button_manager.c
    src/services/keypad_multitap.c
    src/services/channel_config.c
    src/services/channel_hw_caps.c
    src/services/channel_manager.c
    src/services/driver_registry.c
  
    
    # Application Logic (Pure C99)
    src/app/app_fsm.c
    
    # Zephyr BSP Drivers
    src/bsp/bsp_uart_zephyr.c
    src/bsp/bsp_button_zephyr.c
    src/bsp/bsp_lcd_zephyr.c
    src/bsp/lcd_font.c
    src/bsp/bsp_adc_zephyr.c
    src/bsp/bsp_dout_zephyr.c
    src/bsp/bsp_din_zephyr.c
    src/bsp/bsp_rs485_zephyr.c
)