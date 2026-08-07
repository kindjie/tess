// Per-item flow, deadline, settlement, and service-order tracking for
// the budgeted-progress benchmark harness
// (docs/planning/budgeted-progress-benchmarks.md, sections 4.1, 8-9).
//
// FlowCounters snapshots validate the conservation identities;
// headline metrics never derive from `completed` deltas, because
// `completed` is documented non-monotonic (completed-to-stale
// reclassification). The tracker owns per-item records — admission
// tick, terminal outcome and tick, reclassification — and derives
// useful completions and deadline success from them at settlement
// close, after which the verdict is sealed. Counter writes mirror
// `ResumableWorkQueue`'s attached-accounting semantics exactly
// (include/tess/ops/async_work.h), including the guarded decrement on
// reclassification.
//
// Harness support only, never a public header.

#pragma once

#include <tess/diagnostics/diagnostics.h>
#include <tess/ops/queued.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace tess_test::budgeted {

enum class Outcome : std::uint8_t {
  Completed,
  Cancelled,
  Superseded,
  Stale,
  Failed,
  DroppedAfterAdmission,
};

struct DemandClassConfig {
  // Inclusive deadline allowance: an item admitted at tick A must
  // complete by tick A + allowance (design section 8.3).
  std::uint64_t deadline_allowance_ticks = 0;
};

struct ItemId {
  std::uint32_t index = 0;
};

// Aggregated on one counting basis pair (design section 9): throughput
// counts completions whose completion tick fell inside the measured
// window; cohort metrics follow items admitted inside the window
// through settlement.
struct ClassSummary {
  std::uint64_t useful_completions = 0;
  std::uint64_t cohort_admitted = 0;
  std::uint64_t cohort_deadline_met = 0;
  std::uint64_t starved_items = 0;
  std::vector<std::uint64_t> lateness_ticks;  // Completed cohort items only.
};

struct TrackerSummary {
  bool sealed = false;
  bool admission_identity_ok = false;
  bool retention_identity_ok = false;
  std::vector<ClassSummary> classes;
  ClassSummary total;
};

// Benchmark-owned flow adapter accounting transitions at the public
// operation boundary with production `FlowAccounting` semantics.
// Call `observe_tick` once per simulation tick before that tick's
// transitions.
class ItemTracker {
 public:
  ItemTracker(std::vector<DemandClassConfig> classes, std::uint32_t base_tps)
      : classes_(std::move(classes)), base_tps_(base_tps) {}

  [[nodiscard]] auto accounting() noexcept
      -> tess::diagnostics::FlowAccounting& {
    return accounting_;
  }

  [[nodiscard]] auto counters() const noexcept
      -> const tess::diagnostics::FlowCounters& {
    return accounting_.counters;
  }

  void observe_tick(std::uint64_t tick) {
    const std::uint64_t previous = accounting_.last_observed_tick;
    accounting_.observe_tick(tick);
    const std::uint64_t elapsed = tick > previous ? tick - previous : 0;

    std::uint64_t oldest = tick;
    bool any_outstanding = false;
    for (auto& item : items_) {
      if (item.terminal.has_value()) {
        continue;
      }
      any_outstanding = true;
      oldest = item.admitted_tick < oldest ? item.admitted_tick : oldest;
      // Starvation time accrues only while dependency-ready and
      // unserved (design section 9.1; section 13 test 14).
      if (item.ready && elapsed > 0) {
        item.ready_unserved_ticks += elapsed;
        if (item.ready_unserved_ticks > item.max_ready_unserved_ticks) {
          item.max_ready_unserved_ticks = item.ready_unserved_ticks;
        }
      }
    }
    accounting_.counters.oldest_outstanding_age_ticks =
        any_outstanding ? tick - oldest : 0;
  }

