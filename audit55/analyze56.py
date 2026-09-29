#!/usr/bin/env python3
"""analyze56.py DIR [DIR...] [--fresh]: behavioural audit of the v5.6 modules (F_DRY_EVICT, F_CHOKE_GREEDY, F_REENTRY,
F_SYMMETRY, F_MIRROR_SCOUT) from replays (ground truth) and DIAG traces. One column per DIR; cached in DIR/audit56.json.

- symmetry truth: mirrored tiles share one countdown, so the map's declared symmetry is the candidate under which every
  countdown reset has a partner reset (same round, same value) on the mirror tile.
- scouting: pearls a scout eats in the 10 rounds after `scoutarrive`, against its own rate over the whole game.
- dry chambers: rounds one of our heads spends in a small spawning chamber with no pearl in it and none due within 24.
- dead ends: voluntary 2-splits (DIAG mode split, child 2) while our head is in a tree pocket with a pearl in it; deaths
  in tree pockets by length.
- re-entry: after `forcedout`, did the head stand in the chamber it camped in within 12 rounds.
"""
import collections, gzip, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import Static, walk  # noqa: E402
from sim import Game  # noqa: E402

KEYS = ('turn', 'symres', 'symheard', 'symbad', 'scoutclaim', 'scoutarrive', 'scoutgiveup', 'scoutsend', 'dryout',
        'chokehold', 'chokefinal', 'chokeenter', 'forcedout', 'reclaimed', 'reclaimfail', 'camp')


def mirror(p, k, W, H):
    x, y = p
    return [(W - 1 - x, H - 1 - y), (W - 1 - x, y), (x, H - 1 - y), (y, x)][k]


def load_diag(path):
    out = collections.defaultdict(list)
    with gzip.open(path, 'rt', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) >= 4 and p[0] in KEYS and p[1].isdigit() and p[2].isdigit() and p[3].lstrip('-').isdigit() is not None:
                if p[0] in ('symres', 'symheard') and not p[3].isdigit(): continue
                out[p[0]].append(p[1:])
    return out


def xy(s):
    a, b = s.split(','); return (int(a), int(b))


