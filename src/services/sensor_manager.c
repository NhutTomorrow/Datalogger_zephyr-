/**
 * @file sensor_if.c
 * @brief Hiện thực bộ quản lý kênh cảm biến tập trung.
 */

#include "sensor_manager.h"
#include <string.h>

void sensor_manager_init(sensor_manager_t *mgr)
{
    if (mgr == NULL) return;
    memset(mgr, 0, sizeof(sensor_manager_t));
}

int8_t sensor_manager_register_channel(sensor_manager_t *mgr,
                                      const char *name,
                                      sensor_unit_t unit,
                                      const sensor_driver_ops_t *ops)
{
    if (mgr == NULL || ops == NULL || name == NULL) return -1;
    if (mgr->channel_count >= SENSOR_MAX_CHANNELS) return -1;

    uint8_t id = mgr->channel_count;
    sensor_channel_t *ch = &mgr->channel_info[id];
    ch->channel_id = id;
    strncpy(ch->name, name, SENSOR_CHANNEL_NAME_MAX_LEN - 1);
    ch->name[SENSOR_CHANNEL_NAME_MAX_LEN - 1] = '\0';
    ch->unit = unit;
    ch->value = 0.0f;
    ch->is_valid = false;
    ch->fault_code = 0;

    mgr->driver_ops[id] = ops;
    mgr->channel_count++;

    /* Khởi tạo phần cứng driver của kênh nếu có hàm init */
    if (ops->init != NULL) {
        ops->init();
    }

    return (int8_t)id;
}

uint8_t sensor_manager_read_all(sensor_manager_t *mgr, sensor_telemetry_snapshot_t *out_snapshot)
{
    if (mgr == NULL || out_snapshot == NULL) return 0;

    uint8_t success_count = 0;
    out_snapshot->total_channels = mgr->channel_count;

    for (uint8_t i = 0; i < mgr->channel_count; i++) {
        const sensor_driver_ops_t *ops = mgr->driver_ops[i];
        sensor_channel_t *ch = &mgr->channel_info[i];

        if (ops != NULL && ops->read != NULL) {
            float val = 0.0f;
            bool ok = ops->read(&val);
            ch->is_valid = ok;
            if (ok) {
                ch->value = val;
                success_count++;
            }
        }
        /* Copy thông tin sang snapshot */
        out_snapshot->channels[i] = *ch;
    }

    return success_count;
}

void sensor_manager_check_health(sensor_manager_t *mgr)
{
    if (mgr == NULL) return;

    for (uint8_t i = 0; i < mgr->channel_count; i++) {
        const sensor_driver_ops_t *ops = mgr->driver_ops[i];
        sensor_channel_t *ch = &mgr->channel_info[i];

        if (ops != NULL && ops->health_check != NULL) {
            uint16_t fault = 0;
            bool healthy = ops->health_check(&fault);
            ch->fault_code = fault;
            if (!healthy) {
                ch->is_valid = false;
            }
        }
    }
}

void sensor_manager_inject_fault_to_channel(sensor_manager_t *mgr, uint8_t channel_id, uint16_t fault_code, bool enable)
{
    if (mgr == NULL || channel_id >= mgr->channel_count) return;

    const sensor_driver_ops_t *ops = mgr->driver_ops[channel_id];
    if (ops != NULL && ops->inject_fault != NULL) {
        ops->inject_fault(fault_code, enable);
    }
}

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
