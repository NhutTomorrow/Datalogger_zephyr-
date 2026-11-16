/**
 * @file comm_protocol.c
 * @brief Hiện thực giao thức đóng gói & giải mã frame UART.
 * @note Không phụ thuộc phần cứng, không dùng thư viện ngoài ngoài std.
 */

#include "com_protocol.h"
#include <string.h>

/* ========================================================================= */
/* TÍNH TOÁN CRC16-MODBUS                                                    */
/* ========================================================================= */

uint16_t comm_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t i = 0; i < length; i++)
    {
        crc ^= (uint16_t)data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0xA001;
            }
            else
            {
                crc = (crc >> 1);
            }
        }
    }
    return crc;
}

/* ========================================================================= */
/* ĐÓNG GÓI FRAME (PACKET BUILDER)                                           */
/* ========================================================================= */

bool comm_packet_build(uint8_t cmd,
                       uint8_t seq,
                       const void *payload,
                       uint16_t payload_len,
                       uint8_t *out_buf,
                       uint16_t max_out_len,
                       uint16_t *actual_len)
{
    if (out_buf == NULL || actual_len == NULL)
    {
        return false;
    }

    if (payload_len > COMM_MAX_PAYLOAD_LEN)
    {
        return false;
    }

    /* Kích thước tổng: SOF(2) + CMD(1) + SEQ(1) + LEN(2) + PAYLOAD(N) + CRC(2) + EOF(1) */
    uint16_t total_len = 2 + 1 + 1 + 2 + payload_len + 2 + 1;
    if (total_len > max_out_len)
    {
        return false;
    }

    /* 1. Header */
    out_buf[0] = COMM_SOF1;
    out_buf[1] = COMM_SOF2;
    out_buf[2] = cmd;
    out_buf[3] = seq;
    out_buf[4] = (uint8_t)(payload_len & 0xFF);
    out_buf[5] = (uint8_t)((payload_len >> 8) & 0xFF);

    /* 2. Payload */
    if (payload != NULL && payload_len > 0)
    {
        memcpy(&out_buf[6], payload, payload_len);
    }

    /* 3. Tính CRC trên phạm vi: [CMD, SEQ, LEN_L, LEN_H, PAYLOAD...] */
    uint16_t crc_calc_len = 4 + payload_len; /* CMD + SEQ + LEN(2B) + payload */
    uint16_t crc = comm_crc16(&out_buf[2], crc_calc_len);

    uint16_t crc_offset = 6 + payload_len;
    out_buf[crc_offset] = (uint8_t)(crc & 0xFF);
    out_buf[crc_offset + 1] = (uint8_t)((crc >> 8) & 0xFF);

    /* 4. Footer EOF */
    out_buf[crc_offset + 2] = COMM_EOF;

    *actual_len = total_len;
    return true;
}

/* ========================================================================= */
/* GIẢI MÃ FRAME (STREAM STATE MACHINE PARSER)                               */
/* ========================================================================= */

void comm_parser_init(comm_parser_t *parser, comm_packet_handler_t handler, void *user_ctx)
{
    if (parser == NULL)
        return;

    memset(parser, 0, sizeof(comm_parser_t));
    parser->state = COMM_PARSE_STATE_WAIT_SOF1;
    parser->handler = handler;
    parser->user_ctx = user_ctx;
}

