/*
 * com_protocol.h
 *
 *  Created on: Sep 30, 2026
 *      Author: ASUS
 */

#ifndef COM_PROTOCOL_H_
#define COM_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include "channel_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ========================================================================= */
/* CẤU TRÚC ĐỊNH DẠNG KHUNG TRUYỀN (FRAME STRUCTURE)                         */
/* ========================================================================= */

#define COMM_SOF1 0xAA
#define COMM_SOF2 0x55
#define COMM_EOF 0x0D

#define COMM_FRAME_OVERHEAD 9
#define COMM_MAX_PAYLOAD_LEN 128
#define COMM_MAX_FRAME_LEN (COMM_MAX_PAYLOAD_LEN + 10)

/* ========================================================================= */
/* MÃ LỆNH ĐIỀU KHIỂN (COMMAND CODES)                                        */
/* ========================================================================= */

typedef enum
{
    COMM_CMD_PING               = 0x01,
    COMM_CMD_PONG               = 0x81,

    /* Nhóm đọc dữ liệu cảm biến tức thời (0x02, 0x82) */
    COMM_CMD_READ_CHANNEL       = 0x02,  /**< ESP32 -> STM32: Đọc dữ liệu 1 kênh cụ thể (Payload: 1B channel_id) */
    COMM_CMD_RESP_CHANNEL       = 0x82,  /**< STM32 -> ESP32: Trả dữ liệu 1 kênh (Payload: comm_payload_read_channel_resp_t) */

    /* Nhóm quản lý và chẩn đoán lỗi (0x03, 0x04) */
    COMM_CMD_GET_FAULTS         = 0x03,  /**< ESP32 -> STM32: Truy vấn danh sách lỗi đang active */
    COMM_CMD_RESP_FAULTS        = 0x83,  /**< STM32 -> ESP32: Trả danh sách mã lỗi active */
    COMM_CMD_CLEAR_FAULTS       = 0x04,  /**< ESP32 -> STM32: Xóa lỗi / Reset cờ lỗi */

    /* Nhóm cấu hình và điều khiển ghi dữ liệu (0x05, 0x06) */
    COMM_CMD_SET_LOGGING_MODE   = 0x05,  /**< ESP32 -> STM32: Bật/tắt logging hoặc chu kỳ */
    COMM_CMD_INJECT_FAULT_TEST  = 0x06,  /**< ESP32 -> STM32: Lệnh giả lập lỗi (Demo AI) */

    /* Nhóm điều khiển phần cứng của datalogger */
    COMM_CMD_SET_BACKLIGHT      = 0x07,  /**< Payload: 1 byte (0=Tắt, 1=Bật) */
    COMM_CMD_RESET_MCU          = 0x08,  /**< Không payload: Reboot STM32 */

    /* Nhóm cấu hình kênh động (Dynamic Channel Management 0x10 - 0x1F) */
    COMM_CMD_CONFIG_CHANNEL       = 0x10, /**< ESP32 -> STM32: Cấu hình 1 kênh (Payload: ChannelConfig_t, 32B) */
    COMM_CMD_DISABLE_CHANNEL      = 0x11, /**< ESP32 -> STM32: Vô hiệu hóa 1 kênh (Payload: 1B channel_id) */
    COMM_CMD_GET_CHANNEL_CONFIG   = 0x12, /**< ESP32 -> STM32: Đọc cấu hình 1 kênh (Payload: 1B channel_id) */
    COMM_CMD_RESP_CHANNEL_CONFIG  = 0x92, /**< STM32 -> ESP32: Trả cấu hình 1 kênh (Payload: ChannelConfig_t) */
    COMM_CMD_FACTORY_RESET        = 0x15, /**< ESP32 -> STM32: Reset toàn bộ cấu hình về mặc định */
    COMM_CMD_GET_CONFIG_VERSION   = 0x16, /**< ESP32 -> STM32: Đọc version & CRC bảng config */
    COMM_CMD_RESP_CONFIG_VERSION  = 0x96, /**< STM32 -> ESP32: Trả về comm_payload_config_version_t */
    COMM_CMD_FLUSH_CONFIG_NVS     = 0x17, /**< ESP32 -> STM32: Ép lưu RAM xuống NVS ngay lập tức */

    /* Nhóm điều khiển ngõ ra Digital Output / MOSFET / Relay (0x18, 0x19) */
    COMM_CMD_SET_OUTPUT           = 0x18, /**< ESP32 -> STM32: Điều khiển Output (Payload: comm_payload_set_output_t) */
    COMM_CMD_GET_OUTPUT_STATE     = 0x19, /**< ESP32 -> STM32: Đọc trạng thái Output (Payload: 1B channel_id) */
    COMM_CMD_RESP_OUTPUT_STATE    = 0x99, /**< STM32 -> ESP32: Trả trạng thái Output (Payload: comm_payload_output_state_t) */

    /* Nhóm Telemetry Động (Dynamic Telemetry 0x1A, 0x9A) */
    COMM_CMD_GET_DYNAMIC_TELEMETRY  = 0x1A, /**< ESP32 -> STM32: Yêu cầu Telemetry động (tất cả kênh active) */
    COMM_CMD_RESP_DYNAMIC_TELEMETRY = 0x9A, /**< STM32 -> ESP32: Trả Telemetry động (comm_payload_dynamic_telemetry_t) */

    /* Nhóm phản hồi trạng thái chung */
    COMM_CMD_ACK                = 0x80, /**< Phản hồi thực thi lệnh thành công */
    COMM_CMD_NACK               = 0xFF  /**< Phản hồi từ chối hoặc lỗi thực thi */
} comm_cmd_t;

