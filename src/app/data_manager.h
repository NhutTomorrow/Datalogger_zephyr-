/**
 * @file data_manager.h
 * @brief Quản lý dữ liệu đo lường, chu kỳ lấy mẫu và bộ đệm lưu trữ của Datalogger.
 * @note File thuần C99, độc lập phần cứng.
 */

#ifndef DATA_MANAGER_H
#define DATA_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "sensor_manager.h"
#include "../services/com_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Chu kỳ lấy mẫu mặc định (ms) */
#define DATA_MGR_DEFAULT_INTERVAL_MS    1000

/* ========================================================================= */
/* CẤU TRÚC QUẢN LÝ DỮ LIỆU (DATA MANAGER CONTEXT)                          */
/* ========================================================================= */

typedef struct {
    sensor_manager_t             *sensor_mgr;           /**< Con trỏ tới bộ quản lý kênh đo */
    sensor_telemetry_snapshot_t   latest_snapshot;      /**< Bản chụp dữ liệu mới nhất của trạm */
    bool                          logging_enabled;      /**< Trạng thái ghi dữ liệu (true/false) */
    uint16_t                      sample_interval_ms;   /**< Chu kỳ lấy mẫu (ms) */
    uint32_t                      logged_records_count; /**< Số lượng bản ghi đã lưu vào SD/buffer */
    uint32_t                      last_sample_tick;     /**< Mốc thời gian lấy mẫu trước đó */
} data_manager_t;

/* ========================================================================= */
/* FUNCTION PROTOTYPES                                                       */
/* ========================================================================= */

/**
 * @brief Khởi tạo đối tượng data_manager.
 *
 * @param dm         Con trỏ data_manager
 * @param sensor_mgr Con trỏ sensor_manager quản lý các kênh đo
 */
void data_manager_init(data_manager_t *dm, sensor_manager_t *sensor_mgr);

/**
 * @brief Kiểm tra và thực hiện lấy mẫu định kỳ nếu đã đến chu kỳ.
 *
 * @param dm               Con trỏ data_manager
 * @param current_tick_ms  Mốc thời gian hiện tại (HAL_GetTick())
 * @return true nếu vừa lấy mẫu chu kỳ mới, false nếu chưa đến hạn
 */
bool data_manager_process_periodic(data_manager_t *dm, uint32_t current_tick_ms);

/**
 * @brief Lấy mẫu tức thời toàn bộ các kênh đo ngay lập tức.
 */
bool data_manager_sample_now(data_manager_t *dm);

/**
 * @brief Cấu hình chế độ lưu trữ (Logging mode).
 *
 * @param dm          Con trỏ data_manager
 * @param enable      true: Bật ghi log, false: Tắt
 * @param interval_ms Chu kỳ lấy mẫu (ms), truyền 0 nếu muốn giữ nguyên chu kỳ cũ
 */
void data_manager_set_logging(data_manager_t *dm, bool enable, uint16_t interval_ms);

/**
 * @brief Lấy con trỏ đến bản chụp dữ liệu mới nhất.
 */
const sensor_telemetry_snapshot_t *data_manager_get_snapshot(const data_manager_t *dm);

/**
 * @brief Định dạng và đóng gói dữ liệu sang comm_payload_telemetry_t của comm_protocol.
 * @note Hàm này chuẩn bị dữ liệu nhanh để ESP32 AI Gateway đọc qua UART.
 *
 * @param dm                 Con trỏ data_manager
 * @param active_fault_count Số lượng lỗi active (lấy từ fault_manager)
 * @param out_payload        Con trỏ nhận dữ liệu đóng gói
 */
void data_manager_get_telemetry_payload(const data_manager_t *dm,
                                        uint8_t active_fault_count,
                                        comm_payload_telemetry_t *out_payload);

#ifdef __cplusplus
}
#endif

#endif /* DATA_MANAGER_H */
