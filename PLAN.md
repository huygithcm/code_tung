# Kế hoạch dự án: Xe giao hàng dò line + quét QR

## 1. Tổng quan hệ thống

Xe ESP32 dò line, giao hàng đến 3 node. Có 2 loại hàng, nhận diện bằng **mã QR
quét qua camera điện thoại trên giao diện web**. Web có chức năng điều khiển tay
và giám sát. Server trung gian chạy bằng **Node.js** trên PC.

### Kiến trúc

```
[Điện thoại: web quét QR + nút điều khiển]
[PC: web giám sát]   ⇄ WebSocket ⇄ [Node.js server (PC)] ⇄ WiFi/WebSocket ⇄ [ESP32 xe]
```

- ESP32 đóng vai **WebSocket client**, kết nối tới server PC.
- Server Node.js là **hub trung chuyển**: lệnh từ web → xe; trạng thái xe → web.
- QR **không xử lý trên ESP32** — điện thoại quét bằng camera qua trình duyệt.
- Điều kiện: điện thoại, PC, ESP32 **cùng một mạng WiFi**.

### Phân chia trách nhiệm

| Thành phần | Nhiệm vụ |
|---|---|
| **ESP32** | Dò line, đếm node, chạy/dừng/rẽ, giao hàng (servo), tránh vật cản (IR), nhận lệnh + báo trạng thái |
| **Server Node.js (PC)** | Host web, trung chuyển WebSocket, lưu log/trạng thái 3 node + 2 loại hàng |
| **Web (trình duyệt)** | Quét QR (camera ĐT) → loại hàng → node; nút điều khiển tay; hiển thị trạng thái |

---

## 2. Phần cứng đã có (theo ESP32_xedoline.docx)

**Board:** ESP32 DevKit v1 + 74HC4067 Analog Multiplexer. WiFi luôn bật → chỉ dùng ADC1.

| Khối | Chân ESP32 | Ghi chú |
|---|---|---|
| 8 cảm biến analog (dò line) | qua MUX: SIG=`36`, S0=`27`, S1=`26`, S2=`25`, S3=`33` | Set S0–S3 → chờ 10µs → `analogRead(36)`, đọc CH0–CH7 |
| Servo | `5` (LEDC ch0, 50Hz) | 0–180°, thả hàng xuống tại node (nghiêng/mở khay) |
| Cảm biến IR | `34` (input-only) | Kiểm tra còn hàng trên xe (LOW = còn hàng, HIGH = đã thả) |
| Buzzer | `23` (LEDC ch1) | còi báo |
| Motor A (L298N) | IN1=`18`, IN2=`19`, ENA=`21` (LEDC ch2) | |
| Motor B (L298N) | IN3=`22`, IN4=`4`, ENB=`16` (LEDC ch3) | |
| Encoder trái (A/B) | `15`, `17` | ngắt RISING, có internal pull-up |
| Encoder phải (A/B) | `14`, `13` | đã đảo A/B để sửa chiều đếm |

### Cấu trúc cơ khí của xe

Nhìn từ trên xuống (đầu xe hướng lên trên):

```
                    ĐẦU XE (phía trước)
                          ▲
            ┌─────────────┼─────────────┐
            │   ▣▣▣▣  ▣▣▣▣ │             │ ← thanh dò line 8 mắt
            │   C1 C2 .. C8 (MUX 74HC4067)│   (C1 trái → C8 phải)
            │             │             │
            │            ( )  ← bánh tự do (caster)
            │             │             │
            │             │  100 mm     │  (trục bánh sau → bánh tự do)
            │             │             │
            │      [ khay chở hàng ]    │
            │       servo thả hàng ↓    │
            │      IR soi khay ◦        │
            │             │             │
            │  ╔═══╗──────┼──────╔═══╗  │
            │  ║ L ║      ·      ║ R ║  │ ← 2 bánh sau Ø70mm (motor DC)
            │  ╚═══╝             ╚═══╝  │   L=Motor A, R=Motor B
            │    │◄──── 170 mm ────►│   │   (tâm bánh L → tâm bánh R)
            └─────────────┼─────────────┘
                          ▼
                    ĐUÔI XE (phía sau)

   · = tâm xe (điểm tham chiếu odometry)
```

**Kích thước đã đo:** đường kính bánh **Ø70mm**, encoder **370 xung/vòng**,
khoảng cách 2 bánh sau **170mm**, trục sau → bánh tự do **100mm**.

