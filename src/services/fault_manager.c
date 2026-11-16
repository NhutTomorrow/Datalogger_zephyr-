/**
 * @file fault_manager.c
 * @brief Hiện thực bảng mã lỗi chuẩn hóa và bộ quản lý lỗi.
 * @note File độc lập phần cứng, sẵn sàng nhúng vào cả STM32 và ESP32.
 */

#include "fault_manager.h"
#include <string.h>

/* ========================================================================= */
/* BẢNG MÃ LỖI ĐỊNH SẴN (GROUND TRUTH CHO GEMINI AI VÀ KỸ THUẬT VIÊN)        */
/* ========================================================================= */

static const fault_desc_t FAULT_TABLE[] = {
    {.code = FAULT_4_20MA_LOOP_OPEN,
     .code_str = "ERR_LOOP_OPEN",
     .severity = FAULT_SEV_CRITICAL,
     .name = "Hở mạch vòng dòng 4-20mA (Dòng < 3.6mA)",
     .possible_cause = "Đứt dây cáp tín hiệu, tuột ốc siết cọc domino hoặc transmitter cảm biến bị mất nguồn.",
     .suggested_action = "Dùng đồng hồ VOM đo thông mạch dây tín hiệu; kiểm tra đèn nguồn của cảm biến và siết chặt cọc đấu dây."},
    {.code = FAULT_4_20MA_LOOP_OVERCURRENT,
     .code_str = "ERR_LOOP_OVERCURRENT",
     .severity = FAULT_SEV_CRITICAL,
     .name = "Quá dòng vòng 4-20mA (Dòng > 21.0mA)",
     .possible_cause = "Chập mạch giữa dây nguồn và đường tín hiệu, hoặc cảm biến công nghiệp bị lỗi hỏng tầng ra.",
     .suggested_action = "Ngắt ngay nguồn loop; kiểm tra xem dây tín hiệu có bị chập vào 24VDC không và kiểm tra điện trở nội của cảm biến."},
    {.code = FAULT_SENSOR_TIMEOUT,
     .code_str = "ERR_I2C_TIMEOUT",
     .severity = FAULT_SEV_WARNING,
     .name = "Mất giao tiếp cảm biến nhiệt/ẩm môi trường",
     .possible_cause = "Tuột giắc cắm bus, lỏng dây tiếp xúc hoặc xung nhiễu điện từ làm treo bus ngoại vi.",
     .suggested_action = "Kiểm tra cáp nối cảm biến môi trường, đo nguồn cấp 3.3V cho cảm biến và khởi động lại trạm đo."},
    {.code = FAULT_SENSOR_DATA_OUT_OF_BOUNDS,
     .code_str = "ERR_SENSOR_OUT_OF_BOUNDS",
     .severity = FAULT_SEV_WARNING,
     .name = "Dữ liệu cảm biến vượt ngưỡng vật lý cho phép",
     .possible_cause = "Đầu dò cảm biến bị bám bụi bẩn, ngập nước, hoặc môi trường đo vượt quá thang đo thiết kế.",
     .suggested_action = "Quan sát hiện trạng vật lý của đầu đo; vệ sinh bề mặt cảm biến và hiệu chuẩn lại điểm zero/span."},
    {.code = FAULT_SD_CARD_NOT_DETECTED,
     .code_str = "ERR_SD_NOT_DETECTED",
     .severity = FAULT_SEV_FATAL,
     .name = "Không phát hiện thẻ nhớ SD lưu trữ",
     .possible_cause = "Chưa cắm thẻ nhớ, khe cắm thẻ bị lỏng hoặc chân tiếp xúc thẻ SD bị oxy hóa/bám bẩn.",
     .suggested_action = "Tháo thẻ SD ra vệ sinh chân đồng bằng cồn, cắm chắc chắn lại vào khe và kiểm tra định dạng FAT32."},
    {.code = FAULT_SD_WRITE_ERROR,
     .code_str = "ERR_SD_WRITE_FAIL",
     .severity = FAULT_SEV_FATAL,
     .name = "Lỗi ghi dữ liệu vào thẻ nhớ SD",
     .possible_cause = "Thẻ nhớ bị gạt lẫy chống ghi vật lý (Lock), thẻ bị lỗi bad sector hoặc mất điện đột ngột làm hỏng bảng FAT.",
     .suggested_action = "Kiểm tra gạt mở khóa Lock trên thẻ; sao lưu dữ liệu ra máy tính và chạy công cụ scan disk định dạng lại thẻ."},
    {.code = FAULT_STORAGE_FULL,
     .code_str = "ERR_STORAGE_FULL",
     .severity = FAULT_SEV_WARNING,
     .name = "Dung lượng thẻ nhớ đã đầy trên 95%",
     .possible_cause = "Dữ liệu đo lưu trữ lâu ngày không được sao chép và dọn dẹp định kỳ.",
     .suggested_action = "Lấy thẻ ra sao lưu dữ liệu các tháng cũ vào máy tính; xóa bớt tệp log cũ hoặc thay thẻ nhớ dung lượng lớn hơn."},
    {.code = FAULT_RTC_BATTERY_LOW,
     .code_str = "ERR_RTC_BATTERY_LOW",
     .severity = FAULT_SEV_WARNING,
     .name = "Pin CMOS đồng hồ thời gian thực (RTC) bị yếu",
     .possible_cause = "Pin nút CR2032 trên board đã cạn sau thời gian dài sử dụng (điện áp dưới 2.5V), dẫn đến sai mốc giờ.",
     .suggested_action = "Thay viên pin cúc áo CR2032 mới trên board mạch và đồng bộ lại thời gian chuẩn cho trạm."},
    {.code = FAULT_SUPPLY_UNDERVOLTAGE,
     .code_str = "ERR_SUPPLY_UNDERVOLT",
     .severity = FAULT_SEV_CRITICAL,
     .name = "Điện áp nguồn trạm sụt thấp (<10.5V)",
     .possible_cause = "Nguồn cấp công nghiệp bị quá tải, ắc quy dự phòng sắp cạn hoặc dây cấp nguồn chính bị suy hao áp.",
     .suggested_action = "Dùng VOM đo điện áp tại đầu vào Domino nguồn của trạm; kiểm tra bình ắc quy và bộ sạc nguồn trạm."},
    {.code = FAULT_COMM_UART_PACKET_LOSS,
     .code_str = "ERR_UART_PACKET_LOSS",
     .severity = FAULT_SEV_WARNING,
     .name = "Mất gói tin / lỗi CRC trên đường truyền UART",
     .possible_cause = "Dây nối TX/RX giữa STM32 và ESP32 bị lỏng, chưa nối chung dây GND hoặc có nguồn nhiễu động cơ gần kề.",
     .suggested_action = "Kiểm tra độ tiếp xúc dây cắm TX-RX; đảm bảo bắt buộc phải có dây nối GND chung giữa 2 board."},
    {.code = FAULT_AI_GATEWAY_TIMEOUT,
     .code_str = "ERR_GATEWAY_TIMEOUT",
     .severity = FAULT_SEV_WARNING,
     .name = "ESP32 AI Gateway không phản hồi",
     .possible_cause = "ESP32 bị treo, mất nguồn cấp hoặc đang bị ngắt kết nối mạng Wi-Fi/Cloud.",
     .suggested_action = "Quan sát đèn LED nguồn và LED trạng thái trên ESP32; kiểm tra sóng Wi-Fi và nhấn nút Reset trên ESP32."}};

