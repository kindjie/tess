"use strict";

const root = document.documentElement;
const resultBody = document.querySelector("#strategy-results");
const phase = document.querySelector("#phase");
const pause = document.querySelector("#pause");
const replay = document.querySelector("#replay");
const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");

const names = [
  "Independent A*",
  "Exact route cache",
  "Weighted batch",
  "Distance field",
];
const expectedRequests = [3, 2, 3, 3];
const expectedStarts = [
  [[0, 0], [0, 1], [0, 2]],
  [[0, 0], [0, 0]],
  [[0, 0], [0, 1], [0, 2]],
  [[0, 0], [0, 1], [0, 2]],
];
const calls = [
  "Three calls: astar_path once for each start.",
  "Two calls: the first misses; the exact repeat hits the cache.",
  "One weighted_path_batch call internally groups three requests; its " +
    "returned paths are scratch-backed.",
  "Four calls: build one field, then read it once for each of three starts.",
];
const phaseText = [
  ["Ready to run three searches.", "A* search 1 of 3.",
    "A* search 2 of 3.", "A* search 3 of 3: all routes returned."],
  ["Ready for an exact repeat.", "First call: cache miss and A* search.",
    "Second call: cache hit, zero search expansions.",
    "Exact route reused."],
  ["Ready to submit one batch.",
    "One batch call groups all three requests by goal.",
    "One shared field build returns three scratch-backed paths.",
    "Batch complete: one field build, zero A* fallbacks."],
  ["Ready for caller-controlled reuse.", "Caller builds one distance field.",
    "Caller reads paths 1 and 2 from the field.",
    "Caller reads path 3: one build, three reads."],
];
const announcements = [
  "Ready: compare the four operation chains.",
  "First searches and field-building operations run.",
  "Reuse becomes visible in the cache, batch, and distance field.",
  "Complete: all returned routes are shown.",
];
const fieldRevealCounts = [0, 0, 2, 3];

let snapshots = [];
let animationStage = 0;
let animationTimer = null;
let paused = false;
let lastAnnouncement = "";

function wrap(module, name, args = []) {
  return module.cwrap(name, "number", args);
}

function bindApi(module) {
  return {
    readiness: wrap(module, "tess_strategies_readiness"),
    width: wrap(module, "tess_strategies_width"),
    height: wrap(module, "tess_strategies_height"),
    count: wrap(module, "tess_strategies_count"),
    tilePassable: wrap(module, "tess_strategies_tile_passable",
      ["number", "number"]),
    requestCount: wrap(module, "tess_strategies_request_count", ["number"]),
    pathStatus: wrap(module, "tess_strategies_path_status",
      ["number", "number"]),
    pathCost: wrap(module, "tess_strategies_path_cost",
      ["number", "number"]),
    pathExpansions: wrap(module, "tess_strategies_path_expansions",
      ["number", "number"]),
    pathSize: wrap(module, "tess_strategies_path_size",
      ["number", "number"]),
    pathX: wrap(module, "tess_strategies_path_x",
      ["number", "number", "number"]),
    pathY: wrap(module, "tess_strategies_path_y",
      ["number", "number", "number"]),
    cacheHits: wrap(module, "tess_strategies_cache_hits"),
    cacheMisses: wrap(module, "tess_strategies_cache_misses"),
    batchUniqueGoals: wrap(module, "tess_strategies_batch_unique_goals"),
    batchFieldBuilds: wrap(module, "tess_strategies_batch_field_builds"),
    batchFallbacks: wrap(module, "tess_strategies_batch_fallbacks"),
    fieldBuilds: wrap(module, "tess_strategies_field_builds"),
    fieldExpansions: wrap(module, "tess_strategies_field_expansions"),
    fieldReachedNodes: wrap(module, "tess_strategies_field_reached_nodes"),
  };
}

function requireValue(condition, message) {
  if (!condition) throw new Error(message);
}

