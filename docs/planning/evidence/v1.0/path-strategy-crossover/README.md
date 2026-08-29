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

- Source base: `4780d718f5db87a3bfce3440b7dcbdf59dc43cc0`.
- Benchmark-source and runner SHA-256 values are embedded in every result file.
- Build: CMake `bench-only`, Release, warnings as errors.
- Primary grid: 512x512 with 32x32 chunks and dense residency.
- Counts: 1, 2, 4, 8, 10, 16, 32, 64, 100, 128, 256, 512, and
  1,000 requests.
- Sampling: ten independently launched paired repetitions, randomized arm
  order, a 10 ms minimum measured time, and median CPU time. Every sample,
  order, iteration count, and peak-RSS observation is retained. Cells whose
  per-arm coefficient of variation exceeds 5% are rejected.
- Decision: the paired right/left time-ratio interval from the 5th through
  95th sample percentiles must clear both equality and a 2% practical-effect
  floor. Near ties are inconclusive; later reversals prevent an earlier win
  from becoming the published stable bracket.
- Pairing: both arms receive the same constructed world and request vector.
  Route-cache clearing, field construction, and batch grouping stay timed.
- Correctness: a production A* reference records expected costs outside the
  timed loop; every arm then validates status, endpoints, legal unit steps, and
  independently reconstructed path cost. Primary cells check every request;
  capacity cells deterministically sample up to 32 requests so the oracle
  cannot become the failure mode.
- Counters: A* and unit-field expansions, reconstruction nodes, field builds,
  A* fallbacks, unique goals, unique starts, cache hits, suffix hits, cache
  entries, retained path nodes, and compile-time dense field-page bytes
  accompany time. The weighted batch API does not expose field expansions, so
  no comparable counter is claimed for that arm.

| Environment | Toolchain | Affinity and power |
| --- | --- | --- |
| Apple M3 Max, 64 GB, macOS 26.5.1 | Apple Clang 21.0.0; CMake 4.4.2 | Affinity unavailable; battery charged; single-threaded CPU time |
| Steam Deck, 16 GB, Linux 6.16.12 | Ubuntu Clang 18.1.3; CMake 3.28.3 | Pinned to CPU 2; performance governor; external power online |

Normalized samples, summaries, work counters, and policy metadata live in the
four platform/mode result files linked from the results section below. Raw
Google Benchmark envelopes are not retained because the controller preserves
the auditable cell observations without machine-local metadata.

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
  --memory-limit-gib <conservative-host-budget> [--cpu <linux-cpu>]
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

Results are added here only after both campaigns complete and their method,
measurements, and interpretation pass review.

## Conclusion

The decision rule is a platform- and workload-specific bracket, not a fixed
agent threshold. Use independent A* below the first observed reuse win, then
confirm the same paired cell on the deployment target. Weighted batching is
valuable only when its counters show field construction; all-distinct goals
correctly fall back and should remain close to independent weighted A*.

The capacity sweep is a separate operational envelope. A completed high-count
cell does not mean an application can afford that workload inside its frame
budget, and a timeout is not an algorithmic correctness failure.
