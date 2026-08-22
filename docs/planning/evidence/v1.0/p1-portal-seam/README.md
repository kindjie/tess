# P1 portal-seam screen: retained evidence

Source under measurement: merged `main` `11cf6428`, clean tree. Both arms
build from one worktree and differ only by `-DTESS_P1_SEAM_STANDIN`.

## Devices and binaries

| Device | Build | Base binary SHA-256 | Stand-in binary SHA-256 |
| --- | --- | --- | --- |
| Apple M3 (macOS, arm64) | CMake Release, AppleClang, default generator | `599be175…4dbf3f` | `09d04b5c…7c6bb9` |
| Steam Deck (Zen 2, SteamOS 3.x, 8 CPUs) | steamrt4 SDK container, clang 19, CMake Release, Ninja | `81f0897b…d4c720` | `b84dc761…fe947e` |

Every Steam Deck CPU was at the `performance` governor for the whole
campaign, verified before each pass. Absolute times are never compared
between devices.

## Exact invocations

No custom sentinel configuration exists. `--suspects` replaces the sentinel
set at the command line, so the committed `bench/sentinels.json` was passed
unchanged and supplied the parameters (10 repetitions, 8% relative effect
floor, 2000 ns materiality floor, 2000 bootstrap resamples, 95% confidence).

```sh
SUSPECTS=path/agent_tick_100_weighted_goal_churn_portal_512x512,\
path/agent_tick_100_weighted_fresh_churn_portal_512x512,\
path/agent_tick_100_unit_dirty_world_edit_512x512,\
path/agent_tick_100_unit_dirty_onpath_edit_scoped_512x512,\
path/agent_tick_100_weighted_shared_dirty_512x512

# calibration (A/A): base against itself
python3 tools/paired_bench.py --mode shadow \
  --base-binary <base> --head-binary <base> \
  --sentinels bench/sentinels.json --suspects "$SUSPECTS" \
  --seed 130013   # Steam Deck leg used 130019

# ceiling (A/B): base against the stand-in
python3 tools/paired_bench.py --mode shadow \
  --base-binary <base> --head-binary <standin> \
  --sentinels bench/sentinels.json --suspects "$SUSPECTS" \
  --seed 130013   # Steam Deck leg used 130037
```

## Files

- `m3-calibration-aa.json`, `deck-calibration-aa.json` — A/A passes.
- `m3-ceiling-ab.json`, `deck-ceiling-ab.json` — the ceiling comparisons,
  including the tool's confirmation rerun for any flagged cell.
- `m3-counter-identity.txt`, `deck-counter-identity.txt` — per-arm counters at
  a fixed 2000 iterations on the two decision cells. These substantiate the
  claim that the arms perform identical downstream work on those cells.
- `seam-standin.md` — the measured scaffolding, recorded as source.

## Two limits of what is retained

The artifacts hold medians, bootstrap intervals, and the confirmation rerun,
not the per-round sample arrays; `tools/paired_bench.py` summarises before
writing. Reproduction therefore means rerunning the commands above against
the recorded binaries, not re-analysing retained samples.

The counter-identity dumps were captured at a fixed iteration count on the two
decision cells only, which is a different configuration from the timed runs
(all five cells in one process per round). They do not cover the guardrail
cells, and the record does not claim they do — see the fragment's note on the
stand-in's process-lifetime table.
