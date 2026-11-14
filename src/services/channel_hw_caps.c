/**
 * @file channel_hw_caps.c
 * @brief Hiện thực bảng ràng buộc phần cứng và giải thuật kiểm tra xung đột chân.
 */

#include "channel_hw_caps.h"
#include <stddef.h>

#define MODE_BM(m) (1U << (m))

/* Bảng mô tả năng lực phần cứng cố định cho 18 cổng trên bo mạch BKIT STM32F407 */
static const channel_hw_cap_entry_t s_hw_caps[CHANNEL_MAX_COUNT] = {
    /* CH_01: PC2 - LM35 Temp (ADC) */
    {
        .channel_id         = 1,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_ADC_SINGLE),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PC2",
        .description        = "LM35 Temp Sensor (U22, 10mV/degC)"
    },
    /* CH_02: PC0 - Quang trở LDR GL5528 qua đệm Op-amp LM358 */
    {
        .channel_id         = 2,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_ADC_SINGLE),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PC0",
        .description        = "LDR GL5528 + Op-amp (U23A)"
    },
    /* CH_03: PB0 - Cầu phân áp 15k/1.5k qua Op-amp LM358 */
    {
        .channel_id         = 3,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_ADC_SINGLE),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PB0",
        .description        = "Power Divider 15k/1.5k + Op-amp (U1A)"
    },
    /* CH_04: PB1 - Cảm biến dòng Hall ACS712-5B qua Op-amp LM358 */
    {
        .channel_id         = 4,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_ADC_SINGLE),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PB1",
        .description        = "ACS712-5B Current Sensor + Op-amp (U1B)"
    },
    /* CH_05: PC1 - Biến trở VR1 10k qua đệm Op-amp LM358 */
    {
        .channel_id         = 5,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_ADC_SINGLE),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PC1",
        .description        = "Potentiometer VR1 10k + Op-amp (U23B)"
    },
    /* CH_06: PA4 - ADC12_IN4 trên Header mở rộng P11 pin 3 */
    {
        .channel_id         = 6,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_ADC_SINGLE),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PA4",
        .description        = "ADC12_IN4 Header P11 Pin 3"
    },
    /* CH_07: PA5 - ADC12_IN5 trên Header mở rộng P11 pin 1 (XUNG ĐỘT VỚI CH_08) */
    {
        .channel_id         = 7,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_ADC_SINGLE),
        .conflict_group     = CONFLICT_GROUP_PA5,
        .port_name          = "PA5",
        .description        = "ADC12_IN5 Header P11 Pin 1 (Shared with CH_08)"
    },
    /* CH_08: PA5 - Opto tốc độ cao HCPL-0630 U26 (XUNG ĐỘT VỚI CH_07) */
    {
        .channel_id         = 8,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_PULSE_COUNTER) | MODE_BM(MODE_DIGITAL_IN),
        .conflict_group     = CONFLICT_GROUP_PA5,
        .port_name          = "PA5",
        .description        = "High-Speed Opto HCPL-0630 X0 (Shared with CH_07)"
    },
    /* CH_09: PA6 - Opto tốc độ cao HCPL-0630 U26 */
    {
        .channel_id         = 9,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_PULSE_COUNTER) | MODE_BM(MODE_DIGITAL_IN),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PA6",
        .description        = "High-Speed Opto HCPL-0630 X1"
    },
    /* CH_10: PC4 - Opto tiêu chuẩn EL3H7 U27 */
    {
        .channel_id         = 10,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_DIGITAL_IN),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PC4",
        .description        = "Opto EL3H7 X2 (Digital In)"
    },
    /* CH_11: PC5 - Opto tiêu chuẩn EL3H7 U28 */
    {
        .channel_id         = 11,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_DIGITAL_IN),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PC5",
        .description        = "Opto EL3H7 X3 (Digital In)"
    },
    /* CH_12: PE5 - Opto EL3H7 U24 kích MOSFET NCE6020AK (Open-Drain) */
    {
        .channel_id         = 12,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_OUTPUT),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PE5",
        .description        = "MOSFET Open-Drain NCE6020AK Y0 + LED10"
    },
    /* CH_13: PE6 - Opto EL3H7 U25 kích MOSFET NCE6020AK (Open-Drain) */
    {
        .channel_id         = 13,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_OUTPUT),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PE6",
        .description        = "MOSFET Open-Drain NCE6020AK Y1 + LED11"
    },
    /* CH_14: PF8 - Transistor Q3 kích Buzzer */
    {
        .channel_id         = 14,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_OUTPUT),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PF8",
        .description        = "Buzzer Transistor Driver (2SC1815)"
    },
    /* CH_15: PC10/PC11 - RS485 qua USART3 và PA15 (DE/RE) */
    {
        .channel_id         = 15,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_RS485),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PC10/PC11",
        .description        = "RS485 Port (USART3 + PA15 RTS)"
    },
    /* CH_16: USART1_TX/RX ra cổng DB-9 CON5 */
    {
        .channel_id         = 16,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_RS232),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PA9/PA10",
        .description        = "RS232 DB-9 Port (USART1)"
    },
    /* CH_17: PB10/PB11 - I2C2 Bus trên Header P10 */
    {
        .channel_id         = 17,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_I2C),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PB10/PB11",
        .description        = "I2C2 Bus Header P10 (Pins 5, 7)"
    },
    /* CH_18: PC6/PC7 - USART6 */
    {
        .channel_id         = 18,
        .supported_modes_bm = MODE_BM(MODE_DISABLED) | MODE_BM(MODE_UART),
        .conflict_group     = CONFLICT_GROUP_NONE,
        .port_name          = "PC6/PC7",
        .description        = "UART Port (USART6)"
    }
};

bool channel_hw_caps_is_mode_supported(uint8_t channel_id, channel_interface_mode_t mode)
{
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return false;
    }

    const channel_hw_cap_entry_t *entry = &s_hw_caps[channel_id - 1];
    uint16_t mode_bit = MODE_BM(mode);

    return ((entry->supported_modes_bm & mode_bit) != 0);
}

bool channel_hw_caps_check_conflict(uint8_t channel_id,
                                    const ChannelConfig_t current_table[CHANNEL_MAX_COUNT],
                                    uint8_t *out_conflicting_id)
{
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT || current_table == NULL) {
        return false;
    }

    const channel_hw_cap_entry_t *target_entry = &s_hw_caps[channel_id - 1];

    /* Nếu cổng này không thuộc bất kỳ nhóm xung đột nào -> An toàn */
    if (target_entry->conflict_group == CONFLICT_GROUP_NONE) {
        return true;
    }

    /* Quét toàn bộ bảng xem có cổng nào khác cùng nhóm xung đột đang bật không */
    for (uint8_t i = 0; i < CHANNEL_MAX_COUNT; i++) {
        uint8_t peer_id = i + 1;

        /* Bỏ qua chính cổng đang xét */
        if (peer_id == channel_id) {
            continue;
        }

        /* Nếu cổng đồng nghiệp đang bật và cùng nhóm xung đột */
        if (current_table[i].is_enabled != 0) {
            const channel_hw_cap_entry_t *peer_entry = &s_hw_caps[i];
            if (peer_entry->conflict_group == target_entry->conflict_group) {
                if (out_conflicting_id != NULL) {
                    *out_conflicting_id = peer_id;
                }
                return false; /* Phát hiện xung đột */
            }
        }
    }

    return true; /* Không xung đột */
}

const channel_hw_cap_entry_t *channel_hw_caps_get(uint8_t channel_id)
{
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return NULL;
    }
    return &s_hw_caps[channel_id - 1];
}
