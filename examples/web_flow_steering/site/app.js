"use strict";

const canvas = document.querySelector("#world");
const context = canvas.getContext("2d");
const pauseButton = document.querySelector("#pause");
const resetButton = document.querySelector("#reset");
const summary = document.querySelector("#summary");
const movingStatus = document.querySelector("#moving");
const goalStatus = document.querySelector("#at-goal");
const unreachableStatus = document.querySelector("#unreachable");
const presetButtons = [...document.querySelectorAll("[data-goal-preset]")];
const goalXInput = document.querySelector("#goal-x");
const goalYInput = document.querySelector("#goal-y");
const setGoalButton = document.querySelector("#set-goal");
const interactiveControls = [
  ...document.querySelectorAll("button, input"),
];
const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

const unreachableDistance = 0xffffffff;
const stepInterval = 180;
let api;
let paused = true;
let lastStep = 0;
let stepCount = 0;

function bind(module, name, result, arguments_) {
  return module.cwrap(`tess_flow_${name}`, result, arguments_);
}

function setPauseLabel() {
  pauseButton.setAttribute("aria-pressed", String(paused));
  pauseButton.textContent = reducedMotion.matches
    ? "Step"
    : paused ? "Start" : "Pause";
}

function readAgents() {
  const agents = [];
  for (let index = 0; index < api.agentCount(); ++index) {
    agents.push({
      x: api.agentX(index),
      y: api.agentY(index),
      state: api.agentState(index),
    });
  }
  return agents;
}

function draw() {
  const tileWidth = canvas.width / api.width();
  const tileHeight = canvas.height / api.height();
  const displayTileWidth = canvas.getBoundingClientRect().width / api.width();
  const showDistanceLabels = displayTileWidth >= 24;
  document.documentElement.dataset.distanceLabels =
    String(showDistanceLabels);
  context.fillStyle = "#111827";
  context.fillRect(0, 0, canvas.width, canvas.height);
  context.font = "12px ui-monospace, monospace";
  context.textAlign = "center";
  context.textBaseline = "middle";

  for (let y = 0; y < api.height(); ++y) {
    for (let x = 0; x < api.width(); ++x) {
      const left = x * tileWidth;
      const top = y * tileHeight;
      if (!api.tilePassable(x, y)) {
        context.fillStyle = "#334155";
        context.fillRect(left, top, tileWidth, tileHeight);
        continue;
      }
      context.strokeStyle = "rgba(148, 163, 184, 0.12)";
      context.strokeRect(left, top, tileWidth, tileHeight);
      const distance = api.tileDistance(x, y) >>> 0;
      if (distance !== unreachableDistance && showDistanceLabels) {
        context.fillStyle = "rgba(148, 163, 184, 0.58)";
        context.fillText(String(distance), left + tileWidth / 2,
          top + tileHeight / 2);
      }
    }
  }

  const goalX = api.goalX();
  const goalY = api.goalY();
  context.fillStyle = "#4ade80";
  context.fillRect(goalX * tileWidth + tileWidth * 0.2,
    goalY * tileHeight + tileHeight * 0.2, tileWidth * 0.6,
    tileHeight * 0.6);

  const positions = new Map();
  const agents = readAgents();
  for (const agent of agents) {
    const key = `${agent.x},${agent.y}`;
    const offset = positions.get(key) || 0;
    positions.set(key, offset + 1);
    const color = agent.state === 2
      ? "#fb7185"
      : agent.state === 1 ? "#4ade80" : "#38bdf8";
    context.beginPath();
    context.arc(
      (agent.x + 0.5) * tileWidth + offset * 3,
      (agent.y + 0.5) * tileHeight + offset * 3,
      Math.max(4, Math.min(tileWidth, tileHeight) * 0.3),
      0,
      Math.PI * 2,
    );
    context.fillStyle = color;
    context.fill();
    context.strokeStyle = "#0f172a";
    context.stroke();
  }

  const moving = agents.filter((agent) => agent.state === 0).length;
  const atGoal = agents.filter((agent) => agent.state === 1).length;
  const unreachable = agents.filter((agent) => agent.state === 2).length;
  movingStatus.textContent = `Moving: ${moving}`;
  goalStatus.textContent = `At goal: ${atGoal}`;
  unreachableStatus.textContent = `Unreachable: ${unreachable}`;
  summary.textContent = `Goal (${goalX}, ${goalY}) · step ${stepCount}`;
  document.documentElement.dataset.step = String(stepCount);
}

