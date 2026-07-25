// ============================================================================
// server.js — Hub trung chuyển WebSocket + bộ định tuyến.
//   [Điện thoại/PC web] ⇄ WS ⇄ [server này] ⇄ WS ⇄ [ESP32 xe]
// Server là "trí tuệ": nhận lệnh/QR từ web → tính đường → gửi steps cho xe;
// nhận trạng thái xe → phát cho mọi web.
// (PLAN.md mục 1, 4, 5, GĐ E)
// ============================================================================

const fs = require('fs');
const path = require('path');
const http = require('http');
const https = require('https');
const os = require('os');
const dgram = require('dgram');
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
    cellW: map.CELL_W, cellH: map.CELL_H, cols: map.COLS, rows: map.ROWS,
    stubs: map.STUBS, points: map.POINTS,
  });
});

// ---- URL truy cập trang (để sinh QR mở trên điện thoại) ----
// Tra ve cac URL https://<ip-LAN>:<port> de dien thoai cung WiFi quet mo trang.
app.get('/api/access', (_req, res) => res.json({ urls: ips.map(ip => `https://${ip}:${cfg.PORT}`) }));

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
  pos: { x: 0, y: 0, th: 0 },       // vị trí xe (mm); HOME nhìn sang +x → θ=0
  cargo: true,                      // IR: còn hàng?
  source: 'none',
  step: null,                       // điều phối từng bước: { idx, total, action } hoặc null
  log: [],                          // lịch sử giao hàng
  carlog: [],                       // log thô từ xe ESP32 (cửa sổ "Log xe")
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
// Log thô do xe ESP32 gửi lên (giữ 200 dòng gần nhất) → cửa sổ "Log xe" trên web
function addCarLog(text) {
  const e = { t: new Date().toLocaleTimeString('vi-VN'), text: String(text) };
  state.carlog.push(e);
  state.carlog = state.carlog.slice(-200);
  broadcast({ type: 'carlog', entry: e });
}

// ---- Điều phối TỪNG BƯỚC: server gửi 1 bước, xe báo stepdone, server gửi bước kế ----
// activeRoute giữ toàn bộ chuỗi bước + con trỏ; server là "bộ não" quyết định bước kế dựa
// trên vị trí (odometry) xe báo về, đối chiếu với node kỳ vọng (targetsXY) để kiểm tra trôi.
let activeRoute = null;   // { target, steps, dists, targetsXY, idx, seq, timer }
const DRIFT_ABORT_MM = 400;    // lệch vị trí quá ngưỡng này sau 1 bước -> dừng an toàn
                               // (nới rộng: xe bám line vật lý đúng nhưng odometry trôi trong đoạn;
                               //  chỉ chặn LỖI THẬT lớn, không dừng nhầm xe đang đi đúng)
const STEP_TIMEOUT_MS = 15000; // xe không báo xong 1 bước trong thời gian này -> coi như treo, dừng

function clearActiveRoute() {
  if (activeRoute && activeRoute.timer) clearTimeout(activeRoute.timer);
  activeRoute = null;
  state.step = null;
}

// Gửi bước kế (theo con trỏ idx). Hết bước -> kết thúc chuyến.
function sendNextStep() {
  if (!activeRoute) return;
  const r = activeRoute;
  if (r.idx >= r.steps.length) {
    logEvent(`✅ ${r.target}: xong → về HOME, sẵn sàng`);
    clearActiveRoute();
    state.status = 'idle'; state.node = null; state.step = null; pushState();
    return;
  }
  if (!(carSocket && carSocket.readyState === 1)) { logEvent('⚠️ Mất xe giữa chừng'); clearActiveRoute(); return; }
  const action = r.steps[r.idx];
  const dist = r.dists ? (r.dists[r.idx] || 0) : 0;
  r.seq = r.idx;
  state.step = { idx: r.idx, total: r.steps.length, action };
  // Gửi kèm toạ độ + HƯỚNG node đích (để xe RE-ANCHOR cả vị trí lẫn hướng khi tới nơi,
  // xoá trôi tích luỹ -> map bám sát xe thật). nodeTh = hướng đoạn đi vào node (độ, +x=0).
  const node = r.targetsXY && r.targetsXY[r.idx];
  const msg = { cmd: 'step', seq: r.idx, action, dist };
  if (node) {
    msg.nodeX = Math.round(node.x); msg.nodeY = Math.round(node.y);
    const prev = r.idx > 0 ? r.targetsXY[r.idx - 1] : { x: 0, y: 0 };  // node trước (HOME nếu bước 0)
    const dx = node.x - prev.x, dy = node.y - prev.y;
    if (Math.hypot(dx, dy) > 1) msg.nodeTh = Math.round(Math.atan2(dy, dx) * 180 / Math.PI);  // bỏ qua DROP (0 dài)
  }
  carSocket.send(JSON.stringify(msg));
  pushState();
  if (r.timer) clearTimeout(r.timer);
  r.timer = setTimeout(() => {
    logEvent(`⏱️ ${r.target}: xe không báo xong bước ${r.idx} (${action}) sau ${STEP_TIMEOUT_MS / 1000}s → DỪNG`);
    manual({ cmd: 'stop' });
    clearActiveRoute();
    state.status = 'error'; pushState();
  }, STEP_TIMEOUT_MS);
}

