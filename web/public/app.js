// ============================================================================
// app.js — Web client: WebSocket + vẽ bản đồ/xe + quét QR + điều khiển.
// Map vẽ từ /map (server là nguồn sự thật). Toạ độ mm, y hướng LÊN.
// ============================================================================

let ws = null, MAP = null, ROUTE = null;
let st = { status: 'idle', pos: { x: 0, y: 0, th: 90 }, cargo: true, node: null, source: '' };

const cv = document.getElementById('map'), ctx = cv.getContext('2d');

// ---------- Kết nối WebSocket ----------
function connect() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  ws = new WebSocket(proto + '://' + location.host);
  ws.onopen  = () => setConn(true);
  ws.onclose = () => { setConn(false); setTimeout(connect, 1500); };
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.type === 'hello') { st = m.state; renderLog(m.state.log); renderCarLog(m.state.carlog); }
    else if (m.type === 'state') st = m.state;
    else if (m.type === 'route') ROUTE = m.plan;
    else if (m.type === 'log')   prependLog(m.entry);
    else if (m.type === 'carlog') appendCarLog(m.entry);
    renderStatus(); draw();
  };
}
function setConn(ok) {
  const b = document.getElementById('conn');
  b.className = 'badge ' + (ok ? 'on' : 'off');
  b.textContent = ok ? '● đã kết nối' : '● mất kết nối';
}
function send(o) { if (ws && ws.readyState === 1) ws.send(JSON.stringify(o)); }

// ---------- Lệnh ----------
function go(node) { ROUTE = null; send({ cmd: 'go', node }); }
function cmd(o)   { if (o.cmd === 'home') ROUTE = null; send(o); }
function manual(dir) { send(dir === 'S' ? { cmd: 'stop' } : { cmd: 'manual', dir }); }
function calibLine() {
  if (confirm('Calib line: khi nghe TONE bắt đầu, quét thanh cảm biến qua vạch đen & nền trắng trong 5s. Bắt đầu?'))
    send({ cmd: 'calib' });
}
function numVal(id) { const v = parseFloat(document.getElementById(id).value); return isNaN(v) ? undefined : v; }
function applyTune() {
  const o = { cmd: 'tune' };
  const map = { base: 't-base', min: 't-min', kp: 't-kp', kd: 't-kd', speed: 't-speed',
                turnmin: 't-turnmin', turnmax: 't-turnmax', kpt: 't-kpt', kdt: 't-kdt', turntol: 't-turntol',
                center: 't-center', gyrow: 't-gyrow', gyrosign: 't-gyrosign' };
  for (const k in map) { const el = document.getElementById(map[k]); if (!el) continue; const v = numVal(map[k]); if (v !== undefined) o[k] = v; }
  send(o);
}
function applyWheel() { const d = numVal('t-wheeld'); if (d !== undefined) send({ cmd: 'tune', wheeld: d }); }
function testTurn() { const d = numVal('t-deg'); if (d !== undefined) send({ cmd: 'turn', deg: d }); }
function calDist() { const k = numVal('t-known'); send({ cmd: 'caldist', known: k === undefined ? 475 : k }); }
function gyroCal() { if (confirm('Giữ xe ĐỨNG YÊN trong ~1s để đo trôi tĩnh gyro. Bắt đầu?')) send({ cmd: 'gyrocal' }); }

// ---------- Tải hình học map rồi vẽ ----------
fetch('/map').then(r => r.json()).then(m => { MAP = m; buildGoButtons(); draw(); });

// Sinh nút "Giao Cx" động từ danh sách điểm của map (trừ HOME)
function buildGoButtons() {
  const box = document.getElementById('goButtons');
  if (!box || !MAP) return;
  box.innerHTML = '';
  for (const id in MAP.points) {
    if (id === 'HOME') continue;
    const b = document.createElement('button');
    b.className = 'btn b-go'; b.textContent = 'Giao ' + id;
    b.onclick = () => go(id);
    box.appendChild(b);
  }
}

