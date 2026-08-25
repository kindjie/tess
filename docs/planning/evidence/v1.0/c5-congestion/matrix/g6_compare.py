# G6 (issue #256 amendment 3): the classification tables must be
# identical across platforms. Gate columns: scenario, N, and both arms'
# arrived/crowd_blocked/unreachable plus turnaround flags. Tick counts
# are compared separately and reported, but do not gate G6 -- the
# registered standard is classification identity.
import glob, os, sys

def load(prefix, d):
    cells = {}
    for path in glob.glob(os.path.join(d, prefix + '*.txt')):
        for line in open(path):
            if not line.startswith('cell,'):
                continue
            p = line.strip().split(',')
            key = (p[1], int(p[2]))
            cls = (p[3], p[4], p[5], p[7], p[8], p[9], p[10], p[12])
            ticks = (int(p[6]), int(p[11]))
            cells[key] = (cls, ticks)
    return cells

SCENARIOS = (
    "open", "tip", "two-gates", "four-gates", "goal-wall",
    "browser-guard", "browser-incremental",
)
EXPECTED = {(s, n) for s in SCENARIOS for n in range(16, 1025, 16)}

m3 = load('m3-', sys.argv[1])
deck = load('deck-', sys.argv[2])
assert set(m3) == EXPECTED and set(deck) == EXPECTED, (
    len(m3), len(deck), len(EXPECTED))
cls_diff = [k for k in m3 if m3[k][0] != deck[k][0]]
tick_diff = [k for k in m3 if m3[k][1] != deck[k][1]]
total = len(EXPECTED)
print(f"G6 classification identity over {total} cells: "
      f"{'PASS' if not cls_diff else 'FAIL ' + str(cls_diff[:10])}")
print(f"tick equality (reported, non-gating): "
      f"{'identical on all ' + str(total) + ' cells' if not tick_diff else str(len(tick_diff)) + ' cells differ: ' + str(tick_diff[:10])}")
sys.exit(0 if not cls_diff else 1)
