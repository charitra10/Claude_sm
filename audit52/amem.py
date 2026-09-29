#!/usr/bin/env python3
"""amem.py REPLAY...: alpha-memory trips: distance at start, who ate the remembered pearl and when."""
import collections, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze
from sim import Game
tot = collections.Counter(); lat = collections.defaultdict(list)
for rp in sys.argv[1:]:
    side = rp[:-7].rsplit('_', 1)[1]
    g = Game(rp); diag = analyze.load_diag(rp[:-7] + '.diag.gz')
    eaten = collections.defaultdict(list); heads = collections.defaultdict(dict)
    pres = collections.defaultdict(list)  # tile -> [(round, present)]
    rnd = 0
    for e in g.rp.events:
        if e[0] == 'round': rnd = e[1]
        elif e[0] == 'tile': pres[(e[1], e[2])].append((rnd, e[3]))
    def present(t, r):
        st = False
        for rr, v in pres.get(t, ()):
            if rr >= r: break
            st = v
        return st
    g = Game(rp)
    for ev in analyze.walk(g):
        if ev[0] == 'turn':
            for p in ev[3]: eaten[p].append((ev[1], ev[2], g.team.get(ev[2])))
            if ev[2] in g.bodies: heads[ev[2]][ev[1]] = g.bodies[ev[2]][0]
    eps = []; cur = {}
    for p in diag['amem']:
        try: i, r, t = int(p[0]), int(p[1]), analyze.xy(p[2])
        except Exception: continue
        e = cur.get(i)
        if e and e[1] == t and r - e[2] <= 12: e[2] = r
        else:
            if e: eps.append((e[0], e[1], e[2], i))
            cur[i] = [r, t, r]
    eps += [(e[0], e[1], e[2], i) for i, e in cur.items()]
    W, H = g.W, g.H
    for r0, t, r1, i in eps:
        h = heads[i].get(r0 - 1) or heads[i].get(r0)
        d = min(abs(h[0] - t[0]), W - abs(h[0] - t[0])) + min(abs(h[1] - t[1]), H - abs(h[1] - t[1])) if h else -1
        ev = [x for x in eaten.get(t, ()) if r0 <= x[0] <= r1 + 15]
        if not present(t, r0): k = 'stale'
        elif not ev: k = 'nobody'
        elif ev[0][1] == i: k = 'self'
        elif ev[0][2] == side: k = 'teammate'
        else: k = 'enemy'
        tot[k] += 1; lat[k].append((d, r1 - r0 + 1, (ev[0][0] - r0) if ev else -1))
for k, v in tot.items():
    ds = [x[0] for x in lat[k]]; ln = [x[1] for x in lat[k]]; w = [x[2] for x in lat[k] if x[2] >= 0]
    print('%-9s %4d  start dist %.1f  rounds chosen %.1f  eaten after %s' % (k, v, sum(ds) / len(ds), sum(ln) / len(ln),
          '%.1f' % (sum(w) / len(w)) if w else '-'))
