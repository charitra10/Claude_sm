#!/usr/bin/env python3
"""scoutarr.py DIR: F_MIRROR_SCOUT arrivals (DIAG `scoutarrive`): what is at the spot when the scout gets there (our other
heads, enemy heads and pearls within 3, ground truth), which half of the map it is on (nearer our spawn or theirs), and
what the scout eats in the next 10 rounds."""
import collections, gzip, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'replay_tools'))
from sim import Game  # noqa: E402

d = sys.argv[1]; tot = collections.Counter()
def cheb(a, b, W, H):
    dx = abs(a[0] - b[0]); dy = abs(a[1] - b[1]); return max(min(dx, W - dx), min(dy, H - dy))
def man(a, b, W, H):
    dx = abs(a[0] - b[0]); dy = abs(a[1] - b[1]); return min(dx, W - dx) + min(dy, H - dy)
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]; ours = stem.rsplit('_', 1)[1]
    arr = {}
    for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt', errors='replace'):
        p = l.split()
        if len(p) >= 4 and p[0] == 'scoutarrive' and p[1].isdigit() and p[2].isdigit() and ',' in p[3]:
            arr[(int(p[1]), int(p[2]))] = tuple(map(int, p[3].split(',')))
    if not arr: continue
    g = Game(os.path.join(d, f)); W, H = g.W, g.H
    spawn = {t: [b[0] for i, b in g.bodies.items() if g.team[i] == t] for t in 'AB'}
    them = 'B' if ours == 'A' else 'A'
    pend = []   # (id, r0, side, ...)
    eat = collections.Counter()
    cur = None; rnow = 0
    for t, rnd, i in g.run():
        key = (i, rnd)
        if key in arr and g.team.get(i) == ours and i in g.bodies:
            sp = arr[key]
            mine = sum(1 for j, b in g.bodies.items() if j != i and g.team[j] == ours and cheb(b[0], sp, W, H) <= 3)
            enem = sum(1 for j, b in g.bodies.items() if g.team[j] != ours and cheb(b[0], sp, W, H) <= 3)
            pearls = sum(1 for p in g.pearls if cheb(p, sp, W, H) <= 3)
            dus = min(man(sp, q, W, H) for q in spawn[ours]); dth = min(man(sp, q, W, H) for q in spawn[them])
            side = 'our_half' if dus < dth else 'their_half' if dth < dus else 'middle'
            pend.append([i, rnd, side, mine, enem, pearls, len(g.bodies[i])])
    # pearls eaten per dragon per round
    g = Game(os.path.join(d, f)); r = 0; cur = None
    ate = collections.defaultdict(int)
    for e in g.rp.events:
        if e[0] == 'round': r = e[1]
        elif e[0] == 'turn': cur = e[1]
        elif e[0] == 'tile' and not e[3] and cur is not None: ate[(cur, r)] += 1
    for (i, r0, side, mine, enem, pearls, L) in pend:
        got = sum(ate.get((i, r), 0) for r in range(r0 + 1, r0 + 11))
        if '-v' in sys.argv and mine == 0 and enem == 0 and pearls >= 4 and got == 0:
            print('ex', stem, i, r0, arr[(i, r0)], 'pearls', pearls)
        for k in (side, 'mates%d' % min(mine, 2), 'enemies%d' % min(enem, 2), 'all'):
            tot[k + '_n'] += 1; tot[k + '_ate'] += got; tot[k + '_pearls'] += pearls
for k in sorted(tot):
    if k.endswith('_n'):
        b = k[:-2]; n = tot[k]
        print('%-14s n %4d  ate/arrival %.2f  pearls within 3 at arrival %.2f' % (b, n, tot[b + '_ate'] / n, tot[b + '_pearls'] / n))