- **2 bánh sau** (trái/phải) là bánh dẫn động — Motor A & B qua L298N, điều tốc PWM.
- **1 bánh tự do (caster)** ở trước — đỡ thân xe, xoay tự do khi rẽ.
- **Thanh dò line 8 mắt** đặt ở trước, ngay sau bánh tự do — đọc qua MUX kênh C1–C8.
  **C1 = ngoài cùng BÊN TRÁI**, C8 = ngoài cùng BÊN PHẢI (nhìn theo hướng xe tiến).
  Trong code: C1→C8 = CH0→CH7 = `sensor[0..7]` (trái → phải).
- **Encoder** gắn ở 2 bánh sau để đo quãng đường (odometry).
- **Servo + khay** ở giữa/sau để thả hàng; **IR** soi khay kiểm tra còn hàng.

> Kiểu xe **differential drive** (2 bánh dẫn động + 1 bánh tự do): rẽ bằng cách
> cho 2 bánh sau quay khác tốc/khác chiều. Phù hợp dò line và tính odometry.

### Bảng kênh MUX 74HC4067

| Kênh | S3 | S2 | S1 | S0 | Cảm biến |
|---|---|---|---|---|---|
| CH0 | 0 | 0 | 0 | 0 | Analog 1 |
| CH1 | 0 | 0 | 0 | 1 | Analog 2 |
| CH2 | 0 | 0 | 1 | 0 | Analog 3 |
| CH3 | 0 | 0 | 1 | 1 | Analog 4 |
| CH4 | 0 | 1 | 0 | 0 | Analog 5 |
| CH5 | 0 | 1 | 0 | 1 | Analog 6 |
| CH6 | 0 | 1 | 1 | 0 | Analog 7 |
| CH7 | 0 | 1 | 1 | 1 | Analog 8 |

### Chân còn trống (mở rộng)
GPIO 32 (ADC1), 35, 39 (input-only); tránh 0, 2, 1, 3.
(13, 14, 15, 17 đã gán cho encoder.)

---

## 2b. Odometry — ước lượng vị trí bằng encoder

Dùng **encoder 2 bánh + đường kính bánh** để tính quãng đường đã đi, ước lượng
vị trí tương đối của xe trên map line (không chỉ đếm giao cắt).

### Thông số cần đo / khai báo (CẦN CHỐT)
- [ ] Đường kính bánh `D` (mm).
- [ ] Số xung mỗi vòng `PPR` (sau hộp số, đo thực tế cho chính xác).
- [ ] Khoảng cách 2 bánh `L` (mm) — để tính góc quay khi rẽ.

### Công thức
```
chu_vi      = π × D
quang_duong_moi_xung = chu_vi / PPR
S_trai = đếm_xung_trái × quang_duong_moi_xung
S_phai = đếm_xung_phải × quang_duong_moi_xung
S      = (S_trai + S_phai) / 2          // quãng đường trung tâm xe
góc Δθ = (S_phai − S_trai) / L          // thay đổi hướng (rad)
```

### Cách dùng trong hệ thống
- Mỗi node trên map gán **mốc khoảng cách** (vd Node1 = 50cm, Node2 = 120cm...).
- Encoder cho biết xe đang ở đâu giữa 2 giao cắt → mượt hơn việc chỉ đếm vạch.
- **Kết hợp (sensor fusion):** lấy giao cắt từ cảm biến line làm mốc "reset"
  sai số tích lũy của encoder → vị trí ước lượng luôn bám sát thực tế.
- Gửi vị trí ước lượng lên web để hiển thị xe chạy trên map.

---

## 3. Quy ước dữ liệu (CẦN CHỐT — Giai đoạn 0)

- [ ] Ánh xạ **loại hàng A/B → node** nào; Node 3 dùng làm gì.
- [ ] Nội dung **mã QR**: chỉ `A`/`B` hay mã sản phẩm cần tra bảng.
- [ ] Góc servo: vị trí **giữ hàng** (vd 0°) và vị trí **thả hàng** (vd 90°), thời gian giữ.
- [ ] Sơ đồ **đường line** + vị trí 3 node + cách nhận biết node (đếm giao cắt hay mốc riêng).

### Định dạng tin nhắn WebSocket (gợi ý)
- Web → xe: `{"cmd":"go","node":2}`, `{"cmd":"stop"}`, `{"cmd":"home"}`, `{"cmd":"drop"}`
- Xe → web: `{"status":"moving","node":2}`, `{"status":"arrived","node":2}`, `{"status":"idle"}`, `{"cargo":true/false}` (IR báo còn/hết hàng)

