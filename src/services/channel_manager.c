/**
 * @file channel_manager.c
 * @brief Hiện thực Channel Manager Core (Hợp nhất từ sensor_manager).
 */

#include "channel_manager.h"
#include "channel_hw_caps.h"
#include "driver_registry.h"

#include <string.h>
#include <stdio.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>

LOG_MODULE_REGISTER(channel_mgr, LOG_LEVEL_INF);

#define STORAGE_PARTITION           storage_partition
#define STORAGE_PARTITION_ID        FIXED_PARTITION_ID(STORAGE_PARTITION)
#define DEFERRED_SAVE_DELAY_SEC     100

#pragma pack(push, 1)
/**
 * @brief Bản ghi lưu trữ toàn bộ cấu hình 18 kênh (1 Slot trong Flash).
 */
typedef struct {
    config_table_header_t header;                            /**< 12 bytes */
    ChannelConfig_t        channels[CHANNEL_MAX_COUNT];       /**< 18 * 32 = 576 bytes */
    uint8_t                padding[4];                        /**< Căn gióng 8-byte: 588 + 4 = 592 bytes */
} flash_config_record_t;
#pragma pack(pop)

#define FLASH_SLOT_SIZE             sizeof(flash_config_record_t)

/* Forward declarations */
static void channel_sample_work_handler(struct k_work *work);
static void channel_deferred_save_handler(struct k_work *work);

/* ========================================================================= */
/* HÀM TRỢ GIÚP NỘI BỘ                                                       */
/* ========================================================================= */

/**
 * @brief Tính toán mã kiểm tra toàn vẹn CRC32 (chuẩn Ethernet / IEEE 802.3).
 * @param data Con trỏ đến mảng byte dữ liệu cần tính toán
 * @param length Số lượng byte dữ liệu
 * @return Giá trị checksum 32-bit CRC32
 */
static uint32_t calc_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

/* ========================================================================= */
/* WORKQUEUE HANDLERS                                                        */
/* ========================================================================= */

/**
 * @brief Callback định thời của Zephyr System Workqueue để lấy mẫu định kỳ cho 1 kênh.
 * @note Hàm này chạy trong luồng System Workqueue. Nó gọi hàm driver đọc dữ liệu thô,
 *       áp dụng công thức hiệu chuẩn (Linear Calib), kiểm tra tính hợp lệ,
 *       cập nhật giá trị vào RAM và tự động lập lịch cho chu kỳ kế tiếp.
 * @param work Con trỏ struct k_work được truyền từ Zephyr Workqueue
 * @return Không có
 */
static void channel_sample_work_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    channel_runtime_t *ch = CONTAINER_OF(dwork, channel_runtime_t, sample_work);
    channel_manager_t *mgr = ch->parent_mgr;

    if (ch->state != CHANNEL_STATE_ACTIVE || mgr == NULL) {
        return;
    }

    float raw_val = 0.0f;
    bool ok = false;

    if (ch->driver_ops != NULL && ch->driver_ops->read != NULL) {
        ok = ch->driver_ops->read(ch->config.channel_id, &ch->config, &raw_val);
    }

    if (ok) {
        ch->is_valid = true;
        ch->fault_code = FAULT_NONE;

        /* Áp dụng công thức hiệu chuẩn Linear nếu là nhánh ADC */
        if (ch->config.calib_type == CALIB_LINEAR) {
            ch->last_value = (raw_val * ch->config.calib.adc.scale_a) + ch->config.calib.adc.offset_b;
        } else {
            ch->last_value = raw_val;
        }
    } else {
        ch->is_valid = false;
        /* Nếu driver có health_check, lấy mã lỗi chi tiết */
        if (ch->driver_ops != NULL && ch->driver_ops->health_check != NULL) {
            uint16_t fault = FAULT_NONE;
            ch->driver_ops->health_check(ch->config.channel_id, &fault);
            ch->fault_code = fault;
            if (mgr->fault_mgr != NULL && fault != FAULT_NONE) {
                fault_manager_set(mgr->fault_mgr, fault);
            }
        }
    }

    ch->last_sample_ms = k_uptime_get_32();

    /* Tái lập lịch cho chu kỳ tiếp theo (tối thiểu 50ms) */
    uint16_t interval = ch->config.sample_interval_ms;
    if (interval < 50) {
        interval = 1000;
    }
    k_work_reschedule(&ch->sample_work, K_MSEC(interval));
}

