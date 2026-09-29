#!/usr/bin/env python3
"""analyze57.py DIR [DIR...] [--fresh] [--by-map --k=metric]: behavioural audit of the v5.7 modules. Cached in
DIR/audit57.json. Ground truth from the replay, intent from the DIAG trace:

- F_RENDEZVOUS: every `rdvgo` trip: was our head within 1 of the spot by t + 2, and how many pearls did the dragon eat within
  2 tiles of it in rounds t - 2 .. t + 15 (t: when the cluster was due).
- F_SPLIT_CAP: voluntary 2-splits (DIAG mode split, child 2, not a harvest / farm split) by non-alphas while units > 33.
- F_SPLIT_COOL: the same within SPLIT_COOL = 8 rounds of birth by a dragon born 4+ long in a split (round > 0).
- F_PROTECT: our deliberate strikes (mode kill, head to head): pearls of the two drops eaten by us / them / nobody.
- F_PAIR_SEP: turns in which a non-alpha of ours has another non-alpha head within 2 (Chebyshev) and has eaten nothing for
  3+ turns running, counted from the 4th such turn (the rule turns one of them away on the 3rd).
- F_CHOKE_LOOP: rescue splits (mode rescue), and the most on any one tile in a game.
- F_REM_COMMIT: remembered-pearl turns (DIAG tgt rem) and how often the target tile changed from the previous turn's.
- F_CHAMBER_ONE: two of ours entering the same small spawning chamber within 1 round of each other.
- F_FEED_CLEAR: our longest dragon (10+) dying after round 350 by hitting one of our own bodies or with no move.
"""
import collections, gzip, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import Static, walk  # noqa: E402
from sim import Game  # noqa: E402

UNIT_CAP, SPLIT_COOL = 33, 8
KEYS = ('turn', 'tgt', 'rdvgo', 'harvest', 'tailbfs', 'farmharvest', 'farmsplit', 'tailtrap', 'assassin', 'earlykill',
        'exchange', 'ram', 'pairsep')


