# PINOUT — Xe dò line ESP32 (đã chốt qua hiệu chuẩn)

> Nguồn chuẩn = file [src/main.cpp](src/main.cpp). Bảng này phản ánh cấu hình
> thực tế đã test trên board ESP32-D0WD-V3, cổng COM5.

## 1. Bảng chân (GPIO)

| Khối | Chức năng | GPIO | LEDC ch | Ghi chú |
|---|---|---|---|---|
| **MUX 74HC4067** | SIG (analog in) | 36 | — | đọc 8 cảm biến line |
| | S0 | 27 | — | bit chọn kênh |
| | S1 | 26 | — | |
| | S2 | 25 | — | |
| | S3 | 33 | — | |
| **Motor A (bánh TRÁI)** | IN1 | 18 | — | |
| | IN2 | 19 | — | |
| | ENA (PWM) | 21 | 2 | 5 kHz, 8-bit |
| **Motor B (bánh PHẢI)** | IN3 | 22 | — | |
| | IN4 | 4 | — | |
| | ENB (PWM) | 16 | 3 | 5 kHz, 8-bit |
| **Encoder TRÁI** | A | 15 | — | ngắt RISING |
| | B | 17 | — | |
| **Encoder PHẢI** | A | 14 | — | **đã đảo A/B** để sửa chiều đếm |
| | B | 13 | — | |
| **Servo** | SIG | 5 | 0 | 50 Hz, thả hàng |
| **IR** | OUT | 34 | — | input-only, check còn hàng |
| **Buzzer** | + | 23 | 1 | còi báo |

## 2. Cấu hình chiều (đã hiệu chuẩn)

| Tham số | Giá trị | Ý nghĩa |
|---|---|---|
| `INVERT_A` | `true` | đảo chiều motor trái cho đúng hướng tiến |
| `INVERT_B` | `true` | đảo chiều motor phải |
| `INVERT_TURN` | `false` | chiều actuation vòng rẽ (đã sửa cho khớp odometry) |

Quy ước: **lệnh tiến → 2 bánh quay tới, encoder đếm DƯƠNG.**
Rẽ: vạch/đích bên PHẢI → xe xoay phải; bám line bẻ về phía vạch (đã sửa dấu correction).

## 3. MUX 74HC4067 — thứ tự kênh

Đọc tuần tự: set S0–S3 → chờ 10µs → `analogRead(36)`.

| Nhãn | Kênh MUX | S3 S2 S1 S0 | Vị trí mắt |
|---|---|---|---|
| C1 | CH0 | 0000 | ngoài cùng **TRÁI** |
| C2 | CH1 | 0001 | |
| C3 | CH2 | 0010 | |
| C4 | CH3 | 0011 | giữa-trái |
| C5 | CH4 | 0100 | giữa-phải |
| C6 | CH5 | 0101 | |
| C7 | CH6 | 0110 | |
| C8 | CH7 | 0111 | ngoài cùng **PHẢI** |

Trong code: C1→C8 = CH0→CH7 = `sensor[0..7]` (trái → phải).

> ⚠️ **Mắt C1 (CH0) HỎNG** — kẹt ~835 bất kể vạch. Đã **mask** trong firmware
> (`SENSOR_OK[0]=false`), bám line dùng 7 mắt **C2–C8**. In line hiện `x` cho mắt mask.
>
> Đọc MUX đã cải thiện: settle 60µs + trung bình 4 mẫu (giảm nhiễu/crosstalk ADC).
> Tương phản thấp: đen ~3000, trắng ~3800 → **bắt buộc dùng ngưỡng riêng từng mắt**
> (ngưỡng cố định 2000 KHÔNG dùng được). Ngưỡng lưu trong NVS, tự nạp sau reset.

## 4. Thông số hiệu chuẩn (đã đo)

| Tham số | Biến trong code | Giá trị | Trạng thái |
|---|---|---|---|
| Đường kính bánh (mm) | `WHEEL_DIAMETER_MM` | **70.0** | ✅ đã đo |
| Số xung/vòng | `ENCODER_PPR` | **370** | ✅ đã đo |
| Khoảng cách tâm 2 bánh sau (mm) | `WHEEL_BASE_MM` | **170.0** | ✅ đã đo |
| Trục bánh sau → bánh tự do (mm) | `CASTER_DIST_MM` | **100.0** | ✅ đã đo |
| Góc servo GIỮ | `SERVO_HOLD` | 0° | ⬜ cần chỉnh |
| Góc servo THẢ | `SERVO_DROP` | 90° | ⬜ cần chỉnh |

