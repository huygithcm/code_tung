# 🚗 ESP32 Xe dò line

Firmware xe dò line dùng **ESP32** + L298N (2 motor DC có encoder), thanh 8 cảm biến qua MUX 74HC4067, servo thả hàng, buzzer. Điều khiển qua **Serial** và **Web (WiFi)**, hỗ trợ **OTA**, lưu hiệu chuẩn vào **NVS**.

> Chi tiết chân cắm & thông số: xem [PINOUT.md](PINOUT.md).

---

## ✨ Tính năng

- **Bám line PID** (8 mắt → 7 mắt sau khi mask C1 hỏng), ngưỡng riêng từng mắt.
- **Rẽ tại chỗ theo góc** (PID dùng odometry) — sai số <2°, settle ~0.5s.
- **Odometry** (x, y, θ) từ encoder vi sai.
- **Web điều khiển**: START/STOP, chỉnh tốc độ realtime, xem trạng thái.
- **OTA** — nạp firmware qua WiFi, không cần cắm USB.
- **NVS** — calibration line + WHEEL_BASE lưu flash, tự nạp sau reset.
- **Vẽ quỹ đạo** trên trang local [path_view.html](path_view.html).
- **Autotune PID rẽ** ([autotune_turn2.ps1](autotune_turn2.ps1)) — nhận dạng hệ bậc 2, tính thẳng Kp/Kd.

---

## 📁 Cấu trúc

| File | Vai trò |
|---|---|
| `src/main.cpp` | toàn bộ firmware |
| `platformio.ini` | cấu hình build (`debug`/`release`/`ota`) |
| `PINOUT.md` | chân cắm, thông số, bảng lệnh serial |
| `flash.ps1` | build + flash (USB hoặc `-Ota`) |
| `autotune_turn2.ps1` | tự hiệu chuẩn PID rẽ |
| `path_view.html` | vẽ quỹ đạo realtime (chạy trên máy) |
| `calibrate.ps1` / `monitor.ps1` | tiện ích serial |

---

## 🔧 Build & Flash

```powershell
# Lần đầu (qua USB):
.\flash.ps1 -Port COM5

# Các lần sau (qua WiFi, không cắm dây):
.\flash.ps1 -Ota
# hoặc:  pio run -e ota -t upload
```
> OTA cần `upload_port` = IP của xe trong `platformio.ini`. Xem IP: gõ serial `w` hoặc nhìn log boot.

---

## 🌐 Web

Mở `http://<IP>` (mặc định `192.168.1.14`) từ điện thoại/máy **cùng WiFi 2.4GHz**.

| Endpoint | Chức năng |
|---|---|
| `/` | trang điều khiển (START/STOP + slider tốc độ + trạng thái) |
| `/start` `/stop` | bật/tắt bám line |
| `/status` | JSON: run, err, lost, spd, x, y, th, encL, encR (có CORS) |
| `/speed?v=0..255` | đặt baseSpeed |
| `/reset` | reset odometry |
| `/wbase?n=N` | hiệu chuẩn WHEEL_BASE sau khi quay tay N vòng |

---

## 📐 Quy trình hiệu chuẩn

**Cảm biến line:** đặt xe trên vạch → serial `c` → rê thanh cảm biến qua vạch & nền trong 5s. Tự lưu NVS.

**WHEEL_BASE (giảm trôi odometry):**
1. `/reset` (hoặc serial `e`).
2. Quay xe tại chỗ **bằng tay** đúng **N vòng** (bánh lăn trên sàn) về mốc.
3. `/wbase?n=N` (hoặc serial `W<N>`) → tự tính `WHEEL_BASE_mới = cũ × góc_odom/(N×360°)` và lưu NVS.

**PID rẽ:** chạy `.\autotune_turn2.ps1 -Port COM5` (xe quay tự do, 2 lần test, tự ghi gain).

**PID bám line:** chiều bẻ lái đã đúng; chốt `Kp/Kd/baseSpeed` khi cho chạy đường thật (chỉnh sống qua web/serial).

---

## ⚙️ Thông số đã hiệu chuẩn

| Nhóm | Giá trị |
|---|---|
| Bánh / encoder | `WHEEL_DIAMETER=70mm`, `PPR=370`, `WHEEL_BASE=170mm`* |
| PID rẽ | `KpT=200`, `KdT=35`, `TURN_MIN=110`, `TURN_MAX=255` |
| PID bám line | `Kp=25`, `Kd=15`, `baseSpeed=160` (cần tune đường thật) |
| Cảm biến | C1 mask, đen~3000 / trắng~3800, ngưỡng riêng (NVS) |

\* `WHEEL_BASE` cập nhật được qua hiệu chuẩn `/wbase`.

---

## 📡 Lệnh serial chính (115200 + Newline)

`?` menu · `f/b/l/r/x` lái · `c` cali line · `g`/`x` bám line on/off ·
`B<n>` tốc độ · `kp/kd/ki<n>` PID line · `T<độ>` rẽ · `Tp/Td<n>` PID rẽ ·
`Tn/Tx/To<n>` min/max/tol rẽ · `W<N>` cali WHEEL_BASE · `w` xem IP · `q/e/p` pose.

> ⚠️ L298N nóng & sụt áp lớn → pin yếu gây stall thất thường. Cân nhắc driver MOSFET (TB6612/DRV8833).
