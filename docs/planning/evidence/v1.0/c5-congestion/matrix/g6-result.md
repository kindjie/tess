# G6: cross-platform comparison (recorded result)

Same source, two builds: M3 (clang, ARM64, macOS; `m3-*.txt`) and
Steam Deck (gcc 14, x86-64 Linux, -static; `deck-*.txt`), compared by
`g6_compare.py`. The registered gate is classification-table identity;
tick equality is reported separately and does not gate.

```
G6 classification identity over 448 cells: PASS
tick equality (reported, non-gating): identical on all 448 cells
```

Both platforms produce byte-identical `cell,` tables: every
classification, turnaround flag, AND tick count matches on all 448
cells — the demo's simulation is fully deterministic across the two
compilers and architectures.
