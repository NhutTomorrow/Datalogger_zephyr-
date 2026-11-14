/**
 * @file data_manager.c
 * @brief Hiện thực module quản lý dữ liệu đo lường và chu kỳ ghi log.
 */

#include "data_manager.h"
#include <string.h>

void data_manager_init(data_manager_t *dm, sensor_manager_t *sensor_mgr)
{
    if (dm == NULL) return;

    memset(dm, 0, sizeof(data_manager_t));
    dm->sensor_mgr = sensor_mgr;
    dm->logging_enabled = false;
    dm->sample_interval_ms = DATA_MGR_DEFAULT_INTERVAL_MS;
    dm->logged_records_count = 0;
    dm->last_sample_tick = 0;
}

bool data_manager_process_periodic(data_manager_t *dm, uint32_t current_tick_ms)
{
    if (dm == NULL || dm->sensor_mgr == NULL) return false;

    /* Kiểm tra chu kỳ lấy mẫu theo non-blocking tick */
    if (current_tick_ms - dm->last_sample_tick >= dm->sample_interval_ms) {
        dm->last_sample_tick = current_tick_ms;
        bool ok = data_manager_sample_now(dm);

        /* Nếu đang ở chế độ Logging, tăng bộ đếm bản ghi (lưu trữ) */
        if (dm->logging_enabled && ok) {
            dm->logged_records_count++;
        }
        return true;
    }
    return false;
}

bool data_manager_sample_now(data_manager_t *dm)
{
    if (dm == NULL || dm->sensor_mgr == NULL) return false;

    uint8_t read_count = sensor_manager_read_all(dm->sensor_mgr, &dm->latest_snapshot);
    return (read_count > 0);
}

void data_manager_set_logging(data_manager_t *dm, bool enable, uint16_t interval_ms)
{
    if (dm == NULL) return;

    dm->logging_enabled = enable;
    if (interval_ms > 0) {
        dm->sample_interval_ms = interval_ms;
    }
}

const sensor_telemetry_snapshot_t *data_manager_get_snapshot(const data_manager_t *dm)
{
    return (dm != NULL) ? &dm->latest_snapshot : NULL;
}

void data_manager_get_telemetry_payload(const data_manager_t *dm,
                                        uint8_t active_fault_count,
                                        comm_payload_telemetry_t *out_payload)
{
    if (dm == NULL || out_payload == NULL) return;

    memset(out_payload, 0, sizeof(comm_payload_telemetry_t));

    /* Trích xuất các kênh đo chính theo đơn vị (độc lập với thứ tự đăng ký kênh) */
    for (uint8_t i = 0; i < dm->latest_snapshot.total_channels; i++) {
        const sensor_channel_t *ch = &dm->latest_snapshot.channels[i];
        if (!ch->is_valid) continue;

        if (ch->unit == UNIT_CELSIUS) {
            out_payload->temperature = ch->value;
        } else if (ch->unit == UNIT_PERCENT_RH) {
            out_payload->humidity = ch->value;
        } else if (ch->unit == UNIT_MILLIAMP) {
            out_payload->current_loop_mA = ch->value;
        }
    }

    out_payload->timestamp_sec      = dm->latest_snapshot.timestamp_sec;
    out_payload->logging_state      = dm->logging_enabled ? 1 : 0;
    out_payload->active_fault_count = active_fault_count;
}
