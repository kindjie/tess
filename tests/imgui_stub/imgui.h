#pragma once

// Minimal Dear ImGui stub used only to compile-check
// tess/debug/imgui/panels.h without pulling in the real Dear ImGui dependency.
// The signatures mirror the real ImGui API for the three text primitives the
// reference panels use, so a panel that compiles against this stub also
// compiles against the real header. The bodies are no-ops.

#define IMGUI_VERSION "tess-stub"

namespace ImGui {

inline void Text(const char*, ...) {}
inline void TextUnformatted(const char*, const char* = nullptr) {}
inline void Separator() {}

}  // namespace ImGui
