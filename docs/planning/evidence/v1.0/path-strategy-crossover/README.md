# Path-strategy crossover campaign

This directory retains advisory evidence for the public C++ grid pathfinding
comparison. It informs API selection; it is not a CI threshold, release gate,
or promise about another machine.

## Question

At what request-count bracket does measured reuse repay its cold setup cost,
and how does that bracket differ between an Apple M3 Max and a Steam Deck?
The campaign also exposes an opt-in grid/request capacity envelope without
deliberately exhausting host memory.

## Controlled method

- Source commit: `fcaa2165a8bd0194578bd0d9ab0a86662f0e90cb`.
- Benchmark-source and runner SHA-256 values are embedded in every result file.
- Build: CMake `bench-only` on M3 and `linux-bench` on Deck; Release, warnings
  as errors.
- Primary grid: 512x512 with 32x32 chunks and dense residency.
- Counts: 1, 2, 4, 8, 10, 16, 32, 64, 100, 128, 256, 512, and
  1,000 requests.
- Sampling: ten independently launched paired repetitions, randomized arm
  order, and median CPU time. The affinity-pinned Steam Deck used a 10 ms
  minimum measured time. The M3 used 100 ms after a shorter preflight proved
  inadequate for its unpinned microsecond-scale cache cells. Every sample,
  order, iteration count, and peak-RSS observation is retained. Cells whose
  per-arm coefficient of variation exceeds 5% are rejected.
- Decision: the empirical range from the 5th through 95th sample percentiles
  of the ten paired right/left time ratios must clear both equality and a 2%
  practical-effect floor. It is not a confidence interval. Near ties are
  inconclusive; later reversals prevent an earlier win from becoming the
  published stable bracket.
- Pairing: both arms receive the same constructed world and request vector.
  Reusable scratch/storage is pre-reserved and each arm receives one untimed
  harness warmup. Route-cache logical state is cleared; cache population,
  field construction, and batch grouping stay timed.
- Correctness: a production A* reference records expected costs outside the
  timed loop; every arm then validates status, endpoints, legal unit steps, and
  independently reconstructed path cost. Primary cells check every request;
  capacity cells deterministically sample up to 32 requests to bound oracle
  work and reduce its chance of dominating the process envelope.
- Counters: A* and unit-field expansions, reconstruction nodes, field builds,
  A* fallbacks, unique goals, unique starts, cache hits, suffix hits, cache
  entries, retained path nodes, and compile-time dense field-page bytes
  accompany time. The weighted batch API does not expose field expansions, so
  no comparable counter is claimed for that arm.

| Environment | Toolchain | Affinity and power |
| --- | --- | --- |
| Apple M3 Max, 64 GB, macOS 26.5.1 | Apple Clang 21.0.0; CMake 4.4.2 | Affinity unavailable; battery charged; single-threaded CPU time |
| Steam Deck, 16 GB, Linux 6.16.12 | Debian Clang 19.1.7; CMake 3.31.6 | Pinned to CPU 2; performance governor; external power online |

Normalized samples, summaries, work counters, and policy metadata are split
by platform and workload family in the result files linked below. Splitting
keeps every public file below the repository token limit without dropping any
of the 1,820 paired repetitions (3,640 arm timings) or 349 capacity
observations. Raw Google Benchmark envelopes are not retained because the
controller preserves the auditable cell observations without machine-local
metadata. [SHA-256 checksums](SHA256SUMS) cover every normalized JSON file.

## Capacity protocol

The binary additionally registers grids 128x128, 256x256, 1,024x1,024,
2,048x2,048, 4,096x4,096, 8,192x8,192, and 16,384x16,384, plus counts
2,048, 4,096, 8,192, 16,384, 32,768, 65,536, and 131,072.
The controller runs one exact registration per process under a declared wall
timeout and records the largest completed rung. Linux uses a hard address-space
limit; macOS uses a 20 ms sampled-RSS watchdog and fails closed when RSS cannot
be observed. These are different resource boundaries and are reported as such.
Each ladder stops after a timeout, resource-limit event, or child failure; that
does not prove every larger cell fails. The all-distinct-goal fixture registers
only counts that fit its unique perimeter-goal set. Weighted grid ladders use
two same-goal requests or two requests per each of eight goals so the batch arm
actually builds fields; the all-distinct ladder intentionally measures fallback.

