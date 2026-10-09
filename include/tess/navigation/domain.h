#pragma once

#include <tess/core/fail_fast.h>

#include <algorithm>
#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace tess {

/// A local state qualified by a non-recycled domain lifetime identity.
struct NavigationLocation {
  std::uint64_t domain = 0;
  std::uint64_t node = 0;
  auto operator<=>(const NavigationLocation&) const noexcept = default;
};

/// Availability is separate from endpoint membership and edge cost.
enum class NavigationAvailability : std::uint8_t {
  Legal,
  Blocked,
  Unavailable,
  Invalid,
  Overflow,
};

/// An original directed local edge, including during reverse enumeration.
struct NavigationLocalEdge {
  std::uint64_t from = 0;
  std::uint64_t to = 0;
  std::uint64_t id = 0;
  std::uint64_t cost = 0;
  std::uint64_t key = 0;
  NavigationAvailability availability = NavigationAvailability::Legal;
};

/// Checked directed attachment; geometry and opaque key meaning are host owned.
struct NavigationConnection {
  NavigationLocation from{};
  NavigationLocation to{};
  std::uint64_t id = 0;
  std::uint64_t cost = 0;
  std::uint64_t key = 0;
  NavigationAvailability availability = NavigationAvailability::Legal;
};

/// A retained selected transition; local identity includes its domain and
/// original source node.
struct NavigationTransition : NavigationConnection {
  bool connection = false;
};

/**
 * Immutable indexed domain interface, retained by composition snapshots.
 *
 * Implementations must keep all transitively referenced data frozen, including
 * runtime policy. No mutation through other aliases is allowed while retained.
 * Each edge lookup returns one candidate, not a hidden whole adjacency scan.
 * Reverse enumeration returns the ORIGINAL forward edge and its forward cost.
 * Membership includes unavailable nodes; availability describes current data.
 * Callbacks and their allocation/time costs are outside the query work bound.
 * Instances have unique lifetime IDs within one linked image; copying is
 * prohibited.
 */
class NavigationDomain {
 public:
  NavigationDomain() : identity_(next_identity_.fetch_add(1)) {
    if (identity_ == 0 ||
        identity_ == std::numeric_limits<std::uint64_t>::max())
      detail::fail_fast("NavigationDomain identity space exhausted");
  }
  virtual ~NavigationDomain() = default;
  NavigationDomain(const NavigationDomain&) = delete;
  NavigationDomain& operator=(const NavigationDomain&) = delete;
  [[nodiscard]] auto identity() const noexcept -> std::uint64_t {
    return identity_;
  }
  [[nodiscard]] auto location(std::uint64_t node) const noexcept
      -> NavigationLocation {
    return {identity_, node};
  }
  [[nodiscard]] virtual auto valid() const noexcept -> bool { return true; }
  [[nodiscard]] virtual auto contains(std::uint64_t node) const noexcept
      -> bool = 0;
  [[nodiscard]] virtual auto availability(std::uint64_t node) const
      -> NavigationAvailability = 0;
  [[nodiscard]] virtual auto degree(std::uint64_t node, bool reverse) const
      -> std::size_t = 0;
  [[nodiscard]] virtual auto edge(std::uint64_t node, std::size_t cursor,
                                  bool reverse) const
      -> NavigationLocalEdge = 0;
  [[nodiscard]] virtual auto complete() const noexcept -> bool = 0;

 private:
  inline static std::atomic<std::uint64_t> next_identity_{1};
  std::uint64_t identity_;
};

/**
 * Owns a finite directed graph; construction is explicit unbudgeted
 * preparation.
 *
 * Edges may be parallel and zero cost; IDs must be unique within this domain.
 * Invalid endpoints, duplicate IDs and reserved UINT64_MAX costs invalidate the
 * graph. Storage is immutable after construction. No spatial meaning is
 * inferred.
 */
