#pragma once

#include <tess/core/shape.h>

#include <cstddef>
#include <span>
#include <vector>

namespace tess {

// A non-owning view over a path's coordinates, handed out by `PathResult` and
// the path runtime's ticket accessors. It carries the same lifetime contract as
// the span it wraps -- valid only until the underlying storage is reused (A*
// scratch on the next query, or the runtime's node buffer on the next
// process/clear) -- and copying a PathView never copies path data. Beyond
// read-only span parity it offers `suffix()`, the remaining path from a walked
// index, which is the common consumer shape (an agent advancing along
// `path_index`, an overlay drawing the rest of a route). Extract the raw
// `std::span` with `span()` when an API needs it.
class PathView {
 public:
  constexpr PathView() noexcept = default;
  constexpr PathView(std::span<const Coord3> nodes) noexcept : nodes_(nodes) {}
  PathView(const std::vector<Coord3>& nodes) noexcept : nodes_(nodes) {}
  // A view over a temporary vector would dangle at the end of the full
  // expression; reject it at compile time. Lvalue vectors (the A*/runtime
  // node buffers results are built from) still bind to the const-ref ctor.
  PathView(std::vector<Coord3>&&) = delete;
  PathView(const std::vector<Coord3>&&) = delete;

  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
    return nodes_.size();
  }
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return nodes_.empty();
  }
  [[nodiscard]] constexpr auto operator[](std::size_t index) const noexcept
      -> const Coord3& {
    return nodes_[index];
  }
  [[nodiscard]] constexpr auto front() const noexcept -> const Coord3& {
    return nodes_.front();
  }
  [[nodiscard]] constexpr auto back() const noexcept -> const Coord3& {
    return nodes_.back();
  }
  [[nodiscard]] constexpr auto begin() const noexcept { return nodes_.begin(); }
  [[nodiscard]] constexpr auto end() const noexcept { return nodes_.end(); }
  [[nodiscard]] constexpr auto data() const noexcept -> const Coord3* {
    return nodes_.data();
  }

  // The underlying span, for the call sites that need a raw `std::span`.
  [[nodiscard]] constexpr auto span() const noexcept
      -> std::span<const Coord3> {
    return nodes_;
  }

  // The remaining path from `offset`, bounds-clamped (an offset at or past the
  // end yields an empty view). Non-owning: the result shares this view's
  // storage and lifetime and copies no path data.
  [[nodiscard]] constexpr auto suffix(std::size_t offset) const noexcept
      -> PathView {
    const auto clamped = offset < nodes_.size() ? offset : nodes_.size();
    return PathView{nodes_.subspan(clamped)};
  }

 private:
  std::span<const Coord3> nodes_{};
};

}  // namespace tess
