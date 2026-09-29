#!/usr/bin/env python3
"""analyze56.py DIR [DIR ...] [--fresh] [--by-map --k=metric ...]: behavioural audit of the v5.6 modules (see
strategyV_5_9e.md). Same layout as analyze.py: one column per run directory, cached in DIR/audit56.json.

F_DRY_EVICT     dry_*: turns / stays of non-alpha dragons in a small chamber with no pearl and no spawn due within 24
                rounds (ground truth from the replay's spawn timers); re-entries by teammates after a dry exit.
F_CHOKE_GREEDY  pk_*: splits, deaths and eating inside tree-shaped dead ends of the map (bridge pockets).
F_REENTRY       camp_exit_*: a dragon that held a paying chamber 8+ rounds leaves it through a portal; back within 12?
F_SYMMETRY      sym_*: which of our dragons learn the map's symmetry (resolve or hear it), when, and whether correctly.
"""
import collections, gzip, json, multiprocessing, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze
from sim import Game, DI

DRY_HORIZON = 24


def true_symmetry(g):
    """Candidates (0 rotation, 1 x -> W-1-x, 2 y -> H-1-y, 3 x <-> y) that map every tile's spawn gaps and every edge onto
    themselves, and the declared one."""
    W, H = g.W, g.H
    def img(p, k):
        x, y = p
        return [((W - 1 - x) % W, (H - 1 - y) % H), ((W - 1 - x) % W, y), (x, (H - 1 - y) % H), (y, x)][k]
    def mdir(d, k):  # d index into 'NESW'
        return [(d + 2) % 4, [0, 3, 2, 1][d], [2, 1, 0, 3][d], [3, 2, 1, 0][d]][k]
    ok = []
    for k in range(4):
        if k == 3 and W != H: continue
        good = True
        for y in range(H):
            for x in range(W):
                q = img((x, y), k)
                if g.tiles.get((x, y), (0, 0)) != g.tiles.get(q, (0, 0)): good = False; break
                for d in range(4):
                    if g.edge(x, y, d)[0] != g.edge(q[0], q[1], mdir(d, k))[0]: good = False; break
                if not good: break
            if not good: break
        if good: ok.append(k)
    decl = next((l.split()[1] for l in g.rp.map_text.split('\n') if l.startswith('SYMMETRY')), '')
    declared = {'xy': 0, 'y': 1, 'x': 2}.get(decl, -1)
    return ok, declared


def load_diag(path):
    out = collections.defaultdict(list)
    if not os.path.exists(path): return out
    with gzip.open(path, 'rt', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) >= 3 and p[0] in ('symres', 'symheard', 'forcedout', 'reclaimed', 'reclaimfail', 'dryout',
                                        'chokehold', 'chokefinal', 'chokeenter', 'camp'):
                out[p[0]].append(p[1:])
    return out


