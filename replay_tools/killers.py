import sys, collections; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from sim import Game
def classify(path, verbose=False):
    """Deaths of team A by cause: wall / self / ally body / ally head / enemy body / enemy head / feed(suicide into own neck)."""
    g = Game(path); acts = {}; rnd = 0; tc = -1
    out = collections.Counter(); per_round = collections.defaultdict(collections.Counter); rows = []
    last_dead = None
    for e in g.rp.events:
        k = e[0]
        if k == 'round': rnd = e[1]
        elif k == 'turn': tc += 1
        elif k == 'action': acts[e[1]] = (e[2], e[3], list(g.bodies.get(e[1], [])))
        elif k == 'death':
            i = e[1]; tm = g.team.get(i)
            a = acts.get(i); cause = e[2]
            if a and a[0] == 'move' and a[2]:
                h = a[2][0]
                for d in a[1]:
                    n = g.dest(h, d)
                    if n is None: break
                    h = n
                occ = [(j, g.team[j], b.index(h)) for j, b in g.bodies.items() if h in b and j != i]
                if cause == 'hit other body' or cause == 'head to head':
                    if occ:
                        j, tj, idx = occ[0]
                        cause = ('ally ' if tj == tm else 'enemy ') + ('head' if idx == 0 else 'body')
                    elif cause == 'head to head' and last_dead is not None and last_dead[0] == tc:
                        cause = ('ally ' if g.team.get(last_dead[1]) == tm else 'enemy ') + 'head'
                    else: cause += ' ?'
            ind = g.ind.get(i, '')
            if cause == 'hit self' and ind.endswith(':feed'): cause = 'feed'
            out[(tm, cause)] += 1; per_round[rnd // 50][(tm, cause)] += 1
            rows.append((tc, rnd, i, tm, cause, len(a[2]) if a else 0, ind))
        # state update
        if k == 'tile': (g.pearls.add if e[3] else g.pearls.discard)((e[1], e[2]))
        elif k == 'ind': g.ind[e[1]] = e[2]
        elif k == 'update':
            _, i, facing, hx, hy, tx, ty = e; body = g.bodies.get(i)
            if body is None: continue
            pa = acts.get(i); steps = pa[1] if pa and pa[0] == 'move' else facing
            hh = body[0]
            for d in steps:
                n = g.dest(hh, d)
                if n is None: break
                body.insert(0, n); hh = n
            if body[0] != (hx, hy): body.insert(0, (hx, hy))
            if (tx, ty) in body:
                j = len(body) - 1 - body[::-1].index((tx, ty)); del body[j + 1:]
            else: body[:] = [(hx, hy), (tx, ty)]
        elif k == 'split':
            _, par, ch, tm2, fc, pb, cb = e
            g.bodies[par] = list(pb); g.bodies[ch] = list(cb); g.team[ch] = tm2
        elif k == 'death':
            g.bodies.pop(e[1], None); last_dead = (tc, e[1])
    return g, out, per_round, rows
if __name__ == '__main__':
    for p in sys.argv[1:]:
        g, out, pr, rows = classify(p)
        print(g.name, {k[1]: v for k, v in sorted(out.items()) if k[0] == 'A'})
        print('   enemy B:', {k[1]: v for k, v in sorted(out.items()) if k[0] == 'B'})
