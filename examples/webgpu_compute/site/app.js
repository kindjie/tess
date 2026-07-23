"use strict";

const message = document.querySelector("#message");

createTessWebGpu().then((module) => {
  const status = module.cwrap("tess_webgpu_status", "number", []);
  const started = performance.now();
  const poll = () => {
    const result = status();
    if (result === 1) {
      document.documentElement.dataset.tessWebgpu = "ready";
      message.textContent = "WebGPU compute and summary readback verified";
    } else if (result === -1) {
      document.documentElement.dataset.tessWebgpu = "unsupported";
      message.textContent = "WebGPU is unavailable in this browser";
    } else if (result < -1) {
      document.documentElement.dataset.tessWebgpu = "failed";
      message.textContent = `WebGPU compute verification failed (${result})`;
    } else if (performance.now() - started > 10000) {
      document.documentElement.dataset.tessWebgpu = "unsupported";
      message.textContent = "No WebGPU adapter became available";
    } else {
      setTimeout(poll, 25);
    }
  };
  poll();
}).catch((error) => {
  document.documentElement.dataset.tessWebgpu = "failed";
  message.textContent = "Could not load the WebGPU smoke test";
  console.error(error);
});
