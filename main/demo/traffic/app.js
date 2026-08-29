'use strict';

const canvas = document.getElementById('traffic-world');
const ctx = canvas.getContext('2d');
const terrainLayer = document.createElement('canvas');
const terrainCtx = terrainLayer.getContext('2d');
const scenario = document.getElementById('scenario');
const resetButton = document.getElementById('reset');
const measurementButton = document.getElementById('measurement-snapshot');
const measurementOutput = document.getElementById('measurement-output');
const message = document.getElementById('message');
const metrics = document.getElementById('metrics');
const query = new URLSearchParams(window.location.search);
const requestedScenario = query.get('scenario');
const scenarioValues = {
  aligned: '0',
  'shuffled-crossing': '1',
  funnel: '2',
  'multi-gate': '3',
};
if (Object.hasOwn(scenarioValues, requestedScenario)) {
  scenario.value = scenarioValues[requestedScenario];
}

let api = null;
let module = null;
let width = 0;
let height = 0;
let count = 0;
let lastTimestamp = 0;
let emaUpdateUs = 0;
let emaPlanningUs = 0;
let emaRenderMs = 0;
let emaFrameMs = 0;
const measurementEnabled = query.get('measure') === '1';
const measurementCapacity = 4096;

class BoundedSamples {
  constructor(capacity) {
    this.data = new Float64Array(capacity);
    this.capacity = capacity;
    this.count = 0;
  }

  push(value) {
    if (this.count === this.capacity) {
      return;
    }
    this.data[this.count] = value;
    this.count += 1;
  }

  clear() {
    this.count = 0;
  }

  values() {
    return Array.from(this.data.subarray(0, this.count));
  }
}

const measurement = measurementEnabled ? {
  updateUs: new BoundedSamples(measurementCapacity),
  planningUs: new BoundedSamples(measurementCapacity),
  planningQueries: new BoundedSamples(measurementCapacity),
  renderMs: new BoundedSamples(measurementCapacity),
  frameMs: new BoundedSamples(measurementCapacity),
  catchupFrames: 0,
} : null;

function nearestRank(values, quantile) {
  const ordered = [...values].sort((left, right) => left - right);
  return ordered[Math.max(0, Math.ceil(quantile * ordered.length) - 1)];
}

function summarizeSamples(samples) {
  const values = samples.values();
  return {
    samples: values.length,
    minimum: values.length > 0 ? Math.min(...values) : null,
    p50: values.length >= 20 ? nearestRank(values, 0.50) : null,
    p95: values.length >= 200 ? nearestRank(values, 0.95) : null,
    p99: values.length >= 2000 ? nearestRank(values, 0.99) : null,
    maximum: values.length > 0 ? Math.max(...values) : null,
    raw: values,
  };
}

function measurementSnapshot() {
  return {
    scenario: Number(scenario.value),
    wasmMemoryBytes: module.HEAPU8.buffer.byteLength,
    capacity: measurementCapacity,
    catchupFrames: measurement.catchupFrames,
    updateUs: summarizeSamples(measurement.updateUs),
    planningUs: summarizeSamples(measurement.planningUs),
    planningQueries: measurement.planningQueries.values(),
    renderMs: summarizeSamples(measurement.renderMs),
    frameMs: summarizeSamples(measurement.frameMs),
  };
}

function measurementSummary() {
  const snapshot = measurementSnapshot();
  for (const family of [
    snapshot.updateUs,
    snapshot.planningUs,
    snapshot.renderMs,
    snapshot.frameMs,
  ]) {
    delete family.raw;
  }
  delete snapshot.planningQueries;
  return snapshot;
}

function clearMeasurement() {
  if (!measurement) {
    return;
  }
  for (const samples of [
    measurement.updateUs,
    measurement.planningUs,
    measurement.planningQueries,
    measurement.renderMs,
    measurement.frameMs,
  ]) {
    samples.clear();
  }
  measurement.catchupFrames = 0;
  measurementOutput.textContent = '';
  measurementOutput.hidden = true;
}

const smooth = (current, sample) =>
  current === 0 ? sample : current * 0.9 + sample * 0.1;

function cacheTerrain() {
  terrainLayer.width = width;
  terrainLayer.height = height;
  const terrainPtr = api.terrain();
  const terrain = module.HEAPU8.subarray(
      terrainPtr, terrainPtr + width * height);
  const image = terrainCtx.createImageData(width, height);
  for (let i = 0; i < terrain.length; i += 1) {
    const wall = terrain[i] !== 0;
    const pixel = i * 4;
    image.data[pixel] = wall ? 84 : 16;
    image.data[pixel + 1] = wall ? 98 : 20;
    image.data[pixel + 2] = wall ? 126 : 28;
    image.data[pixel + 3] = 255;
  }
  terrainCtx.putImageData(image, 0, 0);
}

function reset() {
  api.reset(Number(scenario.value));
  cacheTerrain();
  lastTimestamp = 0;
  emaUpdateUs = 0;
  emaPlanningUs = 0;
  emaRenderMs = 0;
  emaFrameMs = 0;
  clearMeasurement();
}

function draw() {
  const renderBegin = performance.now();
  ctx.drawImage(terrainLayer, 0, 0);
  const currentPtr = api.agents();
  const previousPtr = api.previousAgents();
  const current = module.HEAP16.subarray(
      currentPtr / 2, currentPtr / 2 + count * 2);
  const previous = module.HEAP16.subarray(
      previousPtr / 2, previousPtr / 2 + count * 2);
  const alpha = api.interpolationAlpha();
  ctx.fillStyle = '#60dfbe';
  for (let i = 0; i < count; i += 1) {
    const x = previous[i * 2] +
        (current[i * 2] - previous[i * 2]) * alpha;
    const y = previous[i * 2 + 1] +
        (current[i * 2 + 1] - previous[i * 2 + 1]) * alpha;
    ctx.fillRect(x, y, 2, 2);
  }
  return performance.now() - renderBegin;
}

