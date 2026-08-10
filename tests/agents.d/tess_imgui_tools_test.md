# tess_imgui_tools_test

- `tess_imgui_tools_test` (`TESS_ENABLE_IMGUI` on, diagnostics off): verifies
  the optional tools have no diagnostics dependency. Its controllable
  `Checkbox` covers dense and sparse world and chunk inspection,
  missing/out-of-bounds selection, and boolean field edits that return
  caller-applied intents without mutating world storage.
