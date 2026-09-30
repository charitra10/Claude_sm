#!/usr/bin/env python3
"""feedpick.py DIR: F_FEED_SCORE. For each of our dragons that died feeding (mode feed, hit self), the alpha it had last
picked (DIAG `feedpick`): was that our longest dragon at the end of the game, and was it still alive then."""
import collections, gzip, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'replay_tools'))
from sim import Game  # noqa: E402
d = sys.argv[1]; tot = collections.Counter()
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]; ours = stem.rsplit('_', 1)[1]
    pick = collections.defaultdict(list)
    for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt', errors='replace'):
        p = l.split()
        if len(p) >= 5 and p[0] == 'feedpick' and p[1].isdigit() and p[2].isdigit() and p[4].isdigit():
            pick[int(p[1])].append((int(p[2]), int(p[4])))
    g = Game(os.path.join(d, f))
    for t, r, i in g.run(): pass
    fin = [(len(b), j) for j, b in g.bodies.items() if g.team[j] == ours]
    if not fin: continue
    apex = max(fin)[1]
    for i, (rd, cause, body, ind) in g.dead.items():
        if g.team.get(i) != ours or not ind.endswith(':feed') or cause != 'hit self' or rd < 300: continue
        ps = [x for x in pick.get(i, []) if x[0] <= rd]
        if not ps: tot['no_pick'] += 1; continue
        tgt = ps[-1][1]
        tot['feeders'] += 1
        k = 'to_final_apex' if (tgt & 8191) == (apex & 8191) else 'to_alive_other' if tgt in g.bodies else 'to_dead_alpha'
        tot[k] += 1
        if k == 'to_dead_alpha':
            dd = g.dead.get(tgt)
            if dd is None: tot['  target_never_seen'] += 1
            else:
                how = 'merged(feed)' if dd[3].endswith(':feed') else dd[1].replace(' ', '_')
                tot['  target_died_' + how] += 1
                tot['  target_died_before_feeder' if dd[0] <= rd else '  target_died_after_feeder'] += 1
for k in sorted(tot): print('%-16s %d' % (k, tot[k]))
