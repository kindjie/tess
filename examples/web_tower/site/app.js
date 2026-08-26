'use strict';

// Isometric view of a tess world that is six floors tall. Everything is
// drawn on a 2D canvas: a phone can carry this, and the demo is about
// routing rather than about rendering.

const canvas = document.getElementById('view');
const ctx = canvas.getContext('2d');
const loading = document.getElementById('loading');
const out = {
  arrived: document.getElementById('arrived'),
  total: document.getElementById('total'),
  climbing: document.getElementById('climbing'),
  blocked: document.getElementById('blocked'),
  leg: document.getElementById('leg'),
};
const floorsInput = document.getElementById('floors');
const floorsOut = document.getElementById('floorsOut');
const agentsInput = document.getElementById('agents');
const agentsOut = document.getElementById('agentsOut');

let api = null;
let width = 0;
let depth = 0;
let floors = 0;
let levels = 0;

// Camera: four orientations keep the controls simple on a touch screen
// and keep the projection exact.
const camera = {
  yaw: 0,
  zoom: 1,
  panX: 0,
  panY: 0,
  visibleFloors: 6
};

const TILE_W = 14;
const TILE_H = 7;
const FLOOR_RISE = 46;

function rotate(x, y) {
  const cx = width / 2;
  const cy = depth / 2;
  const dx = x - cx;
  const dy = y - cy;
  switch (camera.yaw & 3) {
    case 1:
      return [cx - dy, cy + dx];
    case 2:
      return [cx - dx, cy - dy];
    case 3:
      return [cx + dy, cy - dx];
    default:
      return [x, y];
  }
}

function project(x, y, floor) {
  const [rx, ry] = rotate(x, y);
  return [
    (rx - ry) * (TILE_W / 2) * camera.zoom + camera.panX,
    ((rx + ry) * (TILE_H / 2) - floor * FLOOR_RISE) * camera.zoom + camera.panY,
  ];
}

function diamond(x, y, floor, w, h, fill) {
  const [sx, sy] = project(x, y, floor);
  const halfW = (w * TILE_W * camera.zoom) / 2;
  const halfH = (h * TILE_H * camera.zoom) / 2;
  ctx.beginPath();
  ctx.moveTo(sx, sy - halfH);
  ctx.lineTo(sx + halfW, sy);
  ctx.lineTo(sx, sy + halfH);
  ctx.lineTo(sx - halfW, sy);
  ctx.closePath();
  ctx.fillStyle = fill;
  ctx.fill();
}

function floorPlate(floor) {
  const corners = [[0, 0], [width, 0], [width, depth], [0, depth]].map(
      ([x, y]) => project(x, y, floor));
  ctx.beginPath();
  ctx.moveTo(corners[0][0], corners[0][1]);
  for (let i = 1; i < corners.length; i += 1) {
    ctx.lineTo(corners[i][0], corners[i][1]);
  }
  ctx.closePath();
  ctx.fillStyle = floor === 0 ? '#141a26' : 'rgba(20, 26, 38, 0.82)';
  ctx.fill();
  ctx.strokeStyle = '#273041';
  ctx.lineWidth = 1;
  ctx.stroke();
}

function resize() {
  const rect = canvas.getBoundingClientRect();
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  canvas.width = Math.max(1, Math.round(rect.width * dpr));
  canvas.height = Math.max(1, Math.round(rect.height * dpr));
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}

function recentre() {
  const rect = canvas.getBoundingClientRect();
  // Fit the whole tower: its isometric width is (w+d) tiles and its
  // height is the plate depth plus the stack rise.
  const spanX = (width + depth) * (TILE_W / 2);
  const spanY = (width + depth) * (TILE_H / 2) + (floors - 1) * FLOOR_RISE;
  camera.zoom = Math.min(rect.width / (spanX + 40), rect.height / (spanY + 40));
  camera.panX = rect.width / 2;
  camera.panY = rect.height / 2 +
      ((floors - 1) * FLOOR_RISE * camera.zoom) / 2 -
      ((width + depth) * (TILE_H / 2) * camera.zoom) / 2;
}