void comm_parser_feed_byte(comm_parser_t *parser, uint8_t byte)
{
    if (parser == NULL)
        return;

    switch (parser->state)
    {
    case COMM_PARSE_STATE_WAIT_SOF1:
        if (byte == COMM_SOF1)
        {
            parser->state = COMM_PARSE_STATE_WAIT_SOF2;
        }
        break;

    case COMM_PARSE_STATE_WAIT_SOF2:
        if (byte == COMM_SOF2)
        {
            parser->state = COMM_PARSE_STATE_GET_CMD;
        }
        else if (byte == COMM_SOF1)
        {
            /* Giữ nguyên state nếu lặp lại SOF1 */
            parser->state = COMM_PARSE_STATE_WAIT_SOF2;
        }
        else
        {
            parser->state = COMM_PARSE_STATE_WAIT_SOF1;
        }
        break;

    case COMM_PARSE_STATE_GET_CMD:
        parser->rx_packet.cmd = byte;
        parser->state = COMM_PARSE_STATE_GET_SEQ;
        break;

    case COMM_PARSE_STATE_GET_SEQ:
        parser->rx_packet.seq = byte;
        parser->state = COMM_PARSE_STATE_GET_LEN_L;
        break;

    case COMM_PARSE_STATE_GET_LEN_L:
        parser->rx_packet.len = byte;
        parser->state = COMM_PARSE_STATE_GET_LEN_H;
        break;

    case COMM_PARSE_STATE_GET_LEN_H:
        parser->rx_packet.len |= ((uint16_t)byte << 8);
        if (parser->rx_packet.len > COMM_MAX_PAYLOAD_LEN)
        {
            /* Tràn kích thước cho phép -> reset state */
            parser->state = COMM_PARSE_STATE_WAIT_SOF1;
        }
        else if (parser->rx_packet.len == 0)
        {
            /* Gói tin không có payload, chuyển thẳng sang đọc CRC */
            parser->state = COMM_PARSE_STATE_GET_CRC_L;
        }
        else
        {
            parser->payload_index = 0;
            parser->state = COMM_PARSE_STATE_GET_PAYLOAD;
        }
        break;

    case COMM_PARSE_STATE_GET_PAYLOAD:
        parser->rx_packet.payload[parser->payload_index++] = byte;
        if (parser->payload_index >= parser->rx_packet.len)
        {
            parser->state = COMM_PARSE_STATE_GET_CRC_L;
        }
        break;

    case COMM_PARSE_STATE_GET_CRC_L:
        parser->received_crc = byte;
        parser->state = COMM_PARSE_STATE_GET_CRC_H;
        break;

    case COMM_PARSE_STATE_GET_CRC_H:
        parser->received_crc |= ((uint16_t)byte << 8);
        parser->state = COMM_PARSE_STATE_WAIT_EOF;
        break;

    case COMM_PARSE_STATE_WAIT_EOF:
        if (byte == COMM_EOF)
        {
            /* Kiểm tra CRC tính toán so với received_crc */
            /* Cần tính CRC trên: CMD (1B), SEQ (1B), LEN (2B), và PAYLOAD (N B) */
            uint8_t header_buf[4];
            header_buf[0] = parser->rx_packet.cmd;
            header_buf[1] = parser->rx_packet.seq;
            header_buf[2] = (uint8_t)(parser->rx_packet.len & 0xFF);
            header_buf[3] = (uint8_t)((parser->rx_packet.len >> 8) & 0xFF);

            /* Tính CRC 2 chặng: Header 4B rồi đến Payload */
            uint16_t calc_crc = 0xFFFF;
            /* Header */
            for (uint8_t i = 0; i < 4; i++)
            {
                calc_crc ^= (uint16_t)header_buf[i];
                for (uint8_t bit = 0; bit < 8; bit++)
                {
                    if (calc_crc & 0x0001)
                        calc_crc = (calc_crc >> 1) ^ 0xA001;
                    else
                        calc_crc = (calc_crc >> 1);
                }
            }
            /* Payload */
            for (uint16_t i = 0; i < parser->rx_packet.len; i++)
            {
                calc_crc ^= (uint16_t)parser->rx_packet.payload[i];
                for (uint8_t bit = 0; bit < 8; bit++)
                {
                    if (calc_crc & 0x0001)
                        calc_crc = (calc_crc >> 1) ^ 0xA001;
                    else
                        calc_crc = (calc_crc >> 1);
                }
            }

            if (calc_crc == parser->received_crc)
            {
                if (parser->handler != NULL)
                {
                    parser->handler(&parser->rx_packet, parser->user_ctx);
                }
            }
        }
        /* Hoàn thành frame hoặc EOF sai -> quay về đợi gói tiếp theo */
        parser->state = COMM_PARSE_STATE_WAIT_SOF1;
        break;

    default:
        parser->state = COMM_PARSE_STATE_WAIT_SOF1;
        break;
    }
}

void comm_parser_feed_buffer(comm_parser_t *parser, const uint8_t *buf, uint16_t length)
{
    if (parser == NULL || buf == NULL)
        return;

    for (uint16_t i = 0; i < length; i++)
    {
        comm_parser_feed_byte(parser, buf[i]);
    }
}

// /* ========================================================================= */
// /* ĐÓNG GÓI & GIẢI MÃ TLV (TAG-LENGTH-VALUE ENCODER / DECODER)               */
// /* ========================================================================= */

// uint16_t comm_tlv_encode_channels(const comm_channel_entry_t *entries,
//                                   uint8_t count,
//                                   uint8_t *out_buf,
//                                   uint16_t max_len)
// {
//     if (entries == NULL || out_buf == NULL)
//     {
//         return 0;
//     }

//     /* Mỗi entry: Tag(1B) + Length(1B) + Status(1B) + Value(4B) = 7 bytes */
//     const uint8_t item_val_len = (uint8_t)(sizeof(uint8_t) + sizeof(float)); /* 5 bytes */
//     const uint16_t needed_len = (uint16_t)count * (2 + item_val_len);

//     if (needed_len > max_len)
//     {
//         return 0;
//     }

//     uint16_t offset = 0;
//     for (uint8_t i = 0; i < count; i++)
//     {
//         out_buf[offset++] = entries[i].channel_id;   /* Tag */
//         out_buf[offset++] = item_val_len;           /* Length = 5 */
//         out_buf[offset++] = entries[i].status;       /* Value[0] = status */
//         memcpy(&out_buf[offset], &entries[i].value, sizeof(float)); /* Value[1..4] */
//         offset += (uint16_t)sizeof(float);
//     }

//     return offset;
// }

// uint8_t comm_tlv_decode_channels(const uint8_t *tlv_buf,
//                                  uint16_t tlv_len,
//                                  comm_channel_entry_t *out_entries,
//                                  uint8_t max_entries)
// {
//     if (tlv_buf == NULL || out_entries == NULL || max_entries == 0)
//     {
//         return 0;
//     }

//     uint16_t offset = 0;
//     uint8_t decoded_count = 0;

//     while (offset + 2 <= tlv_len && decoded_count < max_entries)
//     {
//         uint8_t tag = tlv_buf[offset++];
//         uint8_t len = tlv_buf[offset++];

//         /* Kiểm tra tránh đọc tràn buffer */
//         if (offset + len > tlv_len)
//         {
//             break;
//         }

//         /* Định dạng chuẩn: Tag (1..18), Length >= 5 (Status 1B + Float 4B) */
//         if (len >= 5)
//         {
//             out_entries[decoded_count].channel_id = tag;
//             out_entries[decoded_count].status     = tlv_buf[offset];
//             memcpy(&out_entries[decoded_count].value, &tlv_buf[offset + 1], sizeof(float));
//             decoded_count++;
//         }

//         /* Tự động nhảy qua phần mở rộng nếu len > 5 (Forward Compatibility) */
//         offset += len;
//     }

//     return decoded_count;
// }

