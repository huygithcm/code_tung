# Web điều khiển & giám sát — Xe giao hàng dò line

Server trung gian (Node.js) + web theo [PLAN.md](../PLAN.md) (mục 5, GĐ 4–7).

```
[Điện thoại: quét QR + điều khiển]  ┐
                                     ├─ WebSocket ─ [server.js (PC)] ─ WS ─ [ESP32 xe]
[PC: giám sát bản đồ]               ┘
```

## Chạy

```bash
cd web
npm install
npm start
```

Server chạy **HTTPS** (cert tự ký sinh tự động) để camera điện thoại quét QR được.
Khi khởi động, console in sẵn URL kèm IP LAN.

- PC giám sát: <https://localhost:3000>
- **Điện thoại (cùng WiFi): `https://<IP-máy-PC>:3000`** — mở để quét QR.
  Lần đầu trình duyệt cảnh báo cert tự ký → bấm **Nâng cao → Vẫn truy cập**.
  (Gõ `http://...` cũng được, tự chuyển sang `https`.)
- Trang admin quản lý hàng: <https://localhost:3000/admin.html>

> Camera (`getUserMedia`) chỉ chạy trên origin bảo mật (HTTPS) hoặc `localhost` —
> đó là lý do server dùng HTTPS thay vì HTTP.

## Tính năng

| Khu vực | Mô tả |
|---|---|
| 🗺️ Bản đồ | Lưới 3×3 (mm), 3 điểm giao tâm ô + HOME, vẽ đường định tuyến + xe thời gian thực |
| 📷 Quét QR | `html5-qrcode` qua camera ĐT → tra `config.QR_MAP` → tự điều phối giao |
| 🎮 Điều khiển | Giao D0/D1/D2, Về HOME, DỪNG, lái tay F/B/L/R |
| 📜 Nhật ký | Log sự kiện giao hàng + trạng thái IR (còn/hết hàng) |
| 🧪 Giả lập | Khi chưa có ESP32, server tự mô phỏng xe chạy để test web độc lập |
| 🛠️ Admin | `/admin.html` thêm/sửa/xóa hàng (mã QR → điểm giao), lưu bền `goods.json` |

## File

- `server.js` — hub WebSocket + điều phối + giả lập xe
- `map.js` — hình học lưới + Dijkstra + sinh lệnh tương đối `F/L/R/DROP/HOME` (GĐ E)
- `store.js` — danh mục hàng (mã QR→điểm giao), lưu bền `goods.json`
- `config.js` — góc servo, bật/tắt giả lập, seed danh mục hàng lần đầu
- `public/` — `index.html` + `app.js` (điều khiển/giám sát), `admin.html` + `admin.js` (quản lý hàng), `style.css`

## Giao thức WebSocket

**Web → server**
```json
{"cmd":"qr","value":"A"}       // quét QR → tra điểm giao
{"cmd":"go","node":"D1"}       // chọn điểm thủ công
{"cmd":"home"} | {"cmd":"stop"} | {"cmd":"manual","dir":"F"}
```

**Server → ESP32** (xe thật)
```json
{"cmd":"route","target":"D1","steps":["F","L","R","DROP","HOME", ...]}
{"cmd":"stop"} | {"cmd":"manual","dir":"F"}
```

**ESP32 → server** (khai báo + trạng thái)
```json
{"role":"car"}
{"status":"moving","node":"D1","pos":{"x":..,"y":..,"th":..},"cargo":true}
```

**Server → web**: `{"type":"state",...}`, `{"type":"route",...}`, `{"type":"log",...}`

## Cần chốt (PLAN mục 3)

- [ ] Nội dung mã QR thực tế → cập nhật `config.QR_MAP`.
- [ ] Góc servo giữ/thả → `config.SERVO_HOLD/DROP`.
- [ ] ESP32 firmware: kết nối `ws://<PC>:3000`, gửi `{"role":"car"}`, thực thi `steps`.