  [[nodiscard]] auto admit(std::uint32_t class_index, std::uint64_t tick,
                           bool ready = true) -> ItemId {
    ++accounting_.counters.offered;
    accounting_.record_admitted();
    Item item;
    item.class_index = class_index;
    item.admitted_tick = tick;
    item.deadline_tick = tick + classes_[class_index].deadline_allowance_ticks;
    item.ready = ready;
    items_.push_back(item);
    return ItemId{static_cast<std::uint32_t>(items_.size() - 1)};
  }

  void offer_rejected() {
    ++accounting_.counters.offered;
    ++accounting_.counters.rejected;
  }

  void offer_coalesced() {
    ++accounting_.counters.offered;
    ++accounting_.counters.coalesced_into_pending;
  }

  void set_ready(ItemId id, bool ready) {
    auto& item = items_[id.index];
    item.ready = ready;
    if (!ready) {
      item.ready_unserved_ticks = 0;
    }
  }

  // One service quantum reached the item: its no-service streak resets.
  void record_service(ItemId id) { items_[id.index].ready_unserved_ticks = 0; }

  void resolve(ItemId id, Outcome outcome, std::uint64_t tick) {
    auto& item = items_[id.index];
    if (item.terminal.has_value()) {
      return;
    }
    item.terminal = outcome;
    item.terminal_tick = tick;
    auto& counters = accounting_.counters;
    switch (outcome) {
      case Outcome::Completed:
        ++counters.completed;
        break;
      case Outcome::Cancelled:
        ++counters.cancelled;
        break;
      case Outcome::Superseded:
        ++counters.superseded;
        break;
      case Outcome::Stale:
        ++counters.stale;
        break;
      case Outcome::Failed:
        ++counters.failed;
        break;
      case Outcome::DroppedAfterAdmission:
        ++counters.dropped_after_admission;
        break;
    }
    accounting_.record_left_outstanding();
    counters.residence_ticks_accumulated +=
        accounting_.last_observed_tick - item.admitted_tick;
  }

  // Completed-to-stale reclassification: the buckets swap under the
  // documented guarded decrement and the per-item completion is
  // invalidated so settlement attribution removes it from useful
  // completions and deadline success (design section 8.2).
  void reclassify_stale(ItemId id) {
    auto& item = items_[id.index];
    if (item.terminal != Outcome::Completed) {
      return;
    }
    item.terminal = Outcome::Stale;
    item.reclassified = true;
    auto& counters = accounting_.counters;
    if (counters.completed > 0) {
      --counters.completed;
    }
    ++counters.stale;
  }

  void begin_window(std::uint64_t tick) { window_start_tick_ = tick; }

  void end_window(std::uint64_t tick) { window_end_tick_ = tick; }

  // Seals the verdict: reclassifications observed before this call are
  // attributed back to the admission window; anything after cannot
  // change the summary (design section 8.2; section 13 tests 15/18).
  void close_settlement() {
    sealed_summary_ = derive_summary();
    sealed_summary_->sealed = true;
  }

  [[nodiscard]] auto summary() const -> TrackerSummary {
    if (sealed_summary_.has_value()) {
      return *sealed_summary_;
    }
    return derive_summary();
  }

 private:
  struct Item {
    std::uint32_t class_index = 0;
    std::uint64_t admitted_tick = 0;
    std::uint64_t deadline_tick = 0;
    std::uint64_t terminal_tick = 0;
    std::optional<Outcome> terminal;
    bool reclassified = false;
    bool ready = false;
    std::uint64_t ready_unserved_ticks = 0;
    std::uint64_t max_ready_unserved_ticks = 0;
  };

  [[nodiscard]] auto starvation_window_ticks(std::uint32_t class_index) const
      -> std::uint64_t {
    // max(4 * allowance, one simulation second): one sim second is
    // always base_tps ticks (design section 9.1).
    const std::uint64_t four =
        4 * classes_[class_index].deadline_allowance_ticks;
    const std::uint64_t sim_second = base_tps_;
    return four > sim_second ? four : sim_second;
  }