/* Alias tương thích mã nguồn cũ */
#define COMM_CMD_GET_TELEMETRY  COMM_CMD_READ_CHANNEL
#define COMM_CMD_RESP_TELEMETRY COMM_CMD_RESP_CHANNEL

/* ========================================================================= */
/* MÃ LỖI GIAO THỨC & THỰC THI (NACK ERROR CODES)                            */
/* ========================================================================= */

typedef enum {
    COMM_ERR_OK                 = 0x00,
    COMM_ERR_INVALID_SOF        = 0x01,
    COMM_ERR_CRC_MISMATCH       = 0x02,
    COMM_ERR_PAYLOAD_OVERFLOW   = 0x03,
    COMM_ERR_UNKNOWN_CMD        = 0x04,
    COMM_ERR_INVALID_PARAM      = 0x05,
    COMM_ERR_BUSY               = 0x06,
    COMM_ERR_HARDWARE_FAILURE   = 0x07,

    /* Lỗi đặc thù quản lý kênh động */
    COMM_ERR_PIN_CONFLICT       = 0x10, /**< Xung đột phần cứng (VD: PA5 giữa CH_07 và CH_08) */
    COMM_ERR_UNSUPPORTED_MODE   = 0x11, /**< Cổng không hỗ trợ chế độ này */
    COMM_ERR_INVALID_THRESHOLD  = 0x12, /**< Ngưỡng cảnh báo không hợp lệ */
    COMM_ERR_CHANNEL_NOT_FOUND  = 0x13, /**< Channel ID ngoài dải 1..18 */
    COMM_ERR_STORAGE_FAIL       = 0x14  /**< NVS Flash lỗi hoặc không thể ghi */
} comm_err_t;

/* ========================================================================= */
/* CẤU TRÚC GÓI TIN ĐÃ GIẢI MÃ (PARSED PACKET)                               */
/* ========================================================================= */

typedef struct
{
    uint8_t cmd;
    uint8_t seq;
    uint16_t len;
    uint8_t payload[COMM_MAX_PAYLOAD_LEN];
} comm_packet_t;

/* Callback khi nhận xong 1 gói tin hợp lệ */
typedef void (*comm_packet_handler_t)(const comm_packet_t *packet, void *user_ctx);

/* ========================================================================= */
/* STATE MACHINE PARSER                                                      */
/* ========================================================================= */

typedef enum {
    COMM_PARSE_STATE_WAIT_SOF1 = 0,
    COMM_PARSE_STATE_WAIT_SOF2,
    COMM_PARSE_STATE_GET_CMD,
    COMM_PARSE_STATE_GET_SEQ,
    COMM_PARSE_STATE_GET_LEN_L,
    COMM_PARSE_STATE_GET_LEN_H,
    COMM_PARSE_STATE_GET_PAYLOAD,
    COMM_PARSE_STATE_GET_CRC_L,
    COMM_PARSE_STATE_GET_CRC_H,
    COMM_PARSE_STATE_WAIT_EOF
} comm_parse_state_t;

typedef struct {
    comm_parse_state_t     state;
    comm_packet_t          rx_packet;
    uint16_t               payload_index;
    uint16_t               received_crc;
    comm_packet_handler_t  handler;
    void                  *user_ctx;
} comm_parser_t;

/* ========================================================================= */
/* CẤU TRÚC PAYLOAD (Đảm bảo Byte Alignment trên cả ARM Cortex-M & ESP32)    */
/* ========================================================================= */

#pragma pack(push, 1)

/**
 * @brief Payload yêu cầu đọc dữ liệu 1 kênh đơn lẻ (COMM_CMD_READ_CHANNEL - 0x02)
 */
typedef struct
{
    uint8_t channel_id; /**< ID cổng vật lý cần đọc (1 .. 18) */
} comm_payload_read_channel_req_t;

/**
 * @brief Payload cho lệnh COMM_CMD_RESP_CHANNEL (0x82) - Trả kết quả đo 1 kênh đơn lẻ (10 bytes)
 */
