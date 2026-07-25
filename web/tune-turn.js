// ============================================================================
// tune-turn.js — Hiệu chuẩn TỰ ĐỘNG thông số rẽ (lead trái/phải).
//
// CÁCH DÙNG: đặt xe ở GIAO ĐIỂM (ngã tư), hướng theo line, rồi chạy:
//     node tune-turn.js              (mặc định 3 cặp/vòng, tối đa 4 vòng)
//     node tune-turn.js --pairs 4 --rounds 5
//
// Nguyên lý: rẽ +90 rồi −90 (xe quay về hướng ban đầu nên KHÔNG cần đặt lại xe
// giữa các lần). Đọc "vot" (vọt lố) từ log [turnres], tính trung bình mỗi chiều,
// rồi cộng vào lead của chiều đó (lead = số độ dừng SỚM để bù quán tính).
// Lặp tới khi |vọt| <= TOL. Cuối cùng in giá trị để BAKE vào main.cpp.
// ============================================================================

const WebSocket = require('ws');

const args = process.argv.slice(2);
const argN = (name, def) => {
  const i = args.indexOf('--' + name);
  return i >= 0 && args[i + 1] ? parseFloat(args[i + 1]) : def;
};
const PAIRS = argN('pairs', 3);     // số cặp (+90,−90) mỗi vòng
const ROUNDS = argN('rounds', 4);   // số vòng hiệu chuẩn tối đa
const TOL = argN('tol', 0.8);       // |vọt| trung bình <= TOL (độ) -> coi là đạt
const HOST = 'localhost', PORT = 3000;
const TURN_WAIT_MS = 9000;          // chờ tối đa 1 lệnh rẽ báo kết quả
const SETTLE_MS = 900;              // nghỉ giữa 2 lần rẽ cho xe đứng hẳn

const sleep = ms => new Promise(r => setTimeout(r, ms));

function connect() {
  return new Promise((res, rej) => {
    const ws = new WebSocket(`wss://${HOST}:${PORT}`, { rejectUnauthorized: false });
    ws.on('open', () => res(ws));
    ws.on('error', rej);
  });
}

// Gửi 1 lệnh rẽ, chờ dòng [turnres] tương ứng -> { tgt, dat, vot, lead }
function doTurn(ws, deg) {
  return new Promise((resolve, reject) => {
    let done = false;
    const onMsg = (raw) => {
      let m; try { m = JSON.parse(raw.toString()); } catch { return; }
      if (m.type !== 'carlog') return;
      const t = m.entry.text;
      const g = t.match(/\[turnres\] tgt=(-?[\d.]+) dat=(-?[\d.]+) err=(-?[\d.]+) vot=(-?[\d.]+) lead=([\d.]+)/);
      if (!g) return;
      if (Math.abs(parseFloat(g[1]) - deg) > 0.5) return;   // của lệnh khác -> bỏ
      finish({ tgt: +g[1], dat: +g[2], err: +g[3], vot: +g[4], lead: +g[5], timeout: /TIMEOUT/.test(t) });
    };
    function finish(v) {
      if (done) return; done = true;
      ws.off('message', onMsg); clearTimeout(timer);
      resolve(v);
    }
    const timer = setTimeout(() => {
      if (done) return; done = true;
      ws.off('message', onMsg);
      reject(new Error(`không nhận [turnres] cho ${deg}° sau ${TURN_WAIT_MS}ms (xe có nối hub?)`));
    }, TURN_WAIT_MS);
    ws.on('message', onMsg);
    ws.send(JSON.stringify({ cmd: 'turn', deg }));
  });
}

const mean = a => a.reduce((x, y) => x + y, 0) / a.length;
const fmt = n => (n >= 0 ? '+' : '') + n.toFixed(2);

(async () => {
  console.log(`Kết nối hub wss://${HOST}:${PORT} ...`);
  const ws = await connect();

  // Chờ hello để biết xe đã nối chưa
  const okCar = await new Promise((res) => {
    const t = setTimeout(() => res(false), 4000);
    ws.on('message', function h(raw) {
      const m = JSON.parse(raw.toString());
      if (m.type === 'hello') { clearTimeout(t); ws.off('message', h); res(m.state.source === 'car'); }
    });
  });
  if (!okCar) { console.error('❌ Chưa có xe kết nối hub. Bật xe rồi thử lại.'); process.exit(1); }
  console.log('✅ Xe đã nối. ĐẶT XE Ở NGÃ TƯ, hướng theo line.\n');

  let leadL = null, leadR = null;

  for (let round = 1; round <= ROUNDS; round++) {
    console.log(`── Vòng ${round}/${ROUNDS} — chạy ${PAIRS} cặp (+90°, −90°) ──`);
    const votL = [], votR = [];
    for (let i = 0; i < PAIRS; i++) {
      const L = await doTurn(ws, 90);   await sleep(SETTLE_MS);
      const R = await doTurn(ws, -90);  await sleep(SETTLE_MS);
      leadL = L.lead; leadR = R.lead;
      votL.push(L.vot); votR.push(R.vot);
      console.log(`   cặp ${i + 1}: TRÁI dat=${L.dat.toFixed(2)} vọt=${fmt(L.vot)}` +
                  (L.timeout ? ' [TIMEOUT]' : '') +
                  ` | PHẢI dat=${R.dat.toFixed(2)} vọt=${fmt(R.vot)}` + (R.timeout ? ' [TIMEOUT]' : ''));
    }
    const mL = mean(votL), mR = mean(votR);
    console.log(`   → vọt TB: TRÁI ${fmt(mL)}°  PHẢI ${fmt(mR)}°   (lead hiện tại L=${leadL} R=${leadR})`);

    if (Math.abs(mL) <= TOL && Math.abs(mR) <= TOL) {
      console.log(`\n🎯 ĐẠT (|vọt| <= ${TOL}°).`);
      break;
    }
    // Vọt dương (quay quá) -> tăng lead (dừng sớm hơn). Vọt âm (thiếu) -> giảm lead.
    // DAMP < 1: bước chỉnh có hãm. Quay tại chỗ ở ngã tư có quán tính khác lúc chạy route
    // thật, cộng thêm nhiễu ±1-2 độ giữa các lần đo -> cập nhật full bước dễ VỌT QUA giá trị
    // tối ưu (đã gặp: lead bị đẩy 10.3 -> 13.8 làm xe thiếu góc 3-4 độ khi chạy route).
    const DAMP = 0.6;
    const newL = Math.max(0, +(leadL + DAMP * mL).toFixed(2));
    const newR = Math.max(0, +(leadR + DAMP * mR).toFixed(2));
    console.log(`   ⇒ cập nhật lead: L ${leadL} → ${newL} · R ${leadR} → ${newR}\n`);
    ws.send(JSON.stringify({ cmd: 'tune', leadL: newL, leadR: newR }));
    leadL = newL; leadR = newR;
    await sleep(600);
    if (round === ROUNDS) console.log('\n(Hết số vòng — dùng giá trị cuối cùng bên dưới.)');
  }

  console.log('\n================ KẾT QUẢ ================');
  console.log(`TURN_LEAD_DEG_L = ${leadL};`);
  console.log(`TURN_LEAD_DEG_R = ${leadR};`);
  console.log('Bake 2 dòng này vào esp32_car/src/main.cpp rồi flash lại (giá trị hiện tại chỉ ở RAM).');
  ws.close();
  process.exit(0);
})().catch(e => { console.error('Lỗi:', e.message); process.exit(1); });