/**
 * @brief Callback hoãn ghi Flash NVS khi hết 10 giây không còn thao tác cấu hình (Chiến lược B).
 * @note Hàm này gom toàn bộ các thay đổi cấu hình trong RAM, tính toán CRC32 toàn bảng,
 *       thực hiện 1 lần ghi NVS đồng loạt và xóa cờ is_dirty để bảo vệ tuổi thọ sector Flash.
 * @param work Con trỏ struct k_work của deferred_save_work
 * @return Không có
 */
/**
 * @brief Ghi lưu cấu hình xuống Flash theo cơ chế Append-Only Rolling Slot.
 * @note Không xóa sector mỗi lần ghi. Chỉ ghi tịnh tiến vào slot trống kế tiếp.
 */
static int channel_manager_save_internal(channel_manager_t *mgr)
{
    if (mgr == NULL || !mgr->flash_valid) {
        return -ENODEV;
    }

    const struct flash_area *fa;
    int rc = flash_area_open(STORAGE_PARTITION_ID, &fa);
    if (rc != 0) {
        LOG_ERR("Khong the mo flash_area storage_partition khi luu: %d", rc);
        return rc;
    }

    /* Kiểm tra nếu sector đã đầy (hết 220+ slots) -> Xóa sector 1 lần duy nhất để quay vòng về slot 0 */
    if (mgr->current_slot_offset + sizeof(flash_config_record_t) > fa->fa_size) {
        LOG_INF("Flash Sector da su dung het slots (%u bytes). Xoa sector de quay vong...", mgr->current_slot_offset);
        rc = flash_area_erase(fa, 0, fa->fa_size);
        if (rc != 0) {
            LOG_ERR("Loi xoa Flash Sector: %d", rc);
            flash_area_close(fa);
            return rc;
        }
        mgr->current_slot_offset = 0;
    }

    /* Đóng gói toàn bộ cấu hình 18 kênh */
    flash_config_record_t record;
    memset(&record, 0, sizeof(record));

    for (uint8_t i = 0; i < CHANNEL_MAX_COUNT; i++) {
        record.channels[i] = mgr->channels[i].config;
    }

    mgr->header.magic_number = CHANNEL_CONFIG_MAGIC;
    mgr->header.crc32 = calc_crc32((const uint8_t *)record.channels, sizeof(record.channels));
    record.header = mgr->header;

    /* Ghi tịnh tiến vào slot trống hiện tại mà KHÔNG CẦN XÓA SECTOR */
    rc = flash_area_write(fa, mgr->current_slot_offset, &record, sizeof(record));
    if (rc == 0) {
        mgr->is_dirty = false;
        uint32_t slot_idx = mgr->current_slot_offset / FLASH_SLOT_SIZE;
        LOG_INF("Luu cau hinh vao Flash Slot %u (offset 0x%05X) thanh cong! Ver: %u, CRC: 0x%08X",
                slot_idx, mgr->current_slot_offset, mgr->header.config_version, mgr->header.crc32);
        mgr->current_slot_offset += FLASH_SLOT_SIZE;
    } else {
        LOG_ERR("Loi ghi Flash tai offset 0x%05X: %d", mgr->current_slot_offset, rc);
    }

    flash_area_close(fa);
    return rc;
}

static void channel_deferred_save_handler(struct k_work *work)
{
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    channel_manager_t *mgr = CONTAINER_OF(dwork, channel_manager_t, deferred_save_work);

    if (mgr == NULL) return;

    k_mutex_lock(&mgr->config_lock, K_FOREVER);

    if (!mgr->is_dirty || !mgr->flash_valid) {
        k_mutex_unlock(&mgr->config_lock);
        return;
    }

    LOG_INF("Deferred Write Timer expired -> Ghi dong bo mang cau hinh xuong Flash...");
    channel_manager_save_internal(mgr);

    k_mutex_unlock(&mgr->config_lock);
}

/* ========================================================================= */
/* API QUẢN LÝ KÊNH ĐO                                                       */
/* ========================================================================= */

