// ============================================================================
// map.js — Định nghĩa bản đồ lưới 3×3 + bộ định tuyến (Dijkstra + step F/L/R).
// Dùng chung cho server (trí tuệ) và gửi toạ độ waypoint xuống web để vẽ.
// Toạ độ (mm): HOME = gốc (0,0), y hướng LÊN. Cạnh ô 240 mm, lưới căn giữa x.
// ============================================================================

const CELL = 240;
const COLS = [-360, -120, 120, 360];   // c0..c3 (x)
const ROWS = [920, 680, 440, 200];     // hàng0..hàng3 (y, trên→dưới)

// Nhánh cụt (đâm vào TÂM ô) + HOME — khớp path_view.html
const STUBS = [
  { a: [0, 0],      b: [0, 200]    },           // HOME ra ngoài lưới
  { a: [0, 920],    b: [0, 800]    },           // D1 ô trên-giữa
  { a: [-360, 560], b: [-240, 560] },           // D0 ô giữa-trái
  { a: [360, 560],  b: [240, 560]  },           // D2 ô giữa-phải
];

// Điểm dừng: 3 điểm giao hàng (tâm ô) + HOME
const POINTS = {
  D0:   { x: -240, y: 560, label: 'D0' },
  D1:   { x: 0,    y: 800, label: 'D1' },
  D2:   { x: 240,  y: 560, label: 'D2' },
  HOME: { x: 0,    y: 0,   label: 'HOME' },
};

// --- Dựng đồ thị: 16 giao điểm + midpoint nối nhánh + node giao hàng/HOME ---
function buildGraph() {
  const nodes = {}, adj = {};
  const add = (id, x, y) => { nodes[id] = { x, y }; adj[id] = adj[id] || []; };
  const link = (a, b) => {
    const d = Math.hypot(nodes[a].x - nodes[b].x, nodes[a].y - nodes[b].y);
    adj[a].push({ to: b, w: d }); adj[b].push({ to: a, w: d });
  };
  for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) add('N' + r + c, COLS[c], ROWS[r]);
  add('M1', 0, 920); add('D1', 0, 800);
  add('MH', 0, 200); add('HOME', 0, 0);
  add('M0', -360, 560); add('D0', -240, 560);
  add('M2', 360, 560);  add('D2', 240, 560);
  for (let r = 0; r < 4; r++) for (let c = 0; c < 3; c++) {        // cạnh ngang
    if (r === 0 && c === 1) { link('N01', 'M1'); link('M1', 'N02'); }
    else if (r === 3 && c === 1) { link('N31', 'MH'); link('MH', 'N32'); }
    else link('N' + r + c, 'N' + r + (c + 1));
  }
  for (let r = 0; r < 3; r++) for (let c = 0; c < 4; c++) {        // cạnh dọc
    if (c === 0 && r === 1) { link('N10', 'M0'); link('M0', 'N20'); }
    else if (c === 3 && r === 1) { link('N13', 'M2'); link('M2', 'N23'); }
    else link('N' + r + c, 'N' + (r + 1) + c);
  }
  link('M1', 'D1'); link('MH', 'HOME'); link('M0', 'D0'); link('M2', 'D2');
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

// Đổi chuỗi node → toạ độ waypoint (để web vẽ + giả lập chạy)
function pathToWaypoints(ids) {
  return ids.map(id => ({ id, x: G.nodes[id].x, y: G.nodes[id].y }));
}

// Đổi chuỗi waypoint → lệnh tương đối F/L/R cho xe (GĐ E5 của PLAN).
// heading0: hướng xuất phát (mặc định 'N' = +y, vì HOME ở đáy đi lên).
function waypointsToSteps(wps, dropAt) {
  const steps = [];
  let heading = 90;  // độ, +y = 90°
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
  CELL, COLS, ROWS, STUBS, POINTS, G,
  dijkstra, planDelivery, pathToWaypoints, waypointsToSteps,
};
