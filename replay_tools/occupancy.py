import sys, collections; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from sim import Game
from chambers import chambers
# per chamber: rounds held by A only / B only / both / empty; how A's holds ended
for f in sys.argv[1:]:
    g = Game(f); ch = chambers(g)
    ch = [c for c in ch if len(c[0]) >= 4]
    hold = [collections.Counter() for _ in ch]; holder = [None] * len(ch); ends = [collections.Counter() for _ in ch]
    since = [0] * len(ch); lastr = -1; spells = [[] for _ in ch]
    for t, r, i in g.run():
        if r == lastr: continue
        lastr = r
        for k, (reg, pids, sp) in enumerate(ch):
            inside = {}
            for j, b in g.bodies.items():
                if b[0] in reg: inside[j] = g.team[j]
            teams = set(inside.values())
            st = 'both' if len(teams) == 2 else (teams.pop() if teams else 'empty')
            hold[k][st] += 1
            ids = sorted(j for j, tm in inside.items() if tm == 'A')
            if holder[k] and not ids:
                # our last camper(s) left: died? where?
                why = []
                for j in holder[k]:
                    if j in g.dead: why.append('died:' + g.dead[j][1] + ('@in' if g.dead[j][2][0] in reg else '@out'))
                    elif j in g.bodies: why.append('left:' + g.ind.get(j, '?'))
                    else: why.append('gone')
                ends[k][' '.join(sorted(set(why)))] += 1
                spells[k].append((since[k], r, holder[k], why, st))
            if ids and not holder[k]: since[k] = r
            holder[k] = ids or None
    print(f.split('/')[-1], g.name)
    for k, (reg, pids, sp) in enumerate(ch):
        print('  chamber', sorted(reg)[0], 'size', len(reg), 'portals', pids, dict(hold[k]))
        print('     our holds ended:', dict(ends[k]))
        for s in spells[k][:12]: print('       held r%d-%d by %s -> %s (then %s)' % s)
