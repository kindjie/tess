#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#if defined(TESS_ENABLE_DIAGNOSTICS)
/** Expands to 1 when diagnostic instrumentation is compiled in. */
#define TESS_DIAGNOSTICS_ENABLED 1
/** Compiles `expr` only when diagnostic instrumentation is enabled. */
#define TESS_DIAGNOSTIC_ONLY(expr) \
  do {                             \
    expr;                          \
  } while (false)
/** Increments a diagnostic counter only in instrumented builds. */
#define TESS_DIAGNOSTIC_INC(counter) \
  do {                               \
    ++(counter);                     \
  } while (false)
/** Adds `value` to a diagnostic counter only in instrumented builds. */
#define TESS_DIAGNOSTIC_ADD(counter, value) \
  do {                                      \
    (counter) += (value);                   \
  } while (false)
/** Records a valueless named event when diagnostics are enabled. */
#define TESS_DIAG_EVENT(name)            \
  do {                                   \
    ::tess::diagnostics::event_##name(); \
  } while (false)
/** Records a named event carrying `value` when diagnostics are enabled. */
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
/** Per-thread counters describing path-search work and outcomes. */
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
  std::uint64_t cost_reads = 0;
  std::uint64_t blocked_neighbors = 0;
  std::uint64_t closed_neighbors = 0;
  std::uint64_t relax_attempts = 0;
  std::uint64_t relax_successes = 0;
  std::uint64_t touched_nodes = 0;
  std::uint64_t heuristic_calls = 0;
  std::uint64_t reconstructed_nodes = 0;

  void reset() noexcept { *this = PathCounters{}; }
};

/** Per-thread counts and byte totals reported by instrumented allocators. */
struct AllocationCounters {
  std::uint64_t allocations = 0;
  std::uint64_t allocation_bytes = 0;
  std::uint64_t deallocations = 0;
  std::uint64_t deallocation_bytes = 0;
  // Best-effort retained-byte accounting. Unsized deallocation hooks pass
  // zero and therefore cannot reduce this value; consumers that require exact
  // live memory must supply sized allocator hooks.
  std::uint64_t live_bytes = 0;
  std::uint64_t peak_live_bytes = 0;

  void reset() noexcept { *this = AllocationCounters{}; }
};

/** Per-thread counters for queued execution, dispatch, and dirty merging. */
struct QueuedPhaseCounters {
  std::uint64_t phase_calls = 0;
  std::uint64_t phase_operations = 0;
  std::uint64_t phase_invalid_ranges = 0;
  std::uint64_t phase_failures = 0;
  std::uint64_t partitioned_phase_calls = 0;
  std::uint64_t dirty_partitions = 0;
  std::uint64_t scoped_thread_calls = 0;
  std::uint64_t scoped_thread_workers = 0;
  std::uint64_t worker_pool_calls = 0;
  std::uint64_t worker_pool_workers = 0;
  std::uint64_t dirty_records_collected = 0;
  std::uint64_t dirty_chunks_merged = 0;

  void reset() noexcept { *this = QueuedPhaseCounters{}; }
};

inline thread_local PathCounters* active_path_counters = nullptr;
inline thread_local AllocationCounters* active_allocation_counters = nullptr;
inline thread_local std::uint64_t active_allocation_scope_id = 0;
inline thread_local std::uint64_t next_allocation_scope_id = 1;
inline thread_local QueuedPhaseCounters* active_queued_phase_counters = nullptr;

/** Installs path counters on the current thread for the lifetime of a scope. */
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

/**
 * Installs allocation counters on the current thread for a nested scope.
 */
class ScopedAllocationCounters {
 public:
  explicit ScopedAllocationCounters(AllocationCounters& counters) noexcept
      : previous_{active_allocation_counters},
        previous_scope_id_{active_allocation_scope_id},
        scope_id_{next_allocation_scope_id++} {
    if (next_allocation_scope_id == 0) {
      next_allocation_scope_id = 1;
    }
    active_allocation_counters = &counters;
    active_allocation_scope_id = scope_id_;
  }