---

## 4. Các giai đoạn triển khai

| GĐ | Nội dung | Kết quả mong đợi |
|---|---|---|
| 0 | Chốt quy ước (mục 3) | Có bảng ánh xạ + sơ đồ line |
| 1 | ESP32: đọc 8 cảm biến qua MUX (offline) | In giá trị Serial, calibrate ngưỡng đen/trắng |
| 2 | ESP32: bám line (PID/trọng số) + đếm node | Đến đúng node thứ N rồi dừng, buzzer báo |
| 2b | ESP32: đọc encoder (ngắt) + tính odometry | Ước lượng quãng đường/vị trí, reset sai số tại giao cắt |
| 3 | ESP32: thả hàng (servo) + kiểm tra hàng (IR) | Đến node → servo thả hàng → IR xác nhận đã thả (HIGH) → buzzer báo, gửi trạng thái lên web |
| 4 | Mạng: ESP32 ↔ Node.js qua WebSocket | Gửi/nhận lệnh JSON 2 chiều |
| 5 | Web: điều khiển tay | Nút Đi/Dừng/Về, chọn node, xem trạng thái |
| 6 | Web: quét QR bằng điện thoại (html5-qrcode) | Quét QR → loại hàng → tự đi node tương ứng |
| 7 | Giám sát & hoàn thiện | Hiển thị vị trí xe, log giao hàng, trạng thái hàng |

---

## 5. Cấu trúc thư mục dự án

```
code_tung/
├── ESP32_xedoline.docx     # tài liệu phần cứng + pinout
├── PLAN.md                 # file này
├── esp32_car/              # PlatformIO
│   ├── platformio.ini
│   └── src/
│       ├── config.h        # chân, WiFi, IP server, ngưỡng, node
│       ├── main.cpp
│       ├── mux.cpp/.h       # đọc 74HC4067
│       ├── line.cpp/.h      # bám line + đếm node
│       ├── odometry.cpp/.h  # đọc encoder + ước lượng vị trí
│       ├── motor.cpp/.h     # L298N + servo + buzzer
│       └── comm.cpp/.h      # WebSocket client
└── web/                    # Node.js
    ├── server.js
    ├── package.json
    └── public/
        ├── index.html
        ├── app.js
        └── style.css
```

## 6. Thư viện dự kiến
- **ESP32 (PlatformIO):** `links2004/WebSockets`, `bblanchon/ArduinoJson`, `ESP32Servo`
- **Web/Server:** `express`, `ws`, `html5-qrcode` (CDN trên trình duyệt)

---

## 7. Map lưới 3×3 — Plan breakdown (các bước nhỏ)

> Bối cảnh đã chốt: map = **lưới 3×3 ô vuông** line đen → **4×4 = 16 giao điểm**.
> Toạ độ node `(hàng, cột)` từ 0–3 (xem sơ đồ dưới). Chỉ dùng **6 mắt C2–C7**
> (bỏ C1 hỏng + C8 để giữ đối xứng). Trọng số mới: `{-2.5,-1.5,-0.5,+0.5,+1.5,+2.5}`.

```
        c0      c1      c2      c3
hàng0  N00────N01────N02────N03
        │      │      │      │
hàng1  N10────N11────N12────N13
        │      │      │      │
hàng2  N20────N21────N22────N23
        │      │      │      │
hàng3  N30────N31────N32────N33
```

Phân vai: **ESP32 = thực thi (bám line, F/L/R, dừng khẩn)**; **Server Node.js = trí
tuệ (tính đường, điều phối tránh xe)**. ESP32 không tự tính đường.

### GĐ A — Cảm biến & bám line trên lưới (ESP32)
- [ ] A1. Đổi mảng dùng còn **C2–C7**; cập nhật `SENSOR_OK` và trọng số đối xứng mới.
- [ ] A2. Calibrate (`c`) **trên chính map lưới**, lấy mẫu ở ≥2 node khác nhau.
- [ ] A3. Bám line đoạn thẳng ổn định bằng PID hiện có (giữ tâm C4–C5 = 0).

### GĐ B — Phát hiện giao điểm (ESP32) — *khối mới*
- [ ] B1. Hàm `isIntersection()`: đếm số mắt đen, true khi **≥4/6 mắt** đen.
- [ ] B2. Debounce bằng odometry: bỏ qua giao điểm mới nếu cách giao điểm trước
       < khoảng cách tối thiểu (≈ 0.5 × cạnh ô) → chống đếm trùng 1 ngã tư.
