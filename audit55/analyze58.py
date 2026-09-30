#!/usr/bin/env python3
"""analyze58.py DIR [DIR...] [--fresh] [--by-map --k=metric]: behavioural audit of the v5.8 modules. Cached in
DIR/audit58.json. Ground truth from the replay, intent from the DIAG trace:

- F_ISOLATED: "isolated" turns (one of ours 3+ long, no teammate in view, an enemy head within 5 moves in a straight line);
  deaths during them by cause, and trades lost (a head-on with an enemy shorter than us).
- F_TRAP_AVOID: turns of our dragons 5+ long, and how many ended in a rescue split (no move survives) or a death by wall /
  self / no move while 5+ long, outside dead ends of the map.
- F_EARLY_KILL: `earlykill` strikes: pearls of the two drops eaten by us / them.
- F_FEED_SCORE: feed suicides (mode feed, hit self) whose drops our final longest dragon ate, against all feed drops eaten.
- F_FARM_SEEK: `farmgo` trips: arrived (`farmdone ... there 1`), and pearls the dragon ate within 4 of the farm in the 20
  rounds after setting off.
- F_PORTAL_YIELD: our deaths on coming out of a portal into one of our own dragons.
"""
import collections, gzip, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import Static, walk  # noqa: E402
from sim import Game  # noqa: E402

KEYS = ('turn', 'earlykill', 'farmgo', 'farmdone')


def cheb(a, b, W, H):
    dx = abs(a[0] - b[0]); dy = abs(a[1] - b[1]); return max(min(dx, W - dx), min(dy, H - dy))


def man(a, b, W, H):
    dx = abs(a[0] - b[0]); dy = abs(a[1] - b[1]); return min(dx, W - dx) + min(dy, H - dy)


def xy(s):
    a, b = s.split(','); return (int(a), int(b))


def load_diag(path):
    out = collections.defaultdict(list)
    with gzip.open(path, 'rt', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) >= 4 and p[0] in KEYS and p[1].isdigit() and p[2].isdigit():
                out[p[0]].append(p[1:])
    return out


def analyze(replay, ours):
    g = Game(replay); st = Static(g); W, H = g.W, g.H
    diag = load_diag(replay[:-7] + '.diag.gz')
    m = collections.Counter()
    modes = {}
    for p in diag['turn']:
        if len(p) >= 4: modes[(int(p[0]), int(p[1]))] = p[3]
    ek = {(int(p[0]), int(p[1])) for p in diag['earlykill']}
    iso_now = {}
    pend = []; turn_deaths = []; drops = {}
    feed_drops = {}
    ate_at = collections.defaultdict(list)
    feed_eaten = collections.Counter()
    for ev in walk(g):
        if ev[0] == 'split':
            _, rnd, par, ch = ev
            if g.team.get(ch) != ours: continue
            L = len(g.bodies[par]) + len(g.bodies[ch])
            if modes.get((par, rnd)) == 'rescue' and L >= 5:
                h = g.bodies[par][0]
                if not any(st.pockets[k]['tree'] for k in st.pocket_of.get(h, ())): m['long_rescue_open'] += 1
            continue
        if ev[0] == 'death':
            _, rnd, i, bodyset, ind, cause, dropped, body = ev
            turn_deaths.append((i, rnd, ind, cause, dropped, len(body)))
            if g.team.get(i) != ours: continue
            if iso_now.get(i):
                m['iso_deaths'] += 1
                m['iso_death_' + cause.replace(' ', '_')] += 1
                if cause == 'head to head':
                    for (j, r2, ii, cc, dl, L2) in turn_deaths:
                        if g.team.get(j) != ours and cc == 'head to head' and L2 < len(body): m['iso_lost_trade'] += 1
            if len(body) >= 5 and cause in ('hit wall', 'hit self', 'no valid action') and not ind.endswith(':feed'):
                if not any(st.pockets[k]['tree'] for k in st.pocket_of.get(body[0], ())): m['long_trap_death_open'] += 1
            if ind.endswith(':feed') and cause == 'hit self':
                m['feed_deaths'] += 1
                pend.append(('feed', dropped))
            if (i, rnd) in ek:
                for (j, r2, ii, cc, dl, L2) in turn_deaths:
                    pend.append(('ek', dl))
            continue
        _, rnd, i, ate = ev
        turn_deaths = []
        for kind, lst in pend:
            for p in lst: drops[p] = kind
        pend = []
        for p in ate:
            k = drops.pop(p, None)
            if k == 'ek': m['ek_drop_ours' if g.team.get(i) == ours else 'ek_drop_theirs'] += 1
            elif k == 'feed' and g.team.get(i) == ours: feed_eaten[i] += 1
        if g.team.get(i) != ours or i not in g.bodies: continue
        b = g.bodies[i]; h = b[0]
        for p in ate: ate_at[i].append((rnd, p))
        if len(b) >= 5: m['long_turns'] += 1
        mates = any(j != i and g.team[j] == ours and cheb(bb[0], h, W, H) <= 3 for j, bb in g.bodies.items())
        foes = any(g.team[j] != ours and man(bb[0], h, W, H) <= 5 for j, bb in g.bodies.items())
        iso = len(b) >= 3 and not mates and foes and rnd >= 1
        iso_now[i] = iso
        if iso: m['iso_turns'] += 1
    fin = [(len(b), j) for j, b in g.bodies.items() if g.team[j] == ours]
    apex = max(fin)[1] if fin else None
    m['feed_drop_eaten_ours'] = sum(feed_eaten.values())
    m['feed_drop_eaten_apex'] = feed_eaten.get(apex, 0)
    m['ek_strikes'] = len(ek)
    # farm trips
    done = collections.defaultdict(list)
    for p in diag['farmdone']:
        if len(p) >= 7: done[int(p[0])].append((int(p[1]), p[6] == '1'))
    for p in diag['farmgo']:
        try: i, r0, fp = int(p[0]), int(p[1]), xy(p[2])
        except (ValueError, IndexError): continue
        m['farm_trips'] += 1
        d = [x for x in done.get(i, []) if x[0] >= r0]
        if d and d[0][1]: m['farm_arrived'] += 1
        got = sum(1 for (r, q) in ate_at.get(i, []) if r0 < r <= r0 + 20 and cheb(q, fp, W, H) <= 4)
        m['farm_trip_ate'] += got
        if got >= 2: m['farm_trip_paid'] += 1
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
        cache_f = os.path.join(d, 'audit58.json')
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
