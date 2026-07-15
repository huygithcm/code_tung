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
fetch('/map').then(r => r.json()).then(m => { MAP = m; buildGoButtons(); draw(); drawCarDetail(); });

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
  drawCar(ctx, cx(p), cy(p), th, sc);

  // --- kích thước map (bật/tắt bằng checkbox) ---
  if (showDims()) {
    const P = (x, y) => ({ x: cx({ x, y }), y: cy({ x, y }) });
    // ô ngang 500mm: giữa 2 cột đầu, đặt phía dưới lưới
    let yb = rows[3] - 110;
    let a = P(cols[0], yb), b = P(cols[1], yb);
    extLine(ctx, cx({ x: cols[0], y: rows[3] }), cy({ x: cols[0], y: rows[3] }), a.x, a.y);
    extLine(ctx, cx({ x: cols[1], y: rows[3] }), cy({ x: cols[1], y: rows[3] }), b.x, b.y);
    dimLine(ctx, a.x, a.y, b.x, b.y, '500');
    // ô dọc 475mm: giữa 2 hàng cuối, đặt bên phải lưới
    let xr = cols[3] + 110;
    a = P(xr, rows[2]); b = P(xr, rows[3]);
    extLine(ctx, cx({ x: cols[3], y: rows[2] }), cy({ x: cols[3], y: rows[2] }), a.x, a.y);
    extLine(ctx, cx({ x: cols[3], y: rows[3] }), cy({ x: cols[3], y: rows[3] }), b.x, b.y);
    dimLine(ctx, a.x, a.y, b.x, b.y, '475');
    // nhánh HOME 200mm
    a = P(0, 0); b = P(cols[0], 0);
    dimLine(ctx, a.x, a.y + 16, b.x, b.y + 16, '200', '#e6a23c');
  }

  ctx.fillStyle = '#888'; ctx.font = '12px monospace'; ctx.textAlign = 'left'; ctx.textBaseline = 'alphabetic';
  ctx.fillText('ô 500×475mm | 🟠 điểm giao  🟢 HOME  🔵/🟣 xe(còn/hết hàng)', pad, 16);
  ctx.fillText('xe & map cùng tỉ lệ · ⚪ tâm trục sau = mốc odometry', pad, 30);
}
function line(cx, cy, x1, y1, x2, y2) { ctx.beginPath(); ctx.moveTo(cx({ x: x1, y: y1 }), cy({ x: x1, y: y1 })); ctx.lineTo(cx({ x: x2, y: y2 }), cy({ x: x2, y: y2 })); ctx.stroke(); }
function dot(cx, cy, x, y, r) { ctx.beginPath(); ctx.arc(cx({ x, y }), cy({ x, y }), r, 0, 7); ctx.fill(); }

// ---------- Hình học xe (mm, theo PLAN.md) ----------
// Gốc toạ độ = TÂM TRỤC 2 BÁNH SAU = điểm tham chiếu odometry (poseX/poseY).
// Trục xe: +x = hướng tiến, +y = bên PHẢI xe.
const CAR = {
  // --- ĐÃ ĐO (PLAN.md) ---
  wheelBase: 170,   // tâm bánh L → tâm bánh R
  wheelD:    70,    // đường kính bánh
  casterFwd: 100,   // trục sau → bánh tự do
  // --- CẦN ĐO THỰC TẾ (sửa vào đây là hình khớp ngay) ---
  sensorFwd:  110,  // trục sau → thanh cảm biến (= CENTER_OFFSET_MM)
  sensorSpan:  84,  // bề ngang thanh 8 mắt
  wheelW:      24,  // bề rộng bánh
  bodyBack:    55,  // thân xe kéo về SAU trục
  bodyFront:  135,  // thân xe kéo về TRƯỚC trục
  bodyW:      194,  // bề ngang thân (mặc định = wheelBase + wheelW: bánh sát mép thân)
};

function rrect(g, x, y, w, h, r) {
  g.beginPath();
  if (g.roundRect) g.roundRect(x, y, w, h, r); else g.rect(x, y, w, h);
}