function bounds() {
  // khổ map tự tính từ dữ liệu (cột/hàng/điểm/nhánh) + lề, để luôn thấy đủ
  if (!MAP) return { minx: -400, maxx: 400, miny: -120, maxy: 980 };
  const xs = [], ys = [], push = (x, y) => { xs.push(x); ys.push(y); };
  MAP.cols.forEach(x => MAP.rows.forEach(y => push(x, y)));
  for (const id in MAP.points) { const p = MAP.points[id]; push(p.x, p.y); }
  (MAP.stubs || []).forEach(s => { push(s.a[0], s.a[1]); push(s.b[0], s.b[1]); });
  const m = 60;
  return { minx: Math.min(...xs) - m, maxx: Math.max(...xs) + m,
           miny: Math.min(...ys) - m, maxy: Math.max(...ys) + m };
}
function draw() {
  const W = cv.width, H = cv.height, pad = 36;
  ctx.clearRect(0, 0, W, H);
  if (!MAP) return;
  const b = bounds();
  const sc = Math.min((W - 2 * pad) / (b.maxx - b.minx), (H - 2 * pad) / (b.maxy - b.miny));
  const cx = p => pad + (p.x - b.minx) * sc;
  const cy = p => H - (pad + (p.y - b.miny) * sc);
  const { cols, rows, stubs, points } = MAP;

  // cạnh lưới
  ctx.strokeStyle = '#46506a'; ctx.lineWidth = 4; ctx.lineCap = 'round';
  for (const y of rows) line(cx, cy, cols[0], y, cols[3], y);
  for (const x of cols) line(cx, cy, x, rows[0], x, rows[3]);
  // nhánh cụt
  ctx.strokeStyle = '#5b6577';
  for (const s of stubs) line(cx, cy, s.a[0], s.a[1], s.b[0], s.b[1]);
  // chấm giao điểm
  ctx.fillStyle = '#46506a';
  for (const x of cols) for (const y of rows) dot(cx, cy, x, y, 3);

  // đường đã định tuyến (server gửi)
  if (ROUTE && ROUTE.waypoints && ROUTE.waypoints.length > 1) {
    ctx.strokeStyle = '#2ecc71'; ctx.lineWidth = 3; ctx.setLineDash([8, 6]);
    ctx.beginPath();
    ROUTE.waypoints.forEach((p, i) => i ? ctx.lineTo(cx(p), cy(p)) : ctx.moveTo(cx(p), cy(p)));
    ctx.stroke(); ctx.setLineDash([]);
  }

  // node giao hàng + HOME
  ctx.font = 'bold 12px monospace';
  for (const id in points) {
    const n = points[id], home = id === 'HOME';
    ctx.fillStyle = home ? '#1e9e54' : (st.node === id ? '#e74c3c' : '#e67e22');
    dot(cx, cy, n.x, n.y, 7);
    ctx.fillStyle = '#cfd6e0'; ctx.fillText(n.label, cx(n) + 11, cy(n) - 9);
  }

  // xe (vẽ đúng tỉ lệ mm)
  const p = st.pos, th = (p.th || 0) * Math.PI / 180;
  drawCar(cx(p), cy(p), th, sc);

  ctx.fillStyle = '#888'; ctx.font = '12px monospace';
  ctx.fillText('ô 500×475mm | 🟠 điểm giao  🟢 HOME  🔵/🟣 xe(còn/hết hàng)', pad, 16);
  ctx.fillText('xe vẽ đúng tỉ lệ · ⚪ tâm trục sau (mốc odometry) · 🟡 8 mắt line (🔴 C1 hỏng)', pad, 30);
}
function line(cx, cy, x1, y1, x2, y2) { ctx.beginPath(); ctx.moveTo(cx({ x: x1, y: y1 }), cy({ x: x1, y: y1 })); ctx.lineTo(cx({ x: x2, y: y2 }), cy({ x: x2, y: y2 })); ctx.stroke(); }
function dot(cx, cy, x, y, r) { ctx.beginPath(); ctx.arc(cx({ x, y }), cy({ x, y }), r, 0, 7); ctx.fill(); }

