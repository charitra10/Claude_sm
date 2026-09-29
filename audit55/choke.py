#!/usr/bin/env python3
"""choke.py DIR [-v]: F_CHOKE audit. Every entry of ours into a dead end of the map (a tree-shaped pocket behind a bridge
edge, <= 48 tiles) with fewer than 2 live pearls inside (pearls lying there + tiles due within 1 round): the entrant's length,
its mode that turn, and what else it could have done (free first steps other than into the pocket)."""
import collections, gzip, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import Static, walk  # noqa: E402
from sim import Game, DI  # noqa: E402

d = sys.argv[1]; verbose = '-v' in sys.argv
tot = collections.Counter(); ex = []
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]; ours = stem.rsplit('_', 1)[1]
    modes = {}; enters = set()
    for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt', errors='replace'):
        p = l.split()
        if len(p) < 5 or not p[1].isdigit() or not p[2].isdigit(): continue
        if p[0] == 'turn': modes[(int(p[1]), int(p[2]))] = p[4]
        elif p[0] == 'chokeenter': enters.add((int(p[1]), int(p[2])))
    g = Game(os.path.join(d, f)); st = Static(g)
    cds = []; r = 0
    for e in g.rp.events:
        if e[0] == 'round': r = e[1]
        elif e[0] == 'countdown': cds.append((r, (e[1], e[2]), e[3]))
    due = {}; ci = 0
    prev = {}; free_before = {}
    for ev in walk(g):
        if ev[0] != 'turn': continue
        _, rnd, i, ate = ev
        while ci < len(cds) and cds[ci][0] <= rnd:
            due[cds[ci][1]] = cds[ci][0] + cds[ci][2]; ci += 1
        if g.team.get(i) != ours or i not in g.bodies: continue
        b = g.bodies[i]; h = b[0]
        ph = prev.get(i)
        if ph is not None:
            k = st.entry.get((ph, h))
            if k is not None and st.pockets[k]['tree'] and len(st.pockets[k]['tiles']) >= 2:
                tiles = st.pockets[k]['tiles']
                live = sum(1 for q in tiles if q in g.pearls) + len(ate) + \
                    sum(1 for q in tiles if q not in g.pearls and 0 <= due.get(q, -10) - rnd <= 1)
                L = len(b) - len(ate)
                tot['entries'] += 1
                if live < 2:
                    mode = modes.get((i, rnd), '?')
                    alt = free_before.get(i, -1)
                    cat = 'len2' if L <= 2 else 'len3' if L == 3 else 'len4p'
                    tot['lt2_' + cat] += 1
                    tot['lt2_mode_' + mode] += 1
                    tot['lt2_alt%d' % min(alt, 2)] += 1
                    if (i, rnd) in enters: tot['lt2_chokeenter'] += 1
                    if verbose: ex.append((stem, i, rnd, L, live, mode, alt, len(tiles)))
        prev[i] = h
        # free first steps from here (for the next turn), not counting our neck
        n_free = 0
        for dd in DI:
            n = g.dest(h, dd)
            if n is None or (len(b) > 1 and n == b[1]) or n in g.occ: continue
            kk = st.entry.get((h, n))
            if kk is not None and st.pockets[kk]['tree']: continue
            n_free += 1
        free_before[i] = n_free
for k in sorted(tot): print('%-26s %d' % (k, tot[k]))
for e in ex[:40]: print(e)
