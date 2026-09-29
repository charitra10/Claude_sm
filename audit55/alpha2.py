#!/usr/bin/env python3
"""alpha2.py DIR: turns in which one of our alphas (indicator "Alpha:") is 2 long after round 0, by how it got there:
the primary still 2 long from the round-0 cascade, or the split that last shortened it (DIAG mode of that turn)."""
import collections, gzip, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'replay_tools'))
from sim import Game  # noqa: E402

d = sys.argv[1]
tot = collections.Counter(); per_dragon = collections.Counter()
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]; ours = stem.rsplit('_', 1)[1]
    modes = {}; farm = set(); harvest = set()
    for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt', errors='replace'):
        p = l.split()
        if len(p) < 5 or not p[1].isdigit() or not p[2].isdigit(): continue
        if p[0] == 'turn': modes[(int(p[1]), int(p[2]))] = p[4]
        elif p[0] == 'farmharvest': farm.add((int(p[1]), int(p[2])))
        elif p[0] in ('harvest', 'tailbfs'): harvest.add((int(p[1]), int(p[2])))
    g = Game(os.path.join(d, f))
    last_split = {}
    r = 0
    for e in g.rp.events:
        pass
    it = g.run()
    rnd_now = 0
    splits = collections.defaultdict(list)
    rr = 0
    for e in g.rp.events:
        if e[0] == 'round': rr = e[1]
        elif e[0] == 'split': splits[e[1]].append((rr, len(e[5]), len(e[6])))
    for t, rnd, i in it:
        if g.team.get(i) != ours or i not in g.bodies or rnd == 0: continue
        if not g.ind.get(i, '').startswith('Alpha:') or len(g.bodies[i]) != 2: continue
        prev = [s for s in splits[i] if s[0] <= rnd]
        if not prev: cat = 'primary_from_cascade' if g.born.get(i) == 0 else 'born_2long'
        else:
            sr, pl, cl = prev[-1]
            if sr == 0: cat = 'primary_from_cascade'
            elif (i, sr) in farm: cat = 'farmharvest'
            elif (i, sr) in harvest: cat = 'backharvest'
            else: cat = 'split_%s_child%d' % (modes.get((i, sr), '?'), cl if cl < 4 else 4)
        tot[cat] += 1
        per_dragon[(stem, i, cat)] += 1
for k, v in tot.most_common(): print('%-34s %d' % (k, v))
print('dragons', collections.Counter(c for (_, _, c) in per_dragon).most_common())