  ScopedAllocationCounters(const ScopedAllocationCounters&) = delete;
  auto operator=(const ScopedAllocationCounters&)
      -> ScopedAllocationCounters& = delete;

  ~ScopedAllocationCounters() {
    active_allocation_counters = previous_;
    active_allocation_scope_id = previous_scope_id_;
  }

 private:
  AllocationCounters* previous_;
  std::uint64_t previous_scope_id_;
  std::uint64_t scope_id_;
};

/**
 * Installs queued-phase counters on the current thread for a nested scope.
 */
class ScopedQueuedPhaseCounters {
 public:
  explicit ScopedQueuedPhaseCounters(QueuedPhaseCounters& counters) noexcept
      : previous_{active_queued_phase_counters} {
    active_queued_phase_counters = &counters;
  }

  ScopedQueuedPhaseCounters(const ScopedQueuedPhaseCounters&) = delete;
  auto operator=(const ScopedQueuedPhaseCounters&)
      -> ScopedQueuedPhaseCounters& = delete;

  ~ScopedQueuedPhaseCounters() { active_queued_phase_counters = previous_; }

 private:
  QueuedPhaseCounters* previous_;
};

/** Records one allocation in the current thread's active counter sink. */
inline void record_allocation(std::size_t size) noexcept {
  if (active_allocation_counters != nullptr) {
    auto& counters = *active_allocation_counters;
    ++counters.allocations;
    counters.allocation_bytes += size;
    const auto bytes = static_cast<std::uint64_t>(size);
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    counters.live_bytes = bytes > maximum - counters.live_bytes
                              ? maximum
                              : counters.live_bytes + bytes;
    if (counters.live_bytes > counters.peak_live_bytes) {
      counters.peak_live_bytes = counters.live_bytes;
    }
  }
}

/** Records one deallocation in the current thread's active counter sink. */
inline void record_deallocation(std::size_t size = 0) noexcept {
  if (active_allocation_counters != nullptr) {
    auto& counters = *active_allocation_counters;
    ++counters.deallocations;
    counters.deallocation_bytes += size;
    const auto bytes = static_cast<std::uint64_t>(size);
    counters.live_bytes =
        bytes > counters.live_bytes ? 0 : counters.live_bytes - bytes;
  }
}

/** Records a path scratch reset affecting `nodes` entries. */
inline void event_path_clear(std::uint64_t nodes) noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->scratch_clear_calls;
    active_path_counters->scratch_clear_nodes += nodes;
  }
}

/** Records initialization of one path search. */
inline void event_path_initialize() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->initializations;
  }
}

/** Records a start-tile passability query. */
inline void event_path_start_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->start_passability_checks;
  }
}

/** Records a goal-tile passability query. */
inline void event_path_goal_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->goal_passability_checks;
  }
}

/** Records insertion of a node into the path-search heap. */
inline void event_path_heap_push() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heap_pushes;
  }
}

/** Records removal of a node from the path-search heap. */
inline void event_path_heap_pop() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heap_pops;
  }
}

/** Records a discarded heap entry, classified as closed or stale. */
inline void event_path_skip_pop(bool closed) noexcept {
  if (active_path_counters != nullptr) {
    if (closed) {
      ++active_path_counters->closed_pops;
    } else {
      ++active_path_counters->stale_pops;
    }
  }
}

/** Records examination of one candidate neighbor. */
inline void event_path_neighbor_candidate() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->neighbor_candidates;
  }
}

/** Records a path-search passability lookup. */
inline void event_path_passability_check() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->passability_checks;
  }
}

/** Records a path-search movement-cost lookup. */
inline void event_path_cost_read() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->cost_reads;
  }
}

/** Records rejection of a blocked neighbor. */
inline void event_path_neighbor_blocked() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->blocked_neighbors;
  }
}

/** Records rejection of a neighbor already in the closed set. */
inline void event_path_neighbor_closed() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->closed_neighbors;
  }
}

