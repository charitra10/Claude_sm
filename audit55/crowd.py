#!/usr/bin/env python3
"""crowd.py DIR [-v]: F_CAMP2 audit, "exactly one camper per chamber": episodes of consecutive rounds in which 2+ of our
heads are inside one small spawning chamber (ground truth), by length and chamber size; for long ones, what the bots' own
`camp` lines said (friends seen, crowded / leave flags)."""
import collections, gzip, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import Static  # noqa: E402
from sim import Game  # noqa: E402

d = sys.argv[1]; verbose = '-v' in sys.argv
tot = collections.Counter(); ex = []
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]; ours = stem.rsplit('_', 1)[1]; mp = stem.rsplit('_', 2)[0]
    camp = {}
    for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt', errors='replace'):
        p = l.split()
        if len(p) > 20 and p[0] == 'camp' and p[1].isdigit() and p[2].isdigit(): camp[(int(p[1]), int(p[2]))] = p
    g = Game(os.path.join(d, f)); st = Static(g)
    run = {}   # chamber -> [start round, ids seen]
    last_r = -1
    def close(c, r_end):
        r0, ids = run.pop(c)
        n = r_end - r0
        size = len(st.chambers[c]['tiles'])
        b = '1-2' if n <= 2 else '3-9' if n < 10 else '10+'
        tot['ep_%s_size%s' % (b, 'le8' if size <= 8 else 'gt8')] += 1
        tot['rounds_size%s' % ('le8' if size <= 8 else 'gt8')] += n
        if n >= 10 and size > 8:
            tot['long_map_' + mp] += 1
            if verbose: ex.append((stem, c, size, r0, r_end, sorted(ids)))
    for t, rnd, i in g.run():
        if rnd == last_r: continue
        last_r = rnd
        inside = collections.defaultdict(set)
        for j, b in g.bodies.items():
            if g.team[j] == ours:
                c = st.chamber_of.get(b[0])
                if c is not None and not st.chambers[c]['barren']: inside[c].add(j)
        for c in list(run):
            if len(inside.get(c, ())) < 2: close(c, rnd)
        for c, ids in inside.items():
            if len(ids) >= 2:
                if c not in run: run[c] = [rnd, set()]
                run[c][1] |= ids
    for c in list(run): close(c, 500)
for k in sorted(tot): print('%-34s %d' % (k, tot[k]))
for e in ex[:40]:
    stem, c, size, r0, r1, ids = e
    print(e)
    for j in ids:
        cl = [camp.get((j, r)) for r in range(r0, r1)] if False else None
