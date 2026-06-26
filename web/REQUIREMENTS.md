# Yêu cầu — Web/Server (Node.js)

Phần server trung gian + web điều khiển/giám sát.

## Môi trường

| Mục | Yêu cầu |
|---|---|
| Node.js | ≥ 18 (khuyến nghị 20 LTS trở lên) |
| npm | đi kèm Node |
| Hệ điều hành | Windows / macOS / Linux |
| Mạng | PC, điện thoại, ESP32 **cùng một mạng WiFi** |

## Thư viện Node (cài bằng `npm install`)

| Gói | Phiên bản | Dùng để |
|---|---|---|
| `express` | ^4.19.2 | Host web tĩnh + REST API danh mục hàng |
| `ws` | ^8.18.0 | WebSocket server (hub web ⇄ ESP32) |
| `selfsigned` | ^5.5.0 | Sinh chứng chỉ HTTPS tự ký (camera điện thoại cần HTTPS) |

> Khai báo trong `package.json`. Cài: `cd web && npm install`.

## Thư viện trình duyệt (tải qua CDN — cần Internet lần đầu)

| Thư viện | Nguồn | Dùng để |
|---|---|---|
| `html5-qrcode` 2.3.8 | unpkg | Quét QR bằng camera điện thoại (`index.html`) |
| `qrcodejs` | jsDelivr (gh `davidshimjs/qrcodejs`) | Sinh mã QR test (`qr.html`) |

## Trình duyệt

- Chrome / Edge / Safari hiện đại có hỗ trợ `getUserMedia` (camera).
- Quét QR chỉ chạy trên **HTTPS** hoặc `localhost` → server đã bật HTTPS tự ký.

## Chạy

```bash
cd web
npm install
npm start
```

- PC: <https://localhost:3000>
- Điện thoại (cùng WiFi): `https://<IP-PC>:3000` (chấp nhận cảnh báo cert tự ký).