/** Records an attempt to relax a path-search node. */
inline void event_path_relax_attempt() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->relax_attempts;
  }
}

/** Records a successful path-search relaxation. */
inline void event_path_relax_success() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->relax_successes;
  }
}

/** Records first use of a path scratch node in the current search. */
inline void event_path_touch_node() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->touched_nodes;
  }
}

/** Records evaluation of the path heuristic. */
inline void event_path_heuristic() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->heuristic_calls;
  }
}

/** Records one node copied into a reconstructed path. */
inline void event_path_reconstruct_node() noexcept {
  if (active_path_counters != nullptr) {
    ++active_path_counters->reconstructed_nodes;
  }
}

/** Records execution of a queued phase containing `operations` entries. */
inline void event_queued_phase_execute(std::uint64_t operations) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->phase_calls;
    active_queued_phase_counters->phase_operations += operations;
  }
}

/** Records rejection of an invalid queued-operation range. */
inline void event_queued_phase_invalid_range() noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->phase_invalid_ranges;
  }
}

/** Records one failed operation during queued-phase execution. */
inline void event_queued_phase_failure() noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->phase_failures;
  }
}

/** Records execution of a queued phase split into `partitions`. */
inline void event_queued_partitioned_phase(std::uint64_t partitions) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->partitioned_phase_calls;
    active_queued_phase_counters->dirty_partitions += partitions;
  }
}

/** Records a scoped-thread dispatch using `workers` workers. */
inline void event_queued_scoped_thread_dispatch(
    std::uint64_t workers) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->scoped_thread_calls;
    active_queued_phase_counters->scoped_thread_workers += workers;
  }
}

/** Records a worker-pool dispatch using `workers` workers. */
inline void event_queued_worker_pool_dispatch(std::uint64_t workers) noexcept {
  if (active_queued_phase_counters != nullptr) {
    ++active_queued_phase_counters->worker_pool_calls;
    active_queued_phase_counters->worker_pool_workers += workers;
  }
}

/** Records collection of `records` planned dirty entries. */
inline void event_queued_dirty_collect(std::uint64_t records) noexcept {
  if (active_queued_phase_counters != nullptr) {
    active_queued_phase_counters->dirty_records_collected += records;
  }
}

/** Records merging dirtiness for `chunks` unique chunks. */
inline void event_queued_dirty_merge(std::uint64_t chunks) noexcept {
  if (active_queued_phase_counters != nullptr) {
    active_queued_phase_counters->dirty_chunks_merged += chunks;
  }
}
#endif

/**
 * Deterministic admission and terminal accounting for one bounded flow.
 *
 * Plain ungated data, like `EventStream` rejection counts: flows update
 * an attached accountant at the transition where each fact is known, so
 * histories are never reconstructed from current state. Two
 * conservation identities hold at every quiescent point; they are
 * invariants, not tunable goldens. `completed` is non-monotonic in
 * exactly one documented case: a produced result that later goes stale
 * before retirement is reclassified from `completed` to `stale` so one
 * admission lands in exactly one terminal bucket.
 */
struct FlowCounters {
  /// Admission offers, including rejected and coalesced ones.
  std::uint64_t offered = 0;
  /// Offers accepted as new flow items.
  std::uint64_t admitted = 0;
  /// Offers refused at the admission boundary.
  std::uint64_t rejected = 0;
  /// Offers absorbed by an item that was already pending.
  std::uint64_t coalesced_into_pending = 0;
  /// Items that reached their intended result.
  std::uint64_t completed = 0;
  /// Items explicitly cancelled by the caller.
  std::uint64_t cancelled = 0;
  /// Items replaced by a newer admission of the same slot or goal.
  std::uint64_t superseded = 0;
  /// Items whose result no longer satisfies its version requirement.
  std::uint64_t stale = 0;
  /// Items that terminated in an error state.
  std::uint64_t failed = 0;
  /// Admitted items discarded before reaching any other terminal state.
  std::uint64_t dropped_after_admission = 0;
  /// Work units offered through explicitly bounded budgets.
  std::uint64_t offered_work_units = 0;
  /// Work units actually consumed by flow items.
  std::uint64_t consumed_work_units = 0;
  /// Admitted, non-terminal items right now.
  std::uint64_t outstanding_current = 0;
  /// Highest simultaneous outstanding count observed.
  std::uint64_t outstanding_high_water = 0;
  /// Sum over observed ticks of outstanding items times elapsed ticks.
  std::uint64_t inventory_tick_weighted = 0;
  /// Total admission-to-terminal ticks over terminalized items.
  std::uint64_t residence_ticks_accumulated = 0;
  /// Age in ticks of the oldest still-outstanding item, as last observed.
  std::uint64_t oldest_outstanding_age_ticks = 0;

