#!/usr/bin/env python3
"""pairsep.py DIR: F_PAIR_SEP. For every `pairsep ID R from MATE`: the two heads' distance (Chebyshev) at R and 3 / 6 rounds
later (both alive), and whether they were side by side again (within 2) at R + 6."""
import collections, gzip, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'replay_tools'))
from sim import Game  # noqa: E402
d = sys.argv[1]; tot = collections.Counter()
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]
    ev = []
    for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt', errors='replace'):
        p = l.split()
        if len(p) >= 5 and p[0] == 'pairsep' and p[1].isdigit() and p[2].isdigit() and p[4].isdigit():
            ev.append((int(p[1]), int(p[2]), int(p[4])))
    if not ev: continue
    want = {}
    for (i, r, j) in ev:
        for k in (0, 3, 6): want.setdefault(r + k, set()).update((i, j))
    g = Game(os.path.join(d, f)); W, H = g.W, g.H
    pos = {}
    last = -1
    for t, rnd, i in g.run():
        if rnd != last and rnd - 1 in want:
            for j in want[rnd - 1]:
                if j in g.bodies: pos[(j, rnd - 1)] = g.bodies[j][0]
        last = rnd
    def ch(a, b):
        dx = abs(a[0] - b[0]); dy = abs(a[1] - b[1]); return max(min(dx, W - dx), min(dy, H - dy))
    for (i, r, j) in ev:
        ds = []
        for k in (0, 3, 6):
            a, b = pos.get((i, r + k)), pos.get((j, r + k))
            ds.append(ch(a, b) if a and b else None)
        if None in ds: tot['incomplete'] += 1; continue
        tot['n'] += 1
        tot['d0'] += ds[0]; tot['d3'] += ds[1]; tot['d6'] += ds[2]
        if ds[2] <= 2: tot['together_at6'] += 1
for k in sorted(tot): print(k, tot[k])
if tot['n']: print('mean distance at 0 / 3 / 6 rounds: %.2f %.2f %.2f' % (tot['d0'] / tot['n'], tot['d3'] / tot['n'], tot['d6'] / tot['n']))
