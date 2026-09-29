#!/usr/bin/env python3
"""show.py REPLAY ID R0 R1 [--r=6] [--also=ID,ID]: the board round ID's head after each of ID's turns in rounds R0..R1
(and after the turns of --also dragons, e.g. its split child). Pearls 'o', our/their heads A/B, bodies a/b."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'replay_tools'))
from sim import Game  # noqa: E402

args = [a for a in sys.argv[1:] if not a.startswith('--')]
opts = dict(a[2:].split('=', 1) for a in sys.argv[1:] if a.startswith('--') and '=' in a)
path, who, r0, r1 = args[0], int(args[1]), int(args[2]), int(args[3])
rad = int(opts.get('r', 6))
also = {int(x) for x in opts.get('also', '').split(',') if x}
g = Game(path)
center = None
for t, rnd, i in g.run():
    if rnd > r1: break
    if rnd < r0 or (i != who and i not in also): continue
    b = g.bodies.get(i)
    if b is None:
        print('--- round %d id %d: dead %s' % (rnd, i, g.dead.get(i, ('?',))[1:2])); continue
    if i == who or center is None: center = b[0]
    x, y = center
    print('--- round %d id %d len %d head %s tail %s  %s' % (rnd, i, len(b), b[0], b[-1], g.ind.get(i, '')))
    print(g.render(x - rad, x + rad, y - rad, y + rad))
