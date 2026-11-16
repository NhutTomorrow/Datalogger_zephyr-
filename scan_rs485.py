#!/usr/bin/env python3
import sys
import time
import struct
import argparse
import glob
import serial
import serial.tools.list_ports

def calc_crc16(data: bytes) -> bytes:
    """Modbus RTU CRC16 (poly 0xA001, init 0xFFFF)."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return struct.pack('<H', crc)

def build_modbus_frame(slave_id: int, func_code: int, start_reg: int, reg_count: int) -> bytes:
    payload = struct.pack('>BBHH', slave_id, func_code, start_reg, reg_count)
    return payload + calc_crc16(payload)

def verify_response(req_frame: bytes, resp: bytes) -> bool:
    if len(resp) < 5:
        return False
    payload = resp[:-2]
    expected_crc = resp[-2:]
    if calc_crc16(payload) != expected_crc:
        return False
    if resp[0] != req_frame[0]:
        return False
    return True

def parse_sensor_data(resp: bytes):
    if len(resp) >= 7 and resp[1] in [0x03, 0x04]:
        byte_count = resp[2]
        if byte_count == 4 and len(resp) == 3 + 4 + 2:
            raw_hum, raw_temp = struct.unpack('>HH', resp[3:7])
            if raw_temp > 32767:
                raw_temp -= 65536
            print(f"       -> Giá trị giải mã sơ bộ: Temp = {raw_temp / 10.0:.1f}°C, Humidity = {raw_hum / 10.0:.1f}%RH")

def find_serial_port(wait: bool = False):
    while True:
        ports = glob.glob('/dev/ttyUSB*') + glob.glob('/dev/ttyACM*')
        if ports:
            return ports[0]
        if not wait:
            return None
        print("[-] Đang chờ cắm USB RS485 converter (/dev/ttyUSB* / /dev/ttyACM*)...", end='\r', flush=True)
        time.sleep(1)

def scan(port: str, baudrates, id_range, registers, functions):
    print(f"[*] Cổng mục tiêu : {port}")
    print(f"[*] Baudrates     : {baudrates}")
    print(f"[*] Dải Slave ID  : {id_range[0]} -> {id_range[1]}")
    print(f"[*] Thanh ghi     : {[hex(r) for r in registers]}")
    print(f"[*] Function codes: {[hex(f) for f in functions]}")
    print("-" * 60)

    found_devices = []

    for baud in baudrates:
        print(f"\n[+] Thử nghiệm Baudrate: {baud}...")
        try:
            ser = serial.Serial(
                port=port,
                baudrate=baud,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.08
            )
        except Exception as e:
            print(f"[-] Lỗi mở cổng {port} ở {baud} baud: {e}")
            return found_devices

        try:
            for slave_id in range(id_range[0], id_range[1] + 1):
                hit = False
                for reg in registers:
                    for fc in functions:
                        req = build_modbus_frame(slave_id, fc, reg, 2)
                        ser.reset_input_buffer()
                        ser.write(req)

                        time.sleep(0.02)
                        resp = ser.read(64)

                        if resp:
                            if verify_response(req, resp):
                                hex_req = req.hex(' ').upper()
                                hex_resp = resp.hex(' ').upper()
                                print(f"\n[!] PHÁT HIỆN THIẾT BỊ HỢP LỆ:")
                                print(f"    - Cấu hình: {baud} 8N1")
                                print(f"    - Slave ID: {slave_id} (0x{slave_id:02X})")
                                print(f"    - Function: 0x{fc:02X}")
                                print(f"    - Thanh ghi bắt đầu: 0x{reg:04X} (Đọc 2 thanh ghi)")
                                print(f"    - Frame gửi TX : [ {hex_req} ]")
                                print(f"    - Frame nhận RX: [ {hex_resp} ]")
                                parse_sensor_data(resp)
                                found_devices.append({
                                    'baud': baud,
                                    'id': slave_id,
                                    'fc': fc,
                                    'reg': reg,
                                    'tx': hex_req,
                                    'rx': hex_resp
                                })
                                hit = True
                                break
                            elif len(resp) >= 5 and (resp[1] == (fc | 0x80)):
                                err_code = resp[2]
                                print(f"\n[?] Bắt được Exception từ Slave ID {slave_id} (Baud {baud}): Modbus Code 0x{err_code:02X}")
                                hit = True
                                break
                    if hit:
                        break
        finally:
            ser.close()

    print("\n" + "=" * 60)
    print(f"[*] Kết quả quét: {len(found_devices)} phản hồi hợp lệ.")
    for dev in found_devices:
        print(f" -> ID: {dev['id']} | Baud: {dev['baud']} | TX: {dev['tx']} | RX: {dev['rx']}")
    return found_devices

def main():
    parser = argparse.ArgumentParser(description="Tool dò frame Modbus RTU RS485 Sensor")
    parser.add_argument("--port", type=str, default=None, help="Cổng Serial (vd: /dev/ttyUSB0)")
    parser.add_argument("--baud", type=str, default="9600,4800,19200,115200", help="Danh sách baudrate phân tách bằng dấu phẩy")
    parser.add_argument("--min-id", type=int, default=1, help="Slave ID bắt đầu (mặc định: 1)")
    parser.add_argument("--max-id", type=int, default=32, help="Slave ID kết thúc (mặc định: 32)")
    parser.add_argument("--wait", action="store_true", help="Chờ thiết bị cắm vào nếu chưa tìm thấy")
    args = parser.parse_args()

    port = args.port
    if not port:
        port = find_serial_port(wait=args.wait)
        if not port:
            print("[-] Không tìm thấy thiết bị /dev/ttyUSB* hoặc /dev/ttyACM*!")
            print("[-] Gợi ý: Kiểm tra cáp USB-RS485 hoặc thêm cờ --wait để chờ thiết bị cắm vào.")
            sys.exit(1)

    baudrates = [int(b.strip()) for b in args.baud.split(",") if b.strip().isdigit()]
    scan(
        port=port,
        baudrates=baudrates,
        id_range=(args.min_id, args.max_id),
        registers=[0x0000, 0x0001, 0x0002, 0x0100],
        functions=[0x03, 0x04]
    )

if __name__ == '__main__':
    main()
