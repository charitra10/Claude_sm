#!/usr/bin/env python3
"""harvest.py DIR [key]: every back-harvest split (DIAG `harvest` / `tailbfs` / `farmharvest`): the child's pearls in its first
8 rounds, whether it died (and how), and how many teammates' segments were within 2 of the tail at the split."""
import collections, gzip, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'replay_tools'))
from sim import Game  # noqa: E402

d = sys.argv[1]; key = sys.argv[2] if len(sys.argv) > 2 else 'harvest'
tot = collections.Counter(); rows = []
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]; ours = stem.rsplit('_', 1)[1]
    ev = [l.split() for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt') if l.startswith(key + ' ')]
    if not ev: continue
    want = {(int(e[1]), int(e[2])): e for e in ev if len(e) > 2 and e[1].isdigit() and e[2].isdigit()}
    g = Game(os.path.join(d, f))
    W, H = g.W, g.H
    crowd = {}; kid = {}; ate = collections.Counter(); first = {}
    pending = None
    for e in g.rp.events:
        if e[0] == 'split' and pending is None:
            pass
    rnd = 0
    import itertools
    it = g.run()
    # track splits via the event stream alongside run(): run() applies them; record child by (parent, born round)
    for t, r, i in it:
        pass
    for (par, r), e in want.items():
        ch = [c for c, br in g.born.items() if br == r and g.team.get(c) == ours]
        rows.append((stem, par, r, e))
    # second pass: pearls eaten per child in its first 8 rounds
    g = Game(os.path.join(d, f))
    prevp = set()
    kids = {}
    r_now = 0
    for ev_ in g.rp.events:
        if ev_[0] == 'round': r_now = ev_[1]
        if ev_[0] == 'split' and (ev_[1], r_now) in want:
            kids[ev_[2]] = (ev_[1], r_now, ev_[5][-1] if ev_[5] else None)
    g = Game(os.path.join(d, f))
    cur = None; pearls_before = set()
    r_now = 0
    for ev_ in g.rp.events:
        k = ev_[0]
        if k == 'round': r_now = ev_[1]
        elif k == 'turn': cur = ev_[1]
        elif k == 'tile' and not ev_[3] and cur in kids and r_now <= kids[cur][1] + 8:
            ate[cur] += 1
    g2 = Game(os.path.join(d, f))
    for t, r, i in g2.run(): pass
    for c, (par, r, _) in kids.items():
        tot['n'] += 1
        a = ate[c]
        dr = g2.dead.get(c)
        died = dr is not None and dr[0] <= r + 8
        tot['ate'] += a
        tot['paid' if a >= 2 else 'one' if a == 1 else 'zero'] += 1
        if died: tot['died8'] += 1; tot['died8_' + dr[1].replace(' ', '_')] += 1
        if died and a == 0: tot['died_before_eating'] += 1
        if a == 0 and '-v' in sys.argv: print('zero', stem, par, r, '->', c, 'died', dr[:2] if died else None, ' '.join(want[(par, r)][3:]))
        tot['map_' + stem.rsplit('_', 2)[0]] += 1
        if a >= 2: tot['paidmap_' + stem.rsplit('_', 2)[0]] += 1
for k in sorted(tot): print('%-40s %d' % (k, tot[k]))