// ---------- Hình học xe (mm, theo PLAN.md) ----------
// Gốc toạ độ = TÂM TRỤC 2 BÁNH SAU = điểm tham chiếu odometry (poseX/poseY).
// Trục xe: +x = hướng tiến, +y = bên PHẢI xe.
const CAR = {
  wheelBase: 170,   // tâm bánh L → tâm bánh R   (đã đo)
  wheelD:    70,    // đường kính bánh            (đã đo)
  casterFwd: 100,   // trục sau → bánh tự do      (đã đo)
  sensorFwd: 110,   // trục sau → thanh cảm biến  (≈ CENTER_OFFSET_MM — CẦN ĐO)
  wheelW:    24,    // bề rộng bánh               (ước lượng)
  bodyW:     200,   // bề ngang thân              (ước lượng)
  bodyBack:  55,    // thân kéo về SAU trục       (ước lượng)
  bodyFront: 135,   // thân kéo về TRƯỚC trục     (ước lượng)
  sensorSpan: 84,   // bề ngang thanh 8 mắt       (ước lượng)
};

function rrect(x, y, w, h, r) {
  ctx.beginPath();
  if (ctx.roundRect) ctx.roundRect(x, y, w, h, r); else ctx.rect(x, y, w, h);
}

// Vẽ xe tại (px,py) trên canvas, hướng th (rad, hệ world y-lên), tỉ lệ sc (px/mm)
function drawCar(px, py, th, sc) {
  const S = mm => mm * sc;
  ctx.save();
  ctx.translate(px, py);
  ctx.rotate(-th);            // canvas y hướng xuống → xoay -th; local +x = hướng xe, +y = bên phải

  // --- thân xe ---
  ctx.fillStyle = st.cargo ? 'rgba(45,156,223,0.18)' : 'rgba(155,89,182,0.18)';
  ctx.strokeStyle = st.cargo ? '#2d9cdf' : '#9b59b6';
  ctx.lineWidth = 2;
  rrect(-S(CAR.bodyBack), -S(CAR.bodyW / 2), S(CAR.bodyBack + CAR.bodyFront), S(CAR.bodyW), S(14));
  ctx.fill(); ctx.stroke();

  // --- 2 bánh sau (nằm ngay trên trục, y = ±85) ---
  ctx.fillStyle = '#232a38'; ctx.strokeStyle = '#8794ab'; ctx.lineWidth = 1.5;
  for (const side of [-1, 1]) {
    ctx.beginPath();
    ctx.rect(-S(CAR.wheelD / 2), side * S(CAR.wheelBase / 2) - S(CAR.wheelW / 2), S(CAR.wheelD), S(CAR.wheelW));
    ctx.fill(); ctx.stroke();
  }

  // --- trục sau (đường nối tâm 2 bánh = trục quay tại chỗ) ---
  ctx.strokeStyle = '#8794ab'; ctx.lineWidth = 1;
  ctx.setLineDash([3, 3]);
  ctx.beginPath();
  ctx.moveTo(0, -S(CAR.wheelBase / 2)); ctx.lineTo(0, S(CAR.wheelBase / 2));
  ctx.stroke(); ctx.setLineDash([]);

  // --- bánh tự do (caster) ---
  ctx.fillStyle = '#3a4459'; ctx.strokeStyle = '#8794ab';
  ctx.beginPath(); ctx.arc(S(CAR.casterFwd), 0, Math.max(2, S(13)), 0, 7);
  ctx.fill(); ctx.stroke();

  // --- thanh dò line 8 mắt (C1 trái → C8 phải; C1 hỏng = đỏ) ---
  ctx.strokeStyle = '#cfd6e0'; ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(S(CAR.sensorFwd), -S(CAR.sensorSpan / 2));
  ctx.lineTo(S(CAR.sensorFwd),  S(CAR.sensorSpan / 2));
  ctx.stroke();
  for (let i = 0; i < 8; i++) {
    // i=0 (C1) là ngoài cùng TRÁI → y âm
    const y = -S(CAR.sensorSpan / 2) + (i + 0.5) * S(CAR.sensorSpan) / 8;
    ctx.fillStyle = (i === 0) ? '#e74c3c' : '#f1c40f';   // C1 hỏng (đã mask)
    ctx.beginPath(); ctx.arc(S(CAR.sensorFwd), y, Math.max(1.2, S(4)), 0, 7);
    ctx.fill();
  }

  // --- mũi tên hướng ---
  ctx.strokeStyle = '#f1c40f'; ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(0, 0); ctx.lineTo(S(CAR.bodyFront + 26), 0);
  ctx.stroke();
  ctx.fillStyle = '#f1c40f';
  ctx.beginPath();
  ctx.moveTo(S(CAR.bodyFront + 38), 0);
  ctx.lineTo(S(CAR.bodyFront + 22), -S(12));
  ctx.lineTo(S(CAR.bodyFront + 22),  S(12));
  ctx.closePath(); ctx.fill();

  // --- điểm tham chiếu odometry (tâm trục sau) ---
  ctx.fillStyle = '#fff';
  ctx.beginPath(); ctx.arc(0, 0, 3, 0, 7); ctx.fill();
  ctx.strokeStyle = '#111'; ctx.lineWidth = 1; ctx.stroke();

  ctx.restore();
}