class NavigationGraph final : public NavigationDomain {
 public:
  NavigationGraph(std::size_t nodes, std::vector<NavigationLocalEdge> edges,
                  bool complete = true)
      : edges_(std::move(edges)),
        forward_(nodes),
        reverse_(nodes),
        complete_(complete) {
    std::map<std::uint64_t, bool> ids;
    for (std::size_t i = 0; i < edges_.size(); ++i) {
      const auto& e = edges_[i];
      if (e.from >= nodes || e.to >= nodes ||
          e.cost == std::numeric_limits<std::uint64_t>::max() ||
          !ids.emplace(e.id, true).second) {
        valid_ = false;
        continue;
      }
      forward_[static_cast<std::size_t>(e.from)].push_back(i);
      reverse_[static_cast<std::size_t>(e.to)].push_back(i);
      if (e.availability == NavigationAvailability::Unavailable)
        complete_ = false;
    }
  }
  [[nodiscard]] auto valid() const noexcept -> bool override { return valid_; }
  [[nodiscard]] auto contains(std::uint64_t node) const noexcept
      -> bool override {
    return valid_ && node < forward_.size();
  }
  [[nodiscard]] auto availability(std::uint64_t node) const
      -> NavigationAvailability override {
    return contains(node) ? NavigationAvailability::Legal
                          : NavigationAvailability::Invalid;
  }
  [[nodiscard]] auto degree(std::uint64_t node, bool reverse) const
      -> std::size_t override {
    return contains(node) ? (reverse ? reverse_ : forward_)[node].size() : 0;
  }
  [[nodiscard]] auto edge(std::uint64_t node, std::size_t cursor,
                          bool reverse) const -> NavigationLocalEdge override {
    if (!contains(node))
      return {.availability = NavigationAvailability::Invalid};
    const auto& indices = (reverse ? reverse_ : forward_)[node];
    if (cursor >= indices.size())
      return {.availability = NavigationAvailability::Invalid};
    return edges_[indices[cursor]];
  }
  [[nodiscard]] auto complete() const noexcept -> bool override {
    return complete_;
  }

 private:
  std::vector<NavigationLocalEdge> edges_;
  std::vector<std::vector<std::size_t>> forward_, reverse_;
  bool complete_;
  bool valid_ = true;
};

/// First preparation failure, inspected before an invalid snapshot is queried.
enum class NavigationPreparationError : std::uint8_t {
  None,
  NullDomain,
  InvalidDomain,
  DuplicateDomain,
  InvalidConnectionFrom,
  InvalidConnectionTo,
  ReservedConnectionCost,
  DuplicateConnectionId,
};

/**
 * Compact first-failure diagnostic, owned by its immutable snapshot.
 *
 * Domain errors index the supplied domain vector; connection errors index the
 * supplied connection vector. The ID is the domain identity or connection ID
 * respectively (zero for a null domain). None uses index SIZE_MAX and ID zero.
 * Domain validation precedes connection validation, each in input order.
 */
struct NavigationPreparationDiagnostic {
  NavigationPreparationError reason = NavigationPreparationError::None;
  std::size_t index = std::numeric_limits<std::size_t>::max();
  std::uint64_t id = 0;
};

/**
 * Owns domain leases and explicit connections for one immutable policy
 * snapshot.
 *
 * Construction validates and indexes attachments outside query budgets. Replace
 * the snapshot for ANY content/topology/residency/profile change. Identity
 * equality, not a caller-reused revision or address, decides retained
 * freshness. Domains retain native storage. Endpoints need membership, not
 * residency, so unavailable connections remain expressible. Geometry validation
 * is the host's responsibility. Broad openings use multiple identified endpoint
 * pairs. Direct degree/edge calls require a valid snapshot and a member
 * location; edge additionally requires cursor < degree(location, reverse).
 * Query drivers establish these preconditions before enumerating edges.
 */
