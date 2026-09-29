#!/usr/bin/env python3
"""chain_check.py DIR... [--map default]: F_CHAIN audit from the replays. Every portal crossing of the team under test
(a move step over a portal edge), per dragon in order. A ping-pong is a crossing whose next crossing (same dragon) is
back through the same portal id within 30 rounds. Also: crossings that were rescue/forced (the dragon had just come in).
"""
import collections, glob, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: F401
from sim import Game


def crossings(path, side):
    g = Game(path)
    team = dict(g.team)
    head = {i: b[0] for i, b in g.bodies.items()}
    rnd = 0
    act = {}
    out = collections.defaultdict(list)
    for e in g.rp.events:
        k = e[0]
        if k == 'round': rnd = e[1]
        elif k == 'action': act[e[1]] = e
        elif k == 'split':
            team[e[2]] = e[3]; head[e[2]] = e[6][0]; head[e[1]] = e[5][0]
        elif k == 'update':
            _, i, facing, hx, hy, tx, ty = e
            a = act.pop(i, None)
            if a and a[2] == 'move' and i in head and team.get(i) == side:
                h = head[i]
                for d in a[3]:
                    kind, pid = g.edge(h[0], h[1], d)
                    n = g.dest(h, d)
                    if n is None: break
                    if kind == 2: out[i].append((rnd, pid))
                    h = n
            head[i] = (hx, hy)
    return g, out


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    only = next((a.split('=')[1] for a in sys.argv[1:] if a.startswith('--map=')), None)
    tot = collections.Counter()
    for d in args:
        for f in sorted(glob.glob(os.path.join(d, '*.replay'))):
            name = os.path.basename(f)[:-7]
            if only and not name.startswith(only): continue
            side = name[-1]
            g, cr = crossings(f, side)
            c = collections.Counter()
            for i, seq in cr.items():
                for (r1, p1), (r2, p2) in zip(seq, seq[1:]):
                    c['pairs'] += 1
                    if p1 == p2 and r2 - r1 <= 30:
                        c['ping-pong'] += 1
                        if r2 - r1 <= 3: c['ping-pong <= 3 rounds'] += 1
                c['crossings'] += len(seq)
            print(f'{name:30}', dict(c))
            tot.update(c)
    print('TOTAL', dict(tot), 'ping-pong share %.1f%%' % (100 * tot['ping-pong'] / max(1, tot['pairs'])))


if __name__ == '__main__':
    main()
