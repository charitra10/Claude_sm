#!/usr/bin/env python3
"""sim.py: replay a decoded .replay turn by turn: dragon bodies (head first), teams, pearls, indicators.

    g = Game(path); for snap in g.run(): ...   # yields after every dragon turn: snap = (turn_index, round, dragon_id)
    g.bodies[id], g.team[id], g.pearls (set), g.ind[id], g.W, g.H, g.edge(x, y, d), g.portal_dest(x, y, d)
"""
import sys, collections
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from replay import load

D = {'N': (0, -1), 'E': (1, 0), 'S': (0, 1), 'W': (-1, 0)}
DI = 'NESW'


class Game:
    def __init__(self, path):
        self.rp = load(path)
        lines = self.rp.map_text.split('\n')
        self.W, self.H = map(int, lines[0].split()[1:3])
        self.name = next((l[9:] for l in lines if l.startswith('MAP_NAME')), '?')
        self.edges = {}; self.tiles = {}; self.bodies = {}; self.team = {}
        k = 0
        for l in lines:
            p = l.split()
            if not p: continue
            if p[0] == 'EDGE': self.edges[int(p[1])] = (int(p[2]), int(p[3]))
            elif p[0] == 'TILE': self.tiles[(int(p[1]), int(p[2]))] = (int(p[3]), int(p[4]))
            elif p[0] == 'DRAGON':
                n = int(p[2]); c = list(map(int, p[3:3 + 2 * n]))
                self.bodies[k] = [(c[2 * i], c[2 * i + 1]) for i in range(n)]; self.team[k] = 'AB'[int(p[1])]; k += 1
        self.ends = collections.defaultdict(list)
        for i, (kind, pid) in self.edges.items():
            if kind != 2: continue
            row, x = divmod(i, self.W + 1)
            self.ends[pid].append(((x % self.W, (row // 2) % self.H), 0 if row % 2 == 0 else 3))
        self.pearls = set(); self.ind = {}; self.dead = {}; self.born = {i: 0 for i in self.bodies}
        self.countdown = {}

    def edge(self, x, y, d):
        W, H = self.W, self.H; x %= W; y %= H; d = DI.index(d) if isinstance(d, str) else d
        if d == 0: i = (2 * y) * (W + 1) + x
        elif d == 2: i = (2 * ((y + 1) % H)) * (W + 1) + x
        elif d == 3: i = (2 * y + 1) * (W + 1) + x
        else: i = (2 * y + 1) * (W + 1) + (x + 1) % W
        return self.edges.get(i, (0, -1))

    def step(self, p, d):
        dx, dy = D[d]; return ((p[0] + dx) % self.W, (p[1] + dy) % self.H)

    def dest(self, p, d):
        """Where a head on p moving d lands (portals included); None into kelp."""
        kind, pid = self.edge(p[0], p[1], d)
        if kind == 1: return None
        if kind == 0: return self.step(p, d)
        di = DI.index(d)
        me = (p, di) if di in (0, 3) else (self.step(p, d), 0 if di == 2 else 3)
        pair = self.ends[pid]; other = pair[1] if pair[0] == me else pair[0]
        t = other[0]
        return self.step(t, d) if di in (0, 3) else t

    def run(self):
        rnd = 0; turn = -1; cur = None; pending = None
        for e in self.rp.events:
            k = e[0]
            if k == 'round': rnd = e[1]
            elif k == 'turn':
                if cur is not None: yield (turn, rnd, cur)
                turn += 1; cur = e[1]
            elif k == 'tile':
                if e[3]: self.pearls.add((e[1], e[2]))
                else: self.pearls.discard((e[1], e[2]))
            elif k == 'countdown': self.countdown[(e[1], e[2])] = e[3]
            elif k == 'ind': self.ind[e[1]] = e[2]
            elif k == 'action': pending = e
            elif k == 'update':
                _, i, facing, hx, hy, tx, ty = e
                body = self.bodies.get(i)
                if body is None: continue
                steps = pending[3] if pending and pending[1] == i and pending[2] == 'move' else facing
                h = body[0]
                for d in steps:
                    n = self.dest(h, d)
                    if n is None: break
                    body.insert(0, n); h = n
                if body[0] != (hx, hy): body.insert(0, (hx, hy))
                # trim the tail back to the reported tail
                if (tx, ty) in body:
                    j = len(body) - 1 - body[::-1].index((tx, ty))
                    del body[j + 1:]
                else:
                    body[:] = [(hx, hy), (tx, ty)]
            elif k == 'split':
                _, par, ch, tm, fc, pb, cb = e
                self.bodies[par] = list(pb); self.bodies[ch] = list(cb); self.team[ch] = tm; self.born[ch] = rnd
            elif k == 'death':
                if e[1] in self.bodies:
                    self.dead[e[1]] = (rnd, e[2], list(self.bodies[e[1]]), self.ind.get(e[1], ''))
                    del self.bodies[e[1]]
        if cur is not None: yield (turn, rnd, cur)

    def render(self, x0, x1, y0, y1, mark=None):
        """ASCII board: A/a our head/body, B/b theirs, o pearl, walls as | and --, P portal edge."""
        occ = {}
        for i, b in self.bodies.items():
            for j, c in enumerate(b):
                ch = self.team[i]
                occ[c] = (ch if j == 0 else ch.lower(), i)
        out = ['    ' + ''.join('%3d' % (x % 100) for x in range(x0, x1 + 1))]
        for y in range(y0, y1 + 1):
            top = '    '; mid = '%3d ' % y
            for x in range(x0, x1 + 1):
                k = self.edge(x, y, 0)[0]
                top += '+' + ('--' if k == 1 else 'PP' if k == 2 else '  ')
                kl = self.edge(x, y, 3)[0]
                c = (x % self.W, y % self.H)
                s = occ.get(c, (None, None))[0] or ('o' if c in self.pearls else '.')
                if mark and c in mark: s = '*'
                mid += ('|' if kl == 1 else 'P' if kl == 2 else ' ') + s + ' '
            out.append(top); out.append(mid)
        return '\n'.join(out)

    def heads(self, x0, x1, y0, y1):
        res = []
        for i, b in self.bodies.items():
            h = b[0]
            if x0 <= h[0] <= x1 and y0 <= h[1] <= y1:
                res.append((i, self.team[i], len(b), h, self.ind.get(i, '')))
        return res