The larger grid rungs were enabled only after a separate 4,096x4,096
preflight completed with measured headroom on both target hosts. A
32,768x32,768 grid remains deliberately absent: dense node-indexed scratch at
that size can exceed the declared campaign memory budgets before the world,
request results, and retained products are counted.

## Limitations

- The platforms use different compilers and instruction sets. Compare
  crossovers within each platform, not absolute cross-platform speed or IPC.
- The M3 run could not set affinity. CPU time and work counters reduce, but do
  not eliminate, scheduler and system-load effects.
- Request arrays are nested as count grows, so a larger cell includes every
  request from the smaller cell. It can also add a harder request; time need
  not scale linearly.
- Cold route caches answer the population question. Existing cache-hit and
  field-product replay benchmarks remain the authority for warm service.
- Dense world bytes are exact Tess page storage. Route-cache memory is reported
  as entries and retained path nodes because the allocator footprint is not an
  honest portable byte count.
- Peak RSS is the conservative whole harness process, including untimed world
  setup, oracle, warmup, and correctness scratch. It is not a strategy-only
  retained-memory estimate.

## Reproduction

On either host, create a sanitized `environment.json`, build the Release
benchmark binary, then run:

```sh
python3 tools/path_strategy_campaign.py primary \
  --binary build/bench/bench/tess_bench_path_strategy_crossover \
  --source bench/tess_path_strategy_crossover_bench.cc \
  --environment environment.json --output primary.json \
  --memory-limit-gib <conservative-host-budget> \
  --minimum-time <platform-floor-seconds> [--cpu <linux-cpu>]
python3 tools/path_strategy_campaign.py capacity \
  --binary build/bench/bench/tess_bench_path_strategy_crossover \
  --source bench/tess_path_strategy_crossover_bench.cc \
  --environment environment.json --output capacity.json \
  --memory-limit-gib <conservative-host-budget> [--cpu <linux-cpu>]
```

For the Steam Deck, use the repository's pinned Steam Runtime stage, verify,
governor-pin, run, restore, and retrieve protocol:

```sh
tools/steamdeck/deck path-campaign stage <empty-bundle-dir>
tools/steamdeck/deck path-campaign run \
  <bundle-dir> <run-id> <empty-results-dir>
```

Staging requires a clean publication commit and records the frozen SDK digest
plus the locally built image identity. The bundle inventory and retrieved
result set are SHA-256 verified. The remote wrapper requires external power,
pins CPU 2 and every frequency governor for both phases, retains atomic
checkpoints on-device, and restores the prior governors on exit.

## Results

The Steam Deck accepted all 91 primary cells. The M3 accepted 51; the other 40
remain in the files with `accepted: false` because at least one arm exceeded
the predeclared 5% coefficient-of-variation limit. The M3 curve therefore
supports only the accepted brackets and observed wins below, not a complete
cache crossover curve.

| Workload | Apple M3 Max | Steam Deck |
| --- | --- | --- |
| Unit field, open | No accepted field win through 512 | No field win through 1,000 |
| Unit field, room portals | `(10, 16]` | `(4, 8]` |
| Route cache, exact repeats | Cache win observed by 4; lower bound unresolved | `(2, 8]` |
| Route cache, same-goal suffixes | Cache win observed by 8; lower bound unresolved | `(2, 8]` |
| Weighted batch, one goal | Batch win observed by 8; lower bound unresolved | `(2, 4]` |
| Weighted batch, eight goals | First material win at 10 | First material win at 10 |
| Weighted batch, distinct goals | Inconclusive through 1,000 | Inconclusive through 1,000 |

In the controller's raw crossover summary, `lower_exclusive: 0` means no
accepted decisive baseline win was available before the first accepted reuse
win. It is reported here as “win observed by,” never as a literal `(0, n]`
crossover bracket.

The capacity campaign completed 170 of 177 M3 observations and 153 of 172
Deck observations. The remainder were five M3 timeouts, 17 Deck timeouts, and
two fixture limits on each platform. Notable controlled boundaries were:

