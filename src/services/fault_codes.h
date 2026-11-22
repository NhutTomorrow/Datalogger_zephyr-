/**
 * @file fault_codes.h
 * @brief Định nghĩa mã lỗi chuẩn hóa toàn hệ thống Datalogger (Pure Contract / Macro definitions).
 * Không chứa logic thực thi hay stateful context, an toàn cho cả BSP Drivers và Services sử dụng.
 */

#ifndef FAULT_CODES_H
#define FAULT_CODES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* ĐỊNH NGHĨA MÃ LỖI CHUẨN HÓA (STANDARDIZED FAULT CODES)                   */
/* ========================================================================= */

#define FAULT_NONE                      0x0000

/* Nhóm 0x1000 - 0x1FFF: Cảm biến & Tín hiệu đo lường */
#define FAULT_4_20MA_LOOP_OPEN          0x1001  /**< Dòng < 3.6mA: Hở mạch / đứt cáp tín hiệu */
#define FAULT_4_20MA_LOOP_OVERCURRENT   0x1002  /**< Dòng > 21.0mA: Chập mạch / hỏng transmitter */
#define FAULT_SENSOR_TIMEOUT            0x1003  /**< Cảm biến không phản hồi */
#define FAULT_SENSOR_DATA_OUT_OF_BOUNDS 0x1004  /**< Giá trị vượt ngưỡng vật lý hợp lý */

/* Nhóm 0x2000 - 0x2FFF: Lưu trữ & Hệ thống tập tin */
#define FAULT_SD_CARD_NOT_DETECTED      0x2001  /**< Không nhận diện được thẻ nhớ SD */
#define FAULT_SD_WRITE_ERROR            0x2002  /**< Ghi file log dữ liệu thất bại / hỏng sector */
#define FAULT_STORAGE_FULL              0x2003  /**< Bộ nhớ thẻ SD đã đầy (>95%) */

/* Nhóm 0x3000 - 0x3FFF: Nguồn điện & Phần cứng */
#define FAULT_RTC_BATTERY_LOW           0x3001  /**< Pin backup RTC CR2032 yếu / mất mốc thời gian */
#define FAULT_SUPPLY_UNDERVOLTAGE       0x3002  /**< Điện áp nguồn trạm sụt dưới ngưỡng an toàn (<10.5V) */
#define FAULT_DISPLAY_LCD               0x3003  /**< Màn hình LCD không hoạt động */

/* Nhóm 0x4000 - 0x4FFF: Giao tiếp & Mạng */
#define FAULT_COMM_UART_PACKET_LOSS     0x4001  /**< Lỗi CRC liên tục trên kênh UART liên kết */
#define FAULT_AI_GATEWAY_TIMEOUT        0x4002  /**< ESP32 AI Gateway không phản hồi Keep-alive */

#define FAULT_MAX_ACTIVE_LIST           8       /**< Tối đa số lỗi đồng thời quản lý */

#ifdef __cplusplus
}
#endif

#endif /* FAULT_CODES_H */
