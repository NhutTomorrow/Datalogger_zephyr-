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
#ifdef _cplusplus
extern "C"
{
#endif
// config struct frame communication

#define COMM_SOF1 0xAA
#define COMM_SOF2 0x55
#define COMM_EOF 0x0D

#define COMM_FRAME_OVERHEAD 8
#define COMM_MAX_PAYLOAD_LEN 128
#define COMM_MAX_FRAME_LEN (COMM_MAX_PAYLOAD_LEN + 10)

// COMMANDS CODE

typedef enum
{
			COMM_CMD_PING = 0x01,
			COMM_CMD_PONG = 0x81,
			/* Nhóm lấy dữ liệu quan trắc (0x20 - 0x3F) */
	        COMM_CMD_GET_TELEMETRY = 0x02,  /**< ESP32 -> STM32: Yêu cầu đọc dữ liệu cảm biến */
	        COMM_CMD_RESP_TELEMETRY = 0x82, /**< STM32 -> ESP32: Trả dữ liệu cảm biến tức thời */

	        /* Nhóm quản lý và chẩn đoán lỗi (0x40 - 0x5F) */
	        COMM_CMD_GET_FAULTS = 0x03,   /**< ESP32 -> STM32: Truy vấn danh sách lỗi đang active */
	        COMM_CMD_RESP_FAULTS = 0x83,  /**< STM32 -> ESP32: Trả danh sách mã lỗi active */
	        COMM_CMD_CLEAR_FAULTS = 0x04, /**< ESP32 -> STM32: Xóa lỗi / Reset cờ lỗi */

	        /* Nhóm cấu hình và điều khiển ghi dữ liệu (0x60 - 0x7F) */
	        COMM_CMD_SET_LOGGING_MODE = 0x05,  /**< ESP32 -> STM32: Bật/tắt logging hoặc chu kỳ */
	        COMM_CMD_INJECT_FAULT_TEST = 0x06, /**< ESP32 -> STM32: Lệnh giả lập lỗi (Demo AI) */

	        /* Nhóm phản hồi trạng thái chung */
	        COMM_CMD_ACK = 0x80, /**< Phản hồi thực thi lệnh thành công */
	        COMM_CMD_NACK = 0xFF, /**< Phản hồi từ chối hoặc lỗi thực thi */
			/* Nhóm điều khiển phần cứng của datalogger*/

		    /* ---- THÊM 2 LỆNH MỚI TẠI ĐÂY ---- */
		    COMM_CMD_SET_BACKLIGHT     = 0x07, /**< Payload: 1 byte (0=Tắt, 1=Bật) */
		    COMM_CMD_RESET_MCU         = 0x08 /**< Không payload: Reboot STM32 */


} comm_cmd_t;

// RESPONE NACK OF ERROR PROTOCOL

typedef enum{
    COMM_ERR_OK = 0x00,
    COMM_ERR_INVALID_SOF = 0x01,
    COMM_ERR_CRC_MISMATCH = 0x02,
    COMM_ERR_PAYLOAD_OVERFLOW = 0x03,
    COMM_ERR_UNKNOWN_CMD = 0x04,
    COMM_ERR_INVALID_PARAM = 0x05,
    COMM_ERR_BUSY = 0x06,
    COMM_ERR_HARDWARE_FAILURE = 0x07
}comm_err_t;

//STRUCT PAYLOAD OF CMD



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
  * @brief Payload cho lệnh COMM_CMD_RESP_TELEMETRY (0x82)
  */
 typedef struct
 {
     float temperature;          /**< Nhiệt độ môi trường (°C) */
     float humidity;             /**< Độ ẩm môi trường (%RH) */
     float current_loop_mA;      /**< Dòng vòng 4-20mA (mA) */
     uint32_t timestamp_sec;     /**< Thời gian hệ thống (uptime hoặc epoch) */
     uint8_t logging_state;      /**< 0: STOPPED, 1: LOGGING */
     uint8_t active_fault_count; /**< Số lượng lỗi đang tồn tại */
 } comm_payload_telemetry_t;

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



#ifdef _cplusplus
}
#endif


#endif /* COM_PROTOCOL_H_ */
