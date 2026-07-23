'use strict';

const canvas = document.getElementById('world');
const ctx = canvas.getContext('2d');
const message = document.getElementById('message');
const metrics = document.getElementById('metrics');
const slider = document.getElementById('agents');
const agentCount = document.getElementById('agent-count');
const replan = document.getElementById('replan');
const resetButton = document.getElementById('reset');
const clearButton = document.getElementById('clear-walls');

// Mirrors kWallMinX/kWallMaxX in colony.cc: painting is rejected in the
// spawn band on the left and the turnaround band on the right.
const bandWidth = 10;

let api = null;
let module = null;
let width = 0;
let height = 0;
let cell = 0;
let emaUs = 0;
let activePointer = null;
let lastCell = null;
let lastTimestamp = 0;
let trips = 1;
let arrivedSince = 0;
const walls = new Set();

function paintWall(x, y) {
  const key = y * width + x;
  if (walls.has(key)) {
    return;
  }
  if (api.setWall(x, y) === 1) {
    walls.add(key);
  }
}

// Bresenham between consecutive pointer samples so fast drags leave a
// solid wall instead of a dotted one.
function paintLine(from, to) {
  let x = from.x;
  let y = from.y;
  const dx = Math.abs(to.x - x);
  const dy = -Math.abs(to.y - y);
  const sx = x < to.x ? 1 : -1;
  const sy = y < to.y ? 1 : -1;
  let err = dx + dy;
  for (;;) {
    paintWall(x, y);
    if (x === to.x && y === to.y) {
      break;
    }
    const e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y += sy;
    }
  }
}

function reset() {
  const requested = Number(slider.value);
  const actual = api.reset(requested);
  agentCount.textContent = String(actual);
  api.setStrategy(replan.checked ? 1 : 0);
  for (const key of walls) {
    api.setWall(key % width, Math.floor(key / width));
  }
  emaUs = 0;
  trips = 1;
  arrivedSince = 0;
}

function cellAt(event) {
  const rect = canvas.getBoundingClientRect();
  const clamp = (value, max) => Math.max(0, Math.min(max, value));
  const x = Math.floor(((event.clientX - rect.left) / rect.width) * width);
  const y = Math.floor(((event.clientY - rect.top) / rect.height) * height);
  return {x: clamp(x, width - 1), y: clamp(y, height - 1)};
}

function draw() {
  const tilesPtr = api.tiles();
  const agentsPtr = api.agents();
  const count = api.agentCount();
  const tiles = module.HEAPU8.subarray(tilesPtr, tilesPtr + width * height);
  const agents = module.HEAP16.subarray(
      agentsPtr / 2,
      agentsPtr / 2 + count * 2,
  );

  ctx.fillStyle = '#10141c';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = 'rgb(96 223 190 / 4%)';
  ctx.fillRect(0, 0, bandWidth * cell, canvas.height);
  ctx.fillRect((width - bandWidth) * cell, 0, bandWidth * cell,
      canvas.height);
  ctx.fillStyle = '#54627e';
  for (let y = 0; y < height; y += 1) {
    const row = y * width;
    for (let x = 0; x < width; x += 1) {
      if (tiles[row + x] !== 0) {
        ctx.fillRect(x * cell, y * cell, cell, cell);
      }
    }
  }
  ctx.fillStyle = '#60dfbe';
  for (let i = 0; i < count; i += 1) {
    ctx.fillRect(agents[i * 2] * cell, agents[i * 2 + 1] * cell, cell, cell);
  }
}

function frame(timestamp) {
  try {
    const dt = lastTimestamp === 0 ?
        0 :
        Math.max(0, Math.min((timestamp - lastTimestamp) / 1000, 0.25));
    lastTimestamp = timestamp;
    const us = api.tick(dt);
    if (us >= 0) {
      emaUs = emaUs === 0 ? us : emaUs * 0.9 + us * 0.1;
    }
    draw();
    const count = api.agentCount();
    const arrived = api.arrived();
    if (arrived === count) {
      if (arrivedSince === 0) {
        arrivedSince = timestamp;
      } else if (timestamp - arrivedSince > 1000) {
        trips = api.relaunch();
        arrivedSince = 0;
      }
    } else {
      arrivedSince = 0;
    }
    metrics.textContent = `${emaUs.toFixed(0)} µs/tick · ` +
        `${arrived}/${count} arrived · trip ${trips}`;
  } catch (error) {
    message.textContent = `Tick failed: ${error}`;
    return;
  }
  window.requestAnimationFrame(frame);
}

createTessColony()
    .then((instance) => {
      module = instance;
      api = {
        width: instance.cwrap('tess_colony_width', 'number', []),
        height: instance.cwrap('tess_colony_height', 'number', []),
        reset: instance.cwrap('tess_colony_reset', 'number', ['number']),
        setWall: instance.cwrap(
            'tess_colony_set_wall', 'number',
            [
              'number',
              'number',
            ]),
        setStrategy: instance.cwrap(
            'tess_colony_set_strategy', null,
            [
              'number',
            ]),
        tick: instance.cwrap('tess_colony_tick', 'number', ['number']),
        relaunch: instance.cwrap('tess_colony_relaunch', 'number', []),
        tiles: instance.cwrap('tess_colony_tiles', 'number', []),
        agents: instance.cwrap('tess_colony_agents', 'number', []),
        agentCount: instance.cwrap('tess_colony_agent_count', 'number', []),
        arrived: instance.cwrap('tess_colony_arrived', 'number', []),
      };
      width = api.width();
      height = api.height();
      cell = canvas.width / width;
      reset();

      slider.addEventListener('input', () => {
        agentCount.textContent = slider.value;
      });
      slider.addEventListener('change', reset);
      replan.addEventListener('change', () => {
        api.setStrategy(replan.checked ? 1 : 0);
        emaUs = 0;
      });
      resetButton.addEventListener('click', reset);
      clearButton.addEventListener('click', () => {
        walls.clear();
        reset();
      });

      canvas.addEventListener('pointerdown', (event) => {
        if (activePointer !== null) {
          return;
        }
        activePointer = event.pointerId;
        canvas.setPointerCapture(event.pointerId);
        const at = cellAt(event);
        paintWall(at.x, at.y);
        lastCell = at;
      });
      canvas.addEventListener('pointermove', (event) => {
        if (event.pointerId !== activePointer) {
          return;
        }
        const at = cellAt(event);
        paintLine(lastCell, at);
        lastCell = at;
      });
      const stopPainting = (event) => {
        if (event.pointerId !== activePointer) {
          return;
        }
        activePointer = null;
        lastCell = null;
      };
      canvas.addEventListener('pointerup', stopPainting);
      canvas.addEventListener('pointercancel', stopPainting);

      document.documentElement.dataset.tessColony = 'ready';
      message.textContent = 'Colony running';
      window.requestAnimationFrame(frame);
    })
    .catch((error) => {
      document.documentElement.dataset.tessColony = 'failed';
      message.textContent = `Failed to load: ${error}`;
    });