#define FAULT_TABLE_SIZE (sizeof(FAULT_TABLE) / sizeof(FAULT_TABLE[0]))

static const fault_desc_t UNKNOWN_FAULT_DESC = {
    .code = FAULT_NONE,
    .code_str = "ERR_UNKNOWN",
    .severity = FAULT_SEV_NONE,
    .name = "Lỗi chưa xác định",
    .possible_cause = "Mã lỗi không nằm trong bảng mã chuẩn hóa của hệ thống.",
    .suggested_action = "Kiểm tra lại phiên bản firmware hoặc kiểm tra nhật ký log hệ thống."};

/* ========================================================================= */
/* HÀM TRỢ GIÚP NỘI BỘ                                                      */
/* ========================================================================= */

static void fault_manager_recalc_severity(fault_manager_t *mgr)
{
    fault_severity_t max_sev = FAULT_SEV_NONE;

    for (uint8_t i = 0; i < mgr->active_count; i++)
    {
        const fault_desc_t *desc = fault_lookup_desc(mgr->active_codes[i]);
        if (desc->severity > max_sev)
        {
            max_sev = desc->severity;
        }
    }
    mgr->highest_severity = max_sev;
}

/* ========================================================================= */
/* HIỆN THỰC CÁC HÀM CÔNG KHAI (API)                                        */
/* ========================================================================= */