/**
 * @brief Khởi tạo đối tượng Channel Manager, mount phân vùng Zephyr NVS và khôi phục các kênh.
 * @param mgr Con trỏ đến đối tượng channel_manager_t cần khởi tạo
 * @param fault_mgr Con trỏ đến Fault Manager của toàn trạm để liên kết đẩy mã lỗi
 * @return 0 nếu thành công, mã lỗi âm (ví dụ -EINVAL) nếu tham số không hợp lệ
 */
int channel_manager_init(channel_manager_t *mgr, fault_manager_t *fault_mgr)
{
    if (mgr == NULL) return -EINVAL;

    memset(mgr, 0, sizeof(channel_manager_t));
    mgr->fault_mgr = fault_mgr;
    k_mutex_init(&mgr->config_lock);

    /* Gán con trỏ ngược cho từng kênh */
    for (uint8_t i = 0; i < CHANNEL_MAX_COUNT; i++) {
        mgr->channels[i].parent_mgr = mgr;
        mgr->channels[i].config.channel_id = i + 1;
        mgr->channels[i].state = CHANNEL_STATE_UNCONFIGURED;
        snprintf(mgr->channels[i].name, sizeof(mgr->channels[i].name), "CH_%02u", i + 1);
        k_work_init_delayable(&mgr->channels[i].sample_work, channel_sample_work_handler);
    }

    k_work_init_delayable(&mgr->deferred_save_work, channel_deferred_save_handler);

    /* Mở phân vùng Flash và quét tìm bản ghi cấu hình mới nhất (Rolling Slot) */
    const struct flash_area *fa;
    int rc = flash_area_open(STORAGE_PARTITION_ID, &fa);
    if (rc == 0) {
        mgr->flash_valid = true;
        mgr->nvs_valid = true;
        LOG_INF("Mo Flash storage_partition thanh cong (size %u bytes, slot_size %u bytes)",
                (uint32_t)fa->fa_size, (uint32_t)FLASH_SLOT_SIZE);

        uint32_t first_empty_offset = UINT32_MAX;
        uint32_t latest_slot_offset = UINT32_MAX;
        uint32_t highest_version = 0;
        flash_config_record_t best_record;
        bool found_valid = false;

        for (uint32_t offset = 0; offset + FLASH_SLOT_SIZE <= fa->fa_size; offset += FLASH_SLOT_SIZE) {
            flash_config_record_t rec;
            int r_rc = flash_area_read(fa, offset, &rec, sizeof(rec));
            if (r_rc != 0) {
                LOG_ERR("Loi doc Flash tai offset 0x%05X: %d", offset, r_rc);
                break;
            }

            /* Slot trống trên Flash NOR có giá trị 0xFFFFFFFF */
            if (rec.header.magic_number == 0xFFFFFFFF) {
                first_empty_offset = offset;
                break;
            }

            if (rec.header.magic_number == CHANNEL_CONFIG_MAGIC) {
                uint32_t crc = calc_crc32((const uint8_t *)rec.channels, sizeof(rec.channels));
                if (crc == rec.header.crc32) {
                    if (!found_valid || rec.header.config_version >= highest_version) {
                        highest_version = rec.header.config_version;
                        latest_slot_offset = offset;
                        best_record = rec;
                        found_valid = true;
                    }
                } else {
                    LOG_WRN("Slot tai 0x%05X hong CRC (0x%08X != 0x%08X), bo qua",
                            offset, rec.header.crc32, crc);
                }
            }
        }

        if (first_empty_offset != UINT32_MAX) {
            mgr->current_slot_offset = first_empty_offset;
        } else {
            mgr->current_slot_offset = fa->fa_size;
        }

        if (found_valid) {
            mgr->header = best_record.header;
            for (uint8_t i = 0; i < CHANNEL_MAX_COUNT; i++) {
                mgr->channels[i].config = best_record.channels[i];
            }
            mgr->is_dirty = false;
            LOG_INF("Khoi phuc thanh cong cau hinh tu Flash Slot %u (offset 0x%05X)! Version: %u, CRC: 0x%08X",
                    latest_slot_offset / FLASH_SLOT_SIZE, latest_slot_offset,
                    mgr->header.config_version, mgr->header.crc32);
        } else {
            mgr->header.magic_number = CHANNEL_CONFIG_MAGIC;
            mgr->header.config_version = 1;
            mgr->header.crc32 = 0;
            mgr->is_dirty = true;
            LOG_INF("Chua co cau hinh tren Flash. Chay voi cau hinh RAM mac dinh.");
        }

        flash_area_close(fa);
    } else {
        mgr->flash_valid = false;
        mgr->nvs_valid = false;
        LOG_ERR("Khong mo duoc flash_area storage_partition (code %d)", rc);
        mgr->header.magic_number = CHANNEL_CONFIG_MAGIC;
        mgr->header.config_version = 1;
        mgr->header.crc32 = 0;
        mgr->is_dirty = false;
    }

    /* Tự động kích hoạt các kênh có is_enabled == 1 và khởi tạo driver phần cứng tương ứng */
    for (uint8_t i = 0; i < CHANNEL_MAX_COUNT; i++) {
        channel_runtime_t *ch = &mgr->channels[i];
        if (ch->config.is_enabled != 0) {
            uint8_t id = ch->config.channel_id;
            const channel_driver_ops_t *ops = driver_registry_get_ops(id, (channel_interface_mode_t)ch->config.interface_mode);
            if (ops != NULL) {
                ch->driver_ops = ops;
                if (ops->init != NULL) {
                    ops->init();
                }
            }
            driver_registry_init_channel_hw(id, (channel_interface_mode_t)ch->config.interface_mode);
            ch->state = CHANNEL_STATE_ACTIVE;
            uint16_t interval = ch->config.sample_interval_ms >= 50 ? ch->config.sample_interval_ms : 1000;
            k_work_reschedule(&ch->sample_work, K_MSEC(interval));
            LOG_INF("Khoi dong lai CH_%02u (Mode: %u, Interval: %ums)", id, ch->config.interface_mode, interval);
        }
    }

    return 0;
}

