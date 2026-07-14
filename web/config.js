// ============================================================================
// config.js — Quy ước dữ liệu (PLAN.md mục 3 — CẦN CHỐT).
// Sửa ở đây để ánh xạ QR / loại hàng → điểm giao.
// ============================================================================

module.exports = {
  PORT: 3000,

  // Ánh xạ NỘI DUNG MÃ QR → điểm giao trên map (C1..C9 = 9 ô, trái→phải, trên→dưới).
  // QR có thể là 'A'/'B' (loại hàng) hoặc mã sản phẩm → tra ở đây.
  QR_MAP: {
    A: 'C1',      // hàng loại A → giao ô C1 (trên-trái)
    B: 'C3',      // hàng loại B → giao ô C3 (trên-phải)
    C: 'C5',      // (dự phòng) → giao ô C5 (giữa)
    'SP-001': 'C7',
    'SP-002': 'C9',
  },

  // Góc servo (PLAN mục 3) — gửi tham khảo xuống xe nếu cần.
  SERVO_HOLD: 0,
  SERVO_DROP: 90,
};
