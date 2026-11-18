#!/bin/bash
# Script tự động kết nối OpenOCD và mở RTT Viewer xem log STM32F407 qua cáp nạp ST-Link

PORT=9090

echo "=== ĐANG KHỞI ĐỘNG RTT SERVER QUA CỔNG NẠP ST-LINK ==="

# Chạy OpenOCD dưới nền
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg \
    -c "init" \
    -c "rtt setup 0x20000000 0x20000 \"SEGGER RTT\"" \
    -c "rtt start" \
    -c "rtt server start $PORT 0" > /dev/null 2>&1 &
OPENOCD_PID=$!

# Bẫy tín hiệu Ctrl+C để dọn dẹp tiến trình OpenOCD
cleanup() {
    echo ""
    echo "=== ĐANG ĐÓNG RTT LOG VIEWER ==="
    kill $OPENOCD_PID 2>/dev/null
    exit 0
}
trap cleanup SIGINT SIGTERM EXIT

# Chờ OpenOCD mở cổng RTT
echo "Chờ kết nối ST-Link..."
sleep 1.5

# Kết nối đọc log qua Netcat (nc)
echo "Đã kết nối! Bắt đầu đọc log thời gian thực (Nhấn Ctrl+C để thoát):"
echo "------------------------------------------------------------------"
nc localhost $PORT
