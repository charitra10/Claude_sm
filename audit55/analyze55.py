#!/usr/bin/env python3
"""analyze55.py DIR [DIR...] [--fresh] [--by-map --k=metric]: behavioural audit of the v5.5 modules.

Each DIR holds <map>_<seed>_<side>.replay / .diag.gz from run.py; our team is <side>. Ground truth comes from the replay
(full board); the bot's own view comes from its DIAG lines. One column per DIR, so module-on and module-off runs compare
side by side. Per-game numbers are cached in DIR/audit55.json.

Static structure (pockets = small side of a bridge edge, chambers = <= 20 tiles with a portal edge) is analyze52.Static,
taken from the v5.2 audit (audit52/ on the other branch).
"""
import collections, gzip, json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import Static, walk, xy  # noqa: E402
from sim import Game  # noqa: E402

SOON = 12          # CAMP_SOON
DIAG_KEYS = ('mantle', 'claim', 'mantlebeam', 'mantleguard', 'camp', 'adopt', 'harvest', 'tailbfs', 'cascade',
             'chokesplit', 'chokeenter', 'chokehold', 'chokefinal', 'tailtrap', 'rescue', 'farmharvest', 'dryout')


def load_diag(path):
    out = collections.defaultdict(list)
    if not os.path.exists(path): return out
    with gzip.open(path, 'rt', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) >= 2 and p[0] in DIAG_KEYS:
                out[p[0]].append(p[1:])
    return out


def cheb(a, b, W, H):
    dx = abs(a[0] - b[0]); dy = abs(a[1] - b[1])
    return max(min(dx, W - dx), min(dy, H - dy))


