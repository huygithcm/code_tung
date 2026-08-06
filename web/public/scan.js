// ============================================================================
// scan.js — Trang quét QR độc lập (mở trên điện thoại). Quét → xác nhận → gửi
// lệnh giao cho server qua WebSocket. Hiển thị trạng thái giao + nhật ký.
// ============================================================================

let ws = null;
let st = { status: 'idle', node: null, cargo: true, source: '' };

// ---------- WebSocket tới hub ----------
function connect() {
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  ws = new WebSocket(proto + '://' + location.host);
  ws.onopen = () => setConn(true);
  ws.onclose = () => { setConn(false); setTimeout(connect, 1500); };
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.type === 'hello') { st = m.state; renderLog(m.state.log); }
    else if (m.type === 'state') st = m.state;
    else if (m.type === 'log') prependLog(m.entry);
    renderStatus();
  };
}
function setConn(ok) {
  const b = document.getElementById('conn');
  b.className = 'badge ' + (ok ? 'on' : 'off');
  b.textContent = ok ? '● đã kết nối' : '● mất kết nối';
}
function send(o) { if (ws && ws.readyState === 1) ws.send(JSON.stringify(o)); }

function renderStatus() {
  document.getElementById('status').innerHTML =
    `Trạng thái: <b class="k">${(st.status || '').toUpperCase()}</b> · ` +
    `nguồn: ${st.source === 'car' ? '🚗 xe thật' : '⚠️ chưa có xe'}<br>` +
    `điểm đến: <b class="k">${st.node || '—'}</b> · hàng: ${st.cargo ? '📦 còn' : '✅ đã thả'}`;
}
function renderLog(list) { const ul = document.getElementById('log'); ul.innerHTML = ''; (list || []).forEach(addLi); }
function prependLog(e) { const ul = document.getElementById('log'); ul.insertBefore(liOf(e), ul.firstChild); }
function addLi(e) { document.getElementById('log').appendChild(liOf(e)); }
function liOf(e) { const li = document.createElement('li'); li.innerHTML = `<span class="t">${e.t}</span>${e.text}`; return li; }

// ---------- Quét QR ----------
let qr = null;
let knownNodes = new Set();
document.getElementById('qrStart').onclick = async () => {
  if (qr) return;
  qr = new Html5Qrcode('reader');
  try {
    await qr.start({ facingMode: 'environment' }, { fps: 10, qrbox: 240 }, onScan);
  } catch (e) { document.getElementById('qrResult').textContent = 'Không mở được camera: ' + e; qr = null; }
};
document.getElementById('qrStop').onclick = async () => {
  if (qr) { await qr.stop(); await qr.clear(); qr = null; }
};

let lastScan = 0;
let pendingQR = null;                         // QR đã quét, đang CHỜ xác nhận (chưa giao)
function onScan(text) {
  if (pendingQR !== null) return;             // đang chờ xác nhận -> bỏ qua
  const now = Date.now();
  if (now - lastScan < 2500) return;          // chống quét trùng
  lastScan = now;
  pendingQR = text;
  renderQrConfirm(text);
}
function renderQrConfirm(text) {
  const box = document.getElementById('qrResult');
  const target = normalizeQrToNode(text);
  const safe = String(text).replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  const targetLine = target ? `<br>Điểm giao: <b class="k">${target}</b>` : '';
  box.innerHTML =
    `📷 QR: <b>${safe}</b>${targetLine}<br>Start giao hàng?` +
    `<div class="row" style="margin-top:10px">` +
    `<button class="btn b-start big" onclick="confirmQR()">Yes - start giao</button>` +
    `<button class="btn b-stop big" onclick="cancelQR()">✖ Huỷ</button>` +
    `</div>`;
}
function confirmQR() {
  if (pendingQR === null) return;
  const value = normalizeQrToNode(pendingQR) || pendingQR;
  send({ cmd: 'qr', value });
  document.getElementById('qrResult').textContent = '✅ Đã gửi lệnh start giao hàng: ' + value;
  pendingQR = null;
}
function cancelQR() {
  pendingQR = null;
  lastScan = 0;                               // cho phép quét lại ngay
  document.getElementById('qrResult').textContent = 'Đã huỷ. Hướng camera vào mã QR để quét lại.';
}

function normalizeQrToNode(value) {
  const raw = String(value || '').trim();
  if (knownNodes.has(raw)) return raw;
  try {
    const url = new URL(raw, location.href);
    const node = (url.searchParams.get('node') || url.searchParams.get('qr') || '').trim();
    if (knownNodes.has(node)) return node;
  } catch (_) {}
  return null;
}

function loadFixedNodeFromUrl() {
  const params = new URLSearchParams(location.search);
  const node = (params.get('node') || params.get('qr') || '').trim();
  if (!node) return;
  pendingQR = node;
  renderQrConfirm(node);
}

fetch('/api/points')
  .then(r => r.json())
  .then(nodes => {
    knownNodes = new Set(nodes || []);
    loadFixedNodeFromUrl();
  })
  .catch(loadFixedNodeFromUrl);

connect();
