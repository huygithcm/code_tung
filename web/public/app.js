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
    if (m.type === 'hello') { st = m.state; renderLog(m.state.log); }
    else if (m.type === 'state') st = m.state;
    else if (m.type === 'route') ROUTE = m.plan;
    else if (m.type === 'log')   prependLog(m.entry);
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

// ---------- Tải hình học map rồi vẽ ----------
fetch('/map').then(r => r.json()).then(m => { MAP = m; draw(); });

function bounds() {
  // khổ map cố định để luôn thấy đủ
  return { minx: -400, maxx: 400, miny: -120, maxy: 980 };
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

  // xe
  const p = st.pos, th = (p.th || 0) * Math.PI / 180;
  const px = cx(p), py = cy(p);
  ctx.fillStyle = st.cargo ? '#2d9cdf' : '#9b59b6';
  dot(cx, cy, p.x, p.y, 8);
  ctx.strokeStyle = '#f1c40f'; ctx.lineWidth = 3;
  ctx.beginPath(); ctx.moveTo(px, py); ctx.lineTo(px + 30 * Math.cos(th), py - 30 * Math.sin(th)); ctx.stroke();

  ctx.fillStyle = '#888'; ctx.font = '12px monospace';
  ctx.fillText('ô 240mm | 🟠 điểm giao  🟢 HOME  🔵/🟣 xe(còn/hết hàng)', pad, 16);
}
function line(cx, cy, x1, y1, x2, y2) { ctx.beginPath(); ctx.moveTo(cx({ x: x1, y: y1 }), cy({ x: x1, y: y1 })); ctx.lineTo(cx({ x: x2, y: y2 }), cy({ x: x2, y: y2 })); ctx.stroke(); }
function dot(cx, cy, x, y, r) { ctx.beginPath(); ctx.arc(cx({ x, y }), cy({ x, y }), r, 0, 7); ctx.fill(); }

// ---------- Trạng thái + log ----------
function renderStatus() {
  const s = st;
  document.getElementById('status').innerHTML =
    `trạng thái: <b class="k">${(s.status || '').toUpperCase()}</b>  ` +
    `nguồn: ${s.source === 'car' ? '🚗 xe thật' : '🧪 giả lập'}<br>` +
    `điểm đến: <b class="k">${s.node || '—'}</b>  hàng: ${s.cargo ? '📦 còn' : '✅ đã thả'}<br>` +
    `x=${s.pos.x.toFixed(0)} mm  y=${s.pos.y.toFixed(0)} mm  θ=${(s.pos.th || 0).toFixed(0)}°`;
}
function renderLog(list) { const ul = document.getElementById('log'); ul.innerHTML = ''; (list || []).forEach(addLi); }
function prependLog(e) { const ul = document.getElementById('log'); const li = liOf(e); ul.insertBefore(li, ul.firstChild); }
function addLi(e) { document.getElementById('log').appendChild(liOf(e)); }
function liOf(e) { const li = document.createElement('li'); li.innerHTML = `<span class="t">${e.t}</span>${e.text}`; return li; }

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
