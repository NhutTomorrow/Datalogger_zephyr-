/**
 * @file bsp_rs485_zephyr.c
 * @brief Hiện thực Driver RS485 Modbus RTU Master trên Zephyr RTOS (STM32F407).
 *        Sử dụng USART3 (PC10/PC11) và chân điều khiển hướng DE/RE PA15.
 */

#include "bsp_rs485.h"
#include "fault_manager.h"
#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(bsp_rs485, LOG_LEVEL_INF);

/* Ánh xạ Device Tree */
#define RS485_UART_NODE DT_NODELABEL(usart3)
#define RS485_DE_NODE   DT_NODELABEL(rs485_de)

static const struct device *s_rs485_uart = DEVICE_DT_GET(RS485_UART_NODE);
static const struct gpio_dt_spec s_rs485_de = GPIO_DT_SPEC_GET(RS485_DE_NODE, gpios);

/* Mảng lưu trạng thái ép lỗi giả lập (phục vụ AI Diagnostic) */
static uint16_t s_injected_fault[CHANNEL_MAX_COUNT];

/* Biến đếm lỗi liên tiếp để đánh giá sức khỏe đường truyền */
static uint8_t s_consecutive_errors[CHANNEL_MAX_COUNT];

/* ========================================================================= */
/* TÍNH TOÁN CRC16 MODBUS RTU                                                */
/* ========================================================================= */

/**
 * @brief Tính toán mã kiểm tra CRC16 theo chuẩn Modbus RTU (Polynomial 0xA001).
 */
static uint16_t modbus_crc16(const uint8_t *buffer, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < length; pos++) {
        crc ^= (uint16_t)buffer[pos];
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/* ========================================================================= */
/* PUBLIC API IMPLEMENTATION (GENERIC PARAMETER-DRIVEN ENGINE)              */
/* ========================================================================= */

bool bsp_rs485_init(void)
{
    memset(s_injected_fault, 0, sizeof(s_injected_fault));
    memset(s_consecutive_errors, 0, sizeof(s_consecutive_errors));

    if (!device_is_ready(s_rs485_uart)) {
        LOG_ERR("Thiet bi USART3 cho RS485 chua san sang!");
        return false;
    }

    if (gpio_is_ready_dt(&s_rs485_de)) {
        gpio_pin_configure_dt(&s_rs485_de, GPIO_OUTPUT_INACTIVE); /* Mặc định LOW = RX */
        LOG_INF("Khoi tao RS485 tren USART3 + PA15 (DE/RE) thanh cong!");
    } else {
        LOG_ERR("Chan GPIO PA15 (RS485 DE) chua san sang!");
        return false;
    }

    return true;
}

int bsp_rs485_init_channel(uint8_t channel_id)
{
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return -EINVAL;
    }

    if (channel_id <= CHANNEL_MAX_COUNT) {
        s_injected_fault[channel_id - 1] = FAULT_NONE;
        s_consecutive_errors[channel_id - 1] = 0;
    }

    return 0;
}

int bsp_rs485_deinit_channel(uint8_t channel_id)
{
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return -EINVAL;
    }

    s_injected_fault[channel_id - 1] = FAULT_NONE;
    s_consecutive_errors[channel_id - 1] = 0;
    return 0;
}

/**
 * @brief Đọc giá trị từ cảm biến Modbus RTU bất kỳ dựa hoàn toàn vào tham số động ChannelConfig_t.
 */