function frame(timestamp) {
  const frameBegin = performance.now();
  try {
    const dt = lastTimestamp === 0 ?
        0 : Math.max(0, Math.min((timestamp - lastTimestamp) / 1000, 0.25));
    lastTimestamp = timestamp;
    const updateUs = api.tick(dt);
    const fixedTicks = api.fixedTicksLastCall();
    if (updateUs >= 0) {
      emaUpdateUs = smooth(emaUpdateUs, updateUs);
      emaPlanningUs = smooth(emaPlanningUs, api.planningUs());
    }
    const renderMs = draw();
    emaRenderMs = smooth(emaRenderMs, renderMs);
    const pending = api.planningPending();
    const blocked = api.blocked();
    const arrived = api.arrived();
    const waits = api.waits();
    const advanced = api.advanced();
    const streak = api.oneProgressStreak();
    const longest = api.longestOneProgressStreak();
    message.textContent = pending > 0 ?
        `Planning ${pending} remaining routes` :
        `${arrived}/${count} agents arrived`;
    const memoryMiB = module.HEAPU8.buffer.byteLength / (1024 * 1024);
    if (measurement) {
      measurement.renderMs.push(renderMs);
      if (fixedTicks === 1) {
        measurement.updateUs.push(updateUs);
        measurement.planningUs.push(api.planningUs());
        measurement.planningQueries.push(api.planningQueries());
      } else if (fixedTicks > 1) {
        measurement.catchupFrames += 1;
      }
    }
    const captureCount = measurement ? Math.min(
      measurement.frameMs.count + 1, measurement.frameMs.capacity) : 0;
    const captureText = measurement ?
        ` · capture ${captureCount} frames (measure=1)` : '';
    metrics.textContent =
        `C++ update ${emaUpdateUs.toFixed(0)} µs · ` +
        `planning ${emaPlanningUs.toFixed(0)} µs · ` +
        `render ${emaRenderMs.toFixed(2)} ms · ` +
        `frame ${emaFrameMs.toFixed(2)} ms · ` +
        `${waits} waits · ${blocked} blocked · ${advanced} moved · ` +
        `one-agent streak ${streak} (max ${longest}) · ` +
        `${memoryMiB.toFixed(1)} MiB Wasm memory${captureText}`;
    window.requestAnimationFrame(frame);
    const frameMs = performance.now() - frameBegin;
    emaFrameMs = smooth(emaFrameMs, frameMs);
    if (measurement) {
      measurement.frameMs.push(frameMs);
    }
  } catch (error) {
    document.documentElement.dataset.tessTraffic = 'failed';
    message.textContent = `Traffic Lab failed: ${error}`;
    return;
  }
}

createTessTraffic()
    .then((instance) => {
      module = instance;
      api = {
        width: instance.cwrap('tess_traffic_width', 'number', []),
        height: instance.cwrap('tess_traffic_height', 'number', []),
        agentCount: instance.cwrap(
            'tess_traffic_agent_count', 'number', []),
        reset: instance.cwrap('tess_traffic_reset', 'number', ['number']),
        tick: instance.cwrap('tess_traffic_tick', 'number', ['number']),
        terrain: instance.cwrap('tess_traffic_terrain', 'number', []),
        agents: instance.cwrap('tess_traffic_agents', 'number', []),
        previousAgents: instance.cwrap(
            'tess_traffic_previous_agents', 'number', []),
        interpolationAlpha: instance.cwrap(
            'tess_traffic_interpolation_alpha', 'number', []),
        planningUs: instance.cwrap(
            'tess_traffic_planning_us', 'number', []),
        planningQueries: instance.cwrap(
            'tess_traffic_planning_queries', 'number', []),
        fixedTicksLastCall: instance.cwrap(
            'tess_traffic_fixed_ticks', 'number', []),
        planningPending: instance.cwrap(
            'tess_traffic_planning_pending', 'number', []),
        advanced: instance.cwrap('tess_traffic_advanced', 'number', []),
        waits: instance.cwrap('tess_traffic_waits', 'number', []),
        blocked: instance.cwrap('tess_traffic_blocked', 'number', []),
        arrived: instance.cwrap('tess_traffic_arrived', 'number', []),
        oneProgressStreak: instance.cwrap(
            'tess_traffic_one_progress_streak', 'number', []),
        longestOneProgressStreak: instance.cwrap(
            'tess_traffic_longest_one_progress_streak', 'number', []),
      };
      width = api.width();
      height = api.height();
      count = api.agentCount();
      reset();
      if (measurement) {
        window.tessTrafficMetrics = {snapshot: measurementSnapshot};
        document.documentElement.dataset.tessTrafficMeasurement = 'active';
        measurementButton.hidden = false;
        measurementButton.addEventListener('click', () => {
          measurementOutput.textContent =
              JSON.stringify(measurementSummary(), null, 2);
          measurementOutput.hidden = false;
        });
      }
      scenario.addEventListener('change', reset);
      resetButton.addEventListener('click', reset);
      document.documentElement.dataset.tessTraffic = 'ready';
      window.requestAnimationFrame(frame);
    })
    .catch((error) => {
      document.documentElement.dataset.tessTraffic = 'failed';
      message.textContent = `Failed to load: ${error}`;
    });
