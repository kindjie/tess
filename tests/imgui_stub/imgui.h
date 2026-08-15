#pragma once

#include <array>
#include <cstdarg>
#include <cstring>

// Minimal Dear ImGui stub used only to compile-check
// tess/debug/imgui headers without pulling in the real Dear ImGui dependency.
// The signatures mirror the real ImGui API used by the reference panels and
// editor tools, so code that compiles against this stub also compiles against
// the real header. Text bodies are no-ops; Checkbox has explicit test control.

#define IMGUI_VERSION "tess-stub"
#define IMGUI_HAS_TABLE

struct ImVec2 {
  constexpr ImVec2(float x_value = 0.0F, float y_value = 0.0F) noexcept
      : x(x_value), y(y_value) {}

  float x;
  float y;
};

using ImGuiID = unsigned int;
using ImGuiTableFlags = int;
using ImGuiTableColumnFlags = int;
using ImGuiTableRowFlags = int;

enum ImGuiTableFlags_ {
  ImGuiTableFlags_None = 0,
  ImGuiTableFlags_Resizable = 1 << 0,
  ImGuiTableFlags_NoSavedSettings = 1 << 4,
  ImGuiTableFlags_RowBg = 1 << 6,
  ImGuiTableFlags_BordersInnerV = 1 << 9,
  ImGuiTableFlags_SizingFixedFit = 1 << 13,
  ImGuiTableFlags_ScrollX = 1 << 24,
  ImGuiTableFlags_ScrollY = 1 << 25,
};

// Match Dear ImGui's int-based public flag API in this compile-only stub.
enum ImGuiTableColumnFlags_ {  // NOLINT(performance-enum-size)
  ImGuiTableColumnFlags_None = 0,
  ImGuiTableColumnFlags_WidthFixed = 1 << 4,
};

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
inline const char* last_precision_string = nullptr;
inline int table_begin_count = 0;
inline int table_end_count = 0;
inline int table_column_count = 0;
inline int table_setup_count = 0;
inline int table_header_row_count = 0;
inline int table_row_count = 0;
inline int table_frozen_columns = 0;
inline int table_frozen_rows = 0;
inline ImGuiTableFlags table_flags = ImGuiTableFlags_None;
inline ImVec2 table_outer_size;
inline std::array<int, 6> table_column_visits{};
inline int cursor_pos_x_set_count = 0;
inline std::array<float, 35> cursor_pos_x_values{};

inline void reset() noexcept {
  checkbox_pending = false;
  checkbox_changed = false;
  checkbox_value = false;
  last_precision_string = nullptr;
  table_begin_count = 0;
  table_end_count = 0;
  table_column_count = 0;
  table_setup_count = 0;
  table_header_row_count = 0;
  table_row_count = 0;
  table_frozen_columns = 0;
  table_frozen_rows = 0;
  table_flags = ImGuiTableFlags_None;
  table_outer_size = {};
  table_column_visits.fill(0);
  cursor_pos_x_set_count = 0;
  cursor_pos_x_values.fill(0.0F);
}

inline void set_next_checkbox(bool changed, bool value) noexcept {
  checkbox_pending = true;
  checkbox_changed = changed;
  checkbox_value = value;
}

}  // namespace tess_imgui_stub

namespace ImGui {

inline bool BeginTable(const char*, int columns, ImGuiTableFlags flags = 0,
                       const ImVec2& outer_size = ImVec2{}, float = 0.0F) {
  ++tess_imgui_stub::table_begin_count;
  tess_imgui_stub::table_column_count = columns;
  tess_imgui_stub::table_flags = flags;
  tess_imgui_stub::table_outer_size = outer_size;
  return true;
}
inline void EndTable() { ++tess_imgui_stub::table_end_count; }
inline void TableNextRow(ImGuiTableRowFlags = 0, float = 0.0F) {
  ++tess_imgui_stub::table_row_count;
}
inline bool TableSetColumnIndex(int column) {
  if (column >= 0 &&
      column < static_cast<int>(tess_imgui_stub::table_column_visits.size())) {
    ++tess_imgui_stub::table_column_visits[static_cast<std::size_t>(column)];
  }
  return true;
}
inline void TableSetupColumn(const char*, ImGuiTableColumnFlags = 0,
                             float = 0.0F, ImGuiID = 0) {
  ++tess_imgui_stub::table_setup_count;
}
inline void TableSetupScrollFreeze(int columns, int rows) {
  tess_imgui_stub::table_frozen_columns = columns;
  tess_imgui_stub::table_frozen_rows = rows;
}
inline void TableHeadersRow() { ++tess_imgui_stub::table_header_row_count; }
inline float GetTextLineHeightWithSpacing() { return 16.0F; }
inline ImVec2 GetContentRegionAvail() { return {100.0F, 100.0F}; }
inline ImVec2 CalcTextSize(const char* text, const char* text_end = nullptr,
                           bool = false, float = -1.0F) {
  const auto size = text_end == nullptr
                        ? std::strlen(text)
                        : static_cast<std::size_t>(text_end - text);
  return {static_cast<float>(size) * 8.0F, 16.0F};
}
inline float GetCursorPosX() { return 5.0F; }
inline void SetCursorPosX(float value) {
  if (tess_imgui_stub::cursor_pos_x_set_count <
      static_cast<int>(tess_imgui_stub::cursor_pos_x_values.size())) {
    tess_imgui_stub::cursor_pos_x_values[static_cast<std::size_t>(
        tess_imgui_stub::cursor_pos_x_set_count)] = value;
  }
  ++tess_imgui_stub::cursor_pos_x_set_count;
}

// Attribute lives on a preceding declaration (as in real ImGui); the inline
// definition below inherits it.
inline void Text(const char* fmt, ...) TESS_STUB_IM_FMTARGS(1);
inline void Text(const char* fmt, ...) {
  if (std::strstr(fmt, "%.*s") == nullptr) {
    return;
  }
  std::va_list args;
  va_start(args, fmt);
  static_cast<void>(va_arg(args, const char*));
  static_cast<void>(va_arg(args, int));
  tess_imgui_stub::last_precision_string = va_arg(args, const char*);
  va_end(args);
}
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