- [ ] B3. Khi xác nhận giao điểm: tiến thêm 1 đoạn ngắn để **tâm xe nằm đúng node**
       rồi mới dừng/quyết định (tránh rẽ khi mắt mới chạm mép ngã tư).

### GĐ C — Thực thi bước F/L/R (ESP32) — *state machine*
- [ ] C1. Trạng thái: `FOLLOW → AT_NODE → DECIDE → (TURN|FORWARD) → REACQUIRE → FOLLOW`.
- [ ] C2. `F` (thẳng): qua node, tiếp tục bám line sang đoạn kế.
- [ ] C3. `L`/`R`: gọi `turnRelative(±90)` (PID, cần calib `WHEEL_BASE` qua `W<N>`).
- [ ] C4. **REACQUIRE**: sau khi rẽ, tiến chậm tới khi 6 mắt bắt lại line mới →
       reset sai số góc (không tin 100% odometry). Giới hạn tìm trong ±X° rồi báo lỗi.
- [ ] C5. `DROP`: tới node đích → servo thả → IR (chân 34) xác nhận → buzzer + báo server.
- [ ] C6. Dừng khẩn: lệnh `stop` từ server hoặc (nếu có) cảm biến trước báo vật cản.

### GĐ D — Định vị trên lưới (ESP32)
- [ ] D1. Lưu trạng thái `(row, col, heading∈{N,E,S,W})`.
- [ ] D2. Mỗi `F` cập nhật `(row,col)` theo `heading`; mỗi `L/R` cập nhật `heading`.
- [ ] D3. Reset sai số tại mỗi giao điểm (sensor fusion với odometry — mục 2b).
- [ ] D4. Gửi `(row,col,heading)` lên server để vẽ xe trên map.

### GĐ E — Tính đường tối ưu (Server Node.js) — *khối mới*
- [ ] E1. Dựng đồ thị 16 node + danh sách cạnh (cập nhật nếu có nhánh cụt/điểm chặn).
- [ ] E2. **BFS** đường ngắn nhất giữa 2 node (mọi cạnh dài bằng nhau).
- [ ] E3. Tìm trên trạng thái `(node, heading)` + **phạt mỗi lần rẽ** (k×turn) →
       ưu tiên đường ít khúc cua.
- [ ] E4. Nhiều điểm giao: **duyệt hoán vị** thứ tự ghé (≤5 điểm) chọn tổng chi phí nhỏ nhất.
- [ ] E5. Xuất **chuỗi lệnh tương đối** cho xe: `{"cmd":"route","steps":["F","F","L",...,"DROP","HOME"]}`.

### GĐ F — Tránh xe khác (Server) — *chỉ khi >1 xe*
- [ ] F1. (1 xe → bỏ GĐ này.)
- [ ] F2. **Reservation table**: server giữ chỗ cạnh/node theo thời gian; xe chỉ vào
       cạnh khi trống.
- [ ] F3. Xung đột → cho xe **chờ tại node** hoặc **định tuyến lại** (chạy lại GĐ E
       với cạnh bị chiếm bị loại tạm).
- [ ] F4. (Tuỳ chọn phần cứng) thêm **cảm biến khoảng cách trước** (HC-SR04/VL53L0X,
       dùng GPIO 32/35/39) làm lưới an toàn chống vật cản ngoài dự kiến → dừng phản ứng.

### GĐ G — Điểm đặt hàng
- [ ] G1. Chốt vị trí: ưu tiên **đặt tại node giữa** (N11/N12/N21/N22); HOME ở góc (vd N30).
- [ ] G2. (Tuỳ chọn) dùng **nhánh cụt** cho điểm giao để tách khỏi đường chính,
       chống nhầm với node thường.
- [ ] G3. Đảm bảo **cạnh ô ≥ 15–20 cm** để PID ổn định + debounce giao điểm không trùng.

### Cần chốt trước khi code
- [ ] Số xe chạy đồng thời (1 xe → bỏ GĐ F).
- [ ] Có cảm biến khoảng cách trước không.
- [ ] Kích thước cạnh mỗi ô (mm) — để tính ngưỡng debounce odometry ở B2.
- [ ] Vị trí HOME + các điểm giao (node nào) — để dựng đồ thị GĐ E.