/**
 * @brief Cấu hình động một cổng vật lý, kiểm tra an toàn điện, cập nhật RAM và hẹn giờ hoãn ghi Flash.
 * @param mgr Con trỏ channel_manager_t
 * @param cfg Con trỏ chứa cấu hình 32 bytes của kênh (mode, calib, interval, alarms)
 * @param name Tên nhãn hiển thị của kênh (ví dụ: "NhietDo", có thể NULL để lấy tên mặc định)
 * @param ops Bảng hàm driver ngoại vi (có thể NULL nếu kênh không dùng driver)
 * @return 0 nếu cấu hình thành công;
 *         -EINVAL nếu tham số/channel_id không hợp lệ;
 *         -ENOTSUP nếu chế độ không tương thích với phần cứng cổng;
 *         -EBUSY nếu phát hiện xung đột chân vật lý với kênh khác đang chạy.
 */
int channel_manager_configure(channel_manager_t *mgr,
                              const ChannelConfig_t *cfg,
                              const char *name,
                              const channel_driver_ops_t *ops)
{
    if (mgr == NULL || cfg == NULL) return -EINVAL;
    if (cfg->channel_id < 1 || cfg->channel_id > CHANNEL_MAX_COUNT) return -EINVAL;

    uint8_t id = cfg->channel_id;
    uint8_t idx = id - 1;

    k_mutex_lock(&mgr->config_lock, K_FOREVER);

    /* 1. Kiểm tra mode phần cứng có hỗ trợ không */
    if (!channel_hw_caps_is_mode_supported(id, (channel_interface_mode_t)cfg->interface_mode)) {
        LOG_WRN("Cổng CH_%02u khong ho tro interface_mode %u", id, cfg->interface_mode);
        k_mutex_unlock(&mgr->config_lock);
        return -ENOTSUP;
    }

    /* 2. Kiểm tra xung đột chân vật lý nếu kênh này bật */
    if (cfg->is_enabled != 0) {
        ChannelConfig_t temp_table[CHANNEL_MAX_COUNT];
        for (uint8_t i = 0; i < CHANNEL_MAX_COUNT; i++) {
            temp_table[i] = mgr->channels[i].config;
        }
        uint8_t conflict_id = 0;
        if (!channel_hw_caps_check_conflict(id, temp_table, &conflict_id)) {
            LOG_ERR("Xung dot phan cung: CH_%02u xung dot chan voi CH_%02u dang chay", id, conflict_id);
            k_mutex_unlock(&mgr->config_lock);
            return -EBUSY;
        }
    }

    channel_runtime_t *ch = &mgr->channels[idx];

    /* 3. Dừng worker cũ nếu kênh đang chạy */
    if (ch->state == CHANNEL_STATE_ACTIVE) {
        struct k_work_sync sync;
        k_work_cancel_delayable_sync(&ch->sample_work, &sync);
    }

    /* 4. Cập nhật cấu hình vào RAM */
    ch->config = *cfg;
    if (name != NULL && strlen(name) > 0) {
        strncpy(ch->name, name, sizeof(ch->name) - 1);
        ch->name[sizeof(ch->name) - 1] = '\0';
    }

    /* 5. Gán driver ops: ưu tiên ops truyền vào, nếu NULL thì tự tra cứu Driver Registry */
    if (ops == NULL && cfg->is_enabled != 0) {
        ops = driver_registry_get_ops(id, (channel_interface_mode_t)cfg->interface_mode);
        if (ops == NULL) {
            LOG_WRN("Khong tim thay driver phu hop cho CH_%02u voi mode %u", id, cfg->interface_mode);
            k_mutex_unlock(&mgr->config_lock);
            return -ENOTSUP;
        }
    }

    if (ops != NULL) {
        ch->driver_ops = ops;
        if (ops->init != NULL) {
            ops->init();
        }
    }

    /* Khởi tạo phần cứng riêng của cổng */
    if (cfg->is_enabled != 0) {
        int hw_rc = driver_registry_init_channel_hw(id, (channel_interface_mode_t)cfg->interface_mode);
        if (hw_rc != 0) {
            LOG_ERR("Loi khoi tao phan cung cho CH_%02u: %d", id, hw_rc);
            k_mutex_unlock(&mgr->config_lock);
            return hw_rc;
        }
    }

    /* 6. Chuyển trạng thái và khởi động lấy mẫu */
    if (cfg->is_enabled != 0) {
        ch->state = CHANNEL_STATE_ACTIVE;
        uint16_t interval = cfg->sample_interval_ms >= 50 ? cfg->sample_interval_ms : 1000;
        k_work_reschedule(&ch->sample_work, K_MSEC(interval));
    } else {
        ch->state = CHANNEL_STATE_UNCONFIGURED;
    }

    /* 7. Tăng phiên bản cấu hình và hẹn giờ Deferred Write 10s */
    mgr->header.config_version++;
    mgr->is_dirty = true;
    k_work_reschedule(&mgr->deferred_save_work, K_SECONDS(DEFERRED_SAVE_DELAY_SEC));

    LOG_INF("Da cau hinh CH_%02u [%s] -> Version: %u, Hen gio luu NVS sau %ds",
            id, ch->name, mgr->header.config_version, DEFERRED_SAVE_DELAY_SEC);

    k_mutex_unlock(&mgr->config_lock);
    return 0;
}