def analyze(replay, ours):
    g = Game(replay); st = Static(g); W, H = g.W, g.H
    diag = load_diag(replay[:-7] + '.diag.gz')
    m = collections.Counter()
    # ---- symmetry truth from countdown resets
    cds = []; r = 0
    for e in g.rp.events:
        if e[0] == 'round': r = e[1]
        elif e[0] == 'countdown': cds.append((r, (e[1], e[2]), e[3]))
    evset = {(r, p, v) for r, p, v in cds}
    truth = []
    for k in range(4 if W == H else 3):
        n = ok = 0
        for (r, p, v) in cds:
            q = mirror(p, k, W, H)
            if q == p: continue
            n += 1; ok += (r, q, v) in evset
        if n and ok == n: truth.append(k)
    m['sym_truth_n'] = len(truth)
    # ---- per-dragon symmetry knowledge
    first = {}
    for key in ('symres', 'symheard'):
        for p in diag[key]:
            i, r, k = int(p[0]), int(p[1]), int(p[2])
            if i not in first or r < first[i][0]: first[i] = (r, k, key)
    ids = {int(p[0]) for p in diag['turn']}
    m['sym_dragons'] = len(ids)
    for i in ids:
        if i not in first: m['sym_never'] += 1; continue
        r, k, how = first[i]
        good = k in truth
        m['sym_%s_%s' % ('res' if how == 'symres' else 'heard', 'ok' if good else 'wrong')] += 1
    for p in diag['turn']:
        i, r = int(p[0]), int(p[1])
        f = first.get(i)
        m['sym_turns'] += 1
        if f and f[0] <= r:
            m['sym_turns_known_ok' if f[1] in truth else 'sym_turns_known_wrong'] += 1
    # ---- walk the replay
    modes = {(int(p[0]), int(p[1])): p for p in diag['turn'] if len(p) >= 8}
    arrive = collections.defaultdict(list)   # id -> [round]
    for p in diag['scoutarrive']: arrive[int(p[0])].append(int(p[1]))
    forced = [(int(p[0]), int(p[1])) for p in diag['forcedout']]
    due = {}; ci = 0
    ate_by = collections.defaultdict(list)     # id -> [(round, n)]
    head_hist = collections.defaultdict(list)  # id -> [(round, head)]
    in_tree = {}
    for ev in walk(g):
        if ev[0] == 'split':
            _, rnd, par, ch = ev
            if g.team.get(ch) != ours: continue
            pb = g.bodies[par]; h = pb[0]
            ks = [k for k in st.pocket_of.get(h, ()) if st.pockets[k]['tree']]
            md = modes.get((par, rnd))
            if ks and md and md[3] == 'split' and len(g.bodies[ch]) == 2:
                tiles = st.pockets[ks[0]]['tiles']
                if any(q in g.pearls for q in tiles): m['de_split2_with_pearls'] += 1
                else: m['de_split2_no_pearls'] += 1
            if ks and md and md[3] == 'rescue':
                m['de_rescue'] += 1; m['de_rescue_child_len'] += len(g.bodies[ch])
                m['de_rescue_head_len%d' % min(len(pb), 3)] += 1
            continue
        if ev[0] == 'death':
            _, rnd, i, bodyset, ind, cause, dropped, body = ev
            if g.team.get(i) != ours or not body: continue
            ks = [k for k in st.pocket_of.get(body[0], ()) if st.pockets[k]['tree']]
            if ks:
                m['de_death_len%d' % min(len(body), 4)] += 1
            continue
        _, rnd, i, ate = ev
        while ci < len(cds) and cds[ci][0] <= rnd:
            due[cds[ci][1]] = cds[ci][0] + cds[ci][2]; ci += 1
        if g.team.get(i) != ours or i not in g.bodies: continue
        b = g.bodies[i]; h = b[0]
        ate_by[i].append((rnd, len(ate)))
        head_hist[i].append((rnd, h))
        c = st.chamber_of.get(h)
        if c is not None and not st.chambers[c]['barren']:
            tiles = st.chambers[c]['tiles']
            m['cham_turns'] += 1
            now = any(q in g.pearls for q in tiles)
            soon = any(0 <= due.get(q, -10) - rnd <= 24 for q in tiles)
            if not now and not soon:
                m['cham_dry_turns'] += 1
                if not g.ind.get(i, '').startswith('Alpha:'): m['cham_dry_turns_nonalpha'] += 1
    # ---- scouting: pearls in the 10 rounds after arriving vs the dragon's own mean rate
    for i, rs in arrive.items():
        eats = ate_by.get(i, [])
        tot_rate = sum(n for _, n in eats) / max(1, len(eats))
        for r0 in rs:
            got = sum(n for (r, n) in eats if r0 < r <= r0 + 10)
            m['scout_arrivals'] += 1
            m['scout_after10'] += got
            m['scout_expected10_x100'] += int(100 * 10 * tot_rate)
            if got == 0: m['scout_arrive_nothing'] += 1
    m['scout_claims'] = len(diag['scoutclaim'])
    m['scout_giveups'] = len(diag['scoutgiveup'])
    m['scout_sends'] = len(diag['scoutsend'])
    m['scout_arrive_diag_pearls'] = sum(int(p[4]) for p in diag['scoutarrive'] if len(p) > 4 and p[4].isdigit())
    # ---- re-entry
    for (i, r0) in forced:
        hh = head_hist.get(i, [])
        before = [h for (r, h) in hh if r < r0]
        cham = None
        for h in reversed(before[-4:]):
            if h in st.chamber_of: cham = st.chamber_of[h]; break
        m['forced'] += 1
        if cham is None: m['forced_nochamber'] += 1; continue
        back = [r for (r, h) in hh if r0 < r <= r0 + 12 and st.chamber_of.get(h) == cham]
        if back: m['forced_back12'] += 1; m['forced_back_rounds'] += back[0] - r0
        dead = g.dead.get(i)
        if not back and dead and dead[0] <= r0 + 12: m['forced_died12'] += 1
    for k in ('reclaimed', 'reclaimfail', 'dryout', 'chokehold', 'chokefinal', 'chokeenter'):
        m['diag_' + k] = len(diag[k])
    m['ate'] = sum(n for v in ate_by.values() for _, n in v)
    res = g.rp.result
    m['win'] = 1 if res.get('winner') == ours else 0
    m['games'] = 1
    return m


def _one(path):
    try:
        return dict(analyze(path, path[:-7].rsplit('_', 1)[1]))
    except Exception as ex:
        import traceback; traceback.print_exc()
        return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    cols = []
    for d in args:
        tot = collections.Counter()
        cache_f = os.path.join(d, 'audit56.json')
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
