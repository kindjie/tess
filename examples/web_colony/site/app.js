'use strict';

const canvas = document.getElementById('world');
const ctx = canvas.getContext('2d');
const message = document.getElementById('message');
const metrics = document.getElementById('metrics');
const slider = document.getElementById('agents');
const agentCount = document.getElementById('agent-count');
const replan = document.getElementById('replan');
const resetButton = document.getElementById('reset');

let api = null;
let module = null;
let width = 0;
let height = 0;
let cell = 0;
let emaUs = 0;
let painting = false;

function reset() {
  const requested = Number(slider.value);
  const actual = api.reset(requested);
  agentCount.textContent = String(actual);
  api.setStrategy(replan.checked ? 1 : 0);
  emaUs = 0;
}

function cellAt(event) {
  const rect = canvas.getBoundingClientRect();
  const x = Math.floor(((event.clientX - rect.left) / rect.width) * width);
  const y = Math.floor(((event.clientY - rect.top) / rect.height) * height);
  return {x, y};
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

function frame() {
  try {
    const us = api.tick();
    emaUs = emaUs === 0 ? us : emaUs * 0.9 + us * 0.1;
    draw();
    const arrived = api.arrived();
    metrics.textContent = `${emaUs.toFixed(0)} µs/tick · ` +
        `${arrived}/${api.agentCount()} arrived`;
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
        tick: instance.cwrap('tess_colony_tick', 'number', []),
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

      canvas.addEventListener('pointerdown', (event) => {
        painting = true;
        canvas.setPointerCapture(event.pointerId);
        const {x, y} = cellAt(event);
        api.setWall(x, y);
      });
      canvas.addEventListener('pointermove', (event) => {
        if (!painting) {
          return;
        }
        const {x, y} = cellAt(event);
        api.setWall(x, y);
      });
      canvas.addEventListener('pointerup', () => {
        painting = false;
      });

      document.documentElement.dataset.tessColony = 'ready';
      message.textContent = 'Colony running';
      window.requestAnimationFrame(frame);
    })
    .catch((error) => {
      document.documentElement.dataset.tessColony = 'failed';
      message.textContent = `Failed to load: ${error}`;
    });
