// ============================================================================
// config.js — Quy ước dữ liệu (PLAN.md mục 3 — CẦN CHỐT).
// Sửa ở đây để ánh xạ QR / loại hàng → điểm giao.
// ============================================================================

module.exports = {
  PORT: 3000,

  // Ánh xạ NỘI DUNG MÃ QR → điểm giao trên map.
  // Luồng cố định: mỗi QR C1..C9 gắn cứng với đúng node C1..C9.
  QR_MAP: {
    C1: 'C1',
    C2: 'C2',
    C3: 'C3',
    C4: 'C4',
    C5: 'C5',
    C6: 'C6',
    C7: 'C7',
    C8: 'C8',
    C9: 'C9',
  },

  // Góc servo (PLAN mục 3) — gửi tham khảo xuống xe nếu cần.
  SERVO_HOLD: 0,
  SERVO_DROP: 90,
};
