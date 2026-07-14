// ============================================================================
// map.js — Bản đồ lưới 3×3 ô + bộ định tuyến (Dijkstra + step F/L/R).
// Map mới (theo ảnh esp32_car/image_map): lưới 3×3 ô, HOME thò ra ở CẠNH TRÁI
// (đoạn DƯỚI CÙNG — nối chữ T giữa 2 nút đáy cạnh trái), và MỖI Ô đều có nhánh
// cụt đâm vào tâm → giao hàng ở bất cứ ô nào.
// Toạ độ (mm): HOME = gốc (0,0), y hướng LÊN. Xe ở HOME nhìn sang +x (vào lưới).
// Ô đo từ ảnh: 498.56 mm (ngang) × 475.00 mm (dọc) → dùng tròn 500 × 475.
// ============================================================================

const CELL_W = 500;   // bước ngang giữa 2 đường dọc (mm)  (~498.56)
const CELL_H = 475;   // bước dọc giữa 2 đường ngang (mm)
const STUB   = 200;   // chiều dài nhánh HOME thò ra khỏi lưới (mm)

// 4 đường dọc (x): lưới bắt đầu ngay sau nhánh HOME (x = STUB).
const COLS = [STUB, STUB + CELL_W, STUB + 2 * CELL_W, STUB + 3 * CELL_W]; // [200,700,1200,1700]
// 4 đường ngang (y): nhánh HOME nối vào GIỮA đoạn dưới cùng cạnh trái (giữa N20–N30) = y0 = 0.
const ROWS = [2.5 * CELL_H, 1.5 * CELL_H, 0.5 * CELL_H, -0.5 * CELL_H];  // [1187.5,712.5,237.5,-237.5]

// Tâm 9 ô: cx giữa 2 cột, cy giữa 2 hàng.
const CX = [(COLS[0] + COLS[1]) / 2, (COLS[1] + COLS[2]) / 2, (COLS[2] + COLS[3]) / 2]; // [450,950,1450]
const CY = [(ROWS[0] + ROWS[1]) / 2, (ROWS[1] + ROWS[2]) / 2, (ROWS[2] + ROWS[3]) / 2]; // [475,0,-475]

// Điểm dừng: HOME + 9 tâm ô C1..C9 (trái→phải, trên→dưới).
const POINTS = { HOME: { x: 0, y: 0, label: 'HOME' } };
for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
  const id = 'C' + (i * 3 + j + 1);
  POINTS[id] = { x: CX[j], y: CY[i], label: id };
}

// Nhánh cụt (để web vẽ): HOME→giữa cạnh trái, và giữa cạnh TRÊN mỗi ô→tâm ô.
const STUBS = [{ a: [0, 0], b: [COLS[0], 0] }];
for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++)
  STUBS.push({ a: [CX[j], ROWS[i]], b: [CX[j], CY[i]] });

