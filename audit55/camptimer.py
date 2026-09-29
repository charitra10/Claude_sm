#!/usr/bin/env python3
"""camptimer.py DIR: F_CAMP2's remembered spawn timers. For every `camp` line in a chamber of 9+ tiles, the bot's `next`
(rounds until the next pearl it expects there) against the truth (0 if a pearl lies there, else the earliest countdown)."""
import collections, gzip, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import Static  # noqa: E402
from sim import Game  # noqa: E402

d = sys.argv[1]
tot = collections.Counter(); errs = []
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]; ours = stem.rsplit('_', 1)[1]
    lines = collections.defaultdict(dict)
    for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt', errors='replace'):
        p = l.split()
        if len(p) >= 26 and p[0] == 'camp' and p[1].isdigit() and p[2].isdigit() and int(p[25]) > 8:
            lines[int(p[2])][int(p[1])] = int(p[15])
    if not lines: continue
    g = Game(os.path.join(d, f)); st = Static(g)
    cds = []; r = 0
    for e in g.rp.events:
        if e[0] == 'round': r = e[1]
        elif e[0] == 'countdown': cds.append((r, (e[1], e[2]), e[3]))
    due = {}; ci = 0
    for t, rnd, i in g.run():
        while ci < len(cds) and cds[ci][0] <= rnd:
            due[cds[ci][1]] = cds[ci][0] + cds[ci][2]; ci += 1
        if i not in lines.get(rnd, {}) or i not in g.bodies: continue
        c = st.chamber_of.get(g.bodies[i][0])
        if c is None: continue
        tiles = st.chambers[c]['tiles']
        if any(q in g.pearls for q in tiles): truth = 0
        else:
            ds = [due[q] - rnd for q in tiles if q in due and due[q] >= rnd]
            truth = min(ds) if ds else 100000
        bot = lines[rnd][i]
        tot['n'] += 1
        err = bot - truth
        if err == 0: tot['exact'] += 1
        elif abs(err) <= 2: tot['within2'] += 1
        elif err > 0: tot['bot_later'] += 1; errs.append(err)
        else: tot['bot_sooner'] += 1
        if truth <= 12 and bot > 60: tot['missed_soon_pearl'] += 1
        if bot <= 12 and truth > 60: tot['phantom_soon_pearl'] += 1
for k in sorted(tot): print('%-24s %d' % (k, tot[k]))
