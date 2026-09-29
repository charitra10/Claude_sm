#!/usr/bin/env python3
"""hazards.py REPLAY [N]: each hazard episode (owner, mouth): static pocket or not, what closed it, how it ended."""
import collections, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze
from sim import Game, DI
rp = sys.argv[1]; side = rp[:-7].rsplit('_', 1)[1]
g = Game(rp); st = analyze.Static(g)
diag = analyze.load_diag(rp[:-7] + '.diag.gz')
eps = collections.OrderedDict()
for p in diag['hazard']:
    try: o, r, mo, d, at, L, sz, pe, fwd = int(p[0]), int(p[1]), analyze.xy(p[2]), int(p[4]), analyze.xy(p[6]), int(p[8]), int(p[10]), int(p[12]), int(p[14])
    except Exception: continue
    eps.setdefault((o, mo, d), []).append((r, at, L, sz, pe, fwd))
# walk to get owner fates
fate = {}; heads = collections.defaultdict(list)
for ev in analyze.walk(g):
    if ev[0] == 'turn' and ev[2] in g.bodies: heads[ev[2]].append((ev[1], g.bodies[ev[2]][0]))
    if ev[0] == 'death': fate[ev[2]] = (ev[1], ev[5])
cnt = collections.Counter()
for (o, mo, d), rs in eps.items():
    r0 = rs[0][0]; outside = g.step(mo, DI[(d + 2) % 4])
    k = st.entry.get((outside, mo))
    walked = next((r for r, h in heads[o] if r0 < r <= r0 + 60 and h == outside), None)
    f = fate.get(o)
    kind = 'static-%s(%d)' % ('tree' if st.pockets[k]['tree'] else 'room', len(st.pockets[k]['tiles'])) if k is not None else 'open'
    cnt[(kind.split('(')[0], walked is not None, rs[0][5] == 0)] += 1
    if len(sys.argv) > 2:
        print(o, 'r%d-%d' % (r0, rs[-1][0]), 'mouth', mo, DI[d], kind, 'fwd0' if rs[0][5] == 0 else 'flood', 'len', rs[0][2],
              'size', rs[0][3], 'walked-out r%s' % walked if walked else '', 'died r%d %s' % f if f else '')
for k, v in sorted(cnt.items()): print(k, v)