function draw() {
  const rect = canvas.getBoundingClientRect();
  ctx.clearRect(0, 0, rect.width, rect.height);

  const tiles = api.tiles();
  const current = api.agents();
  const previous = api.previousAgents();
  const alpha = api.alpha();
  const agentCount = api.agentCount();
  const shown = camera.visibleFloors;

  for (let floor = 0; floor < shown; floor += 1) {
    const level = floor * 2;
    floorPlate(floor);

    // Solid tiles. Iterating back to front keeps the overlap correct.
    for (let sum = 0; sum <= width + depth - 2; sum += 1) {
      for (let x = Math.max(0, sum - depth + 1); x <= Math.min(sum, width - 1);
           x += 1) {
        const y = sum - x;
        if (tiles[(level * depth + y) * width + x] === 0) {
          diamond(x + 0.5, y + 0.5, floor, 1, 1, '#2b3446');
        }
      }
    }

    // Stairwells, so the route between floors is legible.
    for (let s = 0; s < api.stairCount(); s += 1) {
      const sx = api.stairX(s);
      const sy = api.stairY(s);
      const open = api.stairOpen(s) === 1;
      diamond(
          sx + 1.5, sy + 1.5, floor, 3, 3,
          open ? 'rgba(110, 168, 254, 0.30)' : 'rgba(240, 160, 90, 0.34)');
    }

    // Agents on this floor, interpolated between fixed ticks.
    for (let i = 0; i < agentCount; i += 1) {
      const cz = current[i * 3 + 2];
      if (cz !== level) {
        continue;
      }
      const cx = current[i * 3];
      const cy = current[i * 3 + 1];
      const px = previous[i * 3];
      const py = previous[i * 3 + 1];
      const pz = previous[i * 3 + 2];
      // Only interpolate within a floor; a step between floors would
      // otherwise slide the marker through the slab.
      const ix = pz === cz ? px + (cx - px) * alpha : cx;
      const iy = pz === cz ? py + (cy - py) * alpha : cy;
      diamond(ix + 0.5, iy + 0.5, floor, 0.8, 0.8, '#6ea8fe');
    }
  }
}

function refreshReadout() {
  out.arrived.textContent = String(api.arrived());
  out.total.textContent = String(api.agentCount());
  out.climbing.textContent = String(api.climbing());
  out.blocked.textContent = String(api.crowdBlocked());
  out.leg.textContent = String(api.leg());
}

let last = 0;
function frame(now) {
  const dt = last === 0 ? 0 : Math.min((now - last) / 1000, 0.25);
  last = now;
  api.tick(dt);
  if (api.turnaroundReady() === 1) {
    api.relaunch();
  }
  draw();
  refreshReadout();
  requestAnimationFrame(frame);
}

// --- touch and pointer gestures -----------------------------------------
const pointers = new Map();
let pinchStart = 0;
let zoomStart = 1;
let dragged = false;

canvas.addEventListener('pointerdown', (event) => {
  canvas.setPointerCapture(event.pointerId);
  pointers.set(event.pointerId, {x: event.clientX, y: event.clientY});
  dragged = false;
  if (pointers.size === 2) {
    const [a, b] = [...pointers.values()];
    pinchStart = Math.hypot(a.x - b.x, a.y - b.y);
    zoomStart = camera.zoom;
  }
});

canvas.addEventListener('pointermove', (event) => {
  const prev = pointers.get(event.pointerId);
  if (!prev) {
    return;
  }
  const dx = event.clientX - prev.x;
  const dy = event.clientY - prev.y;
  pointers.set(event.pointerId, {x: event.clientX, y: event.clientY});
  if (Math.hypot(dx, dy) > 2) {
    dragged = true;
  }
  if (pointers.size === 2) {
    const [a, b] = [...pointers.values()];
    const spread = Math.hypot(a.x - b.x, a.y - b.y);
    if (pinchStart > 0) {
      camera.zoom =
          Math.min(4, Math.max(0.15, zoomStart * (spread / pinchStart)));
    }
    camera.panX += dx / 2;
    camera.panY += dy / 2;
  } else if (pointers.size === 1) {
    camera.panX += dx;
    camera.panY += dy;
  }
});

function endPointer(event) {
  pointers.delete(event.pointerId);
  if (pointers.size < 2) {
    pinchStart = 0;
  }
}
canvas.addEventListener('pointerup', endPointer);
canvas.addEventListener('pointercancel', endPointer);