// Xe báo xong 1 bước: kiểm tra ok + trôi, rồi gửi bước kế.
function onStepDone(m) {
  if (!activeRoute) return;
  const r = activeRoute;
  if (m.seq !== r.seq) return;   // stepdone lạc (của bước cũ) -> bỏ
  if (r.timer) { clearTimeout(r.timer); r.timer = null; }
  if (m.pos) state.pos = m.pos;

  if (!m.ok) {
    logEvent(`❌ ${r.target} bước ${m.seq} (${m.action}) LỖI: ${m.reason}`);
    clearActiveRoute();
    state.status = 'error'; pushState();
    return;
  }

  // Kiểm tra trôi: so vị trí xe với node kỳ vọng (chỉ khi có targetsXY - route tự động).
  if (r.targetsXY && r.targetsXY[r.idx] && m.pos) {
    const exp = r.targetsXY[r.idx];
    const drift = Math.hypot(m.pos.x - exp.x, m.pos.y - exp.y);
    logEvent(`↳ bước ${m.seq} ${m.action} xong · xe(${m.pos.x},${m.pos.y}) vs node(${exp.x},${Math.round(exp.y)}) · lệch ${Math.round(drift)}mm`);
    if (drift > DRIFT_ABORT_MM) {
      logEvent(`⚠️ ${r.target}: lệch ${Math.round(drift)}mm > ${DRIFT_ABORT_MM}mm → DỪNG (kiểm tra hiệu chuẩn)`);
      manual({ cmd: 'stop' });
      clearActiveRoute();
      state.status = 'error'; pushState();
      return;
    }
  }

  r.idx++;
  sendNextStep();
}

// ---- Định tuyến + bắt đầu 1 đơn giao hàng (gửi bước đầu tiên) ----
function dispatch(target, qr) {
  if (!map.POINTS[target]) { logEvent(`❌ Điểm không hợp lệ: ${target}`); return; }
  clearActiveRoute();

  const plan = map.planDelivery(target);
  if (!plan) { logEvent(`❌ Không tìm được đường tới ${target}`); return; }
  logEvent(`📦 ${qr ? `QR "${qr}" → ` : ''}giao ${target} | steps: ${plan.steps.join(' ')}`);
  broadcast({ type: 'route', plan });   // web vẽ đường

  if (!(carSocket && carSocket.readyState === 1)) { logEvent('⚠️ Chưa có xe kết nối'); return; }
  activeRoute = { target, steps: plan.steps, dists: plan.dists, targetsXY: plan.targetsXY, idx: 0, seq: -1, timer: null };
  state.source = 'car';
  state.status = 'moving'; state.node = target; pushState();
  sendNextStep();   // gửi bước đầu tiên (seq 0 -> xe reset odometry tại HOME)
}

