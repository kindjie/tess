#pragma once

// Minimal Dear ImGui stub used only to compile-check
// tess/debug/imgui headers without pulling in the real Dear ImGui dependency.
// The signatures mirror the real ImGui API used by the reference panels and
// editor tools, so code that compiles against this stub also compiles against
// the real header. Text bodies are no-ops; Checkbox has explicit test control.

#define IMGUI_VERSION "tess-stub"

// Mirror of real ImGui's IM_FMTARGS so -Wformat checks panel format strings
// against their arguments when compiling against the stub (real ImGui
// declares Text with this attribute; without it the panels test would never
// diagnose a format/argument mismatch).
#if defined(__GNUC__) || defined(__clang__)
#define TESS_STUB_IM_FMTARGS(fmt) \
  __attribute__((format(printf, (fmt), (fmt) + 1)))
#else
#define TESS_STUB_IM_FMTARGS(fmt)
#endif

namespace tess_imgui_stub {

inline bool checkbox_pending = false;
inline bool checkbox_changed = false;
inline bool checkbox_value = false;

inline void reset() noexcept {
  checkbox_pending = false;
  checkbox_changed = false;
  checkbox_value = false;
}

inline void set_next_checkbox(bool changed, bool value) noexcept {
  checkbox_pending = true;
  checkbox_changed = changed;
  checkbox_value = value;
}

}  // namespace tess_imgui_stub

namespace ImGui {

// Attribute lives on a preceding declaration (as in real ImGui); the inline
// definition below inherits it.
inline void Text(const char* fmt, ...) TESS_STUB_IM_FMTARGS(1);
inline void Text(const char*, ...) {}
inline void TextUnformatted(const char*, const char* = nullptr) {}
inline void Separator() {}
inline bool Checkbox(const char*, bool* value) {
  if (!tess_imgui_stub::checkbox_pending) {
    return false;
  }
  tess_imgui_stub::checkbox_pending = false;
  if (tess_imgui_stub::checkbox_changed) {
    *value = tess_imgui_stub::checkbox_value;
  }
  return tess_imgui_stub::checkbox_changed;
}

}  // namespace ImGui
