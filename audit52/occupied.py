#!/usr/bin/env python3
"""occupied.py REPLAY...: each entry into a small chamber a teammate is already in: who, family ties, reservation heard."""
import collections, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze
from sim import Game, DI
tot = collections.Counter()
for rp in sys.argv[1:]:
    side = rp[:-7].rsplit('_', 1)[1]
    g = Game(rp); st = analyze.Static(g); diag = analyze.load_diag(rp[:-7] + '.diag.gz')
    pid_of = {}
    for k, ch in enumerate(st.chambers):
        for t in ch['tiles']:
            for d in range(4):
                kind, pid = g.edge(t[0], t[1], d)
                if kind == 2: pid_of[k] = pid
    heard = collections.defaultdict(list)
    for p in diag['resvrecv']:
        try: heard[(int(p[0]), int(p[2]))].append(int(p[1]))
        except Exception: pass
    parent = {}; since = {}; prevc = {}; mode = {}
    for ev in analyze.walk(g):
        if ev[0] == 'split': parent[ev[3]] = ev[2]; continue
        if ev[0] != 'turn' or g.team.get(ev[2]) != side or ev[2] not in g.bodies: continue
        _, rnd, i, ate = ev
        c = st.chamber_of.get(g.bodies[i][0])
        if c is not None and prevc.get(i) != c and i in prevc:
            born_here = parent.get(i) is not None and g.born.get(i, -9) >= rnd - 1
            inside = [j for j, b in g.bodies.items() if j != i and g.team[j] == side and any(st.chamber_of.get(q) == c for q in b)]
            inside = [j for j in inside if since.get(j, (None, 0))[0] == c and rnd - since[j][1] >= 8]
            if inside and not born_here:
                fam = any(parent.get(j) == i or parent.get(i) == j or parent.get(parent.get(j)) == i for j in inside)
                pid = pid_of.get(c, -1)
                h = any(rnd - 30 <= r <= rnd for r in heard.get((i, pid), ()))
                tot['occupied_entries'] += 1
                tot['family' if fam else 'stranger'] += 1
                tot['heard_resv' if h else 'no_resv_heard'] += 1
                tot['prevmode_' + mode.get(i, '?')] += 1
                tot['len%s' % min(len(g.bodies[i]), 5)] += 1
        if c is None: since.pop(i, None)
        elif since.get(i, (None,))[0] != c: since[i] = (c, rnd)
        prevc[i] = c
        mode[i] = g.ind.get(i, ':?').partition(':')[2]
print(dict(sorted(tot.items())))
