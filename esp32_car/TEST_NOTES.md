# RECAP — Trạng thái & việc cần test tiếp

> Nhánh: `hieu-chuan-vi-tri-xe`. Cập nhật: 2026-07-16.
> Mục đích: mở file này ra là chạy tiếp được ngay, không phải dò lại từ đầu.

## 1. Khởi động lại môi trường (làm theo thứ tự)

```bash
# 1. Chạy web server
cd web && node server.js
#    -> in ra IP laptop, VD: https://192.168.1.32:3000
```

⚠️ **IP hay đổi (DHCP)** — cả IP laptop lẫn IP xe. Mỗi lần test lại phải kiểm tra:

```bash
ipconfig | grep IPv4          # IP laptop  (hub)
```

**Nếu IP laptop đổi** → phải nạp IP mới xuống xe, nếu không xe không nối được hub:
- Qua Serial (COM5, 115200): gõ `Wh<ip-laptop-moi>` rồi `Ws` (lưu NVS + kết nối lại).
- Xe sẽ in ra IP của chính nó: `>> WiFi OK. Mo trinh duyet: http://<ip-xe>`

**Nếu IP xe đổi** → sửa `upload_port` trong `platformio.ini` (env `ota`) thành IP xe mới.

Giá trị lần test gần nhất: laptop `192.168.1.32`, xe `192.168.1.6`.

## 2. Nạp firmware

```bash
pio run -e ota -t upload          # qua WiFi (thường dùng)
pio run -e debug -t upload --upload-port COM5   # qua USB (khi OTA hỏng)
```

**Bẫy đã gặp — COM5 "Access denied":** có tiến trình giữ cổng (Serial Monitor trong VS Code,
hoặc PowerShell mở SerialPort rồi treo). Kill terminal đang treo → **chờ vài giây** cho Windows
nhả handle → mở lại được. Chỉ 1 chương trình được mở COM tại một thời điểm.

## 3. ĐANG DANG DỞ — việc cần làm ngay lần sau

Xe lúc test đang **bị nhấc lên (sạc pin)** nên mọi số liệu đều không dùng được.
**Đặt xe xuống đúng bản đồ line (bánh chạm đất)** rồi chạy lại 2 phép đo:

Mở web → panel ⚙️ → bấm nút, xem kết quả ở cửa sổ **Log xe**:

| Cần kiểm | Nút | Kết quả ĐÚNG mong đợi |
|---|---|---|
| Cảm biến line | 📟 Đọc 8 mắt line | Nền trắng ~**3800**; chỉ mắt nằm trên vạch mới báo `1` |
| Trượt bánh khi rẽ | 🔄 Test rẽ (90°) | Góc gyro ≈ góc encoder (lệch ít) |

**Số liệu lúc xe bị nhấc (để so sánh — KHÔNG phải lỗi):**
- Line: `RAW: 2071 2254 2253 2828 2459 2633 2286 2313` → cả 7 mắt báo "đen"
  (nhấc lên = không có phản xạ IR = đọc như đen).
- Rẽ 90°: encoder quy ra **30.8°** nhưng gyro chỉ **12.7°** → bánh quay tự do trong không khí.

👉 Nếu đặt xuống đất mà **encoder vẫn lệch nhiều so với gyro** → **bánh trượt thật**;
khi đó gyro là nguồn đúng, cứ để `GYRO_W = 0.98`.

👉 Nếu **nền trắng không lên ~3800** → cảm biến lệch độ cao hoặc vạch đổi → bấm **🎯 Calib line**
(nghe bíp → quét thanh cảm biến qua vạch đen + nền trắng trong 5s).

## 4. Việc còn treo (chưa làm)

- [ ] **`CENTER_OFFSET_MM`** (mặc định 70) — chống *rẽ sớm ở ngã tư*. Cần **đo bằng thước**:
      khoảng cách từ **thanh cảm biến → trục bánh sau**. Nhập ở ô "canh tâm(mm)" → Áp dụng.
      Rẽ sớm → tăng; rẽ trễ/vọt qua ngã tư → giảm.
- [ ] **`WHEEL_DIAMETER_MM`** — hiệu chuẩn quãng đường. Đặt xe ở giao điểm, hướng theo line,
      bấm **📏 Chạy đo 1 cạnh** (biết = 475mm). Chạy 2–3 lần lấy trung bình "gợi ý" →
      nhập ô WHEEL_Ø → **Ghi Ø**. Rồi báo để ghi cứng vào firmware.
- [ ] **`WHEEL_BASE_MM`** (170) — chỉ chỉnh nếu rẽ lệch theo tỉ lệ cố định. Chỉnh qua tune `wbase`.
- [ ] Merge `hieu-chuan-vi-tri-xe` → `develops` khi mọi thứ ổn.

## 5. Đã xong (không phải làm lại)

- **Rẽ tại chỗ:** `TURN_MIN = 200`. Rẽ +90° → 89.94°, −90° → −90.34°, ổn định ~1.2s.
  (110/160 làm xe **kẹt ở ~45°**: `KpT·err` tụt dưới ngưỡng khởi động → mất mô-men.)
  ⚠️ Lưu ý: con số 89.94° này đo bằng **encoder** — có thể bị trượt bánh làm ảo. Cần đo lại có gyro.
- **Motor kêu mà không quay:** `MOTOR_MIN_PWM = 120` (sàn PWM), `baseSpeed = 200`.
- **Encoder đúng chiều:** đi thẳng → `L` và `R` **cùng dấu** (đã xác nhận: L=277, R=271).
- **MPU6050 chạy:** I2C quét thấy `0x68`, bus SDA(32)/SCL(33) đều mức CAO, trôi tĩnh ≈ 0.
  Không thấy MPU → tự động quay về encoder đơn thuần (không crash).
- **Bug đã sửa:** lệnh `Ws` thiếu `startOTA()` → boot lỗi WiFi rồi nối lại bằng `Ws` là **mất OTA
  vĩnh viễn** (biểu hiện: `No response from the ESP`). Đã gọi `startOTA()` sau `setupWiFi()`.
- **Phần cứng:** MUX chỉ dùng CH0–CH7 → **S3 nối GND**, giải phóng GPIO33 làm SCL cho MPU6050
  (SDA=32, SCL=33, AD0=GND, VCC=3.3V). Xem [PINOUT.md](PINOUT.md).

## 6. Lệnh điều khiển từ xa (WS, qua web)

| Lệnh | Việc |
|---|---|
| `i2cscan` | quét I2C + đo mức SDA/SCL + tự init lại MPU nếu thấy |
| `line` | đọc 8 mắt (RAW + nhị phân) |
| `enc` | encoder + pose + `wz` gyro |
| `turn {deg}` | test 1 cú rẽ → in `[turnres] tgt/dat/err/votlo` |
| `caldist {known}` | chạy 1 cạnh line → gợi ý `WHEEL_DIAMETER` |
| `gyrocal` | đo lại trôi tĩnh (xe **đứng yên**) |
| `calib` | hiệu chuẩn line (bíp → quét cảm biến 5s) |
| `tune {...}` | `base/min/kp/kd/speed/turnmin/turnmax/kpt/kdt/turntol/center/wheeld/ppr/wbase/gyrow/gyrosign` |