def cheb(a, b, W, H):
    dx = abs(a[0] - b[0]); dy = abs(a[1] - b[1]); return max(min(dx, W - dx), min(dy, H - dy))


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
    turns = {}
    for p in diag['turn']:
        if len(p) >= 11:
            try: turns[(int(p[0]), int(p[1]))] = (int(p[2]), p[3], int(p[6]), int(p[7]), int(p[8]), int(p[9]))
            except ValueError: pass   # len, mode, child, born, born_len, units
    special = set()
    for k in ('harvest', 'tailbfs', 'farmharvest', 'farmsplit', 'tailtrap'):
        for p in diag[k]: special.add((int(p[0]), int(p[1])))
    role = {}
    ate_at = collections.defaultdict(list)     # id -> [(round, tile)]
    head_at = collections.defaultdict(dict)    # id -> {round: head}
    drops = {}                                  # tile -> kind ('strike')
    strike_deaths = 0
    rescue_tiles = collections.Counter()
    pair_run = collections.Counter()
    last_ate = {}
    cham_enter = collections.defaultdict(list)   # chamber -> [(round, id)]
    in_cham = {}
    apex_deaths = 0
    pend_drops = []
    turn_deaths = []
    for ev in walk(g):
        if ev[0] == 'split':
            _, rnd, par, ch = ev
            if g.team.get(ch) != ours: continue
            t = turns.get((par, rnd))
            if t and t[1] == 'split' and t[2] == 2 and (par, rnd) not in special and role.get(par) != 'Alpha':
                if t[5] > UNIT_CAP: m['cap_split_over'] += 1
                if t[3] > 0 and t[4] >= 4 and rnd - t[3] < SPLIT_COOL: m['cool_split_young'] += 1
            if t and t[1] == 'rescue':
                m['rescue_splits'] += 1
                rescue_tiles[g.bodies[par][0]] += 1
            continue
        if ev[0] == 'death':
            _, rnd, i, bodyset, ind, cause, dropped, body = ev
            turn_deaths.append((i, ind, cause, dropped))
            if g.team.get(i) != ours:
                if cause == 'head to head' and any(g.team.get(j) == ours and ii.endswith(':kill') and cc == 'head to head'
                                                   for (j, ii, cc, _) in turn_deaths):
                    pend_drops.append(dropped)
                continue
            if ind.endswith(':kill') and cause == 'head to head':
                m['strikes'] += 1
                for (j, ii, cc, dl) in turn_deaths:
                    if g.team.get(j) != ours and cc == 'head to head': pend_drops.append(dl)
                # the pearls both bodies drop appear later this turn; walk() fills them into `dropped` then
                pend_drops.append(dropped)
            if body and rnd > 350 and len(body) >= 10:
                others = [len(b) for j, b in g.bodies.items() if g.team[j] == ours and j != i]
                if not others or len(body) >= max(others):
                    if cause in ('hit other body', 'no valid action', 'hit self') and not ind.endswith(':feed'):
                        m['apex_late_deaths'] += 1
            in_cham.pop(i, None)
            continue
        _, rnd, i, ate = ev
        turn_deaths = []
        for lst in pend_drops:
            for p in lst: drops[p] = 'strike'
        pend_drops = []
        for p in ate:
            if p in drops:
                del drops[p]
                m['strike_drop_ours' if g.team.get(i) == ours else 'strike_drop_theirs'] += 1
        if g.team.get(i) != ours or i not in g.bodies: continue
        b = g.bodies[i]; h = b[0]
        role[i] = g.ind.get(i, '').partition(':')[0]
        head_at[i][rnd] = h
        for p in ate: ate_at[i].append((rnd, p))
        if ate: last_ate[i] = rnd
        # pairs
        if role[i] == 'Neutral':
            near = any(j != i and g.team[j] == ours and role.get(j) == 'Neutral' and cheb(bb[0], h, W, H) <= 2
                       for j, bb in g.bodies.items())
            idle = rnd - last_ate.get(i, -100) >= 1
            if near and idle: pair_run[i] += 1
            else: pair_run[i] = 0
            if pair_run[i] > 3: m['pair_idle_turns'] += 1
        # chamber entries
        c = st.chamber_of.get(h)
        if c is not None and not st.chambers[c]['barren']:
            if in_cham.get(i) != c:
                if g.born.get(i, -1) < rnd - 1:
                    for (r0, j) in cham_enter[c]:
                        if j != i and rnd - r0 <= 1 and j in g.bodies and st.chamber_of.get(g.bodies[j][0]) == c:
                            m['chamber_double_entry'] += 1; break
                    cham_enter[c].append((rnd, i))
            in_cham[i] = c
        else:
            in_cham.pop(i, None)
    m['strike_drop_nobody'] = len(drops)
    m['rescue_max_one_tile'] = max(rescue_tiles.values()) if rescue_tiles else 0
    # rendezvous trips
    for p in diag['rdvgo']:
        try: i, r0, spot, t = int(p[0]), int(p[1]), xy(p[2]), int(p[4])
        except (ValueError, IndexError): continue
        m['rdv_trips'] += 1
        hh = head_at.get(i, {})
        on_time = any(cheb(h, spot, W, H) <= 1 for r, h in hh.items() if r0 <= r <= t + 2)
        got = sum(1 for (r, q) in ate_at.get(i, []) if t - 2 <= r <= t + 15 and cheb(q, spot, W, H) <= 2)
        if on_time:
            m['rdv_on_time'] += 1; m['rdv_on_time_ate'] += got
        m['rdv_ate'] += got
        if got == 0: m['rdv_nothing'] += 1
    # remembered-target flips
    prev = {}
    for p in diag['tgt']:
        if len(p) < 4 or p[2] != 'rem': continue
        i, r = int(p[0]), int(p[1])
        m['rem_turns'] += 1
        q = prev.get(i)
        if q and q[0] == r - 1 and q[1] != p[3]: m['rem_flips'] += 1
        prev[i] = (r, p[3])
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
        cache_f = os.path.join(d, 'audit57.json')
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