bool bsp_rs485_read(uint8_t channel_id, const ChannelConfig_t *cfg, float *out_value)
{
    if (out_value == NULL || channel_id < 1 || channel_id > CHANNEL_MAX_COUNT || cfg == NULL) {
        return false;
    }

    /* Kiểm tra nếu có lỗi ép sinh từ AI */
    if (s_injected_fault[channel_id - 1] != FAULT_NONE) {
        return false;
    }

    if (!device_is_ready(s_rs485_uart) || !gpio_is_ready_dt(&s_rs485_de)) {
        return false;
    }

    /* Trích xuất tham số Modbus từ cấu hình kênh */
    uint8_t slave_id      = (cfg->calib.modbus.slave_id != 0) ? cfg->calib.modbus.slave_id : 1;
    uint8_t function_code = (cfg->calib.modbus.function_code != 0) ? cfg->calib.modbus.function_code : 0x03;
    uint16_t reg_addr     = cfg->calib.modbus.register_addr;
    uint8_t reg_count     = (cfg->calib.modbus.reg_count != 0) ? cfg->calib.modbus.reg_count : 1;
    uint8_t data_type     = cfg->calib.modbus.data_type;
    uint8_t byte_swap     = cfg->calib.modbus.byte_swap_mode;
    float scale           = (cfg->calib.modbus.scale_factor != 0.0f) ? cfg->calib.modbus.scale_factor : 1.0f;

    if (reg_count > 4) {
        reg_count = 4;
    }

    /* 1. Dựng khung truyền Modbus RTU Read Request động */
    uint8_t tx_frame[8];
    tx_frame[0] = slave_id;
    tx_frame[1] = function_code;
    tx_frame[2] = (uint8_t)((reg_addr >> 8) & 0xFF);
    tx_frame[3] = (uint8_t)(reg_addr & 0xFF);
    tx_frame[4] = (uint8_t)((reg_count >> 8) & 0xFF);
    tx_frame[5] = (uint8_t)(reg_count & 0xFF);

    uint16_t tx_crc = modbus_crc16(tx_frame, 6);
    tx_frame[6] = (uint8_t)(tx_crc & 0xFF);
    tx_frame[7] = (uint8_t)((tx_crc >> 8) & 0xFF);

    /* 2. Xóa các byte rác còn sót lại trong UART RX FIFO trước khi phát */
    uint8_t dummy;
    while (uart_poll_in(s_rs485_uart, &dummy) == 0) {
        /* Xả sạch */
    }

    /* 3. Bật chân DE/RE (PA15 = HIGH) để chuyển IC RS485 sang chế độ TRANSMIT */
    gpio_pin_set_dt(&s_rs485_de, 1);
    k_busy_wait(50); /* Thời gian ổn định phần cứng thu phát */

    /* 4. Truyền 8 bytes qua USART3 */
    for (size_t i = 0; i < sizeof(tx_frame); i++) {
        uart_poll_out(s_rs485_uart, tx_frame[i]);
    }

    /* 5. Chờ truyền xong byte cuối cùng trong Shift Register */
    k_busy_wait(2500);

    /* 6. Lập tức kéo chân DE/RE về LOW đón phản hồi */
    gpio_pin_set_dt(&s_rs485_de, 0);

    /* 7. Lắng nghe phản hồi từ Sensor với timeout 200ms.
     * Độ dài frame phản hồi chuẩn: 1 (Slave) + 1 (Func) + 1 (ByteCount) + (reg_count * 2) + 2 (CRC)
     */
    uint8_t rx_buf[32];
    uint8_t rx_idx = 0;
    uint8_t expected_len = 5 + (reg_count * 2);
    if (expected_len > sizeof(rx_buf)) {
        expected_len = sizeof(rx_buf);
    }
    uint32_t start_ms = k_uptime_get_32();

    while ((k_uptime_get_32() - start_ms) < 200 && rx_idx < expected_len) {
        uint8_t ch;
        if (uart_poll_in(s_rs485_uart, &ch) == 0) {
            rx_buf[rx_idx++] = ch;
        } else {
            k_busy_wait(200); /* Polling ngắn tránh nghẽn CPU */
        }
    }

    /* 8. Kiểm tra tính hợp lệ của phản hồi */
    if (rx_idx < expected_len) {
        s_consecutive_errors[channel_id - 1]++;
        return false;
    }

    /* Kiểm tra Exception Response (Func | 0x80) */
    if (rx_buf[1] == (function_code | 0x80)) {
        LOG_WRN("RS485 Modbus Exception code: 0x%02X", rx_buf[2]);
        s_consecutive_errors[channel_id - 1]++;
        return false;
    }

    /* Kiểm tra Slave ID, Function Code và Byte Count */
    if (rx_buf[0] != slave_id || rx_buf[1] != function_code || rx_buf[2] != (reg_count * 2)) {
        s_consecutive_errors[channel_id - 1]++;
        return false;
    }

    /* Kiểm tra CRC16 */
    uint16_t expected_crc = modbus_crc16(rx_buf, expected_len - 2);
    uint16_t received_crc = rx_buf[expected_len - 2] | (rx_buf[expected_len - 1] << 8);
    if (expected_crc != received_crc) {
        LOG_WRN("RS485 CRC Mismatch! Expected 0x%04X, got 0x%04X", expected_crc, received_crc);
        s_consecutive_errors[channel_id - 1]++;
        return false;
    }

    /* 9. Giải mã dữ liệu theo data_type, byte_swap_mode và scale_factor */
    const uint8_t *payload = &rx_buf[3];

    if (reg_count == 1) {
        /* Dữ liệu 16-bit (1 thanh ghi) */
        uint8_t b0 = payload[0];
        uint8_t b1 = payload[1];
        if (byte_swap == MODBUS_BYTE_SWAP_BADC || byte_swap == MODBUS_BYTE_SWAP_DCBA) {
            uint8_t tmp = b0; b0 = b1; b1 = tmp;
        }

        if (data_type == MODBUS_DATA_TYPE_INT16) {
            int16_t raw_val = (int16_t)((b0 << 8) | b1);
            *out_value = (float)raw_val * scale;
        } else {
            /* Mặc định MODBUS_DATA_TYPE_UINT16 */
            uint16_t raw_val = (uint16_t)((b0 << 8) | b1);
            *out_value = (float)raw_val * scale;
        }
    } else {
        /* Dữ liệu 32-bit (2 thanh ghi = 4 bytes) */
        uint8_t ordered[4];
        uint8_t a = payload[0], b = payload[1], c = payload[2], d = payload[3];

        switch (byte_swap) {
        case MODBUS_BYTE_SWAP_CDAB: /* Word Swap */
            ordered[0] = c; ordered[1] = d; ordered[2] = a; ordered[3] = b;
            break;
        case MODBUS_BYTE_SWAP_BADC: /* Byte Swap */
            ordered[0] = b; ordered[1] = a; ordered[2] = d; ordered[3] = c;
            break;
        case MODBUS_BYTE_SWAP_DCBA: /* Full Little-Endian */
            ordered[0] = d; ordered[1] = c; ordered[2] = b; ordered[3] = a;
            break;
        case MODBUS_BYTE_SWAP_ABCD: /* Standard Big-Endian */
        default:
            ordered[0] = a; ordered[1] = b; ordered[2] = c; ordered[3] = d;
            break;
        }

        uint32_t raw_u32 = ((uint32_t)ordered[0] << 24) |
                           ((uint32_t)ordered[1] << 16) |
                           ((uint32_t)ordered[2] << 8)  |
                           (uint32_t)ordered[3];

        if (data_type == MODBUS_DATA_TYPE_FLOAT32) {
            float float_val = 0.0f;
            memcpy(&float_val, &raw_u32, sizeof(float));
            *out_value = float_val * scale;
        } else if (data_type == MODBUS_DATA_TYPE_INT32) {
            int32_t int_val = (int32_t)raw_u32;
            *out_value = (float)int_val * scale;
        } else {
            /* MODBUS_DATA_TYPE_UINT32 */
            *out_value = (float)raw_u32 * scale;
        }
    }

    s_consecutive_errors[channel_id - 1] = 0;
    return true;
}

