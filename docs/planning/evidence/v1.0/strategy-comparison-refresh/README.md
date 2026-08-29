# Strategy-comparison table refresh, two platforms

The third pre-1.0 audit round found that the timing table in
`docs/pathfinding-strategy-comparison.md` recorded exact values with no
source commit, toolchain, or retained artifact. Rather than disclaim the
numbers, this refresh regenerated them under recorded conditions on two
platforms and retained the raw output here.

## Conditions

Both platforms measured the same eight benchmarks from the same source
commit, `d653d813`, single-threaded, ten repetitions at one second
minimum per repetition, `--benchmark_report_aggregates_only`.

| | Apple M3 Max | Steam Deck (LCD) |
| --- | --- | --- |
| Build | `bench-only` preset, Release, Apple Clang 21.0.0, arm64 | `linux-bench` preset, Release, Clang 19.1.7 in the pinned steamrt4 SDK, x86_64 Zen 2 |
| Run environment | Developer workstation, load averages 6.44/5.76/4.95 sampled at completion, no affinity pinning | Stock SteamOS outside the container, load average 0.16, external power, `performance` governor, edge temperature at or below 49 C after the run |
| Median CV | 0.47-1.09% | 0.03-0.57% |
| Raw output | `m3-max.txt` | `steam-deck.txt` |

The Deck run is not governor-pinned by the campaign wrapper (its
non-interactive sudo rule is not installed on the device), but the
governor already reported `performance` before and after the run, so the
pin would have been a no-op. The M3 host was not idle; its load average
is disclosed above and its per-benchmark CVs stayed at or under 1.09%.

## Medians

| Benchmark | M3 Max | Steam Deck |
| --- | ---: | ---: |
| `astar_batch_100_shared_room_portals_512x512` | 18.28 ms | 68.96 ms |
| `distance_field_batch_100_shared_room_portals_512x512` | 2.91 ms | 5.52 ms |
| `astar_batch_100_mixed_repeated_room_portals_512x512` | 50.62 ms | 191.92 ms |
| `cached_astar_batch_100_mixed_repeated_room_portals_512x512` | 15.25 ms | 65.80 ms |
| `astar_batch_100_suffix_open_512x512` | 112.52 us | 259.19 us |
| `cached_astar_batch_100_suffix_open_512x512` | 17.84 us | 24.66 us |
| `weighted_astar_batch_100_multigoal_sparse_512x512` | 467.98 ms | 904.07 ms |
| `weighted_batch_planner_100_multigoal_sparse_512x512` | 44.80 ms | 75.44 ms |

## What the refresh established

- **The unprovenanced table reproduced.** The published M3 ratios
  (6.4x, 3.4x, 6.5x, 10.4x) match the refreshed ones (6.3x, 3.3x, 6.3x,
  10.4x) within 0.2, so the original run's conclusions were sound
  even though its conditions were lost.
- **The ordering holds on both architectures.** Every reuse strategy
  beats its independent-search baseline on both platforms. The
  magnitudes differ with the machine: the field and planner strategies
  win *bigger* on Zen 2 (12.5x and 12.0x vs 6.3x and 10.4x), while the
  exact route cache wins somewhat less (2.9x vs 3.3x).
- Work counters were identical across platforms and repetitions for
  every benchmark, so the comparisons time the same work.