function readPath(api, strategy, request, width, height) {
  const status = api.pathStatus(strategy, request);
  const cost = api.pathCost(strategy, request);
  const expansions = api.pathExpansions(strategy, request);
  const size = api.pathSize(strategy, request);
  requireValue(status === 1, `Unexpected status for ${strategy}:${request}`);
  requireValue(cost >= 0 && expansions >= 0 && size > 0 && size <= 256,
    `Invalid scalar result for ${strategy}:${request}`);
  const path = [];
  for (let point = 0; point < size; ++point) {
    const x = api.pathX(strategy, request, point);
    const y = api.pathY(strategy, request, point);
    requireValue(x >= 0 && x < width && y >= 0 && y < height,
      `Invalid path coordinate for ${strategy}:${request}:${point}`);
    path.push({x, y});
  }
  const [startX, startY] = expectedStarts[strategy][request];
  requireValue(path[0].x === startX && path[0].y === startY,
    `Unexpected start for ${strategy}:${request}`);
  requireValue(path.at(-1).x === 15 && path.at(-1).y === 15,
    `Unexpected goal for ${strategy}:${request}`);
  requireValue(size === cost + 1,
    `Unexpected path length for ${strategy}:${request}`);
  return {status, cost, expansions, path};
}

function validateSnapshot(api) {
  requireValue(api.readiness() === 1, "C++ model did not become ready");
  const width = api.width();
  const height = api.height();
  requireValue(width === 16 && height === 16 && api.count() === 4,
    "Unexpected strategy world shape");
  const passability = [];
  for (let y = 0; y < height; ++y) {
    for (let x = 0; x < width; ++x) {
      const tile = api.tilePassable(x, y);
      requireValue(tile === 0 || tile === 1,
        `Invalid map tile at ${x}:${y}`);
      passability.push(tile === 1);
    }
  }
  requireValue(passability.filter((tile) => !tile).length === 45,
    "Unexpected obstacle map");
  const values = [];
  for (let strategy = 0; strategy < 4; ++strategy) {
    const requestCount = api.requestCount(strategy);
    requireValue(requestCount === expectedRequests[strategy],
      `Unexpected request count for ${strategy}`);
    const requests = [];
    for (let request = 0; request < requestCount; ++request) {
      requests.push(readPath(api, strategy, request, width, height));
    }
    values.push({name: names[strategy], requests});
  }
  const metrics = {
    cacheHits: api.cacheHits(),
    cacheMisses: api.cacheMisses(),
    batchUniqueGoals: api.batchUniqueGoals(),
    batchFieldBuilds: api.batchFieldBuilds(),
    batchFallbacks: api.batchFallbacks(),
    fieldBuilds: api.fieldBuilds(),
    fieldExpansions: api.fieldExpansions(),
    fieldReachedNodes: api.fieldReachedNodes(),
  };
  requireValue(metrics.cacheHits === 1, "Cache hits: 1 expected");
  requireValue(metrics.cacheMisses === 1, "Cache misses: 1 expected");
  requireValue(metrics.batchUniqueGoals === 1,
    "Unique batch goals: 1 expected");
  requireValue(metrics.batchFieldBuilds === 1, "Field builds: 1 expected");
  requireValue(metrics.batchFallbacks === 0, "A* fallbacks: 0 expected");
  requireValue(metrics.fieldBuilds === 1 && metrics.fieldExpansions > 0 &&
    metrics.fieldReachedNodes === passability.filter(Boolean).length,
    "Distance-field build evidence missing");
  values.metrics = metrics;
  values.passability = passability;
  return values;
}

function createTextCell(text) {
  const cell = document.createElement("td");
  cell.textContent = text;
  return cell;
}

function pathSummary(snapshot) {
  return snapshot.requests.map((request) => {
    const first = request.path[0];
    const last = request.path.at(-1);
    return `(${first.x},${first.y}) to (${last.x},${last.y}): ` +
      `${request.cost} steps, Found`;
  }).join("; ");
}