def analyze(replay, ours):
    g = Game(replay)
    st = Static(g)
    diag = load_diag(replay[:-len('.replay')] + '.diag.gz')
    W, H = g.W, g.H
    m = collections.Counter()
    role = {}                    # id -> role at its last completed turn
    hist = collections.defaultdict(list)   # id -> [(rnd, head, ate, ind, len)]
    births = {}                  # child -> (rnd, parent, child len, parent len after)
    split_at = {}                # (parent, rnd) -> [child ...]
    due = {}                     # tile -> round of its next spawn attempt
    initial = {i: len(b) for i, b in g.bodies.items() if g.team[i] == ours}
    m['spawn_bodies'] = len(initial)
    m['spawn_bodies_4plus'] = sum(1 for L in initial.values() if L >= 4)
    # --- mantle bookkeeping
    handovers = []               # dicts
    demoted_at = {}              # id -> round it handed over
    # --- pockets (dead ends)
    visit = {}                   # id -> dict(k, r0, len, pearls, ate)
    # --- chambers
    prev_in = collections.defaultdict(set)   # chamber -> our heads inside at the end of the previous round
    cur_round = 0
    alpha_ids_round = set()

    def chamber_pearls(c):
        tiles = st.chambers[c]['tiles']
        now = sum(1 for p in tiles if p in g.pearls)
        soon = sum(1 for p in tiles if p not in g.pearls and 0 <= due.get(p, -10) - cur_round <= SOON)
        return now, soon

    def end_round(r):
        # chambers: who of ours is inside now (heads)
        heads = {}
        for j, b in g.bodies.items():
            if g.team[j] != ours: continue
            c = st.chamber_of.get(b[0])
            if c is not None and not st.chambers[c]['barren']: heads.setdefault(c, set()).add(j)
        theirs = set()
        for j, b in g.bodies.items():
            if g.team[j] == ours: continue
            c = st.chamber_of.get(b[0])
            if c is not None: theirs.add(c)
        for c, ch in enumerate(st.chambers):
            if ch['barren']: continue
            inside = heads.get(c, set())
            now, soon = chamber_pearls(c)
            m['cham_rounds'] += 1
            if len(inside) == 1: m['cham_rounds_one'] += 1
            elif len(inside) >= 2: m['cham_rounds_crowded'] += 1
            elif c in theirs: m['cham_rounds_theirs'] += 1
            else:
                m['cham_rounds_empty'] += 1
                if now + soon: m['cham_rounds_empty_paying'] += 1
            before = prev_in[c]
            if before and not inside:
                gone_alive = [j for j in before if j in g.bodies]
                if len(gone_alive) == len(before):
                    pay = now + soon > 0
                    if len(before) == 1:
                        m['cham_lone_leave'] += 1
                        if pay: m['cham_lone_leave_paying'] += 1
                    else:
                        m['cham_all_leave'] += 1
                        if pay: m['cham_all_leave_paying'] += 1
            prev_in[c] = inside
        # alphas
        na = sum(1 for j in g.bodies if g.team[j] == ours and role.get(j) == 'Alpha')
        m['alpha_count_rounds'] += 1
        m['alpha_count_sum'] += na
        if na >= 3: m['alpha_3plus_rounds'] += 1
        if r == 1:
            ls = [len(b) for j, b in g.bodies.items() if g.team[j] == ours]
            m['r0_units'] = len(ls)
            m['r0_long4plus'] = sum(1 for L in ls if L >= 4)
            m['r0_len_left_4plus'] = sum(L for L in ls if L >= 4)

    # spawn timers: a countdown event at round r (a reset at a spawn attempt) means the next attempt is at r + value
    cds = []
    r = 0
    for e in g.rp.events:
        if e[0] == 'round': r = e[1]
        elif e[0] == 'countdown': cds.append((r, (e[1], e[2]), e[3]))
    cdi = 0
    events = walk(g)
    rnd_seen = 0
    for ev in events:
        if ev[0] == 'split':
            _, rnd, par, ch = ev
            if g.team.get(ch) != ours: continue
            births[ch] = (rnd, par, len(g.bodies[ch]), len(g.bodies[par]))
            split_at.setdefault((par, rnd), []).append(ch)
            if rnd > 0 and role.get(par) == 'Alpha':
                cl, pl = len(g.bodies[ch]), len(g.bodies[par])
                if cl > pl:
                    handovers.append({'par': par, 'ch': ch, 'rnd': rnd, 'cl': cl, 'pl': pl})
            continue
        if ev[0] == 'death':
            _, rnd, i, bodyset, ind, cause, dropped, body = ev
            if g.team.get(i) != ours: continue
            if i in visit:
                v = visit.pop(i)
                m['de_visit_died'] += 1
                m['de_visit_died_len'] += len(body)
                m['de_visit_ate'] += v['ate']
                if v['len'] >= 3: m['de_visit3_died'] += 1
            if ind.endswith(':feed') and cause == 'hit self' and body:
                h = body[0]
                near = [(len(b), j) for j, b in g.bodies.items() if g.team[j] == ours and j != i and cheb(b[0], h, W, H) <= 4]
                if near:
                    L, j = max(near)
                    if j in demoted_at and rnd - demoted_at[j] <= 40 and L <= len(body):
                        m['feed_into_demoted'] += 1
            continue
        _, rnd, i, ate = ev
        while cdi < len(cds) and cds[cdi][0] <= rnd:
            due[cds[cdi][1]] = cds[cdi][0] + cds[cdi][2]
            cdi += 1
        if rnd != rnd_seen:
            # rounds rnd_seen .. rnd-1 are over
            cur_round = rnd_seen
            end_round(rnd)
            rnd_seen = rnd
        if g.team.get(i) != ours or i not in g.bodies: continue
        body = g.bodies[i]
        head = body[0]
        ind = g.ind.get(i, '')
        rl, _, mode = ind.partition(':')
        prev = hist[i][-1] if hist[i] else None
        hist[i].append((rnd, head, len(ate), ind, len(body)))
        role[i] = rl
        m['turns'] += 1
        m['ate'] += len(ate)
        if rl == 'Alpha':
            m['alpha_turns'] += 1
            if len(body) == 2 and rnd > 0: m['alpha_turns_len2'] += 1
        # --- dead-end pockets (tree pockets of the map): entries, what they paid
        if i in visit:
            v = visit[i]
            if head in st.pockets[v['k']]['tiles']:
                v['ate'] += len(ate)
            else:
                visit.pop(i)
                m['de_visit_out'] += 1
                m['de_visit_ate'] += v['ate']
                if v['ate'] >= 2: m['de_visit_paid'] += 1
        if prev is not None and i not in visit:
            k = st.entry.get((prev[1], head))
            if k is not None and st.pockets[k]['tree'] and len(st.pockets[k]['tiles']) >= 2:
                tiles = st.pockets[k]['tiles']
                pearls = sum(1 for p in tiles if p in g.pearls) + len(ate)
                fast = sum(1 for p in tiles if p not in g.pearls and 0 <= due.get(p, -10) - rnd <= 1)
                L = len(body) - len(ate)
                m['de_entries'] += 1
                m['de_entries_len%s' % ('2' if L <= 2 else '3' if L == 3 else '4p')] += 1
                live = pearls + fast
                if live < 2:
                    m['de_entries_lt2'] += 1
                    if L >= 3: m['de_entries_lt2_len3p'] += 1
                if pearls == 0 and fast == 0: m['de_entries_empty'] += 1
                visit[i] = {'k': k, 'r0': rnd, 'len': L, 'pearls': live, 'ate': len(ate)}
    # tail of the last round
    cur_round = rnd_seen
    end_round(rnd_seen + 1)
    for i, v in visit.items():
        m['de_visit_ate'] += v['ate']

    # --- mantle outcomes
    for h in handovers:
        m['mantle_expected'] += 1
        ch, par, r = h['ch'], h['par'], h['rnd']
        chh = hist.get(ch, [])
        if chh:
            if chh[0][3].startswith('Alpha:'): m['mantle_child_alpha'] += 1
            elif h['cl'] >= 4: m['mantle_child4_not_alpha'] += 1
        ph = [x for x in hist.get(par, []) if r <= x[0] <= r + 30]
        if any(x[3].startswith('Alpha:') for x in ph[1:]): m['mantle_parent_realpha'] += 1
        if ph and ph[0][3].startswith('Alpha:'): m['mantle_parent_kept'] += 1
        demoted_at[par] = r
    # DIAG side
    m['diag_mantle'] = len(diag['mantle'])
    m['diag_mantle_child4'] = sum(1 for p in diag['mantle'] if len(p) >= 5 and int(p[4]) >= 4)
    m['diag_claim'] = len(diag['claim'])
    m['diag_mantlebeam_miss'] = sum(1 for p in diag['mantlebeam'] if p[-1] == '0')
    m['diag_mantleguard'] = len(diag['mantleguard'])
    m['diag_cascade'] = len(diag['cascade'])
    # --- harvest children
    for key in ('harvest', 'tailbfs'):
        for p in diag[key]:
            try: i, r = int(p[0]), int(p[1])
            except (ValueError, IndexError): continue
            kids = split_at.get((i, r), [])
            m['hv_%s' % key] += 1
            if not kids: m['hv_%s_nosplit' % key] += 1; continue
            ch = kids[-1]
            hh = [x for x in hist.get(ch, []) if x[0] <= r + 8]
            got = sum(x[2] for x in hh)
            m['hv_%s_child_ate8' % key] += got
            if got >= 2: m['hv_%s_paid' % key] += 1
            if got == 0: m['hv_%s_zero' % key] += 1
            dr = g.dead.get(ch)
            if dr and dr[0] <= r + 8: m['hv_%s_child_died8' % key] += 1
    for p in diag['camp']:
        pass
    m['diag_adopt'] = len(diag['adopt'])
    for k in ('chokesplit', 'chokeenter', 'chokehold', 'chokefinal', 'tailtrap', 'farmharvest', 'dryout'):
        m['diag_' + k] = len(diag[k])
    res = g.rp.result
    m['win'] = 1 if res.get('winner') == ours else 0
    m['longest'] = res.get(ours, (0, 0, 0))[1] if isinstance(res.get(ours), tuple) else 0
    m['games'] = 1
    return m


def _one(path):
    try:
        return dict(analyze(path, path[:-7].rsplit('_', 1)[1]))
    except Exception as ex:
        import traceback; traceback.print_exc()
        print('ERR', path, ex, file=sys.stderr)
        return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    cols = []
    for d in args:
        tot = collections.Counter()
        cache_f = os.path.join(d, 'audit55.json')
        per = json.load(open(cache_f)) if os.path.exists(cache_f) and '--fresh' not in sys.argv else {}
        todo = [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith('.replay') and os.path.basename(f) not in per]
        if todo:
            import multiprocessing
            with multiprocessing.Pool(int(os.environ.get('AUDIT_J', '4'))) as pool:
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
    keys = []
    for _, t, _ in cols:
        for k in t:
            if k not in keys: keys.append(k)
    print('%-28s' % 'metric' + ''.join('%14s' % os.path.basename(d.rstrip('/'))[-13:] for d, _, _ in cols))
    for k in sorted(keys):
        print('%-28s' % k + ''.join('%14s' % t.get(k, 0) for _, t, _ in cols))


if __name__ == '__main__':
    main()
