#!/usr/bin/env python3
"""kept.py DIR: F_MANTLE residue: an alpha (indicator at its previous turn) splits with the rear child longer than what it
keeps, and still shows "Alpha:" on the split turn. Lists the split's DIAG mode and any `mantle` line."""
import collections, gzip, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import walk  # noqa: E402
from sim import Game  # noqa: E402
d = sys.argv[1]; tot = collections.Counter(); ex = []
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]; ours = stem.rsplit('_', 1)[1]
    modes = {}; mant = set()
    for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt', errors='replace'):
        p = l.split()
        if len(p) < 5 or not p[1].isdigit() or not p[2].isdigit(): continue
        if p[0] == 'turn': modes[(int(p[1]), int(p[2]))] = p[4]
        elif p[0] == 'mantle': mant.add((int(p[1]), int(p[2])))
    g = Game(os.path.join(d, f)); role = {}; pend = []
    for ev in walk(g):
        if ev[0] == 'split':
            _, rnd, par, ch = ev
            if g.team.get(ch) == ours and rnd > 0 and role.get(par) == 'Alpha' and len(g.bodies[ch]) > len(g.bodies[par]):
                pend.append((par, rnd, len(g.bodies[ch]), len(g.bodies[par])))
        elif ev[0] == 'turn':
            _, rnd, i, ate = ev
            if i in g.bodies: role[i] = g.ind.get(i, '').partition(':')[0]
            for x in [x for x in pend if x[0] == i]:
                pend.remove(x)
                if role[i] == 'Alpha':
                    k = (modes.get((i, x[1]), '?'), (i, x[1]) in mant)
                    tot[k] += 1
                    ex.append((stem,) + x + k)
for k, v in tot.most_common(): print(k, v)
for e in ex[:15]: print(e)