function reuseFacts(strategy, values) {
  const metrics = values.metrics;
  if (strategy === 0) {
    return values[0].requests.map((request, index) =>
      `search ${index + 1}: ${request.expansions} A* expansions`).join("; ");
  }
  if (strategy === 1) {
    return `Cache hits: ${metrics.cacheHits}; ` +
      `Cache misses: ${metrics.cacheMisses}; first/repeat expansions: ` +
      `${values[1].requests[0].expansions}/` +
      `${values[1].requests[1].expansions}`;
  }
  if (strategy === 2) {
    return `Unique goals: ${metrics.batchUniqueGoals}; ` +
      `Field builds: ${metrics.batchFieldBuilds}; ` +
      `A* fallbacks: ${metrics.batchFallbacks}`;
  }
  return `Field builds: ${metrics.fieldBuilds}; reachable labels: ` +
    `${metrics.fieldReachedNodes}; build expansions: ` +
    `${metrics.fieldExpansions}`;
}

function renderTable(values) {
  resultBody.replaceChildren();
  values.forEach((snapshot, strategy) => {
    const row = document.createElement("tr");
    row.append(createTextCell(snapshot.name));
    row.append(createTextCell(calls[strategy]));
    row.append(createTextCell(
      `${pathSummary(snapshot)}. ${reuseFacts(strategy, values)}.`));
    resultBody.append(row);
  });
  resultBody.dataset.cacheHits = String(values.metrics.cacheHits);
  resultBody.dataset.cacheMisses = String(values.metrics.cacheMisses);
  resultBody.dataset.batchFieldBuilds =
    String(values.metrics.batchFieldBuilds);
  resultBody.dataset.batchFallbacks = String(values.metrics.batchFallbacks);
  resultBody.dataset.fieldReachedNodes =
    String(values.metrics.fieldReachedNodes);
}

function updateTopology(values) {
  const setText = (metric, text) => {
    document.querySelector(`[data-metric="${metric}"]`).textContent = text;
  };
  values[0].requests.forEach((request, index) => {
    setText(`astar-${index}`,
      `Search ${index + 1} · ${request.expansions} expansions`);
  });
  setText("cache-miss",
    `Miss · ${values[1].requests[0].expansions} expansions`);
  setText("cache-hit",
    `Hit · ${values[1].requests[1].expansions} expansions`);
  setText("batch-inputs", `${values[2].requests.length} requests`);
  setText("batch-field",
    `${values.metrics.batchFieldBuilds} grouped field build`);
  setText("batch-results",
    `${values[2].requests.length} scratch-backed paths`);
  setText("field-build",
    `Build · ${values.metrics.fieldReachedNodes} tile labels`);
  setText("field-coverage",
    `${values.metrics.fieldReachedNodes} reachable tiles`);
}

function initializeGrids(passability) {
  document.querySelectorAll("[data-grid]").forEach((grid) => {
    const fragment = document.createDocumentFragment();
    for (let index = 0; index < 256; ++index) {
      const tile = document.createElement("span");
      tile.className = "tile";
      tile.dataset.x = String(index % 16);
      tile.dataset.y = String(Math.floor(index / 16));
      if (!passability[index]) tile.classList.add("impassable");
      fragment.append(tile);
    }
    grid.replaceChildren(fragment);
  });
  const impassableCounts = [...document.querySelectorAll("[data-grid]")].map(
    (grid) => grid.querySelectorAll(".tile.impassable").length);
  requireValue(impassableCounts.every((count) => count === 45),
    "Impassable tiles were not rendered consistently");
  resultBody.dataset.impassableTilesPerGrid = "45";
  resultBody.dataset.passableTiles =
    String(passability.filter(Boolean).length);
}

function clearRoutes(card) {
  card.classList.remove("is-working", "is-cache-hit");
  card.querySelectorAll(".tile").forEach((tile) => {
    tile.classList.remove("route-0", "route-1", "route-2", "start", "goal");
  });
}

function revealRequest(card, request, requestIndex) {
  request.path.forEach(({x, y}, pointIndex) => {
    const tile = card.querySelector(
      `.tile[data-x="${x}"][data-y="${y}"]`);
    tile.classList.add(`route-${requestIndex % 3}`);
    if (pointIndex === 0) tile.classList.add("start");
    if (pointIndex === request.path.length - 1) tile.classList.add("goal");
  });
}

function setAnnouncement(text) {
  if (text !== lastAnnouncement) {
    phase.textContent = text;
    lastAnnouncement = text;
  }
}

