# Kế hoạch Tiến độ Dự án Datalogger (STM32F407 + Zephyr RTOS)

Tài liệu theo dõi tổng quan các hạng mục còn thiếu cần hoàn thiện cho hệ thống Datalogger.

---

## 1. Lưu trữ Cấu hình Flash Persistence (Đã hoàn thành - Cơ chế Rolling Slot)
> **Hiện trạng**: Đã giải quyết triệt để lỗi `-22` (`-EINVAL`) của NVS và tình trạng mất cấu hình khi rút nguồn. Đã thay thế NVS bằng kiến trúc **Append-Only Rolling Slot** tối ưu riêng cho Sector 128KB của STM32F407.

- [x] **1.1. Khắc phục Root Cause lỗi NVS `-22`**:
  - Phát hiện nguyên nhân: `struct nvs_fs` trong Zephyr dùng kiểu `uint16_t sector_size` (tối đa 64KB), bị tràn số khi khai báo Sector 128KB (`0x20000 -> 0`).
  - Chuyển sang dùng trực tiếp Zephyr Flash Map API (`flash_area_read`, `flash_area_write`, `flash_area_erase`) trên phân vùng `storage_partition` (Sector 11: 128KB).
- [x] **1.2. Hiện thực cơ chế Append-Only Rolling Slot bảo vệ tuổi thọ Flash**:
  - Cấu trúc `flash_config_record_t` dài 592 bytes (căn gióng 8-byte), bao gồm Header 12B + 18 kênh (576B) + 4B padding.
  - Phân vùng 128KB chia được 221 slots. Mỗi lần lưu cấu hình chỉ ghi tịnh tiến vào slot trống kế tiếp mà **hoàn toàn không xóa sector**.
  - Chỉ khi ghi hết 221 slots mới xóa Sector 1 lần $\longrightarrow$ Nâng tuổi thọ Flash lên $> 2.2$ triệu lần lưu cấu hình.
- [x] **1.3. Chu trình phục hồi Boot-Sync & Tự khởi động Hardware Driver**:
  - Khi khởi động, quét nhanh các slot để lấy bản ghi có `config_version` cao nhất và CRC32 hợp lệ. Bỏ qua các slot ghi dở nếu bị mất nguồn đột ngột.
  - Tự động tra cứu `driver_registry_get_ops()` và gọi `driver_registry_init_channel_hw()` cho tất cả các kênh có `is_enabled == 1`.
  - Giữ cơ chế hoãn ghi 5 giây (`deferred_save_work`) để gom các thay đổi cấu hình liên tiếp.

---

## 2. Hoàn thiện FSM & Hệ thống Menu Thao tác tay (Button UI)
> **Hiện trạng**: FSM mới chỉ có INIT, IDLE, LOGGING, ERROR, DIAGNOSTIC; thiếu menu tương tác vật lý. Dù có AI vẫn cần luồng thao tác tay và xác nhận an toàn tại chỗ.

- [ ] **2.1. Popup Xác nhận Cấu hình AI (AI Proposal Confirmation)**:
  - Khi AI gửi lệnh thay đổi cấu hình qua UART6 $\longrightarrow$ Màn hình hiện Popup thông báo: *"AI muốn đổi CH_xx sang [Mode/Sensor]. Đồng ý?"*.
  - Người dùng bấm nút cứng: **OK (Đồng ý áp dụng)** hoặc **CANCEL (Hủy bỏ/Giữ nguyên)**.
- [ ] **2.2. Menu Cấu hình Thủ công bằng Nút bấm**:
  - Thiết kế state `FSM_STATE_MENU` điều hướng bằng 16 nút (quét qua 74HC165).
  - Cho phép người dùng trực tiếp: Xem danh sách kênh, Bật/Tắt kênh thủ công, Đổi chu kỳ lấy mẫu mà không cần kết nối máy tính.

Datalogger behavior:
FSM_STATE_INIT: 

- Hiển thị: Logo hãng, phiên bản Firmware, trạng thái kiểm tra phần cứng (SD Card OK/Err, RTC OK/Err, Flash Config OK, Kết nối RS485/ESP32...).

- Chuyển trạng thái: Tự động chuyển sang màn hình chính sau 2–3 giây hoặc báo lỗi nếu hỏng bộ nhớ nghiêm trọng

FSM_HOME_DASHBOARD(state thay thế cho FSM ILDE và FSM LOGGING)

Hiển thị: Giá trị đo lường thời gian thực (Real-time) của các kênh cảm biến (VD: CH1: 28.5C | RUN or  CH1: 28.5C | ERROR nếu có lỗi), trạng thái ghi dữ liệu ([REC] hoặc [STOP]), biểu tượng sóng Wi-Fi/4G, thời gian thực (RTC).