/**
 * @brief Vô hiệu hóa một cổng vật lý, hủy worker định thời và đánh dấu cập nhật Flash.
 * @param mgr Con trỏ channel_manager_t
 * @param channel_id ID cổng cần vô hiệu hóa (1 .. 18)
 * @return 0 nếu thành công, -EINVAL nếu tham số không hợp lệ
 */
int channel_manager_disable(channel_manager_t *mgr, uint8_t channel_id)
{
    if (mgr == NULL || channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return -EINVAL;
    }

    uint8_t idx = channel_id - 1;
    k_mutex_lock(&mgr->config_lock, K_FOREVER);

    channel_runtime_t *ch = &mgr->channels[idx];
    if (ch->state == CHANNEL_STATE_ACTIVE) {
        struct k_work_sync sync;
        k_work_cancel_delayable_sync(&ch->sample_work, &sync);
    }

    ch->config.is_enabled = 0;
    ch->state = CHANNEL_STATE_UNCONFIGURED;
    ch->is_valid = false;

    /* Giải phóng cấu hình phần cứng chân pin an toàn */
    driver_registry_deinit_channel_hw(channel_id);

    mgr->header.config_version++;
    mgr->is_dirty = true;
    k_work_reschedule(&mgr->deferred_save_work, K_SECONDS(DEFERRED_SAVE_DELAY_SEC));

    k_mutex_unlock(&mgr->config_lock);
    return 0;
}

