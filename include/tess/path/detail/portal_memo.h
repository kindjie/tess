#pragma once

#include <tess/core/shape.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace tess::detail {

// Memoizes best_chunk_portal within one chunk-portal selection. The six
// axis orders and the greedy walk re-walk the same chunk seams from the
// same tile, and a census of two portal workloads measured 66.7-67.1% of
// calls repeating a key already answered in the same selection.
//
// The key omits the goal, the world and the movement class. That is
// sound only while one selection owns the memo, because those three are
// invariant across a selection but not across selections. Two mechanisms
// enforce it: a generation stamp retires every entry when a selection
// begins, and a scope guard makes a nested selection bypass the memo
// entirely rather than share a generation with its parent.
//
// Callers supplying their own movement class must keep its passability
// predicate referentially transparent for the duration of one query;
// a predicate that answers differently for the same tile within a
// selection is outside the contract this memo relies on.

// Signed six-way step between adjacent chunks. The Axis enum carries no
// sign, so reusing it would silently depend on goal-monotone stepping.
[[nodiscard]] inline constexpr auto portal_step_code(ChunkCoord3 from,
                                                     ChunkCoord3 to) noexcept
    -> std::int8_t {
  const auto delta = [](auto lhs, auto rhs) {
    return static_cast<std::int64_t>(lhs) - static_cast<std::int64_t>(rhs);
  };
  if (const auto dx = delta(to.x, from.x); dx != 0) {
    return dx > 0 ? std::int8_t{1} : std::int8_t{-1};
  }
  if (const auto dy = delta(to.y, from.y); dy != 0) {
    return dy > 0 ? std::int8_t{2} : std::int8_t{-2};
  }
  if (const auto dz = delta(to.z, from.z); dz != 0) {
    return dz > 0 ? std::int8_t{3} : std::int8_t{-3};
  }
  return 0;
}

/// Request-scoped memo for chunk-portal selection. Not part of the
/// public API surface; lifetime and validity are the selector's.
class PortalMemo {
 public:
  // 256 entries against a measured maximum of 74 live entries per
  // selection in the profiled workloads. Saturation beyond that is
  // handled rather than assumed away: a longer route scales its entry
  // count with chunk distance and can exceed any fixed size.
  static constexpr std::size_t kCapacity = 256;
  static constexpr std::size_t kMask = kCapacity - 1;

  struct Lookup {
    std::size_t slot = 0;
    bool hit = false;
    bool insertable = false;
    std::uint64_t portal_index = 0;
    bool found = false;
  };

  /// Retires every entry in O(1) and reopens the memo for one selection.
  void begin_selection() noexcept {
    ++generation_;
    saturated_ = false;
    hits_ = 0;
    misses_ = 0;
    insertions_ = 0;
    saturations_ = 0;
  }

  /// Bypassed memos answer every lookup with a non-insertable miss, so a
  /// nested selection simply does the original work.
  [[nodiscard]] auto bypassed() const noexcept -> bool { return bypassed_; }
  void set_bypassed(bool value) noexcept { bypassed_ = value; }

  /// True while a selection scope owns this memo.
  [[nodiscard]] auto active() const noexcept -> bool { return active_; }
  void set_active(bool value) noexcept { active_ = value; }

  [[nodiscard]] auto probe(std::uint64_t current_index,
                           std::int8_t step) noexcept -> Lookup {
    if (bypassed_ || saturated_) {
      return Lookup{};
    }
    auto slot = static_cast<std::size_t>(mix(current_index, step)) & kMask;
    for (std::size_t probed = 0; probed < kCapacity; ++probed) {
      const auto& entry = entries_[slot];
      if (entry.generation != generation_) {
        ++misses_;
        return Lookup{slot, false, true, 0, false};
      }
      if (entry.current_index == current_index && entry.step == step) {
        ++hits_;
        return Lookup{slot, true, false, entry.portal_index, entry.found};
      }
      slot = (slot + 1) & kMask;
    }
    // Sticky: without this every later miss would walk the whole table.
    saturated_ = true;
    ++saturations_;
    ++misses_;
    return Lookup{};
  }

  void store(const Lookup& lookup, std::uint64_t current_index,
             std::int8_t step, std::uint64_t portal_index,
             bool found) noexcept {
    if (!lookup.insertable) {
      return;
    }
    ++insertions_;
    entries_[lookup.slot] =
        Entry{generation_, current_index, portal_index, step, found};
  }

