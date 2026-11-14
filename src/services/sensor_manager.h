/**
 * @file sensor_if.h
 * @brief Giao diện trừu tượng cho cảm biến theo kiến trúc Kênh đo (Channel-based Architecture).
 * @note File thuần C99, độc lập 100% phần cứng.
 *       Cho phép thêm bớt cảm biến (nhiệt ẩm, 4-20mA, pH, áp suất...) mà KHÔNG cần sửa file này.
 */

#ifndef SENSOR_IF_H
#define SENSOR_IF_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* CẤU HÌNH & HẰNG SỐ KÊNH ĐO                                                */
/* ========================================================================= */

#define SENSOR_MAX_CHANNELS         8       /**< Số kênh đo tối đa trạm hỗ trợ */
#define SENSOR_CHANNEL_NAME_MAX_LEN 16      /**< Độ dài tối đa tên kênh */

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
/* CẤU TRÚC DỮ LIỆU CỦA 1 KÊNH ĐO (CHANNEL TELEMETRY DATA)                   */
/* ========================================================================= */

typedef struct {
    uint8_t       channel_id;                       /**< Định danh kênh: 0, 1, 2... */
    char          name[SENSOR_CHANNEL_NAME_MAX_LEN];/**< Tên hiển thị (vd: "NhietDo", "Dong4_20mA") */
    float         value;                            /**< Giá trị đo được thời gian thực */
    sensor_unit_t unit;                             /**< Đơn vị đo */
    bool          is_valid;                         /**< true: Dữ liệu chuẩn; false: Đang lỗi/hở mạch */
    uint16_t      fault_code;                       /**< Mã lỗi tương ứng (lấy từ fault_manager.h) */
} sensor_channel_t;

/* Dữ liệu tổng hợp toàn trạm trong 1 chu kỳ lấy mẫu */
typedef struct {
    uint8_t          total_channels;
    sensor_channel_t channels[SENSOR_MAX_CHANNELS];
    uint32_t         timestamp_sec;
} sensor_telemetry_snapshot_t;

/* ========================================================================= */
/* BẢNG HÀM THAO TÁC CỦA MỘT DRIVER CẢM BIẾN (DRIVER OPERATIONS)            */
/* ========================================================================= */

typedef struct {
    /**
     * @brief Khởi tạo phần cứng hoặc bus của cảm biến.
     */
    bool (*init)(void);

    /**
     * @brief Đọc giá trị đo thời gian thực từ cảm biến.
     * @param out_value Con trỏ nhận giá trị đo (float)
     * @return true nếu đọc thành công, false nếu lỗi đọc
     */
    bool (*read)(float *out_value);

    /**
     * @brief Tự kiểm tra sức khỏe cảm biến (đứt dây, lỗi bus, ngoài thang đo).
     * @param out_fault_code Mã lỗi phát hiện được (0x0000 nếu khỏe mạnh)
     * @return true nếu cảm biến hoạt động bình thường, false nếu có lỗi
     */
    bool (*health_check)(uint16_t *out_fault_code);

    /**
     * @brief Kích hoạt hoặc hủy lỗi giả lập (phục vụ Demo AI).
     * @param fault_code Mã lỗi muốn ép sinh
     * @param enable true: tạo lỗi, false: xóa lỗi
     */
    void (*inject_fault)(uint16_t fault_code, bool enable);
} sensor_driver_ops_t;

/* ========================================================================= */
/* BỘ QUẢN LÝ TẬP TRUNG CÁC KÊNH ĐO (SENSOR MANAGER CONTEXT)                 */
/* ========================================================================= */

typedef struct {
    sensor_channel_t          channel_info[SENSOR_MAX_CHANNELS];
    const sensor_driver_ops_t *driver_ops[SENSOR_MAX_CHANNELS];
    uint8_t                   channel_count;
} sensor_manager_t;

/* ========================================================================= */
/* FUNCTION PROTOTYPES (API CHO FSM & APP)                                  */
/* ========================================================================= */

/**
 * @brief Khởi tạo bộ quản lý cảm biến.
 */
void sensor_manager_init(sensor_manager_t *mgr);

/**
 * @brief Đăng ký một cảm biến mới vào hệ thống (Open/Closed Principle).
 * @note Khi có thêm cảm biến mới (vd: pH, CO2), chỉ cần gọi hàm này mà không cần sửa code cũ!
 *
 * @param mgr         Con trỏ sensor_manager
 * @param name        Tên kênh hiển thị
 * @param unit        Đơn vị đo
 * @param ops         Bảng hàm driver cụ thể của cảm biến
 * @return channel_id (>=0) nếu thành công, -1 nếu đã đầy số kênh
 */
int8_t sensor_manager_register_channel(sensor_manager_t *mgr,
                                      const char *name,
                                      sensor_unit_t unit,
                                      const sensor_driver_ops_t *ops);

/**
 * @brief Đọc toàn bộ các kênh đo đã đăng ký trong 1 chu kỳ (gọi trong FSM SAMPLE).
 *
 * @param mgr         Con trỏ sensor_manager
 * @param out_snapshot Dữ liệu đo tổng hợp trả về
 * @return Số lượng kênh đọc thành công
 */
uint8_t sensor_manager_read_all(sensor_manager_t *mgr, sensor_telemetry_snapshot_t *out_snapshot);

/**
 * @brief Kiểm tra sức khỏe toàn bộ cảm biến để cập nhật vào Fault Manager.
 *
 * @param mgr Con trỏ sensor_manager
 */
void sensor_manager_check_health(sensor_manager_t *mgr);

/**
 * @brief Ép tạo lỗi trên 1 kênh đo cụ thể (hỗ trợ lệnh AI / UART test).
 */
void sensor_manager_inject_fault_to_channel(sensor_manager_t *mgr, uint8_t channel_id, uint16_t fault_code, bool enable);

/**
 * @brief Lấy chuỗi ký tự đại diện cho đơn vị đo (ví dụ: "°C", "mA", "%").
 */
const char *sensor_unit_to_str(sensor_unit_t unit);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_IF_H */
