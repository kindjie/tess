#pragma once

#include <tess/experimental/chunk_maintenance.h>
#include <tess/maintenance/scheduler.h>

namespace tess::maintenance {

/// Result of an authoritative dirty mark and its maintenance offer.
using ChunkMarkResult = experimental::maintenance::ChunkMarkResult;

/// Freshness of one adapter-owned derived chunk product.
using ChunkProductState = experimental::maintenance::ChunkProductState;

/// Version and residency identity of one completed derived product.
using ChunkProductToken = experimental::maintenance::ChunkProductToken;

/// Borrowed adapter product plus its explicit freshness classification.
template <typename Product>
using ChunkProductView = experimental::maintenance::ChunkProductView<Product>;

/// Result of a sparse residency acquisition or reconciliation.
using ChunkResidencyStatus = experimental::maintenance::ChunkResidencyStatus;

/// Sparse residency acquisition result and new residency identity.
using ChunkResidencyResult = experimental::maintenance::ChunkResidencyResult;

/// Result of an adapter-owned sparse eviction.
using ChunkEvictionResult = experimental::maintenance::ChunkEvictionResult;

/// Result of retiring every fixed task owned by an adapter.
using ChunkAdapterReleaseResult =
    experimental::maintenance::ChunkAdapterReleaseResult;

/**
 * External fixed-slot adapter for one derived product per chunk.
 *
 * The stable default executes maintenance synchronously. Callers may provide a
 * custom structural backend. FIFO, queued-coalescing, and dirty-bit backends
 * remain experimental and require an explicit experimental type.
 */
template <typename World, typename Product, typename Rebuild,
          typename Backend = ImmediateScheduler>
using ChunkMaintenanceAdapter =
    experimental::maintenance::ChunkMaintenanceAdapter<World, Product, Rebuild,
                                                       Backend>;

}  // namespace tess::maintenance
