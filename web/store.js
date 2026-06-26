// ============================================================================
// store.js — Danh mục hàng (mã QR → điểm giao), lưu bền vào goods.json.
// Mỗi mục: { code, name, node }  (code = nội dung QR; node = D0/D1/D2)
// ============================================================================

const fs = require('fs');
const path = require('path');
const cfg = require('./config');

const FILE = path.join(__dirname, 'goods.json');
let goods = [];

function load() {
  try {
    goods = JSON.parse(fs.readFileSync(FILE, 'utf8'));
  } catch {
    // Lần đầu: seed từ config.QR_MAP
    goods = Object.entries(cfg.QR_MAP).map(([code, node]) => ({
      code, node, name: 'Hàng ' + code,
    }));
    save();
  }
  return goods;
}
function save() {
  fs.writeFileSync(FILE, JSON.stringify(goods, null, 2), 'utf8');
}

function list() { return goods; }
function lookup(code) {
  const g = goods.find(x => x.code === code);
  return g ? g.node : null;
}
function add({ code, name, node }) {
  code = (code || '').trim();
  if (!code || !node) throw new Error('Thiếu mã hoặc điểm giao');
  const i = goods.findIndex(x => x.code === code);
  const item = { code, name: (name || '').trim() || ('Hàng ' + code), node };
  if (i >= 0) goods[i] = item; else goods.push(item);   // trùng mã → cập nhật
  save();
  return item;
}
function remove(code) {
  const n = goods.length;
  goods = goods.filter(x => x.code !== code);
  save();
  return goods.length < n;
}

load();
module.exports = { list, lookup, add, remove };
