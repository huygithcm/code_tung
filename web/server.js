// ============================================================================
// server.js — Hub trung chuyển WebSocket + bộ định tuyến + giả lập xe.
//   [Điện thoại/PC web] ⇄ WS ⇄ [server này] ⇄ WS ⇄ [ESP32 xe]
// Server là "trí tuệ": nhận lệnh/QR từ web → tính đường → gửi steps cho xe;
// nhận trạng thái xe → phát cho mọi web. Khi chưa có xe thật → tự giả lập.
// (PLAN.md mục 1, 4, 5, GĐ E)
// ============================================================================

const path = require('path');
const http = require('http');
const https = require('https');
const os = require('os');
const express = require('express');
const selfsigned = require('selfsigned');
const { WebSocketServer } = require('ws');

// Liệt kê IPv4 LAN để hiển thị URL mở trên điện thoại
function lanIPs() {
  const out = [];
  for (const list of Object.values(os.networkInterfaces()))
    for (const i of list) if (i.family === 'IPv4' && !i.internal) out.push(i.address);
  return out;
}

const cfg = require('./config');
const map = require('./map');
const store = require('./store');

const app = express();
app.use(express.json());
app.use(express.static(path.join(__dirname, 'public')));

// Endpoint tiện cho web lấy hình học map để vẽ (1 nguồn sự thật từ server)
app.get('/map', (_req, res) => {
  res.json({
    cell: map.CELL, cols: map.COLS, rows: map.ROWS,
    stubs: map.STUBS, points: map.POINTS,
  });
});

// ---- API danh mục hàng (trang admin) ----
app.get('/api/goods', (_req, res) => res.json(store.list()));
app.get('/api/points', (_req, res) => res.json(Object.keys(map.POINTS).filter(k => k !== 'HOME')));
app.post('/api/goods', (req, res) => {
  try {
    const { code, name, node } = req.body || {};
    if (!map.POINTS[node]) return res.status(400).json({ error: 'Điểm giao không hợp lệ' });
    const item = store.add({ code, name, node });
    logEvent(`🛠️ Admin: thêm/sửa hàng "${item.code}" → ${item.node}`);
    broadcastGoods();
    res.json(item);
  } catch (e) { res.status(400).json({ error: e.message }); }
});
app.delete('/api/goods/:code', (req, res) => {
  const ok = store.remove(req.params.code);
  if (ok) { logEvent(`🛠️ Admin: xóa hàng "${req.params.code}"`); broadcastGoods(); }
  res.json({ ok });
});

// Server HTTPS được tạo bất đồng bộ trong boot() ở cuối file (cert tự ký async).
const ips = lanIPs();
let server, wss;

// ---- Trạng thái hệ thống ----
const clients = new Set();          // mọi kết nối web
let carSocket = null;               // ESP32 thật (nếu có)
const state = {
  status: 'idle',                   // idle | moving | arrived | error
  node: null,                       // điểm đang tới
  pos: { x: 0, y: 0, th: 90 },      // vị trí xe (mm)
  cargo: true,                      // IR: còn hàng?
  source: cfg.SIMULATOR ? 'sim' : 'none',
  log: [],                          // lịch sử giao hàng
};

function broadcast(obj) {
  const msg = JSON.stringify(obj);
  for (const ws of clients) { try { ws.send(msg); } catch (_) {} }
}
function pushState() { broadcast({ type: 'state', state }); }
function broadcastGoods() { broadcast({ type: 'goods', goods: store.list() }); }
function logEvent(text) {
  const e = { t: new Date().toLocaleTimeString('vi-VN'), text };
  state.log.unshift(e);
  state.log = state.log.slice(0, 50);
  broadcast({ type: 'log', entry: e });
}

// ---- Định tuyến + điều phối 1 đơn giao hàng ----
function dispatch(target, qr) {
  if (!map.POINTS[target]) { logEvent(`❌ Điểm không hợp lệ: ${target}`); return; }
  const plan = map.planDelivery(target);
  if (!plan) { logEvent(`❌ Không tìm được đường tới ${target}`); return; }

  logEvent(`📦 ${qr ? `QR "${qr}" → ` : ''}giao ${target} | steps: ${plan.steps.join(' ')}`);
  broadcast({ type: 'route', plan });   // web vẽ đường

  if (carSocket && carSocket.readyState === 1) {
    // Có xe thật → gửi chuỗi lệnh tương đối
    carSocket.send(JSON.stringify({ cmd: 'route', target, steps: plan.steps }));
    state.source = 'car';
  } else if (cfg.SIMULATOR) {
    state.source = 'sim';
    runSimulator(plan);
  } else {
    logEvent('⚠️ Chưa có xe kết nối và SIMULATOR=false');
  }
}

