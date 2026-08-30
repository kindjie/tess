"use strict";

const canvas = document.querySelector("#stream-world");
const context = canvas.getContext("2d");
const pauseButton = document.querySelector("#pause");
const resetButton = document.querySelector("#reset");
const summary = document.querySelector("#summary");
const residencyStatus = document.querySelector("#residency-status");
const agentStatus = document.querySelector("#agent-status");
const announcement = document.querySelector("#announcement");
const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

const stepInterval = 90;
let api;
let paused = true;
let lastStep = 0;

function bind(module, name, result, arguments_ = []) {
  return module.cwrap(`tess_sparse_${name}`, result, arguments_);
}

function setPauseLabel() {
  pauseButton.textContent = reducedMotion.matches
    ? "Step"
    : paused ? "Start" : "Pause";
}

function readChunks(count, readX, readY) {
  const chunks = [];
  for (let index = 0; index < count(); ++index) {
    chunks.push({x: readX(index), y: readY(index)});
  }
  return chunks;
}

function readAgents() {
  const agents = [];
  for (let index = 0; index < api.agentCount(); ++index) {
    agents.push({
      x: api.agentX(index),
      y: api.agentY(index),
      goalX: api.agentGoalX(index),
      goalY: api.agentGoalY(index),
      status: api.agentStatus(index),
    });
  }
  return agents;
}

function snapshot() {
  const agents = readAgents();
  return {
    step: api.stepCount(),
    cameraX: api.cameraChunkX(),
    cameraY: api.cameraChunkY(),
    required: api.requiredCount(),
    newly: api.newCount(),
    retained: api.retainedCount(),
    evicted: api.evictedCount(),
    resident: api.residentCount(),
    capacity: api.capacity(),
    status: api.status(),
    moving: agents.filter((agent) => agent.status === 0).length,
    waiting: agents.filter((agent) => agent.status === 1).length,
    atGoal: agents.filter((agent) => agent.status === 2).length,
  };
}

function chunkKey(chunk) {
  return `${chunk.x},${chunk.y}`;
}

function draw() {
  const required = readChunks(
    api.requiredCount, api.requiredChunkX, api.requiredChunkY);
  const newly = new Set(readChunks(
    api.newCount, api.newChunkX, api.newChunkY).map(chunkKey));
  const retained = new Set(readChunks(
    api.retainedCount, api.retainedChunkX, api.retainedChunkY).map(chunkKey));
  const evicted = readChunks(
    api.evictedCount, api.evictedChunkX, api.evictedChunkY);
  const agents = readAgents();
  const centerX = api.cameraChunkX();
  const centerY = api.cameraChunkY();
  const viewRadius = 3;
  const viewSize = viewRadius * 2 + 1;
  const tile = Math.min(canvas.width / viewSize, canvas.height / viewSize);
  const left = (canvas.width - tile * viewSize) / 2;
  const top = (canvas.height - tile * viewSize) / 2;

  context.fillStyle = "#0b1020";
  context.fillRect(0, 0, canvas.width, canvas.height);
  context.font = "16px ui-monospace, monospace";
  context.textAlign = "center";
  context.textBaseline = "middle";

  for (let y = centerY - viewRadius; y <= centerY + viewRadius; ++y) {
    for (let x = centerX - viewRadius; x <= centerX + viewRadius; ++x) {
      const key = `${x},${y}`;
      const px = left + (x - centerX + viewRadius) * tile;
      const py = top + (y - centerY + viewRadius) * tile;
      const isRequired = required.some((chunk) => chunkKey(chunk) === key);
      context.fillStyle = newly.has(key)
        ? "#0f766e"
        : retained.has(key) ? "#1e3a5f" : "#111827";
      context.fillRect(px + 2, py + 2, tile - 4, tile - 4);
      context.strokeStyle = isRequired ? "#60a5fa" : "#334155";
      context.lineWidth = isRequired ? 3 : 1;
      context.strokeRect(px + 2, py + 2, tile - 4, tile - 4);
      context.fillStyle = "#b7c6d9";
      context.fillText(`${x},${y}`, px + tile / 2, py + tile / 2);
    }
  }

  for (const chunk of evicted) {
    const dx = chunk.x - centerX;
    const dy = chunk.y - centerY;
    if (Math.abs(dx) <= viewRadius && Math.abs(dy) <= viewRadius) {
      const px = left + (dx + viewRadius) * tile;
      const py = top + (dy + viewRadius) * tile;
      context.strokeStyle = "#fb7185";
      context.lineWidth = 4;
      context.strokeRect(px + 7, py + 7, tile - 14, tile - 14);
    }
  }

  for (const agent of agents) {
    const chunkX = Math.floor(agent.x / 32);
    const chunkY = Math.floor(agent.y / 32);
    const localX = agent.x % 32;
    const localY = agent.y % 32;
    const px = left + (chunkX - centerX + viewRadius) * tile +
      localX / 32 * tile;
    const py = top + (chunkY - centerY + viewRadius) * tile +
      localY / 32 * tile;
    context.beginPath();
    context.arc(px, py, 7, 0, Math.PI * 2);
    context.fillStyle = agent.status === 1 ? "#fbbf24" : "#e0f2fe";
    context.fill();
  }

  const state = snapshot();
  summary.textContent = `Camera chunk (${state.cameraX}, ${state.cameraY}) · ` +
    `step ${state.step}`;
  residencyStatus.textContent =
    `${state.required} required · ${state.newly} newly generated · ` +
    `${state.retained} retained · ${state.evicted} evicted · ` +
    `${state.resident}/${state.capacity} resident`;
  agentStatus.textContent =
    `${state.moving} moving · ${state.waiting} waiting · ` +
    `${state.atGoal} at goal`;
}

