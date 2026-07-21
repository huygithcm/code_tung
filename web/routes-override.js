// ============================================================================
// routes-override.js — Route thủ công cho từng node (ghi đè Dijkstra tự động).
// Lưu bền vào routes.json: { "C1": { steps: [...], dists: [...] }, ... }
// dists[i] = khoảng cách (mm) kỳ vọng cho steps[i] nếu là 'F' (0 = không biết,
// firmware dùng ngưỡng mặc định MIN_EDGE/MAX_EDGE).
// ============================================================================

const fs = require('fs');
const path = require('path');

const FILE = path.join(__dirname, 'routes.json');
let overrides = {};

function load() {
  try {
    overrides = JSON.parse(fs.readFileSync(FILE, 'utf8'));
  } catch {
    overrides = {};
    save();
  }
  return overrides;
}
function save() {
  fs.writeFileSync(FILE, JSON.stringify(overrides, null, 2), 'utf8');
}

function list() { return overrides; }
function get(target) { return overrides[target] || null; }
function set(target, steps, dists) {
  overrides[target] = { steps, dists };
  save();
  return overrides[target];
}
function remove(target) {
  const had = target in overrides;
  delete overrides[target];
  save();
  return had;
}

// Chuoi text nguoi dung nhap -> { steps, dists }. Vi du: "F500 L R F250 R DROP B L F500 L R HOME"
// Token F co the kem so mm (F500) de bao khoang cach ky vong; khong so -> 0 (mac dinh).
function parseSteps(text) {
  const tokens = String(text || '').trim().split(/\s+/).filter(Boolean);
  if (!tokens.length) throw new Error('Chua nhap buoc nao');
  const steps = [], dists = [];
  for (const tok of tokens) {
    const m = tok.match(/^(F|L|R|B|DROP|HOME)(\d+)?$/i);
    if (!m) throw new Error(`Token khong hop le: "${tok}" (dung F/L/R/B/DROP/HOME, F co the kem so mm nhu F500)`);
    steps.push(m[1].toUpperCase());
    dists.push(m[2] ? parseInt(m[2], 10) : 0);
  }
  if (steps[steps.length - 1] !== 'HOME') { steps.push('HOME'); dists.push(0); }
  return { steps, dists };
}

load();
module.exports = { list, get, set, remove, parseSteps };
