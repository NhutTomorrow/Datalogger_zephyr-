/**
 * @file mock_sensors.h
 * @brief Bộ driver cảm biến giả lập (Mock Sensors) để kiểm thử và phục vụ Demo kịch bản AI.
 */

#ifndef MOCK_SENSORS_H
#define MOCK_SENSORS_H

#include "sensor_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lấy bảng hàm driver cho Cảm biến Nhiệt độ giả lập (~28°C).
 */
const sensor_driver_ops_t *mock_sensor_get_temperature_ops(void);

/**
 * @brief Lấy bảng hàm driver cho Cảm biến Dòng 4-20mA giả lập (~12mA).
 * @note Hỗ trợ inject lỗi hở mạch 0x1001 hoặc quá dòng 0x1002.
 */
const sensor_driver_ops_t *mock_sensor_get_current_loop_ops(void);

/**
 * @brief Lấy bảng hàm driver cho Cảm biến Độ pH đất giả lập (~6.8 pH).
 * @note Dùng để minh chứng tính năng thêm cảm biến mới vào trạm mà không sửa core.
 */
const sensor_driver_ops_t *mock_sensor_get_ph_ops(void);

#ifdef __cplusplus
}
#endif

#endif /* MOCK_SENSORS_H */