bool bsp_rs485_health_check(uint8_t channel_id, uint16_t *out_fault_code)
{
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return false;
    }

    uint16_t fault = s_injected_fault[channel_id - 1];

    /* Nếu mất kết nối RS485 liên tiếp nhiều lần */
    if (fault == FAULT_NONE && s_consecutive_errors[channel_id - 1] >= 3) {
        fault = FAULT_SENSOR_TIMEOUT; /* Sử dụng mã lỗi timeout cảm biến */
    }

    if (out_fault_code != NULL) {
        *out_fault_code = fault;
    }

    return (fault == FAULT_NONE);
}

void bsp_rs485_inject_fault(uint8_t channel_id, uint16_t fault_code, bool en)
{
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return;
    }

    if (en) {
        s_injected_fault[channel_id - 1] = fault_code;
    } else {
        s_injected_fault[channel_id - 1] = FAULT_NONE;
    }
}

static const channel_driver_ops_t s_rs485_driver_ops = {
    .init         = bsp_rs485_init,
    .read         = bsp_rs485_read,
    .health_check = bsp_rs485_health_check,
    .inject_fault = bsp_rs485_inject_fault
};

const channel_driver_ops_t *bsp_rs485_get_ops(void)
{
    return &s_rs485_driver_ops;
}
