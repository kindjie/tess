# Supported-population matrix: retained captures

Pre-registration: issue #269, the frozen protocol posted before the
first cell ran, plus a pre-registered re-run of the
`browser-incremental` slice posted before that slice was repeated.

Two platforms, both built from the same source revision:

- `m3-*.txt` — Apple M3, macOS, arm64, Release `examples` preset.
- `deck-*.txt` — Steam Deck, SteamOS, x86_64 Zen 2, built for x86_64 in
  the Steam Runtime container so the SDK matches.

8 scenarios x 64 populations (16..1024 step 16) x 5 arms — canonical,
cooling, escalating stall, stalled+cooling, and queue detection with
spreading — with a per-arm replay. 4,608 runs a side.

## Provenance, and the one slice captured twice

Every capture except `browser-incremental` comes from the original run
at `9a8a04ca`. The `browser-incremental` slice was re-run in full at
`3c77b4ed` after review found that its wall admission had started at
tick 0 rather than the registered tick 4; the batch scenarios were
meant to move to tick 0 and that scenario was not. The complete slice
was replaced atomically — all 64 populations, every arm, both
platforms, replays included — because the record's discipline is one
definition per scenario across a round.

Mixing two capture revisions in one round is only sound if the change
was scenario-local, so that was checked rather than assumed: 70 cells
spanning the other seven scenarios, at both ends of the population
range, were re-run at `3c77b4ed` and reproduce the recorded captures
bit-identically. The re-run changed 306 of 320 browser-incremental
cells' tick counts, which is why replacing the slice mattered.

## Capture format

One row per cell, mirroring the c5 precedent's compact form rather than
the runner's verbose output, which exceeds the repository's per-file
token limit:

    arm,agents,ticks,arrived,crowd_blocked,unreachable,turnaround,
    walls,expansions,scoped_replans,replay

`replay=match` records that the per-arm replay reproduced the row
exactly; `replay=single` marks the canonical arm, which runs once.
Across every capture there are 4,097 replayed cells and **zero**
`replay=DIFFER`. The gates below recompute from these rows and
reproduce the original results cell for cell.

The Deck leg was interrupted once and resumed from its per-cell
markers. The cell in progress at that moment had already written output
and ran again on resume, so six cells were captured twice — including
one canonical row, which the format otherwise never duplicates. Every
duplicate matched its first capture exactly, which is corroboration
that the resumed run was consistent rather than a defect; the rows are
recorded once, in the format's canonical form.

## Results

| gate | outcome |
|---|---|
| G1 retention | 0 failures for all four arms |
| G2 no-worse | 6 failures, all queue+spread on maze |
| G3 re-rejection | queue detection with spreading is REJECTED |
| G4 replay identity | 0 mismatches, either platform |
| G5 cross-platform identity | 0 divergent cells of 2,560 |
| G6 construction | 0 refusals |

Cooling, escalating stall, and stalled+cooling complete every cell on
both platforms, including all 83 where canonical strands agents. Queue
detection with spreading strands more agents than canonical at
populations 752, 864, 896, 912, 944 and 960 — interleaved with passing
populations, which is the defect class the full sweep exists to catch
and a sampled pilot would have missed.

The re-run slice passed every gate on both platforms with zero
refusals, and the pre-registration committed to reporting that outcome
whatever it turned out to be.
