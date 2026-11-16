#ifndef CHANNEL_CONFIG_H
#define CHANNEL_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* ĐƠN VỊ ĐO LƯỜNG (ENGINEERING UNITS)                                       */
/* ========================================================================= */

typedef enum {
    UNIT_CELSIUS = 0,       /**< °C (Nhiệt độ) */
    UNIT_PERCENT_RH,        /**< %RH (Độ ẩm) */
    UNIT_MILLIAMP,          /**< mA (Dòng công nghiệp 4-20mA) */
    UNIT_VOLT,              /**< V (Điện áp DC) */
    UNIT_PH,                /**< pH (Độ kiềm/axit) */
    UNIT_BAR,               /**< Bar (Áp suất) */
    UNIT_PPM,               /**< ppm (Nồng độ khí CO2/chất hòa tan) */
    UNIT_RAW                /**< Giá trị thô / không đơn vị */
} sensor_unit_t;

/* ========================================================================= */
/* HẰNG SỐ HỆ THỐNG                                                          */
/* ========================================================================= */

#define CHANNEL_MAX_COUNT          18          /**< Tổng số cổng vật lý của trạm */
#define CHANNEL_CONFIG_MAGIC       0x55AA1234  /**< Magic nhận diện phân vùng NVS hợp lệ */

/* ========================================================================= */
/* CÁC ENUM ĐỊNH DANH (CHẾ ĐỘ, ĐẠI LƯỢNG, CALIB)                             */
/* ========================================================================= */

/**
 * @brief Chế độ hoạt động của giao diện vật lý cổng.
 */
typedef enum {
    MODE_DISABLED       = 0,  /**< Cổng bị vô hiệu hóa */
    MODE_ADC_SINGLE     = 1,  /**< Đọc Analog đơn (0-3.3V hoặc qua Op-amp) */
    MODE_DIGITAL_IN     = 2,  /**< Đọc ngõ vào số logic (Opto cách ly) */
    MODE_PULSE_COUNTER  = 3,  /**< Đếm xung tốc độ cao (HCPL-0630) */
    MODE_OUT_CONTROL        = 4,  /**< Điều khiển ngõ ra (MOSFET Open-Drain / Relay) */
    MODE_I2C            = 5,  /**< Cảm biến giao tiếp bus I2C */
    MODE_RS485          = 6,  /**< Cảm biến / Thiết bị Modbus RTU qua RS485 */
    MODE_RS232          = 7,  /**< Giao tiếp RS232 chuẩn công nghiệp */
    MODE_UART           = 8   /**< Cổng UART nối tiếp thông thường */
} channel_interface_mode_t;

/**
 * @brief Loại đại lượng vật lý đo lường.
 */
typedef enum {
    METRIC_NONE             = 0,
    METRIC_TEMPERATURE      = 1,  /**< Nhiệt độ (°C, °F) */
    METRIC_HUMIDITY         = 2,  /**< Độ ẩm không khí (%RH) */
    METRIC_CURRENT_4_20MA   = 3,  /**< Dòng công nghiệp (mA) */
    METRIC_VOLTAGE          = 4,  /**< Điện áp nguồn / Pin (V) */
    METRIC_PH               = 5,  /**< Độ kiềm / axit (pH) */
    METRIC_SOIL_MOISTURE    = 6,  /**< Độ ẩm đất (%) */
    METRIC_PRESSURE         = 7,  /**< Áp suất khí / thủy lực (Bar, kPa) */
    METRIC_PULSE_COUNT      = 8,  /**< Số đếm xung tích lũy */
    METRIC_DIGITAL_STATE    = 9   /**< Trạng thái On/Off của relay hoặc ngõ vào */
} channel_metric_type_t;

/**
 * @brief Phương thức hiệu chuẩn (Calibration Type).
 */
typedef enum {
    CALIB_NONE          = 0,  /**< Không hiệu chuẩn (lấy giá trị raw) */
    CALIB_LINEAR        = 1,  /**< Hàm bậc nhất y = a*x + b (áp dụng cho ADC) */
    CALIB_MODBUS_RTU    = 2   /**< Đọc thanh ghi theo chuẩn Modbus RTU */
} channel_calib_type_t;

/* ========================================================================= */
/* CẤU TRÚC DỮ LIỆU ĐÓNG GÓI (PACKED BINARY PROTOCOL)                         */
/* ========================================================================= */

#pragma pack(push, 1)

/**
 * @brief Tham số hiệu chuẩn động cho từng loại cổng (Tối đa 12 bytes).
 */
typedef union {
    /* Nhánh dành cho cảm biến Analog ADC */
    struct {
        float    scale_a;       /**< Hệ số góc (slope) */
        float    offset_b;      /**< Độ lệch điểm 0 (bias/offset) */
        uint16_t raw_min_mv;    /**< Điện áp mV tối thiểu hợp lệ */
        uint16_t raw_max_mv;    /**< Điện áp mV tối đa hợp lệ */
    } adc;

    /* Nhánh dành cho cảm biến Modbus RTU qua RS485 */
    struct {
        uint8_t  slave_id;       /**< Địa chỉ Slave (1 - 247) */
        uint8_t  function_code;  /**< Mã hàm đọc (0x03 Read Holding, 0x04 Read Input) */
        uint16_t register_addr;  /**< Địa chỉ thanh ghi */
        uint8_t  byte_swap_mode; /**< Thứ tự byte: 0: AB CD, 1: CD AB, 2: BA DC */
    } modbus;
} channel_calib_params_t;

/**
 * @brief Cấu hình chi tiết của 1 kênh vật lý (Kích thước chuẩn: đúng 32 bytes).
 */
typedef struct {
    uint8_t                channel_id;         /**< ID cổng vật lý: 1 .. 18 */
    uint8_t                interface_mode;     /**< channel_interface_mode_t */
    uint8_t                is_enabled;         /**< 0: Tắt, 1: Đang hoạt động */
    uint16_t               sample_interval_ms; /**< Chu kỳ lấy mẫu định thời (ms) */
    uint8_t                metric_type;        /**< channel_metric_type_t */
    uint8_t                unit_type;          /**< sensor_unit_t */
    uint8_t                calib_type;         /**< channel_calib_type_t */
    channel_calib_params_t calib;              /**< Tham số calib động (12 bytes) */
    float                  alarm_low;          /**< Ngưỡng cảnh báo dưới */
    float                  alarm_high;         /**< Ngưỡng cảnh báo trên */
    float                  alarm_hysteresis;   /**< Độ trễ chống dội dao động */
} ChannelConfig_t;

/**
 * @brief Metadata Header kiểm soát phiên bản và tính toàn vẹn (Đúng 12 bytes).
 */
typedef struct {
    uint32_t magic_number;   /**< 0x55AA1234: Nhận diện dữ liệu hợp lệ */
    uint32_t config_version; /**< Bộ đếm phiên bản tăng dần cho Boot-Sync */
    uint32_t crc32;          /**< Mã kiểm tra toàn vẹn mảng cấu hình */
} config_table_header_t;

#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* CHANNEL_CONFIG_H */