(() => {
  "use strict";

  const root = document.documentElement;
  const canvas = document.querySelector("#canvas");
  const status = document.querySelector("#status");
  const paused = document.querySelector("#paused");
  const intensity = document.querySelector("#intensity");
  const intensityValue = document.querySelector("#intensity-value");
  const selectedX = document.querySelector("#selected-x");
  const selectedY = document.querySelector("#selected-y");
  const passable = document.querySelector("#passable");
  const verificationTimeoutMs = 30000;

  const fail = (message) => {
    root.dataset.tessDiagnostics = "failed";
    status.textContent = message;
  };

  const start = async () => {
    try {
      const module = await createTessDiagnostics({canvas});
      const api = {
        status: module.cwrap("tess_diagnostics_status", "number", []),
        setPaused: module.cwrap(
          "tess_diagnostics_set_paused", null, ["number"]),
        setIntensity: module.cwrap(
          "tess_diagnostics_set_intensity", null, ["number"]),
        select: module.cwrap(
          "tess_diagnostics_select", "number", ["number", "number"]),
        setPassable: module.cwrap(
          "tess_diagnostics_set_passable", "number",
          ["number", "number", "number"]),
        paused: module.cwrap("tess_diagnostics_paused", "number", []),
        intensity: module.cwrap(
          "tess_diagnostics_intensity", "number", []),
        selectedX: module.cwrap(
          "tess_diagnostics_selected_x", "number", []),
        selectedY: module.cwrap(
          "tess_diagnostics_selected_y", "number", []),
        selectedPassable: module.cwrap(
          "tess_diagnostics_selected_passable", "number", []),
      };

      const sync = () => {
        if (document.activeElement !== paused) {
          paused.checked = api.paused() === 1;
        }
        if (document.activeElement !== intensity) {
          intensity.value = String(api.intensity());
        }
        intensityValue.value = intensity.value;
        if (document.activeElement !== selectedX) {
          selectedX.value = String(api.selectedX());
        }
        if (document.activeElement !== selectedY) {
          selectedY.value = String(api.selectedY());
        }
        if (document.activeElement !== passable) {
          passable.checked = api.selectedPassable() === 1;
        }
      };
      const verifyMirroredControls = () => {
        if (api.select(10, 4) !== 1) {
          return false;
        }
        sync();
        const blockedIsMirrored = selectedX.value === "10" &&
          selectedY.value === "4" && !passable.checked &&
          api.selectedPassable() === 0;
        const restored = api.select(4, 4) === 1;
        sync();
        return blockedIsMirrored && restored;
      };
      if (!verifyMirroredControls()) {
        fail("Accessible controls failed their runtime verification.");
        return;
      }

      const selection = () => {
        const x = Number(selectedX.value);
        const y = Number(selectedY.value);
        const accepted = api.select(x, y) === 1;
        sync();
        return accepted;
      };
      paused.addEventListener("change", () => {
        api.setPaused(paused.checked ? 1 : 0);
        sync();
      });
      intensity.addEventListener("input", () => {
        api.setIntensity(Number(intensity.value));
        sync();
      });
      selectedX.addEventListener("change", selection);
      selectedY.addEventListener("change", selection);
      passable.addEventListener("change", () => {
        const requested = passable.checked;
        if (selection()) {
          api.setPassable(
            Number(selectedX.value), Number(selectedY.value),
            requested ? 1 : 0);
          sync();
        }
      });
      sync();
      window.setInterval(sync, 100);

      const deadline = performance.now() + verificationTimeoutMs;
      const verify = () => {
        const result = api.status();
        if (result === 1) {
          root.dataset.tessDiagnostics = "ready";
          status.textContent = "Diagnostics are live.";
          return;
        }
        if (result < 0 || performance.now() >= deadline) {
          fail("Diagnostics failed their runtime verification.");
          return;
        }
        window.setTimeout(verify, 50);
      };
      verify();
    } catch (error) {
      fail(`Unable to start diagnostics: ${error}`);
    }
  };

  start();
})();
