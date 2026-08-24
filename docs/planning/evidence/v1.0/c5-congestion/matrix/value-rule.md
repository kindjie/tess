# Amendment-3 value rule: recorded computation

Inputs: the 384 `cell,` lines across `m3-*.txt` (6 scenarios x 64
supported populations). Ratio per cell: priced ticks / canonical
ticks; canonical cap-censored cells (41, all tip, N >= 384) enter at
the cap value 5000, conservatively understating any priced win.
Bootstrap: 2000 resamples, seed 0xC5C5, percentile CI.

```python
import glob, math, random
cells = []
for path in sorted(glob.glob('m3-*.txt')):
    for line in open(path):
        if line.startswith('cell,'):
            p = line.strip().split(',')
            cells.append((p[1], int(p[2]), int(p[6]), int(p[11])))
logs = [math.log(pt / ct) for _, _, ct, pt in cells]
gm = math.exp(sum(logs) / len(logs))
rng = random.Random(0xC5C5)
boots = sorted(
    math.exp(sum(rng.choice(logs) for _ in logs) / len(logs))
    for _ in range(2000))
lo, hi = boots[int(0.025 * 2000)], boots[int(0.975 * 2000) - 1]
```

Result: pooled gm = 0.4677, CI [0.4310, 0.5103], over 384 cells.
Rule (pre-declared in amendment 3): gm <= 0.95 and CI high < 1.0 ->
**PASS**.

Per-scenario geometric means (same per-cell ratios):

| scenario | gm |
|---|---|
| tip | 0.2028 |
| two-gates | 0.2203 |
| browser-guard | 0.2320 |
| open | 0.7491 |
| four-gates | 0.9031 |
| goal-wall | 1.4934 |

Extremes: best cells two-gates N in {880, 960, 992} at 0.11x;
worst cells goal-wall N in {432, 496, 512} at 1.89x (+89%).
Canonical fails to complete 41 cells (tip, every N >= 384, stranded at
the 5000-tick cap); the priced arm completes all 384 cells with zero
crowd-blocked and zero durably-unreachable anywhere.