/**
 * @brief Cưỡng bức ghi dữ liệu cấu hình từ RAM xuống NVS Flash ngay lập tức (không chờ 10s).
 * @param mgr Con trỏ channel_manager_t
 * @return 0 nếu ghi thành công, -EINVAL nếu mgr NULL, -ENODEV nếu NVS Flash chưa sẵn sàng
 */
int channel_manager_flush_nvs(channel_manager_t *mgr)
{
    if (mgr == NULL) return -EINVAL;
    if (!mgr->flash_valid) return -ENODEV;

    /* Hủy timer chờ và thực hiện ghi ngay lập tức */
    struct k_work_sync sync;
    k_work_cancel_delayable_sync(&mgr->deferred_save_work, &sync);

    k_mutex_lock(&mgr->config_lock, K_FOREVER);
    int rc = channel_manager_save_internal(mgr);
    k_mutex_unlock(&mgr->config_lock);

    return rc;
}

/**
 * @brief Đọc giá trị đo tức thời của 1 kênh từ RAM (không block bus ngoại vi).
 * @param mgr Con trỏ channel_manager_t
 * @param channel_id ID cổng cần đọc (1 .. 18)
 * @param out_value Con trỏ float nhận giá trị đo
 * @return 0 nếu thành công và dữ liệu hợp lệ;
 *         -EINVAL nếu tham số không hợp lệ;
 *         -EAGAIN nếu kênh chưa active hoặc dữ liệu đang không hợp lệ (hở mạch/lỗi).
 */
int channel_manager_read(channel_manager_t *mgr, uint8_t channel_id, float *out_value)
{
    if (mgr == NULL || out_value == NULL || channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return -EINVAL;
    }

    channel_runtime_t *ch = &mgr->channels[channel_id - 1];
    if (ch->state != CHANNEL_STATE_ACTIVE || !ch->is_valid) {
        return -EAGAIN;
    }

    *out_value = ch->last_value;
    return 0;
}

/**
 * @brief Chụp nhanh toàn bộ dữ liệu các kênh đang active để gửi sang LCD hoặc đóng gói Telemetry.
 * @param mgr Con trỏ channel_manager_t
 * @param out_snapshot Con trỏ nhận bản chụp dữ liệu (channel_snapshot_t)
 * @return Số lượng kênh active được sao chép vào snapshot
 */
uint8_t channel_manager_get_snapshot(channel_manager_t *mgr, channel_snapshot_t *out_snapshot)
{
    if (mgr == NULL || out_snapshot == NULL) return 0;

    uint8_t count = 0;
    out_snapshot->timestamp_sec = k_uptime_get_32() / 1000;

    for (uint8_t i = 0; i < CHANNEL_MAX_COUNT; i++) {
        const channel_runtime_t *ch = &mgr->channels[i];
        if (ch->config.is_enabled != 0) {
            channel_telemetry_item_t *s = &out_snapshot->channels[count];
            s->channel_id = ch->config.channel_id;
            strncpy(s->name, ch->name, sizeof(s->name) - 1);
            s->name[sizeof(s->name) - 1] = '\0';
            s->value = ch->last_value;
            s->unit = (sensor_unit_t)ch->config.unit_type;
            s->is_valid = ch->is_valid;
            s->fault_code = ch->fault_code;
            count++;
        }
    }

    out_snapshot->total_channels = count;
    return count;
}

/**
 * @brief Lấy toàn bộ mảng cấu hình và Header phiên bản hiện tại (phục vụ đối soát Boot-Sync với ESP32).
 * @param mgr Con trỏ channel_manager_t
 * @param out_header Con trỏ nhận header phiên bản (có thể NULL nếu không cần)
 * @param out_table Mảng ChannelConfig_t nhận dữ liệu cấu hình
 * @param max_count Số phần tử tối đa của mảng out_table
 * @return Số lượng kênh được sao chép vào out_table
 */