### PID rẽ tại chỗ (đã hiệu chuẩn — sai số <2°, settle ~0.5s)
| Biến | Giá trị | Ghi chú |
|---|---|---|
| `KpT` | **200** | dùng odometry làm phản hồi |
| `KdT` | **35** | dập vọt lố |
| `TURN_MIN` | **110** | PWM tối thiểu phá ma sát tĩnh |
| `TURN_MAX` | **255** | full PWM — cần đủ lực để chỉnh khi gần đích (chặn thấp → kẹt) |

### PID bám line (chiều đã đúng — giá trị cần tune khi chạy đường thật)
| Biến | Giá trị | Trạng thái |
|---|---|---|
| `Kp` | 25 | ⬜ tune trên đường |
| `Kd` | 15 | ⬜ tune trên đường |
| `baseSpeed` | 160 | chỉnh sống trên web (slider/nút) |

### Hằng số odometry suy ra
```
Chu vi bánh   = π × 70           ≈ 219.9 mm / vòng
mm mỗi xung   = 219.9 / 370      ≈ 0.594 mm/xung
1 vòng bánh   = 370 xung         = 219.9 mm
```

### Sơ đồ kích thước (nhìn từ trên, đầu xe hướng lên)
```
                 ĐẦU XE
                   ▲
                   │
                  ( )  ← bánh tự do (caster)
                   │
                   │  100 mm  (trục sau → bánh tự do)
                   │
        ╔═══╗──────┼──────╔═══╗
        ║ L ║      ·      ║ R ║   ← 2 bánh sau (Ø70mm)
        ╚═══╝             ╚═══╝
          │◄──── 170 mm ────►│
          (tâm bánh L → tâm bánh R = WHEEL_BASE)

   · = tâm xe (điểm tham chiếu odometry)
```

> Ghi chú: `WHEEL_BASE_MM = 170` dùng để tính góc quay khi rẽ
> (Δθ = (S_phải − S_trái) / 170). `CASTER_DIST_MM = 100` là khoảng cách
> dọc từ trục 2 bánh dẫn động tới bánh tự do phía trước.

## 5. Chân còn trống
GPIO 32 (ADC1), 35, 39 (input-only). Tránh 0, 2, 1(TX), 3(RX).

## 6. Lưu ý phần cứng
- L298N: **rút jumper ENA/ENB** để PWM điều tốc được.
- Nguồn motor riêng (7–12V), **GND nối chung** với ESP32.
- Encoder phải ở mức **3.3V** (nếu 5V cần chia áp).
- Chỉ 1 chương trình mở COM tại một thời điểm — đóng Serial Monitor trước khi flash.
- L298N nóng & sụt áp lớn → nếu rẽ/chạy thất thường (motor stall), kiểm tra pin
  (sụt áp làm mất mô-men). Cân nhắc đổi driver MOSFET (TB6612/DRV8833) để bớt nhiệt.

## 7. WiFi / Web / NVS / OTA
- **WiFi STA** (nối router nhà): SSID/PASS đặt ở đầu `src/main.cpp`. Chỉ **2.4GHz**.
- **IP hiện tại:** `192.168.1.14` (DHCP — có thể đổi; gõ serial `w` để xem IP).
- **Web điều khiển:** mở `http://<IP>` → nút **START/STOP** bám line + **slider/nút chỉnh tốc độ** + hiện trạng thái (err, pose) realtime.
  - Endpoint: `/start` `/stop` `/status`(JSON) `/speed?v=<0..255>`.
- **NVS:** calibration line lưu flash, **tự nạp sau reset** (khỏi cali lại mỗi lần bật). Gõ `c` để cali mới (tự lưu đè).
- **OTA (update qua WiFi):** `pio run -e ota -t upload` hoặc `.\flash.ps1 -Ota`
  (cần đã flash USB 1 lần trước; `upload_port` = IP trong `platformio.ini`).

## 8. Lệnh serial chính (115200, kèm Newline)
| Lệnh | Chức năng |
|---|---|
| `?` | menu đầy đủ |
| `f`/`b`/`l`/`r`/`x` | tiến/lùi/trái/phải/dừng |
| `c` | hiệu chuẩn line (c-> tự lưu NVS) |
| `n` / `m` / `M` | in 8 mắt 1 lần / stream / tắt stream |
| `g` / `x` | bật / tắt bám line |
| `B<n>` `kp<>` `kd<>` `ki<>` | baseSpeed / hệ số PID bám line |
| `T<độ>` | rẽ tại chỗ (vd `T90`, `T-90`) |
| `Tp<>` `Td<>` | KpT / KdT (PID rẽ) |
| `Tn<>` `Tx<>` `To<>` | TURN_MIN / TURN_MAX / TURN_TOL |
| `TI` / `TV` | đảo chiều rẽ / bật trace autotune |
| `w` | xem IP / trạng thái WiFi |
| `q` / `e` / `p` | in pose / reset odometry / in xung encoder |
| `o` / `h` / `s<độ>` | servo thả / giữ / đặt góc |
