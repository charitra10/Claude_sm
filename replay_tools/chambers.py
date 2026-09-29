import sys, collections; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from sim import Game, DI
def chambers(g, cap=20):
    """Small sealed regions (no portal crossing) that touch a portal edge: list of (tiles set, portal ids)."""
    seen = set(); out = []
    for pid, ends in g.ends.items():
        for (t, d0) in ends:
            for start in (t, g.step(t, DI[d0])):
                if start in seen: continue
                reg = {start}; q = [start]; big = False; pids = set()
                while q and not big:
                    p = q.pop()
                    for d in DI:
                        kind, ppid = g.edge(p[0], p[1], d)
                        if kind == 1: continue
                        if kind == 2: pids.add(ppid); continue
                        n = g.step(p, d)
                        if n not in reg:
                            reg.add(n); q.append(n)
                            if len(reg) > cap: big = True; break
                if not big:
                    seen |= reg
                    spawn = sum(1 for c in reg if g.tiles.get(c, (0, 0))[1] > 0)
                    out.append((frozenset(reg), pids, spawn))
    return out
if __name__ == '__main__':
    g = Game(sys.argv[1])
    for reg, pids, sp in chambers(g):
        print(sorted(reg)[:6], 'size', len(reg), 'portals', pids, 'spawners', sp)