function tick() {
  const moved = api.tick();
  ++stepCount;
  document.documentElement.dataset.moved = String(moved);
  draw();
  if (moved === 0) {
    paused = true;
    setPauseLabel();
  }
}

function animate(timestamp) {
  const readyForStep = timestamp - lastStep >= stepInterval;
  if (!paused && !reducedMotion.matches && readyForStep) {
    tick();
    lastStep = timestamp;
  }
  requestAnimationFrame(animate);
}

function chooseGoal(x, y) {
  if (!api) {
    return;
  }
  if (!api.setGoal(x, y)) {
    summary.textContent = `(${x}, ${y}) is blocked; choose a passable tile.`;
    return;
  }
  goalXInput.value = String(x);
  goalYInput.value = String(y);
  for (const button of presetButtons) {
    button.setAttribute("aria-current",
      button.dataset.goalPreset === `${x},${y}` ? "true" : "false");
  }
  stepCount = 0;
  draw();
}

pauseButton.addEventListener("click", () => {
  if (reducedMotion.matches) {
    tick();
    return;
  }
  paused = !paused;
  lastStep = performance.now();
  setPauseLabel();
});

resetButton.addEventListener("click", () => {
  api.reset();
  paused = true;
  stepCount = 0;
  setPauseLabel();
  chooseGoal(api.goalX(), api.goalY());
});

for (const button of presetButtons) {
  button.addEventListener("click", () => {
    const [x, y] = button.dataset.goalPreset.split(",").map(Number);
    chooseGoal(x, y);
  });
}

setGoalButton.addEventListener("click", () => {
  chooseGoal(Number(goalXInput.value), Number(goalYInput.value));
});

canvas.addEventListener("pointerdown", (event) => {
  if (!api) {
    return;
  }
  const bounds = canvas.getBoundingClientRect();
  const x = Math.floor((event.clientX - bounds.left) / bounds.width *
    api.width());
  const y = Math.floor((event.clientY - bounds.top) / bounds.height *
    api.height());
  chooseGoal(x, y);
});

reducedMotion.addEventListener("change", () => {
  paused = true;
  setPauseLabel();
});

window.addEventListener("resize", () => {
  if (api) {
    draw();
  }
});

createTessFlowSteering().then((module) => {
  api = {
    width: bind(module, "width", "number", []),
    height: bind(module, "height", "number", []),
    reset: bind(module, "reset", null, []),
    tick: bind(module, "tick", "number", []),
    setGoal: bind(module, "set_goal", "number", ["number", "number"]),
    goalX: bind(module, "goal_x", "number", []),
    goalY: bind(module, "goal_y", "number", []),
    agentCount: bind(module, "agent_count", "number", []),
    agentX: bind(module, "agent_x", "number", ["number"]),
    agentY: bind(module, "agent_y", "number", ["number"]),
    agentState: bind(module, "agent_state", "number", ["number"]),
    tilePassable: bind(module, "tile_passable", "number",
      ["number", "number"]),
    tileDistance: bind(module, "tile_distance", "number",
      ["number", "number"]),
  };
  goalXInput.max = String(api.width() - 1);
  goalYInput.max = String(api.height() - 1);
  for (const control of interactiveControls) {
    control.disabled = false;
  }
  canvas.removeAttribute("aria-disabled");
  api.reset();
  chooseGoal(api.goalX(), api.goalY());
  setPauseLabel();
  const data = document.documentElement.dataset;
  data.tessFlowSteering = "ready";
  requestAnimationFrame(animate);
}).catch((error) => {
  summary.textContent = `The WebAssembly model could not start: ${error}`;
});
