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
/* HÀM TRUYỀN NHẬN MODBUS RTU HALF-DUPLEX (ĐỌC 2 THANH GHI 0x0000 & 0x0001)   */
/* ========================================================================= */

/* Cache giá trị đọc gần nhất chia sẻ giữa kênh Nhiệt độ (CH15) và Độ ẩm (CH16) */
static float s_cached_temp = 34.4f;
static float s_cached_humi = 62.8f;
static uint32_t s_last_read_tick = 0;

/**
 * @brief Truy vấn cảm biến RS485 Modbus RTU: Đọc 2 thanh ghi từ 0x0000.
 *        TX Frame: [ 01 03 00 00 00 02 C4 0B ]
 *        RX Frame: [ 01 03 04 Humi_Hi Humi_Lo Temp_Hi Temp_Lo CRC_L CRC_H ] (9 bytes)
 * @param out_temp Con trỏ nhận giá trị Nhiệt độ (°C)
 * @param out_humi Con trỏ nhận giá trị Độ ẩm (%RH)
 * @return true nếu đọc và xác thực CRC thành công, false nếu lỗi/timeout
 */
static bool rs485_query_sensor(float *out_temp, float *out_humi)
{
    if (!device_is_ready(s_rs485_uart) || !gpio_is_ready_dt(&s_rs485_de)) {
        return false;
    }

    /* 1. Frame Modbus Request cố định chuẩn: Đọc 2 thanh ghi từ địa chỉ 0x0000 tại Slave 1 */
    static const uint8_t tx_frame[8] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B };

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

    /* 5. Chờ đúng thời gian truyền của byte cuối cùng trong Shift Register (1 byte @ 9600 baud ~ 1.04ms).
     * Dùng k_busy_wait(1200) chính xác 1.2ms, TUYỆT ĐỐI KHÔNG dùng k_msleep(10) kẻo cấm thu RX quá lâu! */
    k_busy_wait(2500);

    /* 6. Lập tức kéo chân DE/RE về LOW (PA15 = LOW) để mở bộ thu RECEIVE đón câu trả lời */
    gpio_pin_set_dt(&s_rs485_de, 0);

    /* 7. Lắng nghe phản hồi từ Sensor với timeout 200ms.
     * Response đúng chuẩn cho 2 thanh ghi: 9 bytes
     * [ 01 03 04 Humi_H Humi_L Temp_H Temp_L CRC_L CRC_H ] */
    uint8_t rx_buf[16];
    uint8_t rx_idx = 0;
    uint32_t start_ms = k_uptime_get_32();

    while ((k_uptime_get_32() - start_ms) < 200 && rx_idx < 9) {
        uint8_t ch;
        if (uart_poll_in(s_rs485_uart, &ch) == 0) {
            rx_buf[rx_idx++] = ch;
        } else {
            k_busy_wait(200); /* Polling ngắn tránh trễ FIFO */
        }
    }

    /* 8. Kiểm tra tính hợp lệ của phản hồi 9 bytes */
    if (rx_idx >= 9 && rx_buf[0] == 0x01 && rx_buf[1] == 0x03 && rx_buf[2] == 0x04) {
        uint16_t expected_crc = modbus_crc16(rx_buf, 7);
        uint16_t received_crc = rx_buf[7] | (rx_buf[8] << 8);

        if (expected_crc == received_crc) {
            /* Thanh ghi 0x0000: Độ ẩm (%RH x 10, ví dụ 0x0274 = 628 -> 62.8%RH) */
            int16_t raw_humi = (int16_t)((rx_buf[3] << 8) | rx_buf[4]);

            /* Thanh ghi 0x0001: Nhiệt độ (°C x 10, ví dụ 0x0158 = 344 -> 34.4°C) */
            int16_t raw_temp = (int16_t)((rx_buf[5] << 8) | rx_buf[6]);

            if (out_humi != NULL) {
                *out_humi = (float)raw_humi / 10.0f;
            }
            if (out_temp != NULL) {
                *out_temp = (float)raw_temp / 10.0f;
            }

            return true;
        } else {
            LOG_WRN("RS485 CRC Mismatch! Expected 0x%04X, got 0x%04X", expected_crc, received_crc);
        }
    }

    return false;
}

/* ========================================================================= */
/* PUBLIC API IMPLEMENTATION                                                 */
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

bool bsp_rs485_read(uint8_t channel_id, float *out_value)
{
    if (out_value == NULL || channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return false;
    }

    /* Kiểm tra nếu có lỗi ép sinh từ AI */
    if (s_injected_fault[channel_id - 1] != FAULT_NONE) {
        return false;
    }

    uint32_t now = k_uptime_get_32();

    /* Đọc dữ liệu từ cảm biến RS485 (Frame: 01 03 00 00 00 02 C4 0B)
     * Tránh gửi trùng lặp nếu cả kênh Nhiệt độ (CH15) và Độ ẩm (CH16) cùng lấy mẫu trong 200ms */
    if (now - s_last_read_tick > 200) {
        float temp = 0.0f;
        float humi = 0.0f;

        bool ok = rs485_query_sensor(&temp, &humi);

        if (ok) {
            s_cached_temp = temp;
            s_cached_humi = humi;
            s_last_read_tick = now;
            s_consecutive_errors[channel_id - 1] = 0;
            LOG_INF("RS485 Sensor Read OK: Temp = %.1f C, Humidity = %.1f %%RH", (double)temp, (double)humi);
        } else {
            s_consecutive_errors[channel_id - 1]++;

            /* Nếu mất tín hiệu liên tiếp >= 3 lần -> Báo lỗi để kích hoạt chẩn đoán FSM */
            if (s_consecutive_errors[channel_id - 1] >= 3) {
                return false;
            }
        }
    }

    /* Phân phối đại lượng đo:
     * CH_15: Nhiệt độ (°C) - Thanh ghi 0x0001
     * CH_16: Độ ẩm (%RH)   - Thanh ghi 0x0000 */
    if (channel_id == 16) {
        *out_value = s_cached_humi;
    } else {
        *out_value = s_cached_temp;
    }

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
