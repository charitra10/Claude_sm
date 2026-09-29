import sys, collections; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from sim import Game
g = Game(sys.argv[1]); step = int(sys.argv[2]) if len(sys.argv) > 2 else 25
lastr = -1; deaths = collections.Counter(); seen = set(); contact = collections.Counter()
for t, r, i in g.run():
    for d, v in g.dead.items():
        if d in seen: continue
        seen.add(d); deaths[(g.team[d], v[1])] += 1
    if r != lastr and r % step == 0:
        lastr = r
        s = {}
        for tm in 'AB':
            ls = [len(b) for j, b in g.bodies.items() if g.team[j] == tm]
            s[tm] = (len(ls), max(ls) if ls else 0, sum(ls))
        # enemy heads within 3 (chebyshev) of any of our heads
        near = 0
        for j, b in g.bodies.items():
            if g.team[j] != 'A': continue
            for k, c in g.bodies.items():
                if g.team[k] == 'B':
                    dx = min((b[0][0]-c[0][0]) % g.W, (c[0][0]-b[0][0]) % g.W); dy = min((b[0][1]-c[0][1]) % g.H, (c[0][1]-b[0][1]) % g.H)
                    if max(dx, dy) <= 3: near += 1
        feeding = sum(1 for j in g.bodies if g.team[j] == 'A' and g.ind.get(j, '').endswith(':feed'))
        print(f"r{r} A units/max/total {s['A']}  B {s['B']}  A-B head contacts {near}  A feeding {feeding}")
print(g.name, dict(deaths))
print(g.rp.result)
