# tess_diagnostics_panels_test

- `tess_diagnostics_panels_test` (diagnostics-enabled, `TESS_ENABLE_IMGUI` on):
  compile-and-run coverage for every opt-in diagnostics panel against the
  minimal ImGui stub. It includes both optional headers together to catch
  detail-helper collisions and pins the empty-label case so no null `%.*s`
  pointer reaches ImGui. Timing values straddle decimal digit boundaries so
  the test proves every metric is submitted to a stable table column instead
  of a single shifting text row.