// ---- Giả lập xe chạy dọc waypoint (khi chưa có ESP32) ----
let simTimer = null;
function runSimulator(plan) {
  clearInterval(simTimer);
  const wps = plan.waypoints;
  const segs = [];
  let total = 0;
  for (let i = 0; i < wps.length - 1; i++) {
    const a = wps[i], b = wps[i + 1];
    const len = Math.hypot(b.x - a.x, b.y - a.y);
    segs.push({ a, b, len, acc: total, toId: b.id }); total += len;
  }
  let dist = 0, dropped = false;
  state.status = 'moving'; state.node = plan.target; state.cargo = true; pushState();
  const dt = 0.05; // 20 Hz
  simTimer = setInterval(() => {
    dist += cfg.SIM_SPEED * dt;
    if (dist > total) dist = total;
    let seg = segs[segs.length - 1], t = 1;
    for (const s of segs) { if (dist <= s.acc + s.len) { seg = s; t = s.len ? (dist - s.acc) / s.len : 1; break; } }
    state.pos = {
      x: seg.a.x + (seg.b.x - seg.a.x) * t,
      y: seg.a.y + (seg.b.y - seg.a.y) * t,
      th: Math.atan2(seg.b.y - seg.a.y, seg.b.x - seg.a.x) * 180 / Math.PI,
    };
    // Tới điểm giao → thả hàng
    if (!dropped && seg.toId === plan.target && t > 0.99) {
      dropped = true; state.cargo = false; state.status = 'arrived';
      logEvent(`✅ Đã tới ${plan.target} — thả hàng (IR: hết hàng)`);
    }
    pushState();
    if (dist >= total) {
      clearInterval(simTimer);
      state.status = 'idle'; state.node = null; state.cargo = true;
      logEvent('🏠 Về HOME — sẵn sàng đơn kế tiếp');
      pushState();
    }
  }, dt * 1000);
}

function manual(cmd) {
  if (carSocket && carSocket.readyState === 1) {
    carSocket.send(JSON.stringify(cmd));
  }
  if (cmd.cmd === 'stop') { clearInterval(simTimer); state.status = 'idle'; pushState(); logEvent('■ DỪNG'); }
}

// ---- Xử lý kết nối WebSocket ----
function onConnection(ws) {
  ws._role = 'web';            // mặc định là web; ESP32 sẽ tự khai 'car'
  clients.add(ws);
  ws.send(JSON.stringify({ type: 'hello', state, goods: store.list() }));

  ws.on('message', (raw) => {
    let m; try { m = JSON.parse(raw.toString()); } catch { return; }

    // ESP32 khai báo vai trò
    if (m.role === 'car') {
      ws._role = 'car'; carSocket = ws; state.source = 'car';
      clients.delete(ws);                 // xe không cần nhận broadcast web
      logEvent('🚗 ESP32 (xe thật) đã kết nối');
      return;
    }

    // Trạng thái từ xe thật → cập nhật + phát cho web (PLAN: xe→web)
    if (ws._role === 'car') {
      if (m.status) state.status = m.status;
      if (m.node !== undefined) state.node = m.node;
      if (m.pos) state.pos = m.pos;
      if (m.cargo !== undefined) state.cargo = m.cargo;
      pushState();
      return;
    }

    // Lệnh từ web
    switch (m.cmd) {
      case 'qr': {                         // quét QR → tra điểm giao
        const target = store.lookup(m.value) || (map.POINTS[m.value] ? m.value : null);
        if (!target) { logEvent(`❓ QR không nhận diện: "${m.value}"`); break; }
        dispatch(target, m.value);
        break;
      }
      case 'go':   dispatch(m.node); break;             // chọn điểm thủ công
      case 'stop': manual({ cmd: 'stop' }); break;
      case 'home': dispatch('HOME'); break;
      case 'drop': manual({ cmd: 'drop' }); break;
      case 'manual': manual({ cmd: 'manual', dir: m.dir }); break;  // F/B/L/R tay
    }
  });

  ws.on('close', () => {
    clients.delete(ws);
    if (ws === carSocket) {
      carSocket = null; state.source = cfg.SIMULATOR ? 'sim' : 'none';
      logEvent('🚗 ESP32 ngắt kết nối');
    }
  });
}

// ---- Khởi động: sinh cert tự ký (async) → HTTPS + WS + HTTP redirect ----
async function boot() {
  const altNames = [
    { type: 2, value: 'localhost' },
    { type: 7, ip: '127.0.0.1' },
    ...ips.map(ip => ({ type: 7, ip })),
  ];
  const pems = await selfsigned.generate(
    [{ name: 'commonName', value: 'xe-giao-hang' }],
    { days: 825, keySize: 2048, algorithm: 'sha256',
      extensions: [{ name: 'subjectAltName', altNames }] }
  );

  server = https.createServer({ key: pems.private, cert: pems.cert }, app);
  wss = new WebSocketServer({ server });
  wss.on('connection', onConnection);

  // HTTP → HTTPS redirect (mở http://... tự nhảy sang https://...)
  http.createServer((req, res) => {
    const host = (req.headers.host || '').replace(/:\d+$/, '');
    res.writeHead(301, { Location: `https://${host}:${cfg.PORT}${req.url}` });
    res.end();
  }).listen(cfg.PORT + 1, () => {});

  server.listen(cfg.PORT, () => {
    console.log(`\n  🌐 PC giám sát/điều khiển:  https://localhost:${cfg.PORT}`);
    for (const ip of ips)
      console.log(`  📱 Điện thoại (cùng WiFi):   https://${ip}:${cfg.PORT}   ← mở để quét QR`);
    console.log(`     (gõ http:// cũng được, tự nhảy sang https. Bấm "Vẫn truy cập" khi cảnh báo cert tự ký.)`);
    console.log(`  🚗 ESP32 kết nối WebSocket:  wss://<IP-máy-này>:${cfg.PORT}  (gửi {"role":"car"})`);
    console.log(`  🧪 Giả lập xe: ${cfg.SIMULATOR ? 'BẬT' : 'tắt'}\n`);
  });
}
boot();
