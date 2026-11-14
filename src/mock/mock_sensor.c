/*
 * mock_sensor.c
 *
 *  Created on: Sep 30, 2026
 *      Author: ASUS
 */


/**
 * @file mock_sensors.c
 * @brief Hiện thực các driver cảm biến giả lập.
 */

#include "mock_sensor.h"
#include "../services/fault_manager.h"
#include <math.h>

/* ========================================================================= */
/* 1. MOCK DRIVER: CẢM BIẾN NHIỆT ĐỘ                                         */
/* ========================================================================= */

static uint32_t s_temp_tick = 0;
static uint16_t s_temp_fault = FAULT_NONE;

static bool mock_temp_init(void)
{
    s_temp_tick = 0;
    s_temp_fault = FAULT_NONE;
    return true;
}

static bool mock_temp_read(float *out_val)
{
    if (out_val == NULL) return false;

    if (s_temp_fault == FAULT_SENSOR_I2C_TIMEOUT) {
        return false; /* Đang bị mất bus, không đọc được */
    }

    /* Sinh dạng sóng: 28.0°C dao động ± 2.5°C */
    float wave = (float)sin((double)s_temp_tick * 0.1) * 2.5f;
    *out_val = 28.0f + wave;
    s_temp_tick++;
    return true;
}

static bool mock_temp_health(uint16_t *out_fault)
{
    if (out_fault != NULL) {
        *out_fault = s_temp_fault;
    }
    return (s_temp_fault == FAULT_NONE);
}

static void mock_temp_inject_fault(uint16_t fault_code, bool enable)
{
    if (enable) {
        s_temp_fault = fault_code;
    } else if (s_temp_fault == fault_code) {
        s_temp_fault = FAULT_NONE;
    }
}

static const sensor_driver_ops_t MOCK_TEMP_OPS = {
    .init         = mock_temp_init,
    .read         = mock_temp_read,
    .health_check = mock_temp_health,
    .inject_fault = mock_temp_inject_fault
};

const sensor_driver_ops_t *mock_sensor_get_temperature_ops(void)
{
    return &MOCK_TEMP_OPS;
}

/* ========================================================================= */
/* 2. MOCK DRIVER: VÒNG DÒNG CÔNG NGHIỆP 4-20mA                              */
/* ========================================================================= */

static uint32_t s_loop_tick = 0;
static uint16_t s_loop_fault = FAULT_NONE;

static bool mock_loop_init(void)
{
    s_loop_tick = 0;
    s_loop_fault = FAULT_NONE;
    return true;
}

static bool mock_loop_read(float *out_val)
{
    if (out_val == NULL) return false;

    /* Nếu bị ép lỗi hở mạch 4-20mA */
    if (s_loop_fault == FAULT_4_20MA_LOOP_OPEN) {
        *out_val = 0.0f; /* Dòng rớt về 0mA (<3.6mA) */
        return true;
    }

    /* Nếu bị ép lỗi quá dòng */
    if (s_loop_fault == FAULT_4_20MA_LOOP_OVERCURRENT) {
        *out_val = 23.5f; /* Dòng vọt lên 23.5mA (>21mA) */
        return true;
    }

    /* Điều kiện bình thường: dao động quanh 12.0mA */
    float wave = (float)cos((double)s_loop_tick * 0.15) * 3.0f;
    *out_val = 12.0f + wave;
    s_loop_tick++;
    return true;
}

static bool mock_loop_health(uint16_t *out_fault)
{
    float val = 0.0f;
    mock_loop_read(&val);

    if (val < 3.6f) {
        s_loop_fault = FAULT_4_20MA_LOOP_OPEN;
    } else if (val > 21.0f) {
        s_loop_fault = FAULT_4_20MA_LOOP_OVERCURRENT;
    } else {
        s_loop_fault = FAULT_NONE;
    }

    if (out_fault != NULL) {
        *out_fault = s_loop_fault;
    }
    return (s_loop_fault == FAULT_NONE);
}

static void mock_loop_inject_fault(uint16_t fault_code, bool enable)
{
    if (enable) {
        s_loop_fault = fault_code;
    } else if (s_loop_fault == fault_code) {
        s_loop_fault = FAULT_NONE;
    }
}

static const sensor_driver_ops_t MOCK_LOOP_OPS = {
    .init         = mock_loop_init,
    .read         = mock_loop_read,
    .health_check = mock_loop_health,
    .inject_fault = mock_loop_inject_fault
};

const sensor_driver_ops_t *mock_sensor_get_current_loop_ops(void)
{
    return &MOCK_LOOP_OPS;
}

/* ========================================================================= */
/* 3. MOCK DRIVER: CẢM BIẾN pH ĐẤT                                           */
/* ========================================================================= */

static uint32_t s_ph_tick = 0;

static bool mock_ph_init(void)
{
    s_ph_tick = 0;
    return true;
}

static bool mock_ph_read(float *out_val)
{
    if (out_val == NULL) return false;
    float wave = (float)sin((double)s_ph_tick * 0.05) * 0.6f;
    *out_val = 6.8f + wave;
    s_ph_tick++;
    return true;
}

static bool mock_ph_health(uint16_t *out_fault)
{
    if (out_fault != NULL) *out_fault = FAULT_NONE;
    return true;
}

static void mock_ph_inject_fault(uint16_t fault_code, bool enable)
{
    (void)fault_code;
    (void)enable;
}

static const sensor_driver_ops_t MOCK_PH_OPS = {
    .init         = mock_ph_init,
    .read         = mock_ph_read,
    .health_check = mock_ph_health,
    .inject_fault = mock_ph_inject_fault
};

const sensor_driver_ops_t *mock_sensor_get_ph_ops(void)
{
    return &MOCK_PH_OPS;
}