  [[nodiscard]] auto derive_summary() const -> TrackerSummary {
    TrackerSummary out;
    out.admission_identity_ok = accounting_.counters.admission_identity_holds();
    out.retention_identity_ok = accounting_.counters.retention_identity_holds();
    out.classes.resize(classes_.size());

    for (const auto& item : items_) {
      auto& cls = out.classes[item.class_index];
      const bool valid_completion =
          item.terminal == Outcome::Completed && !item.reclassified;
      // Throughput basis: completion tick inside the window.
      if (valid_completion && item.terminal_tick >= window_start_tick_ &&
          item.terminal_tick <= window_end_tick_) {
        ++cls.useful_completions;
      }
      // Cohort basis: admitted inside the window.
      if (item.admitted_tick >= window_start_tick_ &&
          item.admitted_tick <= window_end_tick_) {
        ++cls.cohort_admitted;
        if (valid_completion && item.terminal_tick <= item.deadline_tick) {
          ++cls.cohort_deadline_met;
        }
        if (valid_completion && item.terminal_tick > item.deadline_tick) {
          cls.lateness_ticks.push_back(item.terminal_tick - item.deadline_tick);
        }
        if (item.max_ready_unserved_ticks >=
            starvation_window_ticks(item.class_index)) {
          ++cls.starved_items;
        }
      }
    }

    for (const auto& cls : out.classes) {
      out.total.useful_completions += cls.useful_completions;
      out.total.cohort_admitted += cls.cohort_admitted;
      out.total.cohort_deadline_met += cls.cohort_deadline_met;
      out.total.starved_items += cls.starved_items;
      out.total.lateness_ticks.insert(out.total.lateness_ticks.end(),
                                      cls.lateness_ticks.begin(),
                                      cls.lateness_ticks.end());
    }
    return out;
  }

  std::vector<DemandClassConfig> classes_;
  std::uint32_t base_tps_ = 0;
  tess::diagnostics::FlowAccounting accounting_;
  std::vector<Item> items_;
  std::uint64_t window_start_tick_ = 0;
  std::uint64_t window_end_tick_ = std::numeric_limits<std::uint64_t>::max();
  std::optional<TrackerSummary> sealed_summary_;
};

// Benchmark service policy (design section 4.1), not a production
// scheduler contract: dependency-ready items only, then existing
// `Priority` order (Immediate to Maintenance), then earliest inclusive
// simulation deadline, then admission sequence.
struct ServiceEntry {
  tess::Priority priority = tess::Priority::Background;
  std::uint64_t deadline_tick = 0;
  std::uint64_t admission_seq = 0;
  bool ready = true;
  bool serviced = false;
};

class ServiceQueue {
 public:
  auto push(ServiceEntry entry) -> std::size_t {
    entries_.push_back(entry);
    return entries_.size() - 1;
  }

  [[nodiscard]] auto entry(std::size_t index) noexcept -> ServiceEntry& {
    return entries_[index];
  }

  // Returns the index of the next entry under the tie-break chain, or
  // npos when no dependency-ready work exists.
  [[nodiscard]] auto pop_next() -> std::size_t {
    std::size_t best = npos;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
      const auto& candidate = entries_[i];
      if (!candidate.ready || candidate.serviced) {
        continue;
      }
      if (best == npos || precedes(candidate, entries_[best])) {
        best = i;
      }
    }
    if (best != npos) {
      entries_[best].serviced = true;
    }
    return best;
  }

  static constexpr std::size_t npos = std::numeric_limits<std::size_t>::max();

 private:
  [[nodiscard]] static auto precedes(const ServiceEntry& lhs,
                                     const ServiceEntry& rhs) -> bool {
    if (lhs.priority != rhs.priority) {
      return static_cast<int>(lhs.priority) < static_cast<int>(rhs.priority);
    }
    if (lhs.deadline_tick != rhs.deadline_tick) {
      return lhs.deadline_tick < rhs.deadline_tick;
    }
    return lhs.admission_seq < rhs.admission_seq;
  }

  std::vector<ServiceEntry> entries_;
};

}  // namespace tess_test::budgeted
