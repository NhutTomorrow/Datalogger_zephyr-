/**
 * @file channel_manager.h
 * @brief Bộ điều phối và quản lý toàn diện các kênh đo động (Unified Channel Manager).
 *
 * PHÂN CẤP KIẾN TRÚC:
 * 1. ChannelConfig_t (channel_config.h) : Dữ liệu tĩnh (32B) lưu Flash NVS / Frame UART.
 * 2. channel_runtime_t                 : Đối tượng runtime của 1 kênh chạy trong RAM (chứa config + timer RTOS).
 * 3. channel_manager_t                 : Quản lý tập trung 18 kênh, Mutex và Flash NVS.
 * 4. channel_snapshot_t                : Bản chụp dữ liệu chỉ đọc (DTO) xuất ra LCD / Telemetry.
 */

#ifndef CHANNEL_MANAGER_H
#define CHANNEL_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

#include "channel_config.h"
#include "channel_driver_ops.h"
#include "fault_manager.h"
#include "com_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* DTO & DRIVER OPS ĐƯỢC ĐỊNH NGHĨA TRONG channel_config.h & channel_driver_ops.h */
/* ========================================================================= */

/* ========================================================================= */
/* 3. VÒNG ĐỜI VÀ TIẾN TRÌNH RUNTIME CỦA 1 KÊNH (CHANNEL RUNTIME CONTEXT)    */
/* ========================================================================= */

typedef enum {
    CHANNEL_STATE_UNCONFIGURED = 0, /**< Chưa cấu hình (Mặc định an toàn) */
    CHANNEL_STATE_ACTIVE       = 1, /**< Đang hoạt động, lấy mẫu định kỳ */
    CHANNEL_STATE_SUSPENDED    = 2, /**< Tạm dừng (lỗi phần cứng/chờ phục hồi) */
    CHANNEL_STATE_STOPPING     = 3  /**< Đang dừng / hủy worker */
} channel_state_t;

struct channel_manager;

/**
 * @brief Ngữ cảnh hoạt động thực tế của 1 kênh trong RAM.
 */
typedef struct {
    ChannelConfig_t             config;         /**< Bản sao cấu hình tĩnh (32B) */
    char                        name[CHANNEL_NAME_MAX_LEN]; /**< Nhãn kênh */
    channel_state_t             state;          /**< Trạng thái vòng đời hiện tại */
    float                       last_value;     /**< Giá trị đo mới nhất */
    bool                        is_valid;       /**< Dữ liệu hợp lệ hay rác */
    uint16_t                    fault_code;     /**< Mã lỗi chẩn đoán */
    const channel_driver_ops_t *driver_ops;     /**< Driver phần cứng điều khiển */
    struct k_work_delayable     sample_work;    /**< Worker định thời của Zephyr */
    struct channel_manager     *parent_mgr;     /**< Con trỏ trỏ về Manager cha */
    uint32_t                    last_sample_ms; /**< Timestamp lần lấy mẫu gần nhất */
} channel_runtime_t;

/* ========================================================================= */
/* 4. PHÒNG ĐIỀU KHIỂN TRUNG TÂM (CHANNEL MANAGER CONTEXT)                   */
/* ========================================================================= */

typedef struct channel_manager {
    channel_runtime_t       channels[CHANNEL_MAX_COUNT]; /**< Quản lý 18 kênh vật lý */
    config_table_header_t   header;                      /**< Header phiên bản & CRC */
    struct k_mutex          config_lock;                 /**< Mutex chống xung đột luồng */
    uint32_t                current_slot_offset;         /**< Offset của Slot ghi tiếp theo (Rolling Slot) */
    bool                    flash_valid;                 /**< Trạng thái phân vùng Flash */
    bool                    nvs_valid;                   /**< Tương thích API (true nếu Flash sẵn sàng) */
    bool                    is_dirty;                    /**< Đánh dấu RAM khác Flash */
    struct k_work_delayable deferred_save_work;          /**< Timer 10s hoãn ghi Flash */
    fault_manager_t        *fault_mgr;                   /**< Fault Manager toàn trạm */
} channel_manager_t;

/* ========================================================================= */
/* API ĐIỀU PHỐI HỆ THỐNG                                                    */
/* ========================================================================= */

int channel_manager_init(channel_manager_t *mgr, fault_manager_t *fault_mgr);

int channel_manager_configure(channel_manager_t *mgr,
                              const ChannelConfig_t *cfg,
                              const char *name,
                              const channel_driver_ops_t *ops);

int channel_manager_disable(channel_manager_t *mgr, uint8_t channel_id);

int channel_manager_flush_nvs(channel_manager_t *mgr);

int channel_manager_read(channel_manager_t *mgr, uint8_t channel_id, float *out_value);

uint8_t channel_manager_get_snapshot(channel_manager_t *mgr, channel_snapshot_t *out_snapshot);

uint8_t channel_manager_get_config_table(channel_manager_t *mgr,
                                         config_table_header_t *out_header,
                                         ChannelConfig_t *out_table,
                                         uint8_t max_count);

uint8_t channel_manager_get_active_count(const channel_manager_t *mgr);

int channel_manager_get_channel_config(channel_manager_t *mgr, uint8_t channel_id, ChannelConfig_t *out_cfg);

/**
 * @brief Đọc dữ liệu tức thời của 1 kênh duy nhất phục vụ lệnh UART AI Gateway (0x02/0x82).
 * @param mgr Con trỏ channel_manager_t
 * @param channel_id ID cổng vật lý (1 .. 18)
 * @param out_resp Con trỏ nhận dữ liệu đóng gói comm_payload_read_channel_resp_t
 * @return true nếu channel_id hợp lệ trong dải 1..18, false nếu ngoài dải
 */
bool channel_manager_read_single(const channel_manager_t *mgr,
                                 uint8_t channel_id,
                                 comm_payload_read_channel_resp_t *out_resp);

/**
 * @brief Định dạng và đóng gói dữ liệu bản chụp toàn trạm sang comm_payload_dynamic_telemetry_t (0x1A/0x9A).
 * @param mgr Con trỏ channel_manager_t
 * @param active_fault_count Số lượng lỗi active (từ fault_manager)
 * @param logging_state Trạng thái logging hiện tại (0: STOPPED, 1: LOGGING)
 * @param out_payload Con trỏ nhận dữ liệu đóng gói
 * @param out_payload_len Con trỏ nhận kích thước byte thực tế của payload
 */
void channel_manager_get_dynamic_telemetry(channel_manager_t *mgr,
                                           uint8_t active_fault_count,
                                           uint8_t logging_state,
                                           comm_payload_dynamic_telemetry_t *out_payload,
                                           uint16_t *out_payload_len);

void channel_manager_check_health(channel_manager_t *mgr);

void channel_manager_inject_fault(channel_manager_t *mgr, uint8_t channel_id, uint16_t fault_code, bool enable);

int channel_manager_factory_reset(channel_manager_t *mgr);

#ifdef __cplusplus
}
#endif

#endif /* CHANNEL_MANAGER_H */