| Axis | Apple M3 Max | Steam Deck |
| --- | --- | --- |
| Most grid ladders | Completed the 16,384x16,384 ceiling | Completed 8,192x8,192; 16,384x16,384 stopped under the resource/time bounds |
| One-goal batch grid | 8,192x8,192 complete; 16,384x16,384 timed out | Same |
| Eight-goal batch grid | 4,096x4,096 complete; 8,192x8,192 timed out | Same |
| Room-portal A\* requests | 65,536 complete; 131,072 timed out | 16,384 complete; 32,768 timed out |
| One/eight-goal weighted A\* requests | 4,096 complete; 8,192 timed out | One goal matched M3; eight goals timed out at the first 1,000-request capacity rung |
| Reuse-heavy request ladders | Completed the 131,072 ceiling | Completed the 131,072 ceiling |

The 16,384x16,384 Deck processes reported `std::bad_alloc` under the 12 GiB
address-space limit before the controller's 20-second stop. The report retains
both facts rather than reclassifying the recorded timeout. On M3, most
16,384x16,384 cells completed; the all-distinct weighted-batch process peaked
near 9.5 GiB RSS. A larger grid was not registered because its dense scratch
could exceed the declared host budgets.

### Primary evidence

| Workload | Apple M3 Max | Steam Deck |
| --- | --- | --- |
| Unit field, open | [JSON](apple-m3-max-primary-unit-shared-open.json) | [JSON](steam-deck-primary-unit-shared-open.json) |
| Unit field, room portals | [JSON](apple-m3-max-primary-unit-shared-room-portals.json) | [JSON](steam-deck-primary-unit-shared-room-portals.json) |
| Route cache, exact repeats | [JSON](apple-m3-max-primary-route-cache-exact-repeats.json) | [JSON](steam-deck-primary-route-cache-exact-repeats.json) |
| Route cache, same-goal suffixes | [JSON](apple-m3-max-primary-route-cache-same-goal-suffixes.json) | [JSON](steam-deck-primary-route-cache-same-goal-suffixes.json) |
| Weighted batch, one goal | [JSON](apple-m3-max-primary-weighted-one-goal.json) | [JSON](steam-deck-primary-weighted-one-goal.json) |
| Weighted batch, eight goals | [JSON](apple-m3-max-primary-weighted-eight-goals.json) | [JSON](steam-deck-primary-weighted-eight-goals.json) |
| Weighted batch, distinct goals | [JSON](apple-m3-max-primary-weighted-all-distinct-goals.json) | [JSON](steam-deck-primary-weighted-all-distinct-goals.json) |

### Capacity evidence

| Group | Apple M3 Max | Steam Deck |
| --- | --- | --- |
| Unit fields | [JSON](apple-m3-max-capacity-unit-shared.json) | [JSON](steam-deck-capacity-unit-shared.json) |
| Route caches | [JSON](apple-m3-max-capacity-route-cache.json) | [JSON](steam-deck-capacity-route-cache.json) |
| Weighted paths | [JSON](apple-m3-max-capacity-weighted.json) | [JSON](steam-deck-capacity-weighted.json) |

## Conclusion

The decision rule is a platform- and workload-specific bracket, not a fixed
agent threshold. Prefer independent A* at measured left-winning counts and
reuse at measured right-winning counts. Measure the deployment target inside
a bracket or wherever the lower boundary remains unresolved. A measured
performance win from weighted batching requires its counters to show field
construction. All-distinct goals correctly fell back and established no
material winner under this decision rule.

The capacity sweep is a separate operational envelope. A completed high-count
cell does not mean an application can afford that workload inside its frame
budget, and a timeout is not an algorithmic correctness failure.

## Deferred follow-ups

- A multithreaded CPU campaign should measure throughput and tail latency with
  worker-owned scratch/runtime state at physical-core and SMT boundaries. A
  `PathRequestRuntime` is unsynchronized and must not simply be shared by
  workers.
- GPU pathfinding is not currently a Tess benchmarkable capability. The
  WebGPU layer transports consumer-owned pipelines; Tess does not ship a GPU
  pathfinding kernel. A future campaign first needs a correctness-tested
  provider, then separate cold upload/dispatch/readback and warm-resident
  measurements against the CPU oracle.