uint8_t channel_manager_get_config_table(channel_manager_t *mgr,
                                         config_table_header_t *out_header,
                                         ChannelConfig_t *out_table,
                                         uint8_t max_count)
{
    if (mgr == NULL || out_table == NULL) return 0;

    k_mutex_lock(&mgr->config_lock, K_FOREVER);

    if (out_header != NULL) {
        *out_header = mgr->header;
    }

    uint8_t copy_count = (max_count < CHANNEL_MAX_COUNT) ? max_count : CHANNEL_MAX_COUNT;
    for (uint8_t i = 0; i < copy_count; i++) {
        out_table[i] = mgr->channels[i].config;
    }

    k_mutex_unlock(&mgr->config_lock);
    return copy_count;
}

/**
 * @brief Đếm tổng số lượng cổng đang có trạng thái is_enabled == 1 trong RAM.
 * @param mgr Con trỏ channel_manager_t
 * @return Số lượng kênh đang hoạt động
 */
uint8_t channel_manager_get_active_count(const channel_manager_t *mgr)
{
    if (mgr == NULL) return 0;

    uint8_t count = 0;
    for (uint8_t i = 0; i < CHANNEL_MAX_COUNT; i++) {
        if (mgr->channels[i].config.is_enabled != 0) {
            count++;
        }
    }
    return count;
}

/**
 * @brief Lấy cấu hình của một cổng cụ thể trong RAM.
 * @param mgr Con trỏ channel_manager_t
 * @param channel_id ID cổng vật lý (1 .. 18)
 * @param out_cfg Con trỏ nhận dữ liệu cấu hình ChannelConfig_t (32 bytes)
 * @return 0 nếu thành công, -EINVAL nếu tham số không hợp lệ
 */
int channel_manager_get_channel_config(channel_manager_t *mgr, uint8_t channel_id, ChannelConfig_t *out_cfg)
{
    if (mgr == NULL || out_cfg == NULL) return -EINVAL;
    if (channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) return -EINVAL;

    k_mutex_lock(&mgr->config_lock, K_FOREVER);
    *out_cfg = mgr->channels[channel_id - 1].config;
    k_mutex_unlock(&mgr->config_lock);
    return 0;
}

/**
 * @brief Đọc dữ liệu tức thời của 1 kênh duy nhất phục vụ lệnh UART AI Gateway (0x02/0x82).
 */
bool channel_manager_read_single(const channel_manager_t *mgr,
                                 uint8_t channel_id,
                                 comm_payload_read_channel_resp_t *out_resp)
{
    if (mgr == NULL || out_resp == NULL || channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) {
        return false;
    }

    memset(out_resp, 0, sizeof(comm_payload_read_channel_resp_t));
    out_resp->channel_id = channel_id;

    const channel_runtime_t *ch = &mgr->channels[channel_id - 1];
    if (ch->state == CHANNEL_STATE_ACTIVE) {
        out_resp->status = ch->is_valid ? 1 : 3; /* 1: ACTIVE (OK), 3: FAULT */
    } else {
        out_resp->status = (uint8_t)ch->state;    /* 0: UNCONFIGURED, 2: SUSPENDED */
    }
    out_resp->value         = ch->last_value;
    out_resp->timestamp_sec = ch->last_sample_ms / 1000;

    return true;
}

/**
 * @brief Định dạng và đóng gói dữ liệu bản chụp toàn trạm sang comm_payload_dynamic_telemetry_t (0x1A/0x9A).
 */
void channel_manager_get_dynamic_telemetry(channel_manager_t *mgr,
                                           uint8_t active_fault_count,
                                           uint8_t logging_state,
                                           comm_payload_dynamic_telemetry_t *out_payload,
                                           uint16_t *out_payload_len)
{
    if (mgr == NULL || out_payload == NULL || out_payload_len == NULL) return;

    memset(out_payload, 0, sizeof(comm_payload_dynamic_telemetry_t));

    out_payload->timestamp_sec        = k_uptime_get_32() / 1000;
    out_payload->logging_state        = logging_state;
    out_payload->active_fault_count   = active_fault_count;

    uint8_t count = 0;
    for (uint8_t i = 0; i < CHANNEL_MAX_COUNT; i++) {
        const channel_runtime_t *ch = &mgr->channels[i];
        if (ch->config.is_enabled != 0 && count < CHANNEL_MAX_COUNT) {
            out_payload->channels[count].channel_id = ch->config.channel_id;
            if (ch->state == CHANNEL_STATE_ACTIVE) {
                out_payload->channels[count].status = ch->is_valid ? 1 : 3;
            } else {
                out_payload->channels[count].status = (uint8_t)ch->state;
            }
            out_payload->channels[count].value      = ch->last_value;
            count++;
        }
    }
    out_payload->active_channel_count = count;

    *out_payload_len = 7 + (uint16_t)(count * sizeof(comm_channel_entry_t));
}

