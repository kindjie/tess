(() => {
  "use strict";

  const root = document.documentElement;
  const canvas = document.querySelector("#canvas");
  const status = document.querySelector("#status");
  const paused = document.querySelector("#paused");
  const reset = document.querySelector("#reset");
  const selectedX = document.querySelector("#selected-x");
  const selectedY = document.querySelector("#selected-y");
  const passable = document.querySelector("#passable");
  const flowAdmission = document.querySelector("#flow-admission");
  const flowRetention = document.querySelector("#flow-retention");
  const flowOutstanding = document.querySelector("#flow-outstanding");
  const planningWork = document.querySelector("#planning-work");
  const queuedWork = document.querySelector("#queued-work");
  const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");
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
        reset: module.cwrap("tess_diagnostics_reset", null, []),
        select: module.cwrap(
          "tess_diagnostics_select", "number", ["number", "number"]),
        setPassable: module.cwrap(
          "tess_diagnostics_set_passable", "number",
          ["number", "number", "number"]),
        paused: module.cwrap("tess_diagnostics_paused", "number", []),
        selectedX: module.cwrap(
          "tess_diagnostics_selected_x", "number", []),
        selectedY: module.cwrap(
          "tess_diagnostics_selected_y", "number", []),
        selectedPassable: module.cwrap(
          "tess_diagnostics_selected_passable", "number", []),
        fixedTicks: module.cwrap(
          "tess_diagnostics_fixed_ticks", "number", []),
        pathPassabilityChecks: module.cwrap(
          "tess_diagnostics_path_passability_checks", "number", []),
        queuedPhaseCalls: module.cwrap(
          "tess_diagnostics_queued_phase_calls", "number", []),
        queuedDirtyMerged: module.cwrap(
          "tess_diagnostics_queued_dirty_merged", "number", []),
        planningQueries: module.cwrap(
          "tess_diagnostics_planning_queries", "number", []),
        planningExpansions: module.cwrap(
          "tess_diagnostics_planning_expansions", "number", []),
        flowOffered: module.cwrap(
          "tess_diagnostics_flow_offered", "number", []),
        flowAdmitted: module.cwrap(
          "tess_diagnostics_flow_admitted", "number", []),
        flowTerminal: module.cwrap(
          "tess_diagnostics_flow_terminal", "number", []),
        flowOutstanding: module.cwrap(
          "tess_diagnostics_flow_outstanding", "number", []),
        flowHighWater: module.cwrap(
          "tess_diagnostics_flow_high_water", "number", []),
        flowAdmissionOk: module.cwrap(
          "tess_diagnostics_flow_admission_ok", "number", []),
        flowRetentionOk: module.cwrap(
          "tess_diagnostics_flow_retention_ok", "number", []),
      };

      const sync = () => {
        if (document.activeElement !== paused) {
          paused.checked = api.paused() === 1;
        }
        if (document.activeElement !== selectedX) {
          selectedX.value = String(api.selectedX());
        }
        if (document.activeElement !== selectedY) {
          selectedY.value = String(api.selectedY());
        }
        if (document.activeElement !== passable) {
          passable.checked = api.selectedPassable() === 1;
        }
        const offered = api.flowOffered();
        const admitted = api.flowAdmitted();
        const terminal = api.flowTerminal();
        const outstanding = api.flowOutstanding();
        flowAdmission.textContent =
          `${offered} offered = ${admitted} admitted; ` +
          (api.flowAdmissionOk() === 1 ? "holds" : "broken");
        flowRetention.textContent =
          `${admitted} admitted = ${terminal} terminal + ` +
          `${outstanding} outstanding; ` +
          (api.flowRetentionOk() === 1 ? "holds" : "broken");
        flowOutstanding.textContent =
          `${outstanding} / ${api.flowHighWater()}`;
        planningWork.textContent =
          `${api.planningQueries()} queries; ` +
          `${api.planningExpansions()} expansions`;
        queuedWork.textContent =
          `${api.queuedPhaseCalls()} calls; ` +
          `${api.queuedDirtyMerged()} dirty chunks`;
        root.dataset.fixedTicks = String(api.fixedTicks());
        root.dataset.pathPassabilityChecks =
          String(api.pathPassabilityChecks());
        root.dataset.queuedPhaseCalls = String(api.queuedPhaseCalls());
        root.dataset.flowOutstanding = String(outstanding);
      };

      const verifyMirroredControls = () => {
        if (api.select(64, 48) !== 1) {
          return false;
        }
        sync();
        return selectedX.value === "64" && selectedY.value === "48" &&
          !passable.checked && api.selectedPassable() === 0;
      };
      if (!verifyMirroredControls()) {
        fail("Accessible controls failed their runtime verification.");
        return;
      }

      const selection = () => {
        const accepted = api.select(
          Number(selectedX.value), Number(selectedY.value)
        ) === 1;
        sync();
        return accepted;
      };
      paused.addEventListener("change", () => {
        api.setPaused(paused.checked ? 1 : 0);
        sync();
      });
      reset.addEventListener("click", () => {
        api.reset();
        if (reducedMotion.matches) {
          api.setPaused(1);
        }
        sync();
      });
      selectedX.addEventListener("change", selection);
      selectedY.addEventListener("change", selection);
      passable.addEventListener("change", () => {
        const requested = passable.checked;
        if (selection()) {
          const accepted = api.setPassable(
            Number(selectedX.value), Number(selectedY.value),
            requested ? 1 : 0
          ) === 1;
          if (!accepted) {
            passable.checked = api.selectedPassable() === 1;
            status.textContent =
              "Wall edit rejected because the selected tile is occupied.";
          }
          sync();
        }
      });
      const applyMotionPreference = () => {
        if (reducedMotion.matches) {
          api.setPaused(1);
        }
        sync();
      };
      reducedMotion.addEventListener("change", applyMotionPreference);
      applyMotionPreference();
      window.setInterval(sync, 100);

      window.tessDiagnosticsTest = {
        reset: api.reset,
        setPaused: api.setPaused,
        setPassable: api.setPassable,
        snapshot: () => ({
          fixedTicks: api.fixedTicks(),
          pathPassabilityChecks: api.pathPassabilityChecks(),
          queuedPhaseCalls: api.queuedPhaseCalls(),
          queuedDirtyMerged: api.queuedDirtyMerged(),
          flowOffered: api.flowOffered(),
          flowAdmitted: api.flowAdmitted(),
          flowTerminal: api.flowTerminal(),
          flowOutstanding: api.flowOutstanding(),
          flowHighWater: api.flowHighWater(),
          admissionOk: api.flowAdmissionOk() === 1,
          retentionOk: api.flowRetentionOk() === 1,
        }),
      };

      const deadline = performance.now() + verificationTimeoutMs;
      const verify = () => {
        const result = api.status();
        if (result === 1) {
          root.dataset.tessDiagnostics = "ready";
          status.textContent = "Colony diagnostics are live.";
          sync();
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
