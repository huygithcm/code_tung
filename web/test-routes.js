// ============================================================================
// test-routes.js — Tự động chạy giao hàng lần lượt tới các điểm (mặc định C1..C9)
// qua hub thật (WebSocket), đọc log xe (carlog) để xác định thành công/thất bại,
// và in báo cáo tổng kết. Dùng để hồi quy sau khi sửa firmware/map mà không cần
// bấm tay từng nút trên web.
//
// Chạy:
//   node test-routes.js                 (test C1..C9, xe thật đang nối hub)
//   node test-routes.js C1 C6 C8        (chỉ test các điểm chỉ định)
//   node test-routes.js --host localhost --port 3000
// ============================================================================

const WebSocket = require('ws');

const args = process.argv.slice(2);
function argVal(name, def) {
  const i = args.indexOf('--' + name);
  return i >= 0 && args[i + 1] ? args[i + 1] : def;
}
const HOST = argVal('host', 'localhost');
const PORT = argVal('port', '3000');
const targets = args.filter(a => !a.startsWith('--') && !/^\d+$/.test(a) && args[args.indexOf(a) - 1] !== '--host' && args[args.indexOf(a) - 1] !== '--port');
const TARGETS = targets.length ? targets : ['C1', 'C2', 'C3', 'C4', 'C5', 'C6', 'C7', 'C8', 'C9'];

const ROUTE_TIMEOUT_MS = 30000;   // qua thoi gian nay ma chua "xong" -> coi la treo/that bai
const PAUSE_BETWEEN_MS = 1500;    // nghi giua 2 lan giao de xe/hub on dinh

function sleep(ms) { return new Promise(r => setTimeout(r, ms)); }

function connect() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`wss://${HOST}:${PORT}`, { rejectUnauthorized: false });
    ws.on('open', () => resolve(ws));
    ws.on('error', reject);
  });
}

// Chay 1 don giao hang toi `target`, tra ve { target, ok, reason, ms, turns: [{tgt,dat,err}], drop }
function runOne(ws, target) {
  return new Promise((resolve) => {
    const turns = [];
    let dropped = false;
    let settled = false;
    const t0 = Date.now();

    const onMsg = (raw) => {
      let m; try { m = JSON.parse(raw.toString()); } catch { return; }
      if (m.type !== 'carlog') return;
      const text = m.entry.text;

      const tr = text.match(/\[turnres\] tgt=(-?[\d.]+) dat=(-?[\d.]+) err=(-?[\d.]+)/);
      if (tr) turns.push({ tgt: parseFloat(tr[1]), dat: parseFloat(tr[2]), err: parseFloat(tr[3]) });

      if (text.includes('[drop] Da tha hang')) dropped = true;

      if (text.includes('[route] xong -> ve HOME')) {
        finish(true, 'hoan tat');
      } else if (text.includes('MAT LINE')) {
        finish(false, text);
      }
    };

    function finish(ok, reason) {
      if (settled) return;
      settled = true;
      ws.off('message', onMsg);
      clearTimeout(timer);
      resolve({ target, ok, reason, ms: Date.now() - t0, turns, dropped });
    }

    const timer = setTimeout(() => finish(false, `TIMEOUT sau ${ROUTE_TIMEOUT_MS}ms (khong thay "route xong" hay MAT LINE)`), ROUTE_TIMEOUT_MS);

    ws.on('message', onMsg);
    ws.send(JSON.stringify({ cmd: 'go', node: target }));
  });
}

function fmtTurns(turns) {
  if (!turns.length) return '(khong co lenh re)';
  const errs = turns.map(t => Math.abs(t.err));
  const max = Math.max(...errs).toFixed(1);
  const avg = (errs.reduce((a, b) => a + b, 0) / errs.length).toFixed(1);
  return `${turns.length} lan re, err trung binh=${avg}° max=${max}°`;
}

(async () => {
  console.log(`Ket noi hub wss://${HOST}:${PORT} ...`);
  const ws = await connect();
  console.log(`Da ket noi. Se test ${TARGETS.length} diem: ${TARGETS.join(' ')}\n`);

  const results = [];
  for (const t of TARGETS) {
    process.stdout.write(`-> Giao ${t} ... `);
    const r = await runOne(ws, t);
    results.push(r);
    console.log(r.ok ? `OK (${r.ms}ms, ${fmtTurns(r.turns)}, drop=${r.dropped})`
                      : `LOI: ${r.reason} (${r.ms}ms, ${fmtTurns(r.turns)})`);
    await sleep(PAUSE_BETWEEN_MS);
  }

  console.log('\n========== TONG KET ==========');
  const ok = results.filter(r => r.ok).length;
  console.log(`${ok}/${results.length} thanh cong`);
  for (const r of results) {
    console.log(`  ${r.ok ? 'OK  ' : 'LOI '} ${r.target.padEnd(4)} ${fmtTurns(r.turns)}${r.ok ? '' : '  <<  ' + r.reason}`);
  }

  ws.close();
  process.exit(results.every(r => r.ok) ? 0 : 1);
})().catch(e => { console.error('Loi:', e.message); process.exit(1); });