canvas.addEventListener('wheel', (event) => {
  event.preventDefault();
  camera.zoom =
      Math.min(4, Math.max(0.15, camera.zoom * (event.deltaY < 0 ? 1.1 : 0.9)));
}, {passive: false});

// --- controls ------------------------------------------------------------
document.getElementById('rotate').addEventListener('click', () => {
  camera.yaw = (camera.yaw + 1) & 3;
});
document.getElementById('recentre').addEventListener('click', recentre);
document.getElementById('relaunch').addEventListener('click', () => {
  api.relaunch();
});

for (const button of document.querySelectorAll('.stair')) {
  button.addEventListener('click', () => {
    const index = Number(button.dataset.stair);
    const nowOpen = api.stairOpen(index) === 1;
    api.setStairwell(index, nowOpen ? 0 : 1);
    button.setAttribute('aria-pressed', String(nowOpen));
  });
}

floorsInput.addEventListener('input', () => {
  camera.visibleFloors = Number(floorsInput.value);
  floorsOut.textContent = floorsInput.value;
});

agentsInput.addEventListener('input', () => {
  agentsOut.textContent = agentsInput.value;
});
agentsInput.addEventListener('change', () => {
  api.reset(Number(agentsInput.value));
  for (const button of document.querySelectorAll('.stair')) {
    button.setAttribute('aria-pressed', 'false');
  }
});

window.addEventListener('resize', () => {
  resize();
  recentre();
});

createTessTower().then((instance) => {
  const wrap = (name, ret, args) => instance.cwrap(name, ret, args);
  const tilesPtr = wrap('tess_tower_tiles', 'number', []);
  const agentsPtr = wrap('tess_tower_agents', 'number', []);
  const prevPtr = wrap('tess_tower_previous_agents', 'number', []);
  const resetFn = wrap('tess_tower_reset', 'number', ['number']);
  width = wrap('tess_tower_width', 'number', [])();
  depth = wrap('tess_tower_depth', 'number', [])();
  floors = wrap('tess_tower_floors', 'number', [])();
  levels = floors * 2;

  api = {
    reset: (n) => resetFn(n),
    tick: wrap('tess_tower_tick', 'number', ['number']),
    relaunch: wrap('tess_tower_relaunch', 'number', []),
    setStairwell:
        wrap('tess_tower_set_stairwell', 'number', ['number', 'number']),
    stairCount: wrap('tess_tower_stairwell_count', 'number', []),
    stairOpen: wrap('tess_tower_stairwell_open', 'number', ['number']),
    stairX: wrap('tess_tower_stairwell_x', 'number', ['number']),
    stairY: wrap('tess_tower_stairwell_y', 'number', ['number']),
    agentCount: wrap('tess_tower_agent_count', 'number', []),
    arrived: wrap('tess_tower_arrived', 'number', []),
    crowdBlocked: wrap('tess_tower_crowd_blocked', 'number', []),
    climbing: wrap('tess_tower_climbing', 'number', []),
    leg: wrap('tess_tower_leg', 'number', []),
    turnaroundReady: wrap('tess_tower_turnaround_ready', 'number', []),
    alpha: wrap('tess_tower_interpolation_alpha', 'number', []),
    tiles: () => instance.HEAPU8.subarray(
        tilesPtr(), tilesPtr() + width * depth * levels),
    agents: () => instance.HEAP16.subarray(
        agentsPtr() / 2, agentsPtr() / 2 + api.agentCount() * 3),
    previousAgents: () => instance.HEAP16.subarray(
        prevPtr() / 2, prevPtr() / 2 + api.agentCount() * 3),
  };

  // A phone gets fewer agents by default than a desktop.
  const startAgents = window.innerWidth < 700 ? 24 : 48;
  agentsInput.value = String(startAgents);
  agentsOut.textContent = String(startAgents);
  api.reset(startAgents);
  floorsInput.max = String(floors);
  floorsInput.value = String(floors);
  floorsOut.textContent = String(floors);
  camera.visibleFloors = floors;

  resize();
  recentre();
  loading.remove();
  requestAnimationFrame(frame);
});