// --- Dựng đồ thị: 16 giao điểm lưới + midpoint nhánh + tâm ô/HOME ---
function buildGraph() {
  const nodes = {}, adj = {};
  const add = (id, x, y) => { nodes[id] = { x, y }; adj[id] = adj[id] || []; };
  const link = (a, b) => {
    const d = Math.hypot(nodes[a].x - nodes[b].x, nodes[a].y - nodes[b].y);
    adj[a].push({ to: b, w: d }); adj[b].push({ to: a, w: d });
  };

  // 16 giao điểm lưới
  for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) add('N' + r + c, COLS[c], ROWS[r]);
  // HOME + nút giữa đoạn dưới cùng cạnh trái (chẻ đôi cạnh N20–N30)
  add('MH', COLS[0], 0); add('HOME', 0, 0);
  // 9 nút giữa cạnh trên của ô (T) + 9 tâm ô (C)
  for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) {
    add('T' + i + j, CX[j], ROWS[i]);
    add('C' + (i * 3 + j + 1), CX[j], CY[i]);
  }

  // cạnh ngang: hàng 0..2 chẻ đôi qua T; hàng 3 (đáy) nối thẳng
  for (let r = 0; r < 4; r++) for (let c = 0; c < 3; c++) {
    if (r < 3) { link('N' + r + c, 'T' + r + c); link('T' + r + c, 'N' + r + (c + 1)); }
    else link('N' + r + c, 'N' + r + (c + 1));
  }
  // cạnh dọc: cột trái đoạn dưới cùng (N20–N30) chẻ đôi qua MH; còn lại nối thẳng
  for (let c = 0; c < 4; c++) for (let r = 0; r < 3; r++) {
    if (c === 0 && r === 2) { link('N20', 'MH'); link('MH', 'N30'); }
    else link('N' + r + c, 'N' + (r + 1) + c);
  }
  // nhánh HOME + nhánh vào tâm ô
  link('MH', 'HOME');
  for (let i = 0; i < 3; i++) for (let j = 0; j < 3; j++) link('T' + i + j, 'C' + (i * 3 + j + 1));

  return { nodes, adj };
}

const G = buildGraph();

function dijkstra(src, dst) {
  const dist = {}, prev = {}, Q = new Set(Object.keys(G.nodes));
  for (const k of Q) dist[k] = Infinity;
  dist[src] = 0;
  while (Q.size) {
    let u = null, best = Infinity;
    for (const k of Q) if (dist[k] < best) { best = dist[k]; u = k; }
    if (u === null) break;
    Q.delete(u);
    for (const e of G.adj[u]) if (Q.has(e.to) && dist[u] + e.w < dist[e.to]) {
      dist[e.to] = dist[u] + e.w; prev[e.to] = u;
    }
  }
  const path = []; let u = dst;
  if (u !== src && prev[u] == null) return null;
  while (u != null) { path.unshift(u); if (u === src) break; u = prev[u]; }
  return path;
}

// Đổi chuỗi node → toạ độ waypoint (để web vẽ đường)
function pathToWaypoints(ids) {
  return ids.map(id => ({ id, x: G.nodes[id].x, y: G.nodes[id].y }));
}

// Đổi chuỗi waypoint → lệnh tương đối F/L/R cho xe (GĐ E5 của PLAN).
// heading0: hướng xuất phát ở HOME = +x (0°), vì HOME ở cạnh trái nhìn vào lưới.
function waypointsToSteps(wps, dropAt) {
  const steps = [];
  let heading = 0;  // độ, +x = 0°
  for (let i = 1; i < wps.length; i++) {
    const dx = wps[i].x - wps[i - 1].x, dy = wps[i].y - wps[i - 1].y;
    if (Math.hypot(dx, dy) < 1) continue;
    let dir = Math.round(Math.atan2(dy, dx) * 180 / Math.PI);
    let turn = ((dir - heading + 540) % 360) - 180;   // [-180,180]
    if (Math.abs(turn) < 15) steps.push('F');
    else if (Math.abs(turn - 90) < 45) steps.push('L');
    else if (Math.abs(turn + 90) < 45) steps.push('R');
    else steps.push('B');                              // quay đầu 180°
    heading = dir;
    if (dropAt && wps[i].id === dropAt) steps.push('DROP');
  }
  steps.push('HOME');
  return steps;
}

// Lập kế hoạch giao 1 điểm: HOME → đích → HOME. Trả waypoints + steps.
function planDelivery(target) {
  const out = dijkstra('HOME', target);
  const back = dijkstra(target, 'HOME');
  if (!out || !back) return null;
  const ids = out.concat(back.slice(1));
  const wps = pathToWaypoints(ids);
  const steps = waypointsToSteps(wps, target);
  return { target, ids, waypoints: wps, steps };
}

module.exports = {
  CELL_W, CELL_H, COLS, ROWS, STUBS, POINTS, G,
  dijkstra, planDelivery, pathToWaypoints, waypointsToSteps,
};
