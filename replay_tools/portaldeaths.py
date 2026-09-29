import sys, glob; sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from sim import Game, DI
# deaths of team A dragons whose final move crossed a portal: what they ran into
for f in sys.argv[1:]:
    g = Game(f)
    last_action = {}
    turn = -1
    it = iter(g.rp.events)
    body_before = {}
    rnd = 0; cur = None; res = []
    for e in g.rp.events:
        pass
    # replay manually to catch the body at the moment of the action
    gen = g.run()
    prev_turn = None
    for e in g.rp.events: pass
    g2 = Game(f); tcount = -1; rnd = 0
    for e in g2.rp.events:
        k = e[0]
        if k == 'round': rnd = e[1]
        if k == 'turn': tcount += 1
        if k == 'action': last_action[e[1]] = (tcount, rnd, e[2], e[3], list(g2.bodies.get(e[1], [])))
        # let the simulator process the event
    # simulate properly
    g3 = Game(f); acts = {}; tc = -1
    out = []
    for e in g3.rp.events:
        k = e[0]
        if k == 'turn': tc += 1
        if k == 'round': rnd = e[1]
        if k == 'action': acts[e[1]] = (tc, rnd, e[2], e[3], list(g3.bodies.get(e[1], [])))
        if k == 'death' and g3.team.get(e[1]) == 'A':
            a = acts.get(e[1])
            if a and a[2] == 'move' and a[4]:
                h = a[4][0]; crossed = False; path = [h]
                for d in a[3]:
                    kind = g3.edge(h[0], h[1], d)[0]
                    n = g3.dest(h, d)
                    if n is None: break
                    if kind == 2: crossed = True
                    h = n; path.append(h)
                if crossed:
                    # who occupies the landing tile
                    occ = [(i, g3.team[i], b.index(h)) for i, b in g3.bodies.items() if h in b and i != e[1]]
                    out.append((a[0], a[1], e[1], e[2], len(a[4]), path, occ))
        # advance simulator state for this event
        if k == 'tile':
            (g3.pearls.add if e[3] else g3.pearls.discard)((e[1], e[2]))
        elif k == 'ind': g3.ind[e[1]] = e[2]
        elif k == 'update':
            _, i, facing, hx, hy, tx, ty = e; body = g3.bodies.get(i)
            if body is None: continue
            pa = acts.get(i); steps = pa[3] if pa and pa[2] == 'move' else facing
            hh = body[0]
            for d in steps:
                n = g3.dest(hh, d)
                if n is None: break
                body.insert(0, n); hh = n
            if body[0] != (hx, hy): body.insert(0, (hx, hy))
            if (tx, ty) in body:
                j = len(body) - 1 - body[::-1].index((tx, ty)); del body[j + 1:]
            else: body[:] = [(hx, hy), (tx, ty)]
        elif k == 'split':
            _, par, ch, tm, fc, pb, cb = e
            g3.bodies[par] = list(pb); g3.bodies[ch] = list(cb); g3.team[ch] = tm
        elif k == 'death':
            g3.bodies.pop(e[1], None)
    print(f.split('/')[-1], g3.name, 'portal-exit deaths of ours:', len(out))
    for o in out[:25]: print('   turn %d round %d id %d %s len %d path %s hit %s' % o)
