# RECAP — Trạng thái & việc cần test tiếp

> Nhánh: `hieu-chuan-vi-tri-xe`. Cập nhật: 2026-07-16.
> Mục đích: mở file này ra là chạy tiếp được ngay, không phải dò lại từ đầu.

## 1. Khởi động lại môi trường

```bash
cd web && node server.js      # -> https://<ip-laptop>:3000
```

✅ **KHÔNG cần điền IP nữa** (đã xử lý xong — DHCP đổi IP thoải mái):

| Chiều | Cách giải quyết |
|---|---|
| **Nạp firmware** (PC → xe) | `upload_port = esp32-car.local` (mDNS do ArduinoOTA đăng ký). Chỉ cần `pio run -e ota -t upload` |
| **Xe tìm hub** (xe → PC) | Xe broadcast UDP `CAR_WHO?` port **3003** → server trả lời IP → xe lưu NVS + nối lại. **Tự động** |

**Bíp báo trạng thái tìm hub:**
- 🔉 **1 bíp ngắn trầm** mỗi 3s = đang tìm hub (chưa thấy).
- 🔊 **2 bíp lên giọng** = đã tìm thấy, nối được.

Chỉ đi tìm khi **chưa nối được hub** — nối rồi thì im lặng, không tốn băng thông.

> Nếu mạng chặn mDNS/broadcast: xem IP xe ở serial lúc boot (`>> OTA san sang ... (IP x.x.x.x)`),
> điền vào `upload_port`; và nạp IP hub tay bằng `Wh<ip>` + `Ws`.

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

- [x] ~~Đo `CENTER_OFFSET_MM`~~ → **đã đo: 145mm** (thanh cảm biến → trục bánh sau).
      Đã ghi cứng vào firmware (trước để 70 → **thiếu 75mm** → chính là thủ phạm *rẽ sớm*).
      **Chưa test thực tế** — lần sau chạy `go C1` xem còn rẽ sớm không; rẽ sớm → tăng,
      vọt qua ngã tư → giảm (ô "canh tâm(mm)" trên web).
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
  vĩnh viễn** (biểu hiện: `No response from the ESP`). Gom việc bật dịch vụ vào `onWifiUp()`.
- **Bug đã sửa:** WiFi lên **trễ hơn timeout 10s** lúc boot → `wifiOK` kẹt `false` vĩnh viễn →
  xe **có IP, ping được, nhưng hub/OTA/web không bao giờ chạy** (`source = none`).
  Nay `loop()` kiểm tra mỗi 2s, WiFi lên là gọi `onWifiUp()`. Kèm `WiFi.setAutoReconnect(true)`.
- **Hết phải điền IP:** OTA qua `esp32-car.local` (mDNS); xe tự tìm hub qua UDP broadcast
  port 3003 (đã test: nạp IP sai `.99` → xe tự tìm ra `.32` → lưu NVS → nối lại OK).
- **Phần cứng:** MUX chỉ dùng CH0–CH7 → **S3 nối GND**, giải phóng GPIO33 làm SCL cho MPU6050
  (SDA=32, SCL=33, AD0=GND, VCC=3.3V). Xem [PINOUT.md](PINOUT.md).
- **Kích thước xe (đã đo đủ):** bánh Ø70, 2 bánh sau cách 170, caster trước 100,
  **thanh cảm biến trước 145** (⇒ **nằm TRƯỚC caster**), bề ngang thanh mắt 85.
  ⚠️ `PLAN.md` phần chữ ghi *"thanh dò line ngay sau bánh tự do"* là **SAI** — hình vẽ mới đúng.
- **Web:** xe vẽ đúng tỉ lệ mm, gốc = tâm trục sau (mốc odometry thật), chung hệ số tỉ lệ với
  map; có checkbox 📐 hiện kích thước ô 500×475.

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
