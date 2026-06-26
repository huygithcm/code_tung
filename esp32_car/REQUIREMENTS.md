# Yêu cầu — ESP32 (PlatformIO)

Firmware xe dò line: đọc cảm biến, bám line, odometry, servo thả hàng, giao tiếp.

## Công cụ

| Mục | Yêu cầu |
|---|---|
| PlatformIO Core | ≥ 6 (hoặc PlatformIO IDE trong VSCode) |
| Platform | `espressif32` |
| Board | `esp32dev` (ESP32 DevKit v1) |
| Framework | `arduino` |
| Cổng nạp | USB (lần đầu) / OTA WiFi (về sau) |
| Tốc độ monitor | 115200 baud |

## Thư viện (khai báo `lib_deps` trong `platformio.ini`)

| Thư viện | Phiên bản | Dùng để |
|---|---|---|
| `madhephaestus/ESP32Servo` | ^3.0.5 | Điều khiển servo thả hàng |

### Sẽ cần khi nối WebSocket với server (GĐ 4 — PLAN.md)

| Thư viện | Dùng để |
|---|---|
| `links2004/WebSockets` | WebSocket **client** kết nối tới server Node.js (`wss://<PC>:3000`) |
| `bblanchon/ArduinoJson` | Đóng/giải mã JSON lệnh `route`/`stop` và trạng thái `pos`/`cargo` |

> Thêm vào `lib_deps` khi triển khai `comm.cpp`.

## Phần cứng (theo PLAN.md mục 2)

| Khối | Chân ESP32 |
|---|---|
| 8 cảm biến qua MUX 74HC4067 | SIG=36, S0=27, S1=26, S2=25, S3=33 |
| Servo | 5 |
| Cảm biến IR (còn hàng) | 34 |
| Buzzer | 23 |
| Motor A (L298N) | IN1=18, IN2=19, ENA=21 |
| Motor B (L298N) | IN3=22, IN4=4, ENB=16 |
| Encoder trái / phải | 15,17 / 14,13 |

- Nguồn: pin/acquy cấp cho L298N + ESP32 (chung GND).
- WiFi luôn bật → chỉ dùng ADC1.

## Build & nạp

```bash
cd esp32_car
pio run -e debug -t upload        # nạp qua USB (bản DEBUG có log Serial)
pio run -e release -t upload      # bản chạy gọn (tắt log)
pio run -e ota -t upload          # nạp qua WiFi (sau khi đã flash USB 1 lần)
pio device monitor                # xem Serial 115200
```

> Có sẵn script tiện: `flash.ps1`, `monitor.ps1`, `calibrate.ps1`.