// Vẽ xe tại (px,py) trên context g, hướng th (rad, hệ world y-lên), tỉ lệ sc (px/mm)
function drawCar(g, px, py, th, sc) {
  const S = mm => mm * sc;
  g.save();
  g.translate(px, py);
  g.rotate(-th);            // canvas y hướng xuống → xoay -th; local +x = hướng xe, +y = bên phải

  // --- thân xe ---
  g.fillStyle = st.cargo ? 'rgba(45,156,223,0.18)' : 'rgba(155,89,182,0.18)';
  g.strokeStyle = st.cargo ? '#2d9cdf' : '#9b59b6';
  g.lineWidth = 2;
  rrect(g, -S(CAR.bodyBack), -S(CAR.bodyW / 2), S(CAR.bodyBack + CAR.bodyFront), S(CAR.bodyW), S(14));
  g.fill(); g.stroke();

  // --- 2 bánh sau (nằm ngay trên trục, y = ±85) ---
  g.fillStyle = '#232a38'; g.strokeStyle = '#8794ab'; g.lineWidth = 1.5;
  for (const side of [-1, 1]) {
    g.beginPath();
    g.rect(-S(CAR.wheelD / 2), side * S(CAR.wheelBase / 2) - S(CAR.wheelW / 2), S(CAR.wheelD), S(CAR.wheelW));
    g.fill(); g.stroke();
  }

  // --- trục sau (đường nối tâm 2 bánh = trục quay tại chỗ) ---
  g.strokeStyle = '#8794ab'; g.lineWidth = 1;
  g.setLineDash([3, 3]);
  g.beginPath();
  g.moveTo(0, -S(CAR.wheelBase / 2)); g.lineTo(0, S(CAR.wheelBase / 2));
  g.stroke(); g.setLineDash([]);

  // --- bánh tự do (caster) ---
  g.fillStyle = '#3a4459'; g.strokeStyle = '#8794ab';
  g.beginPath(); g.arc(S(CAR.casterFwd), 0, Math.max(2, S(13)), 0, 7);
  g.fill(); g.stroke();

  // --- thanh dò line 8 mắt (C1 trái → C8 phải) ---
  g.strokeStyle = '#cfd6e0'; g.lineWidth = 2;
  g.beginPath();
  g.moveTo(S(CAR.sensorFwd), -S(CAR.sensorSpan / 2));
  g.lineTo(S(CAR.sensorFwd),  S(CAR.sensorSpan / 2));
  g.stroke();
  g.fillStyle = '#f1c40f';
  for (let i = 0; i < 8; i++) {
    const y = -S(CAR.sensorSpan / 2) + (i + 0.5) * S(CAR.sensorSpan) / 8;   // i=0 (C1) = ngoài cùng TRÁI
    g.beginPath(); g.arc(S(CAR.sensorFwd), y, Math.max(1.2, S(4)), 0, 7);
    g.fill();
  }

  // --- mũi tên hướng ---
  g.strokeStyle = '#f1c40f'; g.lineWidth = 2;
  g.beginPath();
  g.moveTo(0, 0); g.lineTo(S(CAR.bodyFront + 26), 0);
  g.stroke();
  g.fillStyle = '#f1c40f';
  g.beginPath();
  g.moveTo(S(CAR.bodyFront + 38), 0);
  g.lineTo(S(CAR.bodyFront + 22), -S(12));
  g.lineTo(S(CAR.bodyFront + 22),  S(12));
  g.closePath(); g.fill();

  // --- điểm tham chiếu odometry (tâm trục sau) ---
  g.fillStyle = '#fff';
  g.beginPath(); g.arc(0, 0, 3, 0, 7); g.fill();
  g.strokeStyle = '#111'; g.lineWidth = 1; g.stroke();

  g.restore();
}

// ---------- Đường kích thước (bản vẽ kỹ thuật) ----------
// Vẽ đường đo 2 đầu mũi tên + nhãn, giữa 2 điểm canvas (x1,y1)-(x2,y2)
function dimLine(g, x1, y1, x2, y2, label, color) {
  const c = color || '#7ee787';
  g.save();
  g.strokeStyle = c; g.fillStyle = c; g.lineWidth = 1;
  g.beginPath(); g.moveTo(x1, y1); g.lineTo(x2, y2); g.stroke();
  const a = Math.atan2(y2 - y1, x2 - x1), h = 5;
  for (const [x, y, dir] of [[x1, y1, a], [x2, y2, a + Math.PI]]) {
    g.beginPath();
    g.moveTo(x, y);
    g.lineTo(x + h * Math.cos(dir - 0.4), y + h * Math.sin(dir - 0.4));
    g.lineTo(x + h * Math.cos(dir + 0.4), y + h * Math.sin(dir + 0.4));
    g.closePath(); g.fill();
  }
  // nhãn ở giữa, có nền để dễ đọc
  const mx = (x1 + x2) / 2, my = (y1 + y2) / 2;
  g.font = 'bold 11px monospace';
  const w = g.measureText(label).width;
  g.fillStyle = '#0f1115';
  g.fillRect(mx - w / 2 - 3, my - 7, w + 6, 14);
  g.fillStyle = c;
  g.textAlign = 'center'; g.textBaseline = 'middle';
  g.fillText(label, mx, my);
  g.restore();
}
// Vạch gióng (extension line)
function extLine(g, x1, y1, x2, y2) {
  g.save();
  g.strokeStyle = '#4b5563'; g.lineWidth = 1;
  g.setLineDash([2, 3]);
  g.beginPath(); g.moveTo(x1, y1); g.lineTo(x2, y2); g.stroke();
  g.restore();
}