/**
 * @brief Quét kiểm tra sức khỏe phần cứng toàn bộ các kênh active và cập nhật vào Fault Manager.
 * @param mgr Con trỏ channel_manager_t
 * @return Không có
 */
void channel_manager_check_health(channel_manager_t *mgr)
{
    if (mgr == NULL) return;

    for (uint8_t i = 0; i < CHANNEL_MAX_COUNT; i++) {
        channel_runtime_t *ch = &mgr->channels[i];
        if (ch->state == CHANNEL_STATE_ACTIVE && ch->driver_ops != NULL && ch->driver_ops->health_check != NULL) {
            uint16_t fault = FAULT_NONE;
            bool healthy = ch->driver_ops->health_check(i + 1, &fault);
            ch->fault_code = fault;
            if (!healthy) {
                ch->is_valid = false;
                if (mgr->fault_mgr != NULL && fault != FAULT_NONE) {
                    fault_manager_set(mgr->fault_mgr, fault);
                }
            }
        }
    }
}

/**
 * @brief Ép tạo hoặc xóa một mã lỗi giả lập trên kênh cụ thể (phục vụ Demo phản hồi cho AI).
 * @param mgr Con trỏ channel_manager_t
 * @param channel_id ID cổng cần inject lỗi (1 .. 18)
 * @param fault_code Mã lỗi cần ép sinh
 * @param enable true để bật lỗi, false để xóa lỗi
 * @return Không có
 */
void channel_manager_inject_fault(channel_manager_t *mgr, uint8_t channel_id, uint16_t fault_code, bool enable)
{
    if (mgr == NULL || channel_id < 1 || channel_id > CHANNEL_MAX_COUNT) return;

    channel_runtime_t *ch = &mgr->channels[channel_id - 1];
    if (ch->driver_ops != NULL && ch->driver_ops->inject_fault != NULL) {
        ch->driver_ops->inject_fault(channel_id, fault_code, enable);
    }
}

/**
 * @brief Xóa trắng phân vùng Flash NVS, dừng toàn bộ worker và reset cấu hình về mặc định nhà sản xuất.
 * @param mgr Con trỏ channel_manager_t
 * @return 0 nếu thành công, -EINVAL nếu mgr NULL
 */
int channel_manager_factory_reset(channel_manager_t *mgr)
{
    if (mgr == NULL) return -EINVAL;

    k_mutex_lock(&mgr->config_lock, K_FOREVER);

    /* Dừng toàn bộ worker và deinit hw */
    for (uint8_t i = 0; i < CHANNEL_MAX_COUNT; i++) {
        struct k_work_sync sync;
        k_work_cancel_delayable_sync(&mgr->channels[i].sample_work, &sync);
        driver_registry_deinit_channel_hw(i + 1);
        memset(&mgr->channels[i].config, 0, sizeof(ChannelConfig_t));
        mgr->channels[i].config.channel_id = i + 1;
        mgr->channels[i].state = CHANNEL_STATE_UNCONFIGURED;
        mgr->channels[i].is_valid = false;
        mgr->channels[i].driver_ops = NULL;
    }

    struct k_work_sync sync_save;
    k_work_cancel_delayable_sync(&mgr->deferred_save_work, &sync_save);

    /* Xóa toàn bộ phân vùng Flash Sector */
    if (mgr->flash_valid) {
        const struct flash_area *fa;
        int rc = flash_area_open(STORAGE_PARTITION_ID, &fa);
        if (rc == 0) {
            flash_area_erase(fa, 0, fa->fa_size);
            flash_area_close(fa);
            LOG_INF("Da erase toan bo Flash storage_partition");
        }
        mgr->current_slot_offset = 0;
    }

    mgr->header.magic_number = CHANNEL_CONFIG_MAGIC;
    mgr->header.config_version = 1;
    mgr->header.crc32 = 0;
    mgr->is_dirty = false;

    LOG_INF("Factory reset thanh cong!");
    k_mutex_unlock(&mgr->config_lock);
    return 0;
}