def analyze56(replay, ours):
    g = Game(replay)
    st = analyze.Static(g)
    diag = load_diag(replay[:-len('.replay')] + '.diag.gz')
    m = collections.Counter()
    # spawn timers: tile -> [(round of the event, next attempt round)]
    timers = collections.defaultdict(list)
    rnd = 0
    for e in g.rp.events:
        if e[0] == 'round': rnd = e[1]
        elif e[0] == 'countdown': timers[(e[1], e[2])].append((rnd, rnd + e[3] if e[3] >= 0 else 10 ** 6))
    def next_spawn(t, r):
        best = 10 ** 6
        for r0, due in timers.get(t, ()):
            if r0 > r: break
            best = due
        return best
    dry_cache = {}
    def dry(c, r):
        key = (c, r)
        if key not in dry_cache:
            tiles = st.chambers[c]['tiles']
            dry_cache[key] = not any(t in g.pearls for t in tiles) and \
                             not any(r <= next_spawn(t, r) <= r + DRY_HORIZON for t in tiles)
        return dry_cache[key]
    # tree-shaped dead ends: tile -> (pocket index) for the smallest tree pocket holding it
    tree_of = {}
    for k, pk in enumerate(st.pockets):
        if not pk['tree']: continue
        for t in pk['tiles']:
            if t not in tree_of or len(st.pockets[tree_of[t]]['tiles']) > len(pk['tiles']): tree_of[t] = k
    def is_tip(head, neck):
        k = tree_of.get(head)
        if k is None: return False
        tiles = st.pockets[k]['tiles']
        return not any(n in tiles and n != neck for (n, _, _) in st.adj[head])

    sym_ok, declared = true_symmetry(g)
    m['sym_declared_valid'] = int(declared in sym_ok)
    known = {}
    for p in diag['symres'] + diag['symheard']:
        try: i, r, k = int(p[0]), int(p[1]), int(p[2])
        except (ValueError, IndexError): continue
        if i not in known or r < known[i][0]: known[i] = (r, k)
    for p in diag['symres']:
        try: k = int(p[2])
        except (ValueError, IndexError): continue
        m['sym_resolved'] += 1
        m['sym_resolved_wrong'] += k != declared
    for p in diag['symheard']:
        try: k = int(p[2])
        except (ValueError, IndexError): continue
        m['sym_heard'] += 1
        m['sym_heard_wrong'] += k != declared
    team_first = min([r for r, _ in known.values()] or [500])
    m['sym_team_first_round'] = team_first

    chamber_since = {}   # id -> (chamber, round entered)
    dry_since = {}       # id -> round its chamber went dry while it was in it
    last_exit_dry = {}   # chamber -> round one of ours left it dry
    hold_exit = []       # (id, chamber, round) a dragon that held a paying chamber 8+ rounds left it via a portal
    prev_head = {}
    in_tree = {}
    died = {}
    for ev in analyze.walk(g):
        if ev[0] == 'split':
            _, r, par, ch = ev
            if g.team.get(par) != ours: continue
            pb = g.bodies.get(par, ()); cb = g.bodies.get(ch, ())
            if pb and pb[0] in tree_of:
                k = tree_of[pb[0]]
                ahead = sum(1 for t in st.pockets[k]['tiles'] if t in g.pearls)
                m['pk_splits'] += 1
                m['pk_split_child_len'] += len(cb)
                m['pk_splits_at_tip' if is_tip(pb[0], pb[1] if len(pb) > 1 else None) else 'pk_splits_before_tip'] += 1
                if ahead: m['pk_splits_pearls_left'] += 1
            continue
        if ev[0] == 'death':
            _, r, i, bodyset, ind, cause, dropped, body = ev
            died[i] = r
            if g.team.get(i) != ours: continue
            if body and body[0] in tree_of:
                m['pk_deaths'] += 1
                m['pk_deaths_len%s' % ('2' if len(body) == 2 else '3' if len(body) == 3 else '4+')] += 1
                if len(body) == 3 and is_tip(body[0], body[1]): m['pk_deaths_len3_at_tip'] += 1
            chamber_since.pop(i, None); dry_since.pop(i, None)
            continue
        _, r, i, ate = ev
        if g.team.get(i) != ours or i not in g.bodies: continue
        body = g.bodies[i]; head = body[0]
        role = g.ind.get(i, '').partition(':')[0]
        m['turns'] += 1
        if r >= 10:
            m['sym_turns'] += 1
            if i in known and known[i][0] <= r: m['sym_turns_known'] += 1
        # dead ends of the map
        if head in tree_of:
            m['pk_turns'] += 1
            m['pk_ate'] += len(ate)
            if not in_tree.get(i): m['pk_entries'] += 1
        in_tree[i] = head in tree_of
        # chambers
        c = st.chamber_of.get(head)
        cur = chamber_since.get(i)
        if cur and cur[0] != c:
            c0, r0 = cur
            ph = prev_head.get(i)
            via_portal = ph is not None and all(g.step(ph, d) != head for d in DI)
            if via_portal and r - r0 >= 4 and not dry(c0, r): hold_exit.append((i, c0, r))
            if dry_since.get(i) is not None:
                m['dry_stays'] += 1
                m['dry_stay_rounds'] += r - dry_since[i]
                last_exit_dry[c0] = (r, i)
            chamber_since.pop(i, None); dry_since.pop(i, None)
        if c is not None:
            if i not in chamber_since:
                chamber_since[i] = (c, r)
                born_here = g.born.get(i, -9) >= r - 1 and i in g.born
                le = last_exit_dry.get(c, (-1000, -1))
                if not born_here and r - le[0] <= 30:
                    m['dry_reentries_30'] += 1
                    m['dry_reentries_30_self' if le[1] == i else 'dry_reentries_30_other'] += 1
                    if not dry(c, r): m['dry_reentries_30_paying'] += 1
            if role != 'Alpha' and not st.chambers[c]['barren']:
                m['chamber_turns'] += 1
                if dry(c, r):
                    m['dry_turns'] += 1
                    if dry_since.get(i) is None: dry_since[i] = r
                else:
                    dry_since[i] = None
                    m['paying_turns'] += 1
        prev_head[i] = head
    for (i, c0, r) in hold_exit:
        m['camp_exits'] += 1
    # re-entries: walk heads again cheaply from the recorded chamber stays
    if hold_exit:
        g2 = Game(replay); visits = collections.defaultdict(list)
        for ev in analyze.walk(g2):
            if ev[0] == 'turn' and g2.team.get(ev[2]) == ours and ev[2] in g2.bodies:
                c = st.chamber_of.get(g2.bodies[ev[2]][0])
                if c is not None: visits[(ev[2], c)].append(ev[1])
        for (i, c0, r) in hold_exit:
            if any(r < rr <= r + 12 for rr in visits[(i, c0)]): m['camp_exit_back_12'] += 1
    # spreading: dragons born after the team first knew it, rounds from birth until they know it (to death / end if never)
    for i, b0 in g.born.items():
        if g.team.get(i) != ours or b0 <= team_first: continue
        end = died.get(i, 500)
        if end - b0 < 5: continue
        m['sym_newborns'] += 1
        kr = known.get(i, (None,))[0]
        if kr is None or kr > end: m['sym_newborn_never'] += 1; m['sym_newborn_lag'] += end - b0
        else: m['sym_newborn_lag'] += kr - b0
    m['forcedout'] = len(diag['forcedout']); m['reclaimed'] = len(diag['reclaimed'])
    m['reclaimfail'] = len(diag['reclaimfail']); m['dryout'] = len(diag['dryout'])
    m['chokehold'] = len(diag['chokehold']); m['chokefinal'] = len(diag['chokefinal'])
    m['chokeenter'] = len(diag['chokeenter'])
    res = g.rp.result
    m['win'] = 1 if res.get('winner') == ours else 0
    fin = [len(b) for j, b in g.bodies.items() if g.team[j] == ours]
    m['final_longest'] = max(fin or [0])
    m['games'] = 1
    return m