typedef struct
{
    uint8_t  channel_id;    /**< ID cổng vật lý (1 .. 18) */
    uint8_t  status;        /**< 0: UNCONFIGURED, 1: ACTIVE (OK), 2: SUSPENDED, 3: STOPPING */
    float    value;         /**< Giá trị đo sau khi calib (IEEE 754 float) */
    uint32_t timestamp_sec; /**< Thời điểm đo (uptime trạm tính bằng giây) */
} comm_payload_read_channel_resp_t;


/**
 * @brief Payload cho lệnh COMM_CMD_RESP_FAULTS (0x83)
 */
typedef struct
{
    uint8_t fault_count;     /**< Số mã lỗi trong mảng */
    uint16_t fault_codes[8]; /**< Danh sách tối đa 8 mã lỗi active */
} comm_payload_faults_t;

/**
 * @brief Payload cho lệnh COMM_CMD_SET_LOGGING_MODE (0x05)
 */
typedef struct
{
    uint8_t enable;              /**< 0: Tắt, 1: Bật */
    uint16_t sample_interval_ms; /**< Chu kỳ lấy mẫu (ms) */
} comm_payload_logging_t;

/**
 * @brief Payload cho lệnh COMM_CMD_INJECT_FAULT_TEST (0x06)
 */
typedef struct
{
    uint16_t fault_code; /**< Mã lỗi muốn kích hoạt hoặc xóa */
    uint8_t action;      /**< 1: Set lỗi, 0: Clear lỗi */
} comm_payload_inject_fault_t;

/**
 * @brief Payload cho lệnh phản hồi ACK (0x80)
 */
typedef struct
{
    uint8_t acked_cmd; /**< Mã lệnh được ACK */
    uint8_t status;    /**< Trạng thái kết quả (0: Thành công) */
} comm_payload_ack_t;

/**
 * @brief Payload cho lệnh phản hồi NACK (0xFF)
 */
typedef struct
{
    uint8_t failed_cmd; /**< Mã lệnh bị lỗi */
    uint8_t error_code; /**< Xem comm_err_t */
} comm_payload_nack_t;

/* ---- PAYLOADS MỚI CHO QUẢN LÝ KÊNH ĐỘNG (PHASE 3) ---- */

/**
 * @brief Payload cho lệnh COMM_CMD_CONFIG_CHANNEL (0x10)
 */
typedef struct
{
    ChannelConfig_t config; /**< Cấu hình 32 bytes của kênh */
} comm_payload_config_channel_t;

/**
 * @brief Payload cho lệnh COMM_CMD_DISABLE_CHANNEL (0x11) & COMM_CMD_GET_CHANNEL_CONFIG (0x12)
 */
typedef struct
{
    uint8_t channel_id; /**< ID cổng (1 .. 18) */
} comm_payload_channel_id_t;

/**
 * @brief Payload cho lệnh phản hồi COMM_CMD_RESP_CONFIG_VERSION (0x96)
 */
typedef struct
{
    uint32_t config_version;        /**< Số phiên bản bảng cấu hình */
    uint32_t crc32;                 /**< Mã kiểm tra toàn vẹn CRC32 */
    uint8_t  active_channels_count; /**< Số lượng kênh đang kích hoạt */
    uint8_t  is_dirty;              /**< 1 nếu có thay đổi chưa flush NVS, 0 nếu sạch */
} comm_payload_config_version_t;

/**
 * @brief Payload cho lệnh COMM_CMD_SET_OUTPUT (0x18)
 */
typedef struct
{
    uint8_t channel_id;   /**< Cổng điều khiển (VD: CH_12, CH_13, CH_14) */
    uint8_t output_state; /**< 0: Tắt (Low / Open), 1: Bật (High / Close) */
} comm_payload_set_output_t;

/**
 * @brief Payload cho phản hồi COMM_CMD_RESP_OUTPUT_STATE (0x99)
 */
typedef struct
{
    uint8_t channel_id;   /**< Cổng điều khiển */
    uint8_t output_state; /**< Trạng thái hiện tại (0 hoặc 1) */
} comm_payload_output_state_t;

/**
 * @brief Entry thu gọn của 1 kênh cho Dynamic Telemetry (6 bytes).
 */
typedef struct
{
    uint8_t channel_id;   /**< ID cổng (1 .. 18) */
    uint8_t status;       /**< 0: UNCONFIGURED, 1: ACTIVE (OK), 2: SUSPENDED, 3: FAULT */
    float   value;        /**< Giá trị đo sau khi hiệu chuẩn (IEEE 754) */
} comm_channel_entry_t;

