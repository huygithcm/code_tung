// admin.js — CRUD danh mục hàng qua REST API.
const $ = id => document.getElementById(id);

async function loadPoints() {
  const pts = await (await fetch('/api/points')).json();
  $('node').innerHTML = pts.map(p => `<option value="${p}">${p}</option>`).join('');
}
async function loadGoods() {
  const goods = await (await fetch('/api/goods')).json();
  $('rows').innerHTML = goods.map(g => `
    <tr>
      <td><b>${esc(g.code)}</b></td>
      <td>${esc(g.name || '')}</td>
      <td>${esc(g.node)}</td>
      <td><button class="btn b-stop" data-del="${esc(g.code)}">Xóa</button></td>
    </tr>`).join('') || '<tr><td colspan="4">Chưa có hàng nào.</td></tr>';
  document.querySelectorAll('[data-del]').forEach(b =>
    b.onclick = () => del(b.getAttribute('data-del')));
}
function esc(s) { return String(s).replace(/[<>&"]/g, c => ({ '<': '&lt;', '>': '&gt;', '&': '&amp;', '"': '&quot;' }[c])); }

$('add').onclick = async () => {
  const body = { code: $('code').value, name: $('name').value, node: $('node').value };
  if (!body.code.trim()) { msg('Nhập mã QR', true); return; }
  const r = await fetch('/api/goods', {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
  });
  const d = await r.json();
  if (!r.ok) { msg(d.error || 'Lỗi', true); return; }
  msg(`Đã lưu "${d.code}" → ${d.node}`);
  $('code').value = ''; $('name').value = '';
  loadGoods();
};
async function del(code) {
  if (!confirm(`Xóa hàng "${code}"?`)) return;
  await fetch('/api/goods/' + encodeURIComponent(code), { method: 'DELETE' });
  msg(`Đã xóa "${code}"`);
  loadGoods();
}
function msg(t, err) { const m = $('msg'); m.textContent = t; m.style.color = err ? '#e74c3c' : '#2ecc71'; }

loadPoints().then(loadGoods);
