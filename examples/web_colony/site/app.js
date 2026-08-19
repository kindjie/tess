'use strict';

const canvas = document.getElementById('world');
const ctx = canvas.getContext('2d');
const message = document.getElementById('message');
const metrics = document.getElementById('metrics');
const slider = document.getElementById('agents');
const agentCount = document.getElementById('agent-count');
const replan = document.getElementById('replan');
const spread = document.getElementById('spread');
const resetButton = document.getElementById('reset');
const clearButton = document.getElementById('clear-walls');

// Mirrors kWallMinX/kWallMaxX in colony_model_internal.h: painting is rejected
// in the spawn band on the left and the turnaround band on the right.
const bandWidth = 18;
// Mirrors the FixedStepAccumulator rate in colony_model.cc.
const ticksPerSecond = 20;
// Five seconds of no agent moving at all. Well above transient contention.
const stallTicks = 5 * ticksPerSecond;

let api = null;
let module = null;
let width = 0;
let height = 0;
let cell = 0;
let emaUs = 0;
let activePointer = null;
let lastCell = null;
let lastTimestamp = 0;
let leg = 1;
let turnaroundSince = 0;
const walls = new Set();

function paintWall(x, y) {
  // Example: persist only edits the C++ model admitted. The browser remembers
  // accepted walls across resets but never claims authority over occupancy.
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
  api.setSpread(spread.checked ? 1 : 0);
  for (const key of walls) {
    api.setWall(key % width, Math.floor(key / width));
  }
  emaUs = 0;
  leg = api.leg();
  turnaroundSince = 0;
  lastTimestamp = 0;
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
  const currentAgentsPtr = api.agents();
  const previousAgentsPtr = api.previousAgents();
  const count = api.agentCount();
  const alpha = api.interpolationAlpha();
  const tiles = module.HEAPU8.subarray(tilesPtr, tilesPtr + width * height);
  const currentAgents = module.HEAP16.subarray(
      currentAgentsPtr / 2,
      currentAgentsPtr / 2 + count * 2,
  );
  const previousAgents = module.HEAP16.subarray(
      previousAgentsPtr / 2,
      previousAgentsPtr / 2 + count * 2,
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
  // Example: interpolate fixed-tick snapshots for presentation. These
  // fractional coordinates exist only in canvas drawing; C++ remains on
  // integer tiles and supplies the accumulator remainder as alpha.
  const interpolate = (previous, current) =>
    previous + (current - previous) * alpha;
  const agentSize = cell * 0.72;
  const agentInset = (cell - agentSize) / 2;
  for (let i = 0; i < count; i += 1) {
    const x = interpolate(previousAgents[i * 2], currentAgents[i * 2]);
    const y = interpolate(previousAgents[i * 2 + 1],
        currentAgents[i * 2 + 1]);
    // A centered inset glyph keeps crossing and permitted-swap motion legible.
    ctx.fillRect(x * cell + agentInset, y * cell + agentInset,
        agentSize, agentSize);
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
    const unreachable = api.unreachable();
    const crowdBlocked = api.crowdBlocked();
    const completedLegs = api.completedLegs();
    const abortedLegs = api.abortedLegs();
    const planningPending = api.planningPending();
    const advancedLastTick = api.advancedLastTick();
    const movementWaitsLastTick = api.movementWaitsLastTick();
    const turnaroundReady = api.turnaroundReady() === 1;
    if (turnaroundReady) {
      if (turnaroundSince === 0) {
        turnaroundSince = timestamp;
      } else if (timestamp - turnaroundSince > 1000) {
        leg = api.relaunch();
        turnaroundSince = 0;
      }
    } else {
      turnaroundSince = 0;
    }
    // A colony can stop dead with nobody durably blocked: two agents each
    // standing on the tile the other needs block each other forever. Reporting
    // only the durable count left that reading as "Colony running" over a
    // frozen grid, so a sustained absence of movement is reported too. The
    // threshold is far above ordinary convoy shuffling, which clears in a tick
    // or two.
    const stalledTicks = api.stalledTicks();
    if (unreachable > 0) {
      message.textContent =
          `${unreachable} agents blocked by walls — Clear walls to continue`;
    } else if (turnaroundReady && crowdBlocked > 0) {
      message.textContent =
          `Turnaround: ${crowdBlocked} agents could not reach this side`;
    } else if (crowdBlocked > 0) {
      message.textContent =
          `${crowdBlocked} crowd-blocked agents waiting for turnaround`;
    } else if (stalledTicks >= stallTicks) {
      message.textContent =
          `Colony stalled: no agent has moved for ${
              Math.floor(stalledTicks / ticksPerSecond)}s`;
    } else if (planningPending > 0) {
      message.textContent =
          `Colony planning: ${planningPending} routes pending`;
    } else {
      message.textContent = 'Colony running';
    }
    metrics.textContent = `C++ update ${emaUs.toFixed(0)} µs/tick; ` +
        `canvas render excluded · ` +
        `${arrived}/${count} arrived · ${crowdBlocked} crowd-blocked · ` +
        `${unreachable} wall-blocked · ${planningPending} pending plans · ` +
        `${advancedLastTick} moved · ` +
        `${movementWaitsLastTick} movement waits · leg ${leg} · ` +
        `${completedLegs} completed · ${abortedLegs} crowd turnarounds`;
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
        setSpread: instance.cwrap(
            'tess_colony_set_spread', null, ['number']),
        tick: instance.cwrap('tess_colony_tick', 'number', ['number']),
        relaunch: instance.cwrap('tess_colony_relaunch', 'number', []),
        leg: instance.cwrap('tess_colony_leg', 'number', []),
        tiles: instance.cwrap('tess_colony_tiles', 'number', []),
        agents: instance.cwrap('tess_colony_agents', 'number', []),
        previousAgents: instance.cwrap(
            'tess_colony_previous_agents', 'number', []),
        interpolationAlpha: instance.cwrap(
            'tess_colony_interpolation_alpha', 'number', []),
        agentCount: instance.cwrap('tess_colony_agent_count', 'number', []),
        arrived: instance.cwrap('tess_colony_arrived', 'number', []),
        unreachable: instance.cwrap(
            'tess_colony_unreachable', 'number', []),
        crowdBlocked: instance.cwrap(
            'tess_colony_crowd_blocked', 'number', []),
        turnaroundReady: instance.cwrap(
            'tess_colony_turnaround_ready', 'number', []),
        completedLegs: instance.cwrap(
            'tess_colony_completed_legs', 'number', []),
        abortedLegs: instance.cwrap(
            'tess_colony_aborted_legs', 'number', []),
        stalledTicks: instance.cwrap(
            'tess_colony_stalled_ticks', 'number', []),
        planningPending: instance.cwrap(
            'tess_colony_planning_pending', 'number', []),
        advancedLastTick: instance.cwrap(
            'tess_colony_advanced_last_tick', 'number', []),
        movementWaitsLastTick: instance.cwrap(
            'tess_colony_movement_waits_last_tick', 'number', []),
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
      spread.addEventListener('change', () => {
        api.setSpread(spread.checked ? 1 : 0);
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