function renderStage(stage, announce = true) {
  snapshots.forEach((snapshot, strategy) => {
    const card = document.querySelector(`[data-card="${strategy}"]`);
    clearRoutes(card);
    card.querySelectorAll(".tile:not(.impassable)").forEach((tile) => {
      tile.classList.toggle("field-covered", strategy === 3 && stage >= 1);
    });
    let revealCount = 0;
    if (strategy === 0) revealCount = Math.min(stage, 3);
    if (strategy === 1) revealCount = stage >= 1 ? 1 : 0;
    if (strategy === 2) revealCount = stage >= 2 ? 3 : 0;
    if (strategy === 3) revealCount = fieldRevealCounts[stage];
    for (let request = 0; request < revealCount; ++request) {
      revealRequest(card, snapshot.requests[request], request);
    }
    if (stage > 0 && stage < 3) card.classList.add("is-working");
    if (strategy === 1 && stage >= 2) card.classList.add("is-cache-hit");
    card.querySelector("[data-card-phase]").textContent =
      phaseText[strategy][stage];
    card.querySelectorAll(".topology-step").forEach((step) => {
      const revealStage = Number(step.dataset.reveal);
      step.classList.toggle("is-complete", stage >= revealStage);
      step.classList.toggle("is-active", stage === revealStage);
    });
  });
  if (announce) {
    setAnnouncement(`Phase ${stage + 1} of 4: ${announcements[stage]}`);
  }
  if (stage === 3) {
    const fieldCard = document.querySelector('[data-card="3"]');
    const covered = fieldCard.querySelectorAll(".tile.field-covered").length;
    const thirdRoute = fieldCard.querySelectorAll(".tile.route-2").length;
    requireValue(covered === snapshots.metrics.fieldReachedNodes,
      "Distance-field coverage was not rendered completely");
    requireValue(thirdRoute > 0,
      "The third distance-field route was not rendered");
    resultBody.dataset.distanceFieldCoveredTiles = String(covered);
    resultBody.dataset.distanceFieldThirdRoute = "ready";
  } else {
    delete resultBody.dataset.distanceFieldCoveredTiles;
    delete resultBody.dataset.distanceFieldThirdRoute;
  }
  syncPauseControl();
}

function stopTimer() {
  if (animationTimer !== null) window.clearInterval(animationTimer);
  animationTimer = null;
}

function syncPauseControl() {
  pause.hidden = reducedMotion.matches;
  pause.disabled = reducedMotion.matches ||
    (!paused && animationStage >= 3);
}

function setPaused(nextPaused) {
  paused = nextPaused;
  pause.setAttribute("aria-pressed", String(paused));
  pause.textContent = paused ? "Resume" : "Pause";
  syncPauseControl();
}

function startTimer() {
  stopTimer();
  if (paused || reducedMotion.matches || animationStage >= 3) {
    syncPauseControl();
    return;
  }
  syncPauseControl();
  animationTimer = window.setInterval(() => {
    animationStage += 1;
    renderStage(animationStage);
    if (animationStage >= 3) stopTimer();
  }, 1500);
}

pause.addEventListener("click", () => {
  setPaused(!paused);
  if (paused) stopTimer();
  else startTimer();
});

replay.addEventListener("click", () => {
  setPaused(false);
  animationStage = reducedMotion.matches ? 3 : 0;
  renderStage(animationStage);
  startTimer();
});

reducedMotion.addEventListener("change", () => {
  if (root.dataset.tessStrategies !== "ready") return;
  stopTimer();
  setPaused(false);
  if (reducedMotion.matches) {
    animationStage = 3;
    renderStage(animationStage);
  }
});

async function initializeStrategies() {
  try {
    const module = await createTessStrategies();
    const api = bindApi(module);
    snapshots = validateSnapshot(api);
    initializeGrids(snapshots.passability);
    renderTable(snapshots);
    updateTopology(snapshots);
    animationStage = reducedMotion.matches ? 3 : 0;
    renderStage(animationStage);
    root.dataset.tessStrategies = "ready";
    replay.disabled = false;
    startTimer();
  } catch (error) {
    root.dataset.tessStrategies = "failed";
    setAnnouncement("Could not validate the C++ WebAssembly results");
    console.error(error);
  }
}

void initializeStrategies();