void fault_manager_init(fault_manager_t *mgr)
{
    if (mgr == NULL)
        return;
    memset(mgr, 0, sizeof(fault_manager_t));
    mgr->highest_severity = FAULT_SEV_NONE;
}

bool fault_manager_set(fault_manager_t *mgr, uint16_t code)
{
    if (mgr == NULL || code == FAULT_NONE)
        return false;

    /* 1. Kiểm tra xem mã lỗi đã có trong danh sách active chưa */
    for (uint8_t i = 0; i < mgr->active_count; i++)
    {
        if (mgr->active_codes[i] == code)
        {
            return true; /* Đã tồn tại, không thêm lặp */
        }
    }

    /* 2. Nếu danh sách còn chỗ thì thêm vào */
    if (mgr->active_count < FAULT_MAX_ACTIVE_LIST)
    {
        mgr->active_codes[mgr->active_count++] = code;
        fault_manager_recalc_severity(mgr);
        return true;
    }

    return false; /* Đã đầy bộ đệm quản lý */
}

bool fault_manager_clear(fault_manager_t *mgr, uint16_t code)
{
    if (mgr == NULL || code == FAULT_NONE)
        return false;

    for (uint8_t i = 0; i < mgr->active_count; i++)
    {
        if (mgr->active_codes[i] == code)
        {
            /* Dồn mảng sang trái để loại bỏ phần tử */
            for (uint8_t j = i; j < mgr->active_count - 1; j++)
            {
                mgr->active_codes[j] = mgr->active_codes[j + 1];
            }
            mgr->active_count--;
            mgr->active_codes[mgr->active_count] = 0;
            fault_manager_recalc_severity(mgr);
            return true;
        }
    }
    return false;
}

void fault_manager_clear_all(fault_manager_t *mgr)
{
    if (mgr == NULL)
        return;
    fault_manager_init(mgr);
}

bool fault_manager_is_active(const fault_manager_t *mgr, uint16_t code)
{
    if (mgr == NULL || code == FAULT_NONE)
        return false;

    for (uint8_t i = 0; i < mgr->active_count; i++)
    {
        if (mgr->active_codes[i] == code)
        {
            return true;
        }
    }
    return false;
}

uint8_t fault_manager_get_count(const fault_manager_t *mgr)
{
    return (mgr != NULL) ? mgr->active_count : 0;
}

uint8_t fault_manager_get_list(const fault_manager_t *mgr, uint16_t *out_codes, uint8_t max_count)
{
    if (mgr == NULL || out_codes == NULL || max_count == 0)
        return 0;

    uint8_t copy_count = (mgr->active_count < max_count) ? mgr->active_count : max_count;
    for (uint8_t i = 0; i < copy_count; i++)
    {
        out_codes[i] = mgr->active_codes[i];
    }
    return copy_count;
}

fault_severity_t fault_manager_get_max_severity(const fault_manager_t *mgr)
{
    return (mgr != NULL) ? mgr->highest_severity : FAULT_SEV_NONE;
}

const fault_desc_t *fault_lookup_desc(uint16_t code)
{
    for (size_t i = 0; i < FAULT_TABLE_SIZE; i++)
    {
        if (FAULT_TABLE[i].code == code)
        {
            return &FAULT_TABLE[i];
        }
    }
    return &UNKNOWN_FAULT_DESC;
}

const char *fault_severity_to_str(fault_severity_t sev)
{
    switch (sev)
    {
    case FAULT_SEV_INFO:
        return "INFOMATION";
    case FAULT_SEV_WARNING:
        return "WARNING";
    case FAULT_SEV_CRITICAL:
        return "CRITICAL";
    case FAULT_SEV_FATAL:
        return "FATAL";
    case FAULT_SEV_NONE:
    default:
        return "NORMAL";
    }
}
