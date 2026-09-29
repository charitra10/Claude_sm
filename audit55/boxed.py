#!/usr/bin/env python3
"""boxed.py DIR KEY: for each DIAG KEY split (harvest / tailbfs / farmharvest), is the child born with a free first move
(an open edge, no kelp, to a tile no dragon is on, other than its own neck)?"""
import collections, gzip, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import walk  # noqa: E402
from sim import Game, DI  # noqa: E402

d, key = sys.argv[1], sys.argv[2]
tot = collections.Counter()
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]
    want = set()
    for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt', errors='replace'):
        p = l.split()
        if len(p) > 2 and p[0] == key and p[1].isdigit() and p[2].isdigit(): want.add((int(p[1]), int(p[2])))
    if not want: continue
    g = Game(os.path.join(d, f))
    for ev in walk(g):
        if ev[0] != 'split' or (ev[2], ev[1]) not in want: continue
        ch = ev[3]; body = g.bodies[ch]; h = body[0]
        free = 0
        for dd in DI:
            n = g.dest(h, dd)
            if n is None or n == body[1]: continue
            if n in g.occ: continue
            free += 1
        tot['n'] += 1
        tot['free%d' % min(free, 2)] += 1
for k in sorted(tot): print('%-10s %d' % (k, tot[k]))
