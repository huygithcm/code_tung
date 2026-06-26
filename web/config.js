// ============================================================================
// config.js — Quy ước dữ liệu (PLAN.md mục 3 — CẦN CHỐT).
// Sửa ở đây để ánh xạ QR / loại hàng → điểm giao.
// ============================================================================

module.exports = {
  PORT: 3000,

  // Ánh xạ NỘI DUNG MÃ QR → điểm giao trên map (D0/D1/D2).
  // QR có thể là 'A'/'B' (loại hàng) hoặc mã sản phẩm → tra ở đây.
  QR_MAP: {
    A: 'D0',      // hàng loại A → giao D0 (ô trên-trái... theo map hiện tại D0 = giữa-trái)
    B: 'D2',      // hàng loại B → giao D2
    C: 'D1',      // (dự phòng) → giao D1
    'SP-001': 'D0',
    'SP-002': 'D2',
  },

  // Góc servo (PLAN mục 3) — gửi tham khảo xuống xe nếu cần.
  SERVO_HOLD: 0,
  SERVO_DROP: 90,

  // Bật giả lập xe khi CHƯA có ESP32 thật kết nối (để test web độc lập).
  SIMULATOR: true,
  SIM_SPEED: 320,      // mm/s
};
