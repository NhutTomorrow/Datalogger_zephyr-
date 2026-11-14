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