/**
 * @brief Payload Telemetry Động (COMM_CMD_RESP_DYNAMIC_TELEMETRY - 0x9A)
 * Header: 7 bytes. Mỗi kênh: 6 bytes.
 * Tối đa 18 kênh: 7 + 18 * 6 = 115 bytes <= 128 bytes (COMM_MAX_PAYLOAD_LEN).
 */
typedef struct
{
    uint32_t timestamp_sec;                         /**< Uptime của trạm (giây) */
    uint8_t  logging_state;                         /**< 0: STOPPED, 1: LOGGING */
    uint8_t  active_fault_count;                    /**< Số lượng lỗi active */
    uint8_t  active_channel_count;                  /**< Số lượng kênh đi kèm trong payload (N) */
    comm_channel_entry_t channels[CHANNEL_MAX_COUNT]; /**< Danh sách dữ liệu các kênh */
} comm_payload_dynamic_telemetry_t;

#pragma pack(pop)

/* ========================================================================= */
/* FUNCTION PROTOTYPES                                                       */
/* ========================================================================= */

/**
 * @brief Tính CRC16-IBM / Modbus (Polynomial 0xA001) cho buffer.
 */
uint16_t comm_crc16(const uint8_t *data, uint16_t length);

/**
 * @brief Đóng gói dữ liệu thành frame hoàn chỉnh để truyền qua UART.
 *
 * @param cmd           Mã lệnh (comm_cmd_t)
 * @param seq           Số thứ tự gói tin (Sequence ID)
 * @param payload       Con trỏ đến dữ liệu payload (hoặc NULL nếu không có)
 * @param payload_len   Độ dài payload (0 nếu không có)
 * @param out_buf       Bộ đệm chứa frame kết quả
 * @param max_out_len   Kích thước tối đa của out_buf
 * @param actual_len    Độ dài thực tế của frame đã đóng gói (trả về)
 * @return true nếu đóng gói thành công, false nếu buffer không đủ chỗ
 */
bool comm_packet_build(uint8_t cmd,
                       uint8_t seq,
                       const void *payload,
                       uint16_t payload_len,
                       uint8_t *out_buf,
                       uint16_t max_out_len,
                       uint16_t *actual_len);

/**
 * @brief Khởi tạo bộ giải mã State Machine.
 *
 * @param parser    Con trỏ đến đối tượng parser
 * @param handler   Callback được gọi khi bắt trọn 1 gói tin hợp lệ
 * @param user_ctx  Dữ liệu tùy biến truyền vào callback (tùy chọn)
 */
void comm_parser_init(comm_parser_t *parser, comm_packet_handler_t handler, void *user_ctx);

/**
 * @brief Đưa 1 byte từ UART vào State Machine giải mã.
 *
 * @param parser    Con trỏ đến đối tượng parser
 * @param byte      Byte nhận được từ ngoại vi UART (ngắt hoặc polling)
 */
void comm_parser_feed_byte(comm_parser_t *parser, uint8_t byte);

/**
 * @brief Đưa một khối buffer nhận được từ DMA/FIFO vào giải mã.
 *
 * @param parser    Con trỏ đến đối tượng parser
 * @param buf       Con trỏ đến mảng byte nhận được
 * @param length    Số lượng byte
 */
void comm_parser_feed_buffer(comm_parser_t *parser, const uint8_t *buf, uint16_t length);

// /**
//  * @brief Đóng gói danh sách kênh sang mảng TLV nhị phân (Tag-Length-Value).
//  * Mỗi TLV item: Tag (1B = channel_id) + Length (1B = 5) + Value (1B status + 4B float).
//  *
//  * @param entries    Mảng các phần tử comm_channel_entry_t
//  * @param count      Số lượng kênh cần đóng gói
//  * @param out_buf    Bộ đệm nhận dữ liệu TLV
//  * @param max_len    Kích thước tối đa của bộ đệm out_buf
//  * @return Số byte đã ghi vào out_buf, hoặc 0 nếu buffer không đủ chỗ
//  */
// uint16_t comm_tlv_encode_channels(const comm_channel_entry_t *entries,
//                                   uint8_t count,
//                                   uint8_t *out_buf,
//                                   uint16_t max_len);

// /**
//  * @brief Giải mã chuỗi TLV nhị phân thành mảng comm_channel_entry_t.
//  *
//  * @param tlv_buf     Bộ đệm chứa chuỗi TLV
//  * @param tlv_len     Độ dài chuỗi TLV
//  * @param out_entries Mảng nhận kết quả giải mã
//  * @param max_entries Số phần tử tối đa của mảng out_entries
//  * @return Số lượng phần tử đã giải mã thành công
//  */
// uint8_t comm_tlv_decode_channels(const uint8_t *tlv_buf,
//                                  uint16_t tlv_len,
//                                  comm_channel_entry_t *out_entries,
//                                  uint8_t max_entries);

#ifdef __cplusplus
}
#endif

#endif /* COM_PROTOCOL_H_ */