function manual(cmd) {
  clearActiveRoute();   // can thiệp tay -> huỷ điều phối tự động đang chạy
  if (carSocket && carSocket.readyState === 1) {
    carSocket.send(JSON.stringify(cmd));
  }
  if (cmd.cmd === 'stop') { state.status = 'idle'; pushState(); logEvent('■ DỪNG'); }
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

    // Log thô từ xe → cửa sổ "Log xe" trên web
    if (ws._role === 'car' && m.type === 'clog') { addCarLog(m.text); return; }

    // Xe báo xong 1 bước → server quyết định bước kế (điều phối từng bước)
    if (ws._role === 'car' && m.type === 'stepdone') { onStepDone(m); return; }

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
      case 'calib':                                     // hiệu chuẩn line từ xa
        if (carSocket && carSocket.readyState === 1) {
          carSocket.send(JSON.stringify({ cmd: 'calib' }));
          logEvent('🎯 Yêu cầu calib line — quét cảm biến qua vạch khi nghe tiếng bíp');
        } else logEvent('⚠️ Chưa có xe để calib');
        break;
      case 'tune':                                      // chỉnh tốc độ/PID runtime
        if (carSocket && carSocket.readyState === 1) carSocket.send(JSON.stringify(m));
        else logEvent('⚠️ Chưa có xe để chỉnh tham số');
        break;
      case 'turn':                                      // test 1 cú rẽ (hiệu chuẩn vị trí)
        if (carSocket && carSocket.readyState === 1) {
          carSocket.send(JSON.stringify(m));
          logEvent(`🔄 Test rẽ ${m.deg}°`);
        } else logEvent('⚠️ Chưa có xe để test rẽ');
        break;
      case 'enc':                                       // đọc encoder (hiệu chuẩn chiều đếm)
        if (carSocket && carSocket.readyState === 1) carSocket.send(JSON.stringify(m));
        break;
      case 'caldist':                                   // hiệu chuẩn quãng đường (1 cạnh line)
        if (carSocket && carSocket.readyState === 1) {
          carSocket.send(JSON.stringify(m));
          logEvent(`📏 Hiệu chuẩn quãng đường (biết ${m.known || 475}mm)`);
        } else logEvent('⚠️ Chưa có xe để hiệu chuẩn quãng đường');
        break;
      case 'gyrocal':                                   // đo trôi tĩnh gyro MPU6050
        if (carSocket && carSocket.readyState === 1) {
          carSocket.send(JSON.stringify(m));
          logEvent('🎯 Đo trôi tĩnh gyro — giữ xe đứng yên');
        } else logEvent('⚠️ Chưa có xe để đo gyro');
        break;
      case 'i2cscan':                                   // quét bus I2C (tìm MPU6050)
      case 'line':                                      // đọc 8 mắt cảm biến line
      case 'linezero':                                  // đặt tâm line = vị trí hiện tại
        if (carSocket && carSocket.readyState === 1) carSocket.send(JSON.stringify(m));
        else logEvent('⚠️ Chưa có xe kết nối');
        break;
      case 'servo':                                     // test servo tay (không qua logic thả hàng)
        if (carSocket && carSocket.readyState === 1) carSocket.send(JSON.stringify(m));
        else logEvent('⚠️ Chưa có xe để test servo');
        break;
    }
  });

  ws.on('close', () => {
    clients.delete(ws);
    if (ws === carSocket) {
      carSocket = null; state.source = 'none';
      clearActiveRoute();          // mất xe -> huỷ điều phối đang chạy
      logEvent('🚗 ESP32 ngắt kết nối');
    }
  });
}

// ---- Khởi động: HTTPS + WS + HTTP redirect ----
// Ưu tiên cert của CA nội bộ (web/certs/, sinh bằng `node make-ca.js`) -> thiết bị đã cài
// rootCA sẽ KHÔNG hiện cảnh báo. Không có file -> tự ký tạm (sẽ có cảnh báo như cũ).
async function boot() {
  let creds;
  const certPath = path.join(__dirname, 'certs', 'server.pem');
  const keyPath = path.join(__dirname, 'certs', 'server-key.pem');
  if (fs.existsSync(certPath) && fs.existsSync(keyPath)) {
    creds = { key: fs.readFileSync(keyPath), cert: fs.readFileSync(certPath) };
    console.log('  🔒 Dùng cert CA nội bộ (web/certs/). Thiết bị đã cài rootCA -> không cảnh báo.');
  } else {
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
    creds = { key: pems.private, cert: pems.cert };
    console.log('  ⚠️ Chưa có cert CA (chạy `node make-ca.js`) -> dùng cert tự ký, trình duyệt sẽ cảnh báo.');
  }

  server = https.createServer(creds, app);
  wss = new WebSocketServer({ server });
  wss.on('connection', onConnection);

  // WS THƯỜNG (không TLS) dành riêng cho xe ESP32 — LAN tin cậy, ESP32 khó
  // bắt tay cert tự ký. Cùng bộ xử lý onConnection (xe tự khai role:'car').
  const wssCar = new WebSocketServer({ port: cfg.PORT + 2 });
  wssCar.on('connection', onConnection);

  // ---- UDP discovery: xe tự tìm hub, KHÔNG cần nạp IP tay khi DHCP đổi IP ----
  // Xe broadcast "CAR_WHO?" tới port này → server trả lời IP LAN của mình.
  const disc = dgram.createSocket({ type: 'udp4', reuseAddr: true });
  disc.on('message', (msg, rinfo) => {
    if (!msg.toString().startsWith('CAR_WHO')) return;
    // Chọn IP cùng subnet với xe để trả lời (máy có nhiều card mạng)
    const pfx = rinfo.address.split('.').slice(0, 3).join('.') + '.';
    const myIp = ips.find(ip => ip.startsWith(pfx)) || ips[0];
    if (!myIp) return;
    const reply = Buffer.from(`HUB ${myIp} ${cfg.PORT + 2}`);
    disc.send(reply, rinfo.port, rinfo.address);
    console.log(`  🔎 Xe ${rinfo.address} hỏi hub → trả lời ${myIp}:${cfg.PORT + 2}`);
  });
  disc.bind(cfg.PORT + 3, () => { try { disc.setBroadcast(true); } catch (_) {} });

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
    console.log(`  🚗 ESP32 kết nối WebSocket:  ws://<IP-máy-này>:${cfg.PORT + 2}  (gửi {"role":"car"})`);
    console.log(`  🔎 Xe tự tìm hub qua UDP broadcast port ${cfg.PORT + 3} — không cần nạp IP tay\n`);
  });
}
boot();
