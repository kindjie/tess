#pragma once

// Minimal Dear ImGui stub used only to compile-check
// tess/debug/imgui/panels.h without pulling in the real Dear ImGui dependency.
// The signatures mirror the real ImGui API for the three text primitives the
// reference panels use, so a panel that compiles against this stub also
// compiles against the real header. The bodies are no-ops.

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

namespace ImGui {

// Attribute lives on a preceding declaration (as in real ImGui); the inline
// definition below inherits it.
inline void Text(const char* fmt, ...) TESS_STUB_IM_FMTARGS(1);
inline void Text(const char*, ...) {}
inline void TextUnformatted(const char*, const char* = nullptr) {}
inline void Separator() {}

}  // namespace ImGui
