/**
 * @file channel_config.c
 * @brief Kiểm chứng kích thước bộ nhớ và tính toàn vẹn căn lề của ChannelConfig.
 */

#include "channel_config.h"

/*
 * Đảm bảo kích thước cấu trúc dữ liệu luôn chuẩn xác khi biên dịch trên
 * bất kỳ toolchain nào (GCC ARM, GCC Xtensa, Clang...).
 * Nếu có lỗi căn lề hoặc padding, trình biên dịch sẽ dừng ngay tại đây.
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(channel_calib_params_t) == 12, "Loi padding trong channel_calib_params_t");
_Static_assert(sizeof(ChannelConfig_t) == 32, "Loi padding trong ChannelConfig_t");
_Static_assert(sizeof(config_table_header_t) == 12, "Loi padding trong config_table_header_t");
#endif

const char *sensor_unit_to_str(sensor_unit_t unit)
{
    switch (unit) {
        case UNIT_CELSIUS:    return "oC";
        case UNIT_PERCENT_RH: return "%";
        case UNIT_MILLIAMP:   return "mA";
        case UNIT_VOLT:       return "V";
        case UNIT_PH:         return "pH";
        case UNIT_BAR:        return "bar";
        case UNIT_PPM:        return "ppm";
        case UNIT_RAW:
        default:              return "";
    }
}