class NavigationSnapshot {
 public:
  explicit NavigationSnapshot(
      std::vector<std::shared_ptr<const NavigationDomain>> domains,
      std::vector<NavigationConnection> connections = {},
      std::uint64_t profile = 0)
      : connections_(std::move(connections)), profile_(profile) {
    for (std::size_t i = 0; i < domains.size(); ++i) {
      auto& domain = domains[i];
      if (!domain) {
        reject(NavigationPreparationError::NullDomain, i, 0);
        continue;
      }
      const auto id = domain->identity();
      if (!domain->valid())
        reject(NavigationPreparationError::InvalidDomain, i, id);
      complete_ = complete_ && domain->complete();
      if (!domains_.emplace(id, std::move(domain)).second)
        reject(NavigationPreparationError::DuplicateDomain, i, id);
    }
    std::map<std::uint64_t, bool> ids;
    for (std::size_t i = 0; i < connections_.size(); ++i) {
      const auto& e = connections_[i];
      auto error = NavigationPreparationError::None;
      if (!contains(e.from))
        error = NavigationPreparationError::InvalidConnectionFrom;
      else if (!contains(e.to))
        error = NavigationPreparationError::InvalidConnectionTo;
      else if (e.cost == std::numeric_limits<std::uint64_t>::max())
        error = NavigationPreparationError::ReservedConnectionCost;
      else if (!ids.emplace(e.id, true).second)
        error = NavigationPreparationError::DuplicateConnectionId;
      if (error != NavigationPreparationError::None) {
        reject(error, i, e.id);
        continue;
      }
      forward_[e.from].push_back(i);
      reverse_[e.to].push_back(i);
      if (e.availability == NavigationAvailability::Unavailable)
        complete_ = false;
    }
  }
  [[nodiscard]] auto valid() const noexcept -> bool { return valid_; }
  /// First rejected input; the returned reference lives with this snapshot.
  [[nodiscard]] auto diagnostic() const noexcept
      -> const NavigationPreparationDiagnostic& {
    return diagnostic_;
  }
  [[nodiscard]] auto complete() const noexcept -> bool { return complete_; }
  [[nodiscard]] auto profile() const noexcept -> std::uint64_t {
    return profile_;
  }
  [[nodiscard]] auto contains(NavigationLocation at) const noexcept -> bool {
    const auto found = domains_.find(at.domain);
    return found != domains_.end() && found->second->contains(at.node);
  }
  [[nodiscard]] auto availability(NavigationLocation at) const
      -> NavigationAvailability {
    const auto found = domains_.find(at.domain);
    return found == domains_.end() ? NavigationAvailability::Invalid
                                   : found->second->availability(at.node);
  }
  [[nodiscard]] auto degree(NavigationLocation at, bool reverse) const
      -> std::size_t {
    const auto& domain = domains_.at(at.domain);
    const auto& index = reverse ? reverse_ : forward_;
    const auto found = index.find(at);
    const auto extra = found == index.end() ? 0 : found->second.size();
    const auto local = domain->degree(at.node, reverse);
    return local > std::numeric_limits<std::size_t>::max() - extra
               ? std::numeric_limits<std::size_t>::max()
               : local + extra;
  }
  [[nodiscard]] auto edge(NavigationLocation at, std::size_t cursor,
                          bool reverse) const -> NavigationTransition {
    const auto& domain = domains_.at(at.domain);
    const auto local = domain->degree(at.node, reverse);
    if (cursor < local) {
      const auto e = domain->edge(at.node, cursor, reverse);
      return {{domain->location(e.from), domain->location(e.to), e.id, e.cost,
               e.key, e.availability},
              false};
    }
    const auto& index = reverse ? reverse_ : forward_;
    return {connections_[index.at(at)[cursor - local]], true};
  }

 private:
  void reject(NavigationPreparationError reason, std::size_t index,
              std::uint64_t id) noexcept {
    valid_ = false;
    if (diagnostic_.reason == NavigationPreparationError::None)
      diagnostic_ = {reason, index, id};
  }
  NavigationPreparationDiagnostic diagnostic_{};
  std::map<std::uint64_t, std::shared_ptr<const NavigationDomain>> domains_;
  std::vector<NavigationConnection> connections_;
  std::map<NavigationLocation, std::vector<std::size_t>> forward_, reverse_;
  std::uint64_t profile_;
  bool valid_ = true;
  bool complete_ = true;
};
}  // namespace tess