  [[nodiscard]] auto hits() const noexcept -> std::size_t { return hits_; }
  [[nodiscard]] auto misses() const noexcept -> std::size_t { return misses_; }
  [[nodiscard]] auto insertions() const noexcept -> std::size_t {
    return insertions_;
  }
  [[nodiscard]] auto saturations() const noexcept -> std::size_t {
    return saturations_;
  }
  [[nodiscard]] auto saturated() const noexcept -> bool { return saturated_; }
  [[nodiscard]] auto generation() const noexcept -> std::uint64_t {
    return generation_;
  }

 private:
  struct Entry {
    // Zero never collides with a live generation: begin_selection
    // increments before first use, so the initial all-zero table reads
    // as empty. A wrap would need 2^64 selections.
    std::uint64_t generation = 0;
    std::uint64_t current_index = 0;
    std::uint64_t portal_index = 0;
    std::int8_t step = 0;
    bool found = false;
  };

  [[nodiscard]] static auto mix(std::uint64_t current_index,
                                std::int8_t step) noexcept -> std::uint64_t {
    // splitmix64 finalizer. Tile indices are dense small integers whose
    // low bits alone would cluster badly under a power-of-two mask.
    //
    // Every constant is typed rather than suffixed: a `ULL` literal is
    // `unsigned long long`, which is a wider type than `uint64_t` on
    // LP64 and pulls the arithmetic into it, and GCC's -Wsign-conversion
    // rejects the resulting widening. The step is bit_cast rather than
    // converted, so its negative values keep an exact bit pattern
    // without a signed-to-unsigned conversion.
    constexpr auto golden = std::uint64_t{0x9e3779b97f4a7c15};
    constexpr auto mix_a = std::uint64_t{0xbf58476d1ce4e5b9};
    constexpr auto mix_b = std::uint64_t{0x94d049bb133111eb};
    auto value = current_index * golden +
                 std::uint64_t{std::bit_cast<std::uint8_t>(step)};
    value ^= value >> 30;
    value *= mix_a;
    value ^= value >> 27;
    value *= mix_b;
    value ^= value >> 31;
    return value;
  }

  std::array<Entry, kCapacity> entries_{};
  std::uint64_t generation_ = 0;
  bool saturated_ = false;
  bool bypassed_ = false;
  bool active_ = false;
  std::size_t hits_ = 0;
  std::size_t misses_ = 0;
  std::size_t insertions_ = 0;
  std::size_t saturations_ = 0;
};

[[nodiscard]] inline auto active_portal_memo() noexcept -> PortalMemo& {
  static thread_local PortalMemo value;
  return value;
}

/// Opens a selection on a memo, and closes it however the selection
/// exits. A nested scope marks its memo bypassed for its own duration,
/// so a user-supplied passability predicate that re-enters selection
/// cannot make the outer query consume entries keyed for another goal.
/// RAII rather than a manual flag because waypoint allocation can throw.
class PortalMemoScope {
 public:
  explicit PortalMemoScope(PortalMemo& memo) noexcept
      : memo_(&memo),
        outer_active_(memo.active()),
        outer_bypassed_(memo.bypassed()) {
    if (outer_active_) {
      // A nested selection would otherwise retire the outer selection's
      // entries and repopulate them for a different goal, which the
      // outer selection would then consume. Bypassing is the fail-open
      // response: the nested query simply does the original work.
      memo_->set_bypassed(true);
      return;
    }
    memo_->set_active(true);
    memo_->set_bypassed(false);
    memo_->begin_selection();
  }

  PortalMemoScope(const PortalMemoScope&) = delete;
  PortalMemoScope(PortalMemoScope&&) = delete;
  auto operator=(const PortalMemoScope&) -> PortalMemoScope& = delete;
  auto operator=(PortalMemoScope&&) -> PortalMemoScope& = delete;

  // Restores exactly what was there, so an exception unwinding out of a
  // selection cannot leave the memo permanently bypassed or active.
  ~PortalMemoScope() {
    memo_->set_bypassed(outer_bypassed_);
    memo_->set_active(outer_active_);
  }

 private:
  PortalMemo* memo_;
  bool outer_active_ = false;
  bool outer_bypassed_ = false;
};

}  // namespace tess::detail
