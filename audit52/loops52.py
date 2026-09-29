#!/usr/bin/env python3
"""loops52.py DIR...: turns our dragons spend in short loops: the last 8 head positions repeat with period <= 4 and nothing
was eaten in them (intentional loops excluded: reside, feed, loop, cascade)."""
import collections, multiprocessing, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze
from sim import Game

def one(rp):
    side = rp[:-7].rsplit('_', 1)[1]; g = Game(rp)
    hist = collections.defaultdict(list); c = collections.Counter()
    for ev in analyze.walk(g):
        if ev[0] != 'turn' or g.team.get(ev[2]) != side or ev[2] not in g.bodies: continue
        _, rnd, i, ate = ev
        hist[i].append((rnd, g.bodies[i][0], len(ate), g.ind.get(i, '').partition(':')[2]))
    for i, hh in hist.items():
        for t in range(7, len(hh)):
            if hh[t][3] in analyze.LOOP_MODES: continue
            w = hh[t - 7:t + 1]
            if w[-1][0] - w[0][0] != 7 or any(x[2] for x in w): continue
            c['turns8'] += 1
            ps = [x[1] for x in w]
            if any(all(ps[k] == ps[k + p] for k in range(8 - p)) for p in (2, 3, 4)): c['short_loop_turns'] += 1
    return c

if __name__ == '__main__':
    for d in sys.argv[1:]:
        fs = [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith('.replay')]
        with multiprocessing.Pool(3) as p: tot = sum(p.map(one, fs), collections.Counter())
        print(os.path.basename(d), dict(tot))
