#!/usr/bin/env python3
"""analyze59.py DIR [DIR...] [--fresh] [--by-map --k=metric]: behavioural audit of v5.9 modules aimed at our own dragons
killing each other and at fights we cannot win. Cached in DIR/audit59.json. Ground truth from the replay:

- friendly fire: our dragon dies moving into one of our own dragons (body or head, not a feed suicide), split by where:
  coming out of a portal (F_PORTAL_PROBE, F_EXIT_CLEAR), in a 1-wide corridor (F_LANE: head-on between two of ours there),
  at the mouth of a dead end (F_MOUTH_CLEAR), anywhere else.
- enemy chambers (F_ENEMY_CHAMBER): our dragons dying inside a small chamber within 10 rounds of entering it while an enemy
  head is inside.
- retreat (F_RETREAT): deaths of ours 4+ long caused by the enemy on the enemy's half of the map (nearer their spawn).
- portal exits into the enemy: our dragon dies coming out of a portal into an enemy (F_PORTAL_PROBE).
"""
import collections, gzip, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import Static, walk  # noqa: E402
from sim import Game, DI  # noqa: E402


def man(a, b, W, H):
    dx = abs(a[0] - b[0]); dy = abs(a[1] - b[1]); return min(dx, W - dx) + min(dy, H - dy)


def analyze(replay, ours):
    g = Game(replay); st = Static(g); W, H = g.W, g.H
    m = collections.Counter()
    spawn = {t: [b[0] for i, b in g.bodies.items() if g.team[i] == t] for t in 'AB'}
    them = 'B' if ours == 'A' else 'A'
    corridor = {}
    for p in st.tiles:
        n = sum(1 for d in DI if g.dest(p, d) is not None)
        corridor[p] = n == 2
    mouth = set()
    for k in st.pockets:
        if k['tree']: mouth.add(k['out'])
    entered = {}   # id -> (chamber, round)
    for ev in walk(g):
        if ev[0] == 'split': continue
        if ev[0] == 'death':
            _, rnd, i, bodyset, ind, cause, dropped, body = ev
            if g.team.get(i) != ours or not body: continue
            m['deaths'] += 1
            vp, hit = getattr(g, 'last_death_portal', (False, None))
            tile = getattr(g, 'last_death_tile', None)
            feed = ind.endswith(':feed')
            if cause in ('hit other body', 'head to head') and not feed:
                if hit == ours:
                    m['ff_deaths'] += 1
                    # was there a free step (open edge, no dragon on the tile, not our neck) it could have taken instead?
                    h0 = body[0]
                    free = 0
                    for d in DI:
                        n = g.dest(h0, d)
                        if n is None or (len(body) > 1 and n == body[1]) or n in g.occ: continue
                        free += 1
                    m['ff_had_free_step' if free else 'ff_boxed_in'] += 1
                    if free and len(body) == 2: m['ff_free_len2'] += 1
                    if vp: m['ff_portal_exit'] += 1
                    elif tile in mouth or body[0] in mouth: m['ff_deadend_mouth'] += 1
                    elif corridor.get(tile or body[0]):
                        m['ff_corridor'] += 1
                        if cause == 'head to head': m['ff_corridor_headon'] += 1
                    else: m['ff_other'] += 1
                elif hit == them:
                    m['enemy_caused'] += 1
                    if vp: m['portal_exit_into_enemy'] += 1
                    if len(body) >= 4:
                        h = body[0]
                        dus = min(man(h, q, W, H) for q in spawn[ours]); dth = min(man(h, q, W, H) for q in spawn[them])
                        if dth < dus: m['long_killed_enemy_half'] += 1
            c = st.chamber_of.get(body[0])
            e = entered.get(i)
            if c is not None and e and e[0] == c and rnd - e[1] <= 10:
                if any(g.team[j] == them and st.chamber_of.get(bb[0]) == c for j, bb in g.bodies.items()):
                    m['died_in_enemy_chamber'] += 1
            entered.pop(i, None)
            continue
        _, rnd, i, ate = ev
        if g.team.get(i) != ours or i not in g.bodies: continue
        h = g.bodies[i][0]
        c = st.chamber_of.get(h)
        if c is None: entered.pop(i, None)
        elif not entered.get(i) or entered[i][0] != c:
            entered[i] = (c, rnd)
            if any(g.team[j] == them and st.chamber_of.get(bb[0]) == c for j, bb in g.bodies.items()):
                m['entered_enemy_chamber'] += 1
    res = g.rp.result
    m['win'] = 1 if res.get('winner') == ours else 0
    m['games'] = 1
    return m


def _one(path):
    try:
        return dict(analyze(path, path[:-7].rsplit('_', 1)[1]))
    except Exception:
        import traceback; traceback.print_exc()
        return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    cols = []
    for d in args:
        tot = collections.Counter()
        cache_f = os.path.join(d, 'audit59.json')
        per = json.load(open(cache_f)) if os.path.exists(cache_f) and '--fresh' not in sys.argv else {}
        todo = [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith('.replay') and f not in per]
        if todo:
            import multiprocessing
            with multiprocessing.Pool(int(os.environ.get('AUDIT_J', '3'))) as pool:
                for f, r in zip(todo, pool.map(_one, todo)):
                    if r is not None: per[os.path.basename(f)] = r
        for f, v in per.items(): tot.update(v)
        json.dump(per, open(cache_f, 'w'))
        cols.append((d, tot, per))
    if '--by-map' in sys.argv:
        for kk in [k for k in sys.argv if k.startswith('--k=')]:
            k = kk[4:]
            print('== %s by map' % k)
            maps = sorted({f.rsplit('_', 2)[0] for _, _, per in cols for f in per})
            for mp in maps:
                print('%-30s' % mp + ''.join('%10s' % sum(v.get(k, 0) for f, v in per.items() if f.rsplit('_', 2)[0] == mp)
                                              for _, _, per in cols))
    keys = sorted({k for _, t, _ in cols for k in t})
    print('%-28s' % 'metric' + ''.join('%14s' % os.path.basename(d.rstrip('/'))[-13:] for d, _, _ in cols))
    for k in keys:
        print('%-28s' % k + ''.join('%14s' % t.get(k, 0) for _, t, _ in cols))


if __name__ == '__main__':
    main()
