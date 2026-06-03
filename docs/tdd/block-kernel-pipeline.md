# TDD: Block Kernel / Pipeline

## 1. Summary

This document defines the expert block-local kernel API and the internal/advanced block-lazy pipeline API.

The primary public interface remains queued operations. Block/kernel and pipeline APIs implement efficient execution internally and support expert users writing custom high-performance systems.

## 2. Goals

- Provide fast chunk/block-local execution primitives.
- Provide expert escape hatch for custom kernels.
- Support planner-generated kernels and fused passes.
- Support frontier algorithms such as BFS, distance fields, and flood fills.
- Support allocation-free tile, chunk, frontier, and field iteration.
- Make write policies explicit.
- Support block-level parallelism.
- Support diagnostics for materialization, writes, allocations, and bandwidth.
- Preserve 2D, vertical 2D, and 3D behavior through Shape traits.

## 3. Non-goals

- Not the primary game-facing API.
- Not a replacement for queued operations.
- No hidden full-world scans.
- No hidden materialization.
- No implicit allocation in hot adapters.
- No arbitrary unsafe mutation in parallel callbacks.
- No direct ECS structural mutation inside block jobs.
- No GPU implementation in v1.

## 4. Core concepts

- Block
- BlockCtx
- ChunkView
- Kernel
- Pipeline
- Domain
- Terminal
- WritePolicy
- Scratch

## 5. Block-local execution

Hot code works on resolved chunks and typed field spans.

Properties:

- fields resolved once per chunk
- spans contiguous
- no global TileKey decode per tile
- no string field lookup
- write ownership explicit
- block list built by planner

## 6. BlockCtx and ChunkView

BlockCtx provides chunks, field spans, masks, metadata, local coordinate conversion, boundary helpers, scratch, and diagnostics.

ChunkView exposes ChunkKey, ChunkCoord3, bounds, local tile iteration, field/mask spans, metadata, and local coord/id conversion.

## 7. Kernel declaration

Kernels declare:

- reads
- writes
- domain kind
- write policy
- scratch needs
- boundary needs
- deterministic behavior
- backend eligibility

## 8. Write policies

- ReadOnly
- UniquePerTile
- UniquePerChunk
- DoubleBuffered
- Atomic
- ThreadLocalThenMerge
- AppendOnly
- Unsafe

## 9. Boundary handling

Strategies:

- InteriorOnly
- BoundaryCallback
- Halo
- TwoPhase
- AtomicBoundary

Kernels declare boundary needs. Planner chooses strategy.

## 10. Pipeline API purpose

Pipeline API supports internal/expert composition:

- map
- filter
- flat_map
- zip
- reduce
- scan/prefix
- frontier expansion
- explicit terminals

This is block-aware, not ordinary std::ranges.

## 11. Block-lazy range model

Blocks/chunks are the unit of parallel splitting. Items inside each block are processed sequentially and lazily. Adapters preserve block structure where possible.

## 12. Pipeline sources and adapters

Sources:

- blocks(domain)
- resident_chunks
- dirty_chunks
- active_chunks
- visible_chunks
- frontier
- goals
- spatial entity buckets
- explicit chunk lists

Adapters include map, filter, flat_map, zip, scan, reduce, neighbors, transitions, passable, reachable, claim_unvisited, relax_distance, dedupe_tiles.

## 13. Terminals

Terminals are explicit:

- for_each
- parallel_for_each
- reduce
- collect_into
- to_sequence
- to_sequence_allocating
- to_frontier
- to_mask
- to_events
- to_field
- to_block_summaries

Allocating terminals must be clearly named.

## 14. Frontier pipelines

Frontier-style algorithms must avoid neighbor list allocation, per-element heap allocation, and unbounded buffers. Use visited/generation markers and caller/planner-owned output buffers.

## 15. Filter/materialization strategy

Implementation strategy:

- split work by blocks/chunks
- process items inside each block lazily
- compute compact per-block metadata when needed
- prefix sum counts if materialization requires stable offsets
- use a second local pass only when materialization requires it
- fuse adjacent operations where safe

Rationale: many simple parallel algorithms are memory-bandwidth limited. Avoiding intermediate writes can be more valuable than reducing arithmetic. Block-local lazy execution preserves parallelism across blocks while composing operations without materializing every intermediate stage.

## 16. Memory management

Rules:

- no per-tile allocation
- no per-neighbor allocation
- no hidden vector growth
- caller/planner provides scratch
- transient allocations use arenas
- frontiers use reusable buffers
- field products use slabs/pools

## 17. Fusing passes

Fuse when domains, ordering, dependencies, write policies, diagnostics, and estimated bandwidth wins allow it. Do not fuse when it hurts working set, debugging, determinism, or load balance.

## 18. Interaction with planner/path/ECS

Planner uses block/pipeline layer internally. Path/field systems use it for frontier expansion, reverse BFS/Dijkstra, reachability, and influence. Block kernels do not mutate ECS.

## 19. Diagnostics

Report domain, blocks, items read/emitted/filtered, materialization points, temp/final bytes, allocations, atomics, boundary accesses, block timings, load imbalance, and fusion decisions.

## 20. API sketch

```cpp
tiles::block::parallel_for_blocks(world, blocks, WritePolicy::UniquePerTile, fn);
```

Pipeline examples use explicit terminals such as `.to_frontier(next_buffer, scratch)`.

## 21. Validation

Compile-time validation covers schema, fields, write policy, domains, boundaries, adapters, terminals, and unsafe APIs.

Runtime validation covers residency, spans, scratch, buffer capacity, generation counters, and debug write policy checks.

## 22. Tests

Test block iteration, field spans, dirty domains, boundary helpers, write policies, pipelines, frontier expansion, no allocation, fused/unfused equivalence, and degenerate axes.

## 23. Benchmarks

- single-chunk vs flat array
- chunked 2D update
- vertical 2D update
- 3D update
- boundary stencil
- dirty update
- map/filter/reduce
- frontier expansion
- fused vs materialized

## 24. Acceptance criteria

- Block kernels run over resolved chunks without per-tile global lookup.
- Field spans are contiguous and typed.
- Write policies are explicit.
- Pipelines support frontier/filter/map/reduce.
- Materialization is explicit.
- Hot adapters do not allocate.
- Diagnostics reveal materialization/allocation/bandwidth hazards.
