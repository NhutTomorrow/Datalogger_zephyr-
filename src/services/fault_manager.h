/**
 * @file fault_manager.h
 * @brief Hệ thống quản lý và chuẩn hóa mã lỗi cho Datalogger công nghiệp.
 * @note File thuần C99, dùng chung cho cả STM32 (Datalogger Core) và ESP32 (AI Gateway).
 *       Cung cấp bảng mã lỗi có sẵn nguyên nhân và giải pháp để Grounding chống Hallucination cho AI.
 */

#ifndef FAULT_MANAGER_H
#define FAULT_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "fault_codes.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* ========================================================================= */
    /* MÃ LỖI ĐƯỢC ĐỊNH NGHĨA TRONG fault_codes.h (Pure Contract)               */
    /* ========================================================================= */

    /* ========================================================================= */
    /* MỨC ĐỘ NGHIÊM TRỌNG (SEVERITY)                                           */
    /* ========================================================================= */

    typedef enum
    {
        FAULT_SEV_NONE = 0,
        FAULT_SEV_INFO = 1,     /**< Thông tin cảnh báo nhẹ, không gián đoạn đo */
        FAULT_SEV_WARNING = 2,  /**< Cảnh báo, một kênh cảm biến hoặc tính năng phụ bị ảnh hưởng */
        FAULT_SEV_CRITICAL = 3, /**< Nghiêm trọng: mất dữ liệu quan trắc, hở mạch chính */
        FAULT_SEV_FATAL = 4     /**< Nguy hiểm: sụt nguồn, thẻ nhớ lỗi không thể ghi log */
    } fault_severity_t;

    /* ========================================================================= */
    /* BẢNG MÔ TẢ CHI TIẾT (DÙNG ĐỂ GROUNDING AI & HIỂN THỊ LCD)                 */
    /* ========================================================================= */

    typedef struct
    {
        uint16_t code;                /**< Mã lỗi số hex (ví dụ 0x1001) */
        const char *code_str;         /**< Chuỗi mã ngắn (ví dụ "ERR_LOOP_OPEN") */
        fault_severity_t severity;    /**< Mức độ nghiêm trọng */
        const char *name;             /**< Tên lỗi tiếng Việt */
        const char *possible_cause;   /**< Nguyên nhân khả dĩ kỹ thuật */
        const char *suggested_action; /**< Hướng dẫn kỹ thuật viên kiểm tra xử lý */
    } fault_desc_t;

    /* ========================================================================= */
    /* ĐỐI TƯỢNG QUẢN LÝ LỖI (FAULT MANAGER CONTEXT)                             */
    /* ========================================================================= */

    typedef struct
    {
        uint16_t active_codes[FAULT_MAX_ACTIVE_LIST];
        uint8_t active_count;
        fault_severity_t highest_severity;
    } fault_manager_t;

    /* ========================================================================= */
    /* FUNCTION PROTOTYPES                                                       */
    /* ========================================================================= */

    /**
     * @brief Khởi tạo đối tượng quản lý lỗi (reset trắng danh sách).
     */
    void fault_manager_init(fault_manager_t *mgr);

    /**
     * @brief Đánh dấu kích hoạt 1 mã lỗi vào hệ thống.
     *
     * @param mgr   Con trỏ đối tượng fault_manager
     * @param code  Mã lỗi (ví dụ FAULT_4_20MA_LOOP_OPEN)
     * @return true nếu ghi nhận thành công hoặc đã có sẵn
     */
    bool fault_manager_set(fault_manager_t *mgr, uint16_t code);

    /**
     * @brief Xóa 1 mã lỗi khỏi hệ thống khi sự cố đã khắc phục.
     *
     * @param mgr   Con trỏ đối tượng fault_manager
     * @param code  Mã lỗi cần xóa
     * @return true nếu xóa thành công, false nếu mã này không active
     */
    bool fault_manager_clear(fault_manager_t *mgr, uint16_t code);

    /**
     * @brief Xóa toàn bộ danh sách lỗi active.
     */
    void fault_manager_clear_all(fault_manager_t *mgr);

    /**
     * @brief Kiểm tra xem một mã lỗi cụ thể có đang active không.
     */
    bool fault_manager_is_active(const fault_manager_t *mgr, uint16_t code);

    /**
     * @brief Lấy số lượng mã lỗi đang active.
     */
    uint8_t fault_manager_get_count(const fault_manager_t *mgr);

    /**
     * @brief Sao chép danh sách các mã lỗi active ra mảng ngoài.
     *
     * @param mgr       Con trỏ fault_manager
     * @param out_codes Mảng nhận mã lỗi
     * @param max_count Kích thước tối đa của out_codes
     * @return Số lượng mã lỗi đã sao chép
     */
    uint8_t fault_manager_get_list(const fault_manager_t *mgr, uint16_t *out_codes, uint8_t max_count);

    /**
     * @brief Lấy mức độ lỗi cao nhất hiện thời của hệ thống.
     */
    fault_severity_t fault_manager_get_max_severity(const fault_manager_t *mgr);

    /**
     * @brief Tra cứu bảng mô tả chi tiết của một mã lỗi.
     * @note Hàm này cực kỳ hữu ích trên ESP32: khi nhận mã lỗi từ STM32,
     *       gọi hàm này để lấy mô tả, nguyên nhân và cách sửa, sau đó đẩy vào Context
     *       cho Gemini LLM trả lời kỹ thuật viên mà không bị hallucination.
     *
     * @param code  Mã lỗi cần tra cứu
     * @return Con trỏ const đến cấu trúc fault_desc_t (hoặc descriptor mặc định nếu không tìm thấy)
     */
    const fault_desc_t *fault_lookup_desc(uint16_t code);

    /**
     * @brief Lấy chuỗi chữ mô tả mức độ nghiêm trọng.
     */
    const char *fault_severity_to_str(fault_severity_t sev);

#ifdef __cplusplus
}
#endif

#endif /* FAULT_MANAGER_H */
