#pragma once

#include <tess/core/shape.h>

#include <cstddef>
#include <span>
#include <vector>

namespace tess {

/**
 * Non-owning, read-only view over a path's coordinates.
 *
 * Copying a `PathView` never copies path data. The view has the lifetime of
 * its backing storage: a view returned from scratch search state is invalidated
 * by the next query that reuses that state, and a runtime result view is
 * invalidated by the next process or clear operation. The caller must keep a
 * directly supplied span or vector alive. Concurrent reads are safe only while
 * no owner mutates or reuses the backing storage.
 */
class PathView {
 public:
  /** Constructs an empty view. */
  constexpr PathView() noexcept = default;

  /** Borrows `nodes`; the caller retains ownership of the elements. */
  constexpr PathView(std::span<const Coord3> nodes) noexcept : nodes_(nodes) {}

  /** Borrows an lvalue vector; the vector must outlive the view. */
  PathView(const std::vector<Coord3>& nodes) noexcept : nodes_(nodes) {}

  /** Rejects a temporary vector whose elements would immediately dangle. */
  PathView(std::vector<Coord3>&&) = delete;

  /**
   * Rejects a const temporary vector whose elements would immediately dangle.
   */
  PathView(const std::vector<Coord3>&&) = delete;

  /** Returns the number of coordinates in the view. */
  [[nodiscard]] constexpr auto size() const noexcept -> std::size_t {
    return nodes_.size();
  }
  /** Returns whether the view contains no coordinates. */
  [[nodiscard]] constexpr auto empty() const noexcept -> bool {
    return nodes_.empty();
  }
  /** Returns a coordinate; `index` must be less than `size()`. */
  [[nodiscard]] constexpr auto operator[](std::size_t index) const noexcept
      -> const Coord3& {
    return nodes_[index];
  }
  /** Returns the first coordinate; the view must not be empty. */
  [[nodiscard]] constexpr auto front() const noexcept -> const Coord3& {
    return nodes_.front();
  }
  /** Returns the last coordinate; the view must not be empty. */
  [[nodiscard]] constexpr auto back() const noexcept -> const Coord3& {
    return nodes_.back();
  }
  /** Returns an iterator to the first coordinate. */
  [[nodiscard]] constexpr auto begin() const noexcept { return nodes_.begin(); }

  /** Returns the past-the-end iterator. */
  [[nodiscard]] constexpr auto end() const noexcept { return nodes_.end(); }

  /**
   * Returns the borrowed coordinate array; the empty-view value is unspecified.
   */
  [[nodiscard]] constexpr auto data() const noexcept -> const Coord3* {
    return nodes_.data();
  }

  /** Returns the underlying borrowed span without copying path data. */
  [[nodiscard]] constexpr auto span() const noexcept
      -> std::span<const Coord3> {
    return nodes_;
  }

  /**
   * Returns the remaining path from `offset` without copying path data.
   *
   * The offset is bounds-clamped, so an offset at or past the end returns an
   * empty view. The result shares this view's backing storage and lifetime.
   */
  [[nodiscard]] constexpr auto suffix(std::size_t offset) const noexcept
      -> PathView {
    const auto clamped = offset < nodes_.size() ? offset : nodes_.size();
    return PathView{nodes_.subspan(clamped)};
  }

 private:
  std::span<const Coord3> nodes_{};
};

}  // namespace tess
