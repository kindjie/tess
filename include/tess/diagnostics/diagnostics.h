#pragma once

#include <cstddef>
#include <cstdint>

#if defined(TESS_ENABLE_DIAGNOSTICS)
#define TESS_DIAGNOSTICS_ENABLED 1
#define TESS_DIAGNOSTIC_ONLY(expr) \
  do {                             \
    expr;                          \
  } while (false)
#define TESS_DIAGNOSTIC_INC(counter) \
  do {                               \
    ++(counter);                     \
  } while (false)
#define TESS_DIAGNOSTIC_ADD(counter, value) \
  do {                                      \
    (counter) += (value);                   \
  } while (false)
#define TESS_DIAG_EVENT(name)            \
  do {                                   \
    ::tess::diagnostics::event_##name(); \
  } while (false)
#define TESS_DIAG_EVENT_VALUE(name, value)    \
  do {                                        \
    ::tess::diagnostics::event_##name(value); \
  } while (false)
#else
#define TESS_DIAGNOSTICS_ENABLED 0
#define TESS_DIAGNOSTIC_ONLY(expr) \
  do {                             \
  } while (false)
#define TESS_DIAGNOSTIC_INC(counter) \
  do {                               \
  } while (false)
#define TESS_DIAGNOSTIC_ADD(counter, value) \
  do {                                      \
  } while (false)
#define TESS_DIAG_EVENT(name) \
  do {                        \
  } while (false)
#define TESS_DIAG_EVENT_VALUE(name, value) \
  do {                                     \
  } while (false)
#endif

namespace tess::diagnostics {

#if TESS_DIAGNOSTICS_ENABLED
struct PathCounters {
  std::uint64_t scratch_clear_calls = 0;
  std::uint64_t scratch_clear_nodes = 0;
  std::uint64_t initializations = 0;
  std::uint64_t start_passability_checks = 0;
  std::uint64_t goal_passability_checks = 0;
  std::uint64_t heap_pushes = 0;
  std::uint64_t heap_pops = 0;
  std::uint64_t stale_pops = 0;
  std::uint64_t closed_pops = 0;
  std::uint64_t neighbor_candidates = 0;
  std::uint64_t passability_checks = 0;
  std::uint64_t blocked_neighbors = 0;
  std::uint64_t closed_neighbors = 0;
  std::uint64_t relax_attempts = 0;
  std::uint64_t relax_successes = 0;
  std::uint64_t touched_nodes = 0;
  std::uint64_t heuristic_calls = 0;
  std::uint64_t reconstructed_nodes = 0;

  void reset() noexcept { *this = PathCounters{}; }
};

struct AllocationCounters {
  std::uint64_t allocations = 0;
  std::uint64_t allocation_bytes = 0;
  std::uint64_t deallocations = 0;
  std::uint64_t deallocation_bytes = 0;

  void reset() noexcept { *this = AllocationCounters{}; }
};

inline thread_local PathCounters* active_path_counters = nullptr;
inline thread_local AllocationCounters* active_allocation_counters = nullptr;

class ScopedPathCounters {
 public:
  explicit ScopedPathCounters(PathCounters& counters) noexcept
      : previous_{active_path_counters} {
    active_path_counters = &counters;
  }

  ScopedPathCounters(const ScopedPathCounters&) = delete;
  auto operator=(const ScopedPathCounters&) -> ScopedPathCounters& = delete;

  ~ScopedPathCounters() { active_path_counters = previous_; }

 private:
  PathCounters* previous_;
};

class ScopedAllocationCounters {
 public:
  explicit ScopedAllocationCounters(AllocationCounters& counters) noexcept
      : previous_{active_allocation_counters} {
    active_allocation_counters = &counters;
  }

  ScopedAllocationCounters(const ScopedAllocationCounters&) = delete;
  auto operator=(const ScopedAllocationCounters&)
      -> ScopedAllocationCounters& = delete;

  ~ScopedAllocationCounters() { active_allocation_counters = previous_; }

 private:
  AllocationCounters* previous_;
};

inline void record_allocation(std::size_t size) noexcept {
  if (active_allocation_counters != nullptr) {
    ++active_allocation_counters->allocations;
    active_allocation_counters->allocation_bytes += size;
  }
}

inline void record_deallocation(std::size_t size = 0) noexcept {
  if (active_allocation_counters != nullptr) {
    ++active_allocation_counters->deallocations;
    active_allocation_counters->deallocation_bytes += size;
  }
}

inline void event_path_clear(std::uint64_t nodes) noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->scratch_clear_calls;
    active_path_counters->scratch_clear_nodes += nodes;
  }
}

inline void event_path_initialize() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->initializations;
  }
}

inline void event_path_start_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->start_passability_checks;
  }
}

inline void event_path_goal_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->goal_passability_checks;
  }
}

inline void event_path_heap_push() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heap_pushes;
  }
}

inline void event_path_heap_pop() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heap_pops;
  }
}

inline void event_path_skip_pop(bool closed) noexcept {
  if (active_path_counters != nullptr) {
    if (closed) {
      ++active_path_counters->closed_pops;
    } else {
      ++active_path_counters->stale_pops;
    }
  }
}

inline void event_path_neighbor_candidate() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->neighbor_candidates;
  }
}

inline void event_path_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->passability_checks;
  }
}

inline void event_path_neighbor_blocked() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->blocked_neighbors;
  }
}

inline void event_path_neighbor_closed() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->closed_neighbors;
  }
}

inline void event_path_relax_attempt() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->relax_attempts;
  }
}

inline void event_path_relax_success() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->relax_successes;
  }
}

inline void event_path_touch_node() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->touched_nodes;
  }
}

inline void event_path_heuristic() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heuristic_calls;
  }
}

inline void event_path_reconstruct_node() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->reconstructed_nodes;
  }
}
#endif

}  // namespace tess::diagnostics