def _one(path):
    try:
        return dict(analyze56(path, path[:-7].rsplit('_', 1)[1]))
    except Exception as ex:
        import traceback; traceback.print_exc()
        print('ERR', path, ex, file=sys.stderr)
        return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    cols = []
    for d in args:
        cache_f = os.path.join(d, 'audit56.json')
        per = json.load(open(cache_f)) if os.path.exists(cache_f) and '--fresh' not in sys.argv else {}
        todo = [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith('.replay') and f not in per]
        if todo:
            with multiprocessing.Pool(int(os.environ.get('AUDIT_J', '4'))) as pool:
                for f, r in zip(todo, pool.map(_one, todo)):
                    if r is not None: per[os.path.basename(f)] = r
        json.dump(per, open(cache_f, 'w'))
        tot = collections.Counter()
        for v in per.values(): tot.update(v)
        cols.append((d, tot, per))
    if '--by-map' in sys.argv:
        for kk in [k for k in sys.argv if k.startswith('--k=')]:
            k = kk[4:]
            print('== %s by map' % k)
            for mp in sorted({f.rsplit('_', 2)[0] for _, _, per in cols for f in per}):
                print('%-30s' % mp + ''.join('%10s' % sum(v.get(k, 0) for f, v in per.items() if f.rsplit('_', 2)[0] == mp)
                                              for _, _, per in cols))
    keys = []
    for _, t, _ in cols:
        for k in sorted(t):
            if k not in keys: keys.append(k)
    print('%-28s' % 'metric' + ''.join('%18s' % os.path.basename(d.rstrip('/'))[-17:] for d, _, _ in cols))
    for k in keys:
        print('%-28s' % k + ''.join('%18s' % t.get(k, 0) for _, t, _ in cols))


if __name__ == '__main__':
    main()
