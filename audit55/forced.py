#!/usr/bin/env python3
"""forced.py DIR: F_REENTRY. A camper whose own `camp` line said "stay" (leave 0) and whose head is outside that chamber on
its next turn went out without meaning to (the forced exit F_REENTRY is about). Did it stand in the chamber again within
REENTRY_TTL = 12 rounds, and how did it end up."""
import collections, gzip, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import Static  # noqa: E402
from sim import Game  # noqa: E402
d = sys.argv[1]; tot = collections.Counter()
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]; ours = stem.rsplit('_', 1)[1]
    stay = set()
    for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt', errors='replace'):
        p = l.split()
        if len(p) >= 26 and p[0] == 'camp' and p[1].isdigit() and p[2].isdigit() and p[17] == '0':
            stay.add((int(p[1]), int(p[2])))
    if not stay: continue
    g = Game(os.path.join(d, f)); st = Static(g)
    last = {}   # id -> (round, chamber) of its last turn
    hist = collections.defaultdict(list)
    for t, rnd, i in g.run():
        if g.team.get(i) != ours or i not in g.bodies: continue
        c = st.chamber_of.get(g.bodies[i][0])
        hist[i].append((rnd, c))
    for i, hh in hist.items():
        for k in range(1, len(hh)):
            r0, c0 = hh[k - 1]; r1, c1 = hh[k]
            if c0 is None or c1 == c0 or (i, r0) not in stay or len(st.chambers[c0]['tiles']) < 5: continue
            tot['forced_exits'] += 1
            back = [r for (r, c) in hh[k:] if c == c0 and r <= r1 + 12]
            if back: tot['back12'] += 1; tot['back_rounds'] += back[0] - r1
            else:
                dr = g.dead.get(i)
                tot['died12' if dr and dr[0] <= r1 + 12 else 'stayed_out'] += 1
for k in sorted(tot): print('%-14s %d' % (k, tot[k]))