// ---------- Bản vẽ xe có kích thước (để kiểm tra số đo) ----------
function showDims() { const el = document.getElementById('dims'); return !el || el.checked; }

function drawCarDetail() {
  const cv2 = document.getElementById('carcv');
  if (!cv2) return;
  const g = cv2.getContext('2d');
  const W = cv2.width, H = cv2.height;
  g.clearRect(0, 0, W, H);

  // khung mm cần hiển thị: từ -bodyBack-60 tới sensorFwd+70 (dọc), ±(bodyW/2+70) (ngang)
  const xMin = -(CAR.bodyBack + 70), xMax = CAR.sensorFwd + 80;
  const yMax = CAR.bodyW / 2 + 75;
  const sc = Math.min(W / (xMax - xMin), H / (2 * yMax));   // px/mm, đồng nhất 2 trục
  const S = mm => mm * sc;
  // đặt gốc (tâm trục sau) sao cho xe nằm giữa; xe hướng LÊN trên màn hình (th = +90°)
  const ox = W / 2, oy = H / 2 + S((xMax + xMin) / 2);

  // vẽ xe hướng lên (th = PI/2)
  drawCar(g, ox, oy, Math.PI / 2, sc);

  if (!showDims()) return;

  // Xe hướng lên: local +x(tiến) = canvas -y ; local +y(phải) = canvas +x
  const FX = (fwd, right) => ({ x: ox + S(right), y: oy - S(fwd) });

  // wheelBase 170 (tâm bánh trái ↔ tâm bánh phải), đặt dưới trục
  let a = FX(-CAR.bodyBack - 40, -CAR.wheelBase / 2), b = FX(-CAR.bodyBack - 40, CAR.wheelBase / 2);
  extLine(g, FX(0, -CAR.wheelBase / 2).x, FX(0, -CAR.wheelBase / 2).y, a.x, a.y);
  extLine(g, FX(0,  CAR.wheelBase / 2).x, FX(0,  CAR.wheelBase / 2).y, b.x, b.y);
  dimLine(g, a.x, a.y, b.x, b.y, 'wheelBase 170');

  // casterFwd 100 (trục sau → caster) — bên trái xe
  a = FX(0, -CAR.bodyW / 2 - 34); b = FX(CAR.casterFwd, -CAR.bodyW / 2 - 34);
  extLine(g, FX(0, 0).x, FX(0, 0).y, a.x, a.y);
  extLine(g, FX(CAR.casterFwd, 0).x, FX(CAR.casterFwd, 0).y, b.x, b.y);
  dimLine(g, a.x, a.y, b.x, b.y, 'caster 100');

  // sensorFwd (trục sau → thanh cảm biến) — bên phải xe  = CENTER_OFFSET_MM
  a = FX(0, CAR.bodyW / 2 + 40); b = FX(CAR.sensorFwd, CAR.bodyW / 2 + 40);
  extLine(g, FX(0, 0).x, FX(0, 0).y, a.x, a.y);
  extLine(g, FX(CAR.sensorFwd, 0).x, FX(CAR.sensorFwd, 0).y, b.x, b.y);
  dimLine(g, a.x, a.y, b.x, b.y, 'cảm biến ' + CAR.sensorFwd + '?', '#e6a23c');

  // wheelD 70 (đường kính bánh) — dọc theo bánh trái
  a = FX(-CAR.wheelD / 2, -CAR.wheelBase / 2 - 30); b = FX(CAR.wheelD / 2, -CAR.wheelBase / 2 - 30);
  dimLine(g, a.x, a.y, b.x, b.y, 'Ø70');

  // sensorSpan (bề ngang thanh mắt)
  a = FX(CAR.sensorFwd + 26, -CAR.sensorSpan / 2); b = FX(CAR.sensorFwd + 26, CAR.sensorSpan / 2);
  dimLine(g, a.x, a.y, b.x, b.y, CAR.sensorSpan + '?', '#e6a23c');

  g.fillStyle = '#7f8c9b'; g.font = '11px monospace'; g.textAlign = 'left'; g.textBaseline = 'alphabetic';
  g.fillText('xanh = đã đo (PLAN)   cam = ước lượng, cần đo', 6, 14);
  g.fillText('gốc ⚪ = tâm trục sau (mốc odometry)', 6, 28);
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
