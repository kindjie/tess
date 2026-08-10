# tess_diagnostics_panels_test

- `tess_diagnostics_panels_test` (diagnostics-enabled, `TESS_ENABLE_IMGUI` on):
  compile-and-run check for the opt-in diagnostics panels against a minimal
  ImGui stub (`tests/imgui_stub/imgui.h`) so a panel bug surfaces here rather
  than only in a real-ImGui consumer. Exercises every diagnostics draw function
  including recent timed spans with allocation traffic and live/peak memory,
  accepts an empty timed-span label without forwarding a null `%.*s` pointer,
  and pins `category_name` for every trace category; includes both optional
  headers together to reject detail-helper collisions.