Tương tác phím:

    UP / DOWN: Cuộn qua các trang kênh (nếu thiết bị có 8–16 kênh mà LCD chỉ hiện được 2–4 kênh/lần).

    LEFT / RIGHT: Chuyển nhanh sang xem trạng thái hệ thống (System Status: dung lượng thẻ nhớ trống, điện áp nguồn 24V, trạng thái hàng đợi Store-and-Forward).

    ENTER(phím E): Yêu cầu vào Menu cài đặt (đẩy qua trạng thái nhập mật khẩu FSM_AUTH_LOGIN).


FSM_AUTH_LOGIN:

Hiển thị: Yêu cầu nhập mã PIN 4–6 chữ số (ENTER PIN: ****).

Tương tác phím: 0-9 để nhập số, BACK/DEL để xóa, ENTER để xác nhận. Sai 3 lần sẽ khóa phím tạm thời hoặc đẩy về HOME_DASHBOARD.

FSM_MAIN_MENU:

Hiển thị: Danh sách cuộn các phân hệ chức năng:

    Channel Config (Cấu hình kênh đo)

    CONFIG PROTOCOL(đặt cho tui tên khác. Mục đích của state này là để config các giao thức để dẩy data lên server có thể thông qua MQTT, hoặc websocket, ethernet,...)

    Logging & Storage (Chu kỳ ghi & Thẻ nhớ. Bắt đầu/Dừng ghi log)

    Communication (Cấu hình Modbus/RS485/Network)

    Alarm & Threshold (Cảnh báo ngưỡng)

    System & RTC (Thời gian, Mật khẩu, Reset)

    Diagnostics / I/O Test (Kiểm tra phần cứng)

Tương tác phím: UP/DOWN di chuyển con trỏ >, ENTER đi sâu vào Sub-menu, Back thoát về HOME_DASHBOARD.


STATE_SUBMENU_LIST (Danh sách Tham số Con):

    Ví dụ khi vào Channel Config, sẽ hiện danh sách CH1 đến CH8. Khi chọn CH1, sẽ hiện: Enable/Disable, Sensor Type (4-20mA / 0-10V / Modbus), Scale Min/Max, Unit.

STATE_EDIT_ENUM (Chỉnh sửa dạng Chọn Lựa):

    Dùng cho: Chọn chuẩn giao tiếp (RS485 / ADC), chọn Baudrate (9600 / 115200), chọn chế độ Bật/Tắt.

    Tương tác phím: Giá trị hiện tại nhấp nháy (hoặc nằm trong dấu [...]). Bấm UP/DOWN hoặc LEFT/RIGHT để xoay vòng các lựa chọn có sẵn. Bấm ENTER để chốt, ESC để hủy bỏ thay đổi.