function tick() {
  api.tick();
  draw();
}

function animate(timestamp) {
  if (!paused && !reducedMotion.matches &&
      timestamp - lastStep >= stepInterval) {
    tick();
    lastStep = timestamp;
  }
  requestAnimationFrame(animate);
}

pauseButton.addEventListener("click", () => {
  if (reducedMotion.matches) {
    tick();
    announcement.textContent = "Advanced one streaming step.";
    return;
  }
  paused = !paused;
  lastStep = performance.now();
  setPauseLabel();
  announcement.textContent = paused
    ? "Streaming paused."
    : "Streaming started.";
});

resetButton.addEventListener("click", () => {
  api.reset();
  paused = true;
  setPauseLabel();
  draw();
  announcement.textContent = "Streaming model reset.";
});

reducedMotion.addEventListener("change", () => {
  paused = true;
  setPauseLabel();
});

createTessSparseStream().then((module) => {
  api = {
    reset: bind(module, "reset", null),
    tick: bind(module, "tick", "number"),
    status: bind(module, "status", "number"),
    cameraChunkX: bind(module, "camera_chunk_x", "number"),
    cameraChunkY: bind(module, "camera_chunk_y", "number"),
    residentCount: bind(module, "resident_count", "number"),
    capacity: bind(module, "capacity", "number"),
    requiredCount: bind(module, "required_count", "number"),
    newCount: bind(module, "new_count", "number"),
    retainedCount: bind(module, "retained_count", "number"),
    evictedCount: bind(module, "evicted_count", "number"),
    requiredChunkX: bind(module, "required_chunk_x", "number", ["number"]),
    requiredChunkY: bind(module, "required_chunk_y", "number", ["number"]),
    newChunkX: bind(module, "new_chunk_x", "number", ["number"]),
    newChunkY: bind(module, "new_chunk_y", "number", ["number"]),
    retainedChunkX: bind(module, "retained_chunk_x", "number", ["number"]),
    retainedChunkY: bind(module, "retained_chunk_y", "number", ["number"]),
    evictedChunkX: bind(module, "evicted_chunk_x", "number", ["number"]),
    evictedChunkY: bind(module, "evicted_chunk_y", "number", ["number"]),
    agentCount: bind(module, "agent_count", "number"),
    agentX: bind(module, "agent_x", "number", ["number"]),
    agentY: bind(module, "agent_y", "number", ["number"]),
    agentGoalX: bind(module, "agent_goal_x", "number", ["number"]),
    agentGoalY: bind(module, "agent_goal_y", "number", ["number"]),
    agentStatus: bind(module, "agent_status", "number", ["number"]),
    stepCount: bind(module, "step_count", "number"),
  };
  pauseButton.disabled = false;
  resetButton.disabled = false;
  canvas.removeAttribute("aria-disabled");
  draw();
  setPauseLabel();
  window.tessSparseStreamTest = {
    snapshot,
    step(count = 1) {
      for (let index = 0; index < count; ++index) {
        api.tick();
      }
      draw();
      return snapshot();
    },
  };
  document.documentElement.dataset.tessSparseStream = "ready";
  requestAnimationFrame(animate);
}).catch((error) => {
  summary.textContent = `The WebAssembly model could not start: ${error}`;
});
