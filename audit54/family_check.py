#!/usr/bin/env python3
"""family_check.py DIR...: friendly head-on collisions between a parent and its split child (born <= 5 rounds before),
by where the child was born: inside a small sealed chamber (<= 20 tiles, portal edges not crossed) or outside, and whether
the child's body straddled a portal edge at birth (its tail next to a portal edge: a straddle split)."""
import collections, glob, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: F401
from sim import Game, D, DI


def chamber(g, p, cache):
    if p in cache: return cache[p]
    seen = {p}; q = [p]
    while q and len(seen) <= 20:
        c = q.pop()
        for d in DI:
            if g.edge(c[0], c[1], d)[0] != 0: continue
            n = g.step(c, d)
            if n not in seen: seen.add(n); q.append(n)
    cache[p] = len(seen) <= 20
    return cache[p]


tot = collections.Counter(); games = 0
for d in sys.argv[1:]:
    for f in sorted(glob.glob(os.path.join(d, '*.replay'))):
        side = os.path.basename(f)[-8]
        g = Game(f); team = dict(g.team); parent = {}; born = {}; birth = {}
        rnd = 0; cur = None; grp = collections.defaultdict(list); cache = {}
        games += 1
        for e in g.rp.events:
            if e[0] == 'round': rnd = e[1]
            elif e[0] == 'turn': cur = e[1]
            elif e[0] == 'split':
                team[e[2]] = e[3]; parent[e[2]] = e[1]; born[e[2]] = rnd; birth[e[2]] = e[6]
            elif e[0] == 'death' and e[2] == 'head to head': grp[(rnd, cur)].append(e[1])
        for (r, c), ids in grp.items():
            if len(ids) != 2 or not all(team.get(i) == side for i in ids): continue
            a, b = ids
            ch = a if parent.get(a) == b else b if parent.get(b) == a else None
            if ch is None or r - born[ch] > 5: continue
            body = birth[ch]
            tail = body[-1]
            strad = any(g.edge(tail[0], tail[1], dd)[0] == 2 for dd in DI)
            inside = chamber(g, body[0], cache)
            tot[(os.path.basename(f).rsplit('_s', 1)[0], 'born inside chamber' if inside else 'born outside',
                 'tail on portal tile' if strad else '', 'child moved' if c == ch else 'parent moved')] += 1
for k, v in tot.most_common(): print(v, k)
print('games', games, 'parent-child head-ons', sum(tot.values()))