STATE_EDIT_NUMERIC (Chỉnh sửa dạng Nhập Số):

    Dùng cho: Nhập hệ số hiệu chuẩn (Calibration Gain/Offset), ngưỡng cảnh báo (High/Low Limit), chu kỳ lấy mẫu (Sampling Interval - giây), Slave ID Modbus.

    Tương tác phím: Bấm trực tiếp phím số 0-9, phím dấu chấm thập phân . (thường gán cho phím *), phím đổi dấu âm/dương +/- (gán cho phím #), LEFT/RIGHT di chuyển con trỏ chữ số.

STATE_SAVE_CONFIRM (Xác nhận Lưu Cấu hình):

    Khi người dùng thoát khỏi menu cấu hình có sự thay đổi dữ liệu, thiết bị hỏi: Save changes? [YES] / [NO].

    Nếu chọn YES(hay phím Enter), hệ thống mới thực hiện ghi xuống Flash nội (hoặc EEPROM) và phát tín hiệu cập nhật lại bảng thanh ghi cấu hình động (Dynamic Register Table) để tránh ghi Flash liên tục gây mòn sector.


 STATE_ALARM_POPUP (Thông báo Lỗi / Cảnh báo Khẩn cấp):
    Kích hoạt: Ngắt từ cảm biến vượt ngưỡng (Over-range), đứt dây vòng dòng 4-20mA (dòng < 3.6mA), đầy thẻ nhớ SD, mất kết nối với module truyền thông.
    Hiển thị: Ghi đè lên màn hình hiện tại (hoặc nhấp nháy đèn nền LCD + còi Buzzer kêu), ghi rõ mã lỗi (ERR_02: CH1 OPEN CIRCUIT).
    Tương tác phím: Bấm ENTER hoặc ACK để xác nhận đã biết lỗi (tắt còi Buzzer nhưng vẫn giữ biểu tượng cảnh báo trên màn hình chính cho đến khi lỗi được khắc phục).
 
 STATE_DIAGNOSTICS_LIVE (Chế độ Kiểm tra & Hiệu chuẩn tại chỗ):Dành cho kỹ sư bảo trì. Màn hình hiển thị giá trị thô (Raw ADC / Raw Hex Modbus) song song với giá trị đã quy đổi vật lý. Hoặc kèm với danh sách mã lỗi hiện tại để tham vấn AI 
 
 STATE_SCREENSAVER_SLEEP (Tiết kiệm năng lượng / Bảo vệ LCD):Nếu sau $N$ phút (VD: 5 phút) không có nút nào được bấm, tắt đèn nền LCD (Backlight OFF) hoặc đưa màn hình về chế độ hiển thị tối giản. Khi bấm bất kỳ phím nào, phím đầu tiên chỉ có tác dụng đánh thức đèn nền (Wake-up) chứ không thực thi lệnh của phím đó (tránh bấm nhầm khi màn hình tối).   


 Global Timeout (Bộ đếm thời gian tự thoát): Ở bất kỳ trạng thái nào trong Menu hoặc Edit, nếu người dùng bỏ đi mà không bấm phím trong 60–120 giây, FSM phải tự động hủy thao tác đang dở và nhảy về STATE_HOME_DASHBOARD (tránh treo ở chế độ Edit làm ngưng trệ các tác vụ khác hoặc lộ mật khẩu).

Cơ chế Dirty Flag (Cờ báo bẩn màn hình): Không vẽ lại toàn bộ LCD trong vòng lặp liên tục (gây nhấp nháy và tốn chu kỳ CPU/giao tiếp I2C/SPI). Chỉ gọi hàm LCD_Render() khi có sự kiện chuyển State, có phím bấm mới, hoặc có cờ cập nhật dữ liệu cảm biến định kỳ (VD: mỗi 500ms).
---

## 3. Hệ thống Quản lý & Chẩn đoán Lỗi (Fault Management & Auto-Healing)
> **Hiện trạng**: Mới có khung `fault_manager`, chưa có cơ chế tự động quét lỗi toàn diện và tự phục hồi.

- [ ] **3.1. Xác định phạm vi phát hiện lỗi**:
  - **Lỗi Cảm biến / Đo lường**:
    - Dùng ngưỡng (Threshold / Limits): Vượt ngưỡng `alarm_low`, `alarm_high`.
    - Dùng tín hiệu vật lý: Đứt dây tín hiệu Analog (0V / 3.3V out-of-range), mất kết nối RS485 (timeout / CRC fail).
    - Tích hợp mô hình AI: Đánh giá bất thường dữ liệu (xu hướng biến thiên đột ngột, kẹt giá trị).
  - **Lỗi Phần cứng Bo mạch**:
    - Mất kết nối ngoại vi (I2C, SPI, Flash NVS đầy/hỏng), sụt áp nguồn.
- [ ] **3.2. Hiển thị Lỗi trên Màn hình LCD**:
  - Cập nhật giao diện `DIAGNOSTIC`: Hiển thị rõ tên lỗi, kênh bị lỗi và thời điểm xảy ra.
- [ ] **3.3. Cơ chế Tự kiểm tra & Tự phục hồi (Auto-Healing Recheck)**:
  - Định kỳ chạy background worker kiểm tra lại các kênh/ngoại vi đang báo lỗi.
  - Nếu kết nối vật lý đã được cắm lại hoặc cảm biến hoạt động bình thường trở lại $\longrightarrow$ Tự động xóa cờ lỗi và chuyển FSM từ `ERROR` / `DIAGNOSTIC` quay về `IDLE` / `LOGGING`.

---

## 4. Ghi Log Dữ liệu vào thẻ nhớ microSD
> **Hiện trạng**: Chưa kích hoạt luồng lưu trữ file log thẻ nhớ.

- [ ] **4.1. Driver phần cứng Thẻ nhớ**:
  - Cấu hình SPI / SDIO cho khe cắm thẻ microSD trên bo mạch.
  - Tích hợp subsystem FATFS của Zephyr (`CONFIG_DISK_ACCESS`, `CONFIG_FILE_SYSTEM`).
- [ ] **4.2. Luồng ghi Datalogger CSV**:
  - Tạo file theo ngày: `/SD:/LOG_YYYYMMDD.csv`.
  - Ghi định kỳ các kênh đang active theo chu kỳ logging: `Timestamp, CH1_Val, CH2_Val, ...`.
  - Bộ đệm an toàn (Buffer Flush) chống hỏng hệ thống file khi mất nguồn đột ngột.

---

## Bảng theo dõi tiến độ tổng thể

| Hạng mục | Mức độ ưu tiên | Trạng thái | Ghi chú |
| :--- | :---: | :---: | :--- |
| **1. Flash Persistence (Rolling Slot)** | **P0 (Khẩn cấp)** | 🟢 Hoàn thành | Bền 2.2 triệu lần ghi, tự nạp driver sau reboot |
| **2. FSM Menu Nút bấm & Popup AI** | **P1 (Quan trọng)** | 🟡 Đang nghiên cứu | Thao tác tay song song với AI |
| **3. Fault Detection & Auto-Healing** | **P1 (Quan trọng)** | 🟡 Đang nghiên cứu | Phân loại lỗi phần cứng vs cảm biến |
| **4. Ghi Log thẻ microSD** | **P2 (Tính năng)** | 🔴 Chưa bắt đầu | Lưu trữ dữ liệu lịch sử dài hạn |
