"use strict";

const canvas = document.querySelector("#world");
const context = canvas.getContext("2d");
const message = document.querySelector("#message");
const metrics = document.querySelector("#metrics");
const buttons = [...document.querySelectorAll("[data-tool]")];

let api;
let width = 32;
let height = 24;
let tool = "wall";
let dragging = false;
let paintValue = true;
let start = {x: 2, y: 12};
let goal = {x: 29, y: 12};
let walls = new Set();
let route = [];

const key = (x, y) => `${x},${y}`;

function findPath() {
  const length = api.find(start.x, start.y, goal.x, goal.y);
  route = [];
  for (let index = 0; index < length; ++index) {
    route.push({x: api.pathX(index), y: api.pathY(index)});
  }
  message.textContent = length > 0 ? "Route found" : "No route exists";
  metrics.textContent = length > 0 ? `${length - 1} steps` : "0 steps";
}

function draw() {
  const cellWidth = canvas.width / width;
  const cellHeight = canvas.height / height;
  context.fillStyle = "#12101a";
  context.fillRect(0, 0, canvas.width, canvas.height);

  context.fillStyle = "#4d455f";
  for (const wall of walls) {
    const [x, y] = wall.split(",").map(Number);
    context.fillRect(x * cellWidth, y * cellHeight, cellWidth, cellHeight);
  }

  context.strokeStyle = "#7d68b7";
  context.lineWidth = Math.max(4, cellWidth * 0.2);
  context.lineCap = "round";
  context.lineJoin = "round";
  if (route.length > 1) {
    context.beginPath();
    route.forEach(({x, y}, index) => {
      const px = (x + 0.5) * cellWidth;
      const py = (y + 0.5) * cellHeight;
      if (index === 0) context.moveTo(px, py);
      else context.lineTo(px, py);
    });
    context.stroke();
  }

  for (let x = 0; x <= width; ++x) {
    context.strokeStyle = "#282232";
    context.lineWidth = 1;
    context.beginPath();
    context.moveTo(x * cellWidth, 0);
    context.lineTo(x * cellWidth, canvas.height);
    context.stroke();
  }
  for (let y = 0; y <= height; ++y) {
    context.beginPath();
    context.moveTo(0, y * cellHeight);
    context.lineTo(canvas.width, y * cellHeight);
    context.stroke();
  }

  for (const [point, color] of [[start, "#60dfbe"], [goal, "#ffb454"]]) {
    context.fillStyle = color;
    context.beginPath();
    context.arc((point.x + 0.5) * cellWidth, (point.y + 0.5) * cellHeight,
      Math.min(cellWidth, cellHeight) * 0.3, 0, Math.PI * 2);
    context.fill();
  }
}

function update() {
  findPath();
  draw();
}

function cellAt(event) {
  const bounds = canvas.getBoundingClientRect();
  return {
    x: Math.max(0, Math.min(width - 1,
      Math.floor((event.clientX - bounds.left) / bounds.width * width))),
    y: Math.max(0, Math.min(height - 1,
      Math.floor((event.clientY - bounds.top) / bounds.height * height))),
  };
}

function applyTool(event) {
  if (!api) return;
  const cell = cellAt(event);
  const cellKey = key(cell.x, cell.y);
  if (tool === "start") {
    if (!walls.has(cellKey) && cellKey !== key(goal.x, goal.y)) start = cell;
  } else if (tool === "goal") {
    if (!walls.has(cellKey) && cellKey !== key(start.x, start.y)) goal = cell;
  } else if (cellKey !== key(start.x, start.y) &&
             cellKey !== key(goal.x, goal.y)) {
    api.setBlocked(cell.x, cell.y, paintValue ? 1 : 0);
    if (paintValue) walls.add(cellKey);
    else walls.delete(cellKey);
  }
  update();
}

canvas.addEventListener("pointerdown", (event) => {
  dragging = true;
  canvas.setPointerCapture(event.pointerId);
  const cell = cellAt(event);
  paintValue = !walls.has(key(cell.x, cell.y));
  applyTool(event);
});
canvas.addEventListener("pointermove", (event) => {
  if (dragging && tool === "wall") applyTool(event);
});
canvas.addEventListener("pointerup", () => { dragging = false; });

buttons.forEach((button) => button.addEventListener("click", () => {
  tool = button.dataset.tool;
  buttons.forEach((item) => item.classList.toggle("active", item === button));
}));

document.querySelector("#clear").addEventListener("click", () => {
  api.reset();
  walls = new Set();
  update();
});

createTessDemo().then((module) => {
  api = {
    width: module.cwrap("tess_demo_width", "number", []),
    height: module.cwrap("tess_demo_height", "number", []),
    reset: module.cwrap("tess_demo_reset", null, []),
    setBlocked: module.cwrap("tess_demo_set_blocked", "number",
      ["number", "number", "number"]),
    find: module.cwrap("tess_demo_find_path", "number",
      ["number", "number", "number", "number"]),
    pathX: module.cwrap("tess_demo_path_x", "number", ["number"]),
    pathY: module.cwrap("tess_demo_path_y", "number", ["number"]),
  };
  width = api.width();
  height = api.height();
  api.reset();
  update();
  document.documentElement.dataset.tessDemo = "ready";
}).catch((error) => {
  document.documentElement.dataset.tessDemo = "failed";
  message.textContent = "Could not load the WebAssembly demo";
  console.error(error);
});