  /// Sum of every terminal outcome bucket.
  [[nodiscard]] constexpr auto terminal() const noexcept -> std::uint64_t {
    return completed + cancelled + superseded + stale + failed +
           dropped_after_admission;
  }

  /// Every offer was admitted, rejected, or coalesced — never lost.
  [[nodiscard]] constexpr auto admission_identity_holds() const noexcept
      -> bool {
    return offered == admitted + rejected + coalesced_into_pending;
  }

  /// Every admitted item is terminal or still outstanding — never both.
  [[nodiscard]] constexpr auto retention_identity_holds() const noexcept
      -> bool {
    return admitted == terminal() + outstanding_current;
  }

  /// Returns the counters to their zero-initialized state.
  void reset() noexcept { *this = FlowCounters{}; }
};

/**
 * Caller-owned accountant one flow updates at its transitions.
 *
 * Attach while the flow is empty, keep it alive for the attachment, and
 * drive time accounting by calling the flow's `observe_flow_tick` once
 * per simulation tick with a monotonic tick, before that tick's
 * transitions. Inventory is weighted by elapsed ticks: an observation
 * at tick `t` adds `outstanding * (t - last_observed_tick)`. Accounting
 * is serial: concurrent flows synchronize updates under their own
 * locks, and snapshots are meaningful only at quiescent points.
 */
struct FlowAccounting {
  /// The accumulated flow counters.
  FlowCounters counters{};
  /// The most recent tick passed to an observation.
  std::uint64_t last_observed_tick = 0;

  /// Weights current inventory by the elapsed ticks since the last
  /// observation and advances the observation clock.
  void observe_tick(std::uint64_t tick) noexcept {
    if (tick > last_observed_tick) {
      counters.inventory_tick_weighted +=
          counters.outstanding_current * (tick - last_observed_tick);
      last_observed_tick = tick;
    }
  }

  /// Records one admission at the current observation tick.
  void record_admitted() noexcept {
    ++counters.admitted;
    ++counters.outstanding_current;
    if (counters.outstanding_current > counters.outstanding_high_water) {
      counters.outstanding_high_water = counters.outstanding_current;
    }
  }

  /// Records one terminal transition leaving the outstanding set.
  void record_left_outstanding() noexcept {
    if (counters.outstanding_current > 0) {
      --counters.outstanding_current;
    }
  }
};

/**
 * UI-agnostic health view of one flow's accounting.
 *
 * Exposes the counters plus both conservation verdicts for debug
 * panels and tools without binding them to any UI toolkit.
 */
struct FlowHealthSnapshot {
  /// The counters at snapshot time.
  FlowCounters counters{};
  /// Whether every offer is accounted for.
  bool admission_identity_ok = false;
  /// Whether every admitted item is terminal or outstanding.
  bool retention_identity_ok = false;
};

/// Builds the health snapshot for one accountant's counters.
[[nodiscard]] inline auto snapshot(const FlowAccounting& accounting) noexcept
    -> FlowHealthSnapshot {
  return FlowHealthSnapshot{
      accounting.counters,
      accounting.counters.admission_identity_holds(),
      accounting.counters.retention_identity_holds(),
  };
}

}  // namespace tess::diagnostics