// ---------- Trạng thái + log ----------
function renderStatus() {
  const s = st;
  document.getElementById('status').innerHTML =
    `trạng thái: <b class="k">${(s.status || '').toUpperCase()}</b>  ` +
    `nguồn: ${s.source === 'car' ? '🚗 xe thật' : '⚠️ chưa có xe'}<br>` +
    `điểm đến: <b class="k">${s.node || '—'}</b>  hàng: ${s.cargo ? '📦 còn' : '✅ đã thả'}<br>` +
    `x=${s.pos.x.toFixed(0)} mm  y=${s.pos.y.toFixed(0)} mm  θ=${(s.pos.th || 0).toFixed(0)}°`;
}
function renderLog(list) { const ul = document.getElementById('log'); ul.innerHTML = ''; (list || []).forEach(addLi); }
function prependLog(e) { const ul = document.getElementById('log'); const li = liOf(e); ul.insertBefore(li, ul.firstChild); }
function addLi(e) { document.getElementById('log').appendChild(liOf(e)); }
function liOf(e) { const li = document.createElement('li'); li.innerHTML = `<span class="t">${e.t}</span>${e.text}`; return li; }

// ---------- Log xe (ESP32) ----------
function carLogRow(e) {
  const esc = String(e.text).replace(/[&<>]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));
  return `<div class="cl-row"><span class="t">${e.t}</span>${esc}</div>`;
}
function renderCarLog(list) {
  const box = document.getElementById('carlog'); if (!box) return;
  box.innerHTML = (list || []).map(carLogRow).join('');
  box.scrollTop = box.scrollHeight;
}
function appendCarLog(e) {
  const box = document.getElementById('carlog'); if (!box) return;
  const atBottom = box.scrollHeight - box.scrollTop - box.clientHeight < 40;
  box.insertAdjacentHTML('beforeend', carLogRow(e));
  while (box.childElementCount > 200) box.removeChild(box.firstChild);
  if (atBottom) box.scrollTop = box.scrollHeight;   // tự cuộn nếu đang ở đáy
}
function clearCarLog() { const box = document.getElementById('carlog'); if (box) box.innerHTML = ''; }

// ---------- Quét QR (html5-qrcode) ----------
let qr = null;
document.getElementById('qrStart').onclick = async () => {
  if (qr) return;
  qr = new Html5Qrcode('reader');
  try {
    await qr.start({ facingMode: 'environment' }, { fps: 10, qrbox: 220 }, onScan);
  } catch (e) { document.getElementById('qrResult').textContent = 'Không mở được camera: ' + e; qr = null; }
};
document.getElementById('qrStop').onclick = async () => {
  if (qr) { await qr.stop(); await qr.clear(); qr = null; }
};
let lastScan = 0;
function onScan(text) {
  const now = Date.now();
  if (now - lastScan < 2500) return;        // chống quét trùng
  lastScan = now;
  document.getElementById('qrResult').textContent = '📷 QR: ' + text;
  ROUTE = null;
  send({ cmd: 'qr', value: text });
}

connect();
