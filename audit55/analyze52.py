#!/usr/bin/env python3
"""analyze.py DIR [DIR...]: behavioural audit of the v5.2 modules from replays (ground truth) and DIAG traces.

Each DIR holds <map>_<seed>_<side>.replay / .diag.gz from run.py; our team is <side>. Prints one table per module, per DIR,
so a module-on and a module-off run can be compared side by side. `--json` writes the raw per-game numbers too.
"""
import collections, gzip, json, math, os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'replay_tools'))
from sim import Game, DI

POCKET_MAX = 48      # the bots' own pocket flood cap
CHAMBER_MAX = 20     # SMALL_ENCLOSURE
LOOP_MODES = ('reside', 'feed', 'loop', 'cascade')


# ---------------------------------------------------------------- static map structure
class Static:
    def __init__(self, g):
        self.g = g
        W, H = g.W, g.H
        self.tiles = [(x, y) for y in range(H) for x in range(W)]
        self.spawner = {p: g.tiles.get(p, (0, 0))[1] > 0 for p in self.tiles}
        self.portal_tile = {p: any(g.edge(p[0], p[1], d)[0] == 2 for d in range(4)) for p in self.tiles}
        # undirected multigraph, portals included
        self.adj = collections.defaultdict(list)
        eid = 0
        seen = set()
        for p in self.tiles:
            for d in DI:
                n = g.dest(p, d)
                if n is None: continue
                back = DI[(DI.index(d) + 2) % 4]
                key = (p, d)
                if key in seen: continue
                seen.add(key)
                # the reverse traversal of the same edge: from n moving back
                seen.add((n, back))
                self.adj[p].append((n, eid, d))
                self.adj[n].append((p, eid, back))
                eid += 1
        self.pockets = []          # dicts: tiles, entry (inside tile, dir from outside), outside tile, tree, spawners
        self.pocket_of = collections.defaultdict(list)
        self.entry = {}            # (outside tile, inside tile) -> pocket index
        for (u, v, d_uv) in self.bridges():
            for (o, i, d) in ((u, v, d_uv), (v, u, DI[(DI.index(d_uv) + 2) % 4])):
                side = self.side(i, o)
                if side is None: continue
                edges = sum(1 for p in side for (n, e, _) in self.adj[p] if n in side) // 2
                k = len(self.pockets)
                self.pockets.append({'tiles': side, 'in': i, 'out': o, 'dir': d, 'tree': edges == len(side) - 1,
                                     'spawners': sum(self.spawner[p] for p in side)})
                for p in side: self.pocket_of[p].append(k)
                self.entry[(o, i)] = k
        # small chambers: components over open (non-portal) edges with a portal edge
        self.chamber_of = {}
        self.chambers = []
        done = set()
        for p in self.tiles:
            if p in done: continue
            comp, q = {p}, [p]
            while q:
                c = q.pop()
                for d in DI:
                    kind, _ = g.edge(c[0], c[1], d)
                    if kind != 0: continue
                    n = g.step(c, d)
                    if n not in comp: comp.add(n); q.append(n)
            done |= comp
            if len(comp) <= CHAMBER_MAX and any(self.portal_tile[c] for c in comp):
                k = len(self.chambers)
                self.chambers.append({'tiles': comp, 'barren': not any(self.spawner[c] for c in comp)})
                for c in comp: self.chamber_of[c] = k

    def side(self, start, block):
        """Tiles reachable from start without stepping onto `block`, if at most POCKET_MAX."""
        comp, q = {start}, [start]
        while q:
            c = q.pop()
            for (n, _, _) in self.adj[c]:
                if n == block and c == start: continue
                if n == block: return None  # a second way back out: not a pocket through this edge
                if n not in comp:
                    comp.add(n); q.append(n)
                    if len(comp) > POCKET_MAX: return None
        return comp

    def bridges(self):
        disc, low, out = {}, {}, []
        t = 0
        for root in self.tiles:
            if root in disc: continue
            disc[root] = low[root] = t; t += 1
            stack = [(root, -1, iter(self.adj[root]))]
            while stack:
                v, pe, it = stack[-1]
                nxt = next(it, None)
                if nxt is None:
                    stack.pop()
                    if stack:
                        u = stack[-1][0]
                        low[u] = min(low[u], low[v])
                        if low[v] > disc[u]:
                            d = next(dd for (n, e, dd) in self.adj[u] if e == pe)
                            out.append((u, v, d))
                    continue
                n, e, d = nxt
                if e == pe: continue
                if n in disc: low[v] = min(low[v], disc[n])
                else:
                    disc[n] = low[n] = t; t += 1
                    stack.append((n, e, iter(self.adj[n])))
        return out


# ---------------------------------------------------------------- replay walk
def walk(g):
    """Yields ('turn', rnd, id, ate_tiles) after each dragon turn and ('death', rnd, id, body, ind, cause, dropped)."""
    rnd = 0; cur = None; pending = None; ate = []; dead_now = []
    g.occ = {}
    for j, b in g.bodies.items():
        for q in b: g.occ[q] = j
    def unmark(j, b):
        for q in b:
            if g.occ.get(q) == j: del g.occ[q]
    def mark(j, b):
        for q in b: g.occ[q] = j
    for e in g.rp.events:
        k = e[0]
        if k == 'round': rnd = e[1]
        elif k == 'turn':
            if cur is not None: yield ('turn', rnd, cur, ate)
            cur = e[1]; ate = []; dead_now = []
        elif k == 'tile':
            p = (e[1], e[2])
            if e[3]:
                g.pearls.add(p)
                for dd in dead_now:
                    if p in dd[3]: dd[6].append(p)
            else:
                if p in g.pearls and cur is not None: ate.append(p)
                g.pearls.discard(p)
        elif k == 'countdown': g.countdown[(e[1], e[2])] = e[3]
        elif k == 'ind': g.ind[e[1]] = e[2]
        elif k == 'action': pending = e
        elif k == 'update':
            _, i, facing, hx, hy, tx, ty = e
            body = g.bodies.get(i)
            if body is None: continue
            steps = pending[3] if pending and pending[1] == i and pending[2] == 'move' else facing
            unmark(i, body)
            h = body[0]
            for d in steps:
                n = g.dest(h, d)
                if n is None: break
                body.insert(0, n); h = n
            if body[0] != (hx, hy): body.insert(0, (hx, hy))
            if (tx, ty) in body:
                j = len(body) - 1 - body[::-1].index((tx, ty)); del body[j + 1:]
            else: body[:] = [(hx, hy), (tx, ty)]
            mark(i, body)
        elif k == 'split':
            _, par, ch, tm, fc, pb, cb = e
            unmark(par, g.bodies.get(par, ()))
            g.bodies[par] = list(pb); g.bodies[ch] = list(cb); g.team[ch] = tm; g.born[ch] = rnd
            mark(par, g.bodies[par]); mark(ch, g.bodies[ch])
            yield ('split', rnd, par, ch)
        elif k == 'death':
            if e[1] in g.bodies:
                body = list(g.bodies[e[1]])
                # did its last move go through a portal, and whose body is on the tile it died on?
                via_portal, hit, h = False, None, body[0] if body else None
                if pending and pending[1] == e[1] and pending[2] == 'move' and body:
                    h = body[0]
                    for d in pending[3]:
                        kind, _ = g.edge(h[0], h[1], d)
                        n = g.dest(h, d)
                        if n is None: break
                        via_portal = kind == 2
                        h = n
                    j = g.occ.get(h)
                    hit = g.team.get(j) if j is not None and j != e[1] else None
                g.last_death_portal = (via_portal, hit)
                g.last_death_tile = h if body else None
                rec = ['death', rnd, e[1], set(body), g.ind.get(e[1], ''), e[2], [], body]
                dead_now.append(rec)
                unmark(e[1], g.bodies[e[1]])
                del g.bodies[e[1]]
                yield rec
    if cur is not None: yield ('turn', rnd, cur, ate)


def load_diag(path):
    out = collections.defaultdict(list)
    if not os.path.exists(path): return out
    with gzip.open(path, 'rt', errors='replace') as f:
        for line in f:
            p = line.split()
            if len(p) >= 4 and p[0] in ('hazard', 'hazrecv', 'feedhold', 'amem', 'cycle1', 'cycle2', 'resvrecv', 'camp',
                                        'dryout', 'useportal', 'feeddrop', 'unblock'):
                out[p[0]].append(p[1:])
    return out


def xy(s):
    a, b = s.split(','); return (int(a), int(b))


def analyze(replay, ours):
    g = Game(replay)
    st = Static(g)
    diag = load_diag(replay[:-len('.replay')] + '.diag.gz')
    W, H = g.W, g.H
    m = collections.Counter()
    hist = collections.defaultdict(list)   # id -> [(rnd, head, ate, ind, len)]
    alive_heads = {}
    in_pocket = {}                          # id -> set of pocket ids its head is in
    chamber_visit = {}                      # id -> [chamber, enter round, ate]
    visits = []
    follow_events = []                      # (rnd, entrant, pocket, occupants)
    pocket_occupants = collections.defaultdict(dict)   # pocket -> {id: entry round}
    died_in_pocket = collections.Counter()
    drops = {}                              # pearl tile -> (rnd dropped, kind)
    drop_fate = collections.Counter()
    drop_eaters = []
    deliveries = {}
    for p in diag['feeddrop']:
        try: deliveries[(int(p[0]), int(p[1]))] = int(p[3])
        except (ValueError, IndexError): pass
    deliver_body = {}
    pile_at_feed = []
    births = {}
    hz_raise = []
    for p in diag['hazard']:
        try: hz_raise.append((int(p[0]), int(p[1]), xy(p[2]), int(p[4]), xy(p[6]), int(p[8])))
        except (ValueError, IndexError): pass
    hz_recv = collections.defaultdict(list)  # (mouth, dir) -> [(id, round)]
    for p in diag['hazrecv']:
        try: hz_recv[(xy(p[2]), int(p[4]))].append((int(p[0]), int(p[1])))
        except (ValueError, IndexError): pass
    live_hz = {}   # (mouth, dir) -> (owner, last raise round)
    raise_by_round = collections.defaultdict(list)
    for r in hz_raise: raise_by_round[r[1]].append(r)
    hz_outcome = []
    hz_violations = []
    eaten_at = collections.defaultdict(list)  # tile -> [(round, id)]

    for ev in walk(g):
        if ev[0] == 'split':
            _, rnd, par, ch = ev
            births[ch] = (rnd, par)
            continue
        if ev[0] == 'death':
            _, rnd, i, bodyset, ind, cause, dropped, body = ev
            if g.team.get(i) != ours: continue
            vp, hit = getattr(g, 'last_death_portal', (False, None))
            if vp and cause in ('hit other body', 'head to head'):
                m['portal_exit_death_ally' if hit == ours else 'portal_exit_death_enemy' if hit else 'portal_exit_death_other'] += 1
                if hit == ours:
                    m['portal_exit_death_ally_in_chamber' if g.last_death_tile in st.chamber_of else 'portal_exit_death_ally_open'] += 1
            if in_pocket.get(i):
                died_in_pocket[min(in_pocket[i])] += 1
            for k in list(pocket_occupants):
                pocket_occupants[k].pop(i, None)
            if i in chamber_visit:
                c, r0, a = chamber_visit.pop(i); visits.append((i, c, r0, rnd, a, 'died'))
            alive_heads.pop(i, None)
            m['deaths'] += 1
            feed = ind.endswith(':feed') and cause == 'hit self'
            tgt = deliveries.get((i, rnd))
            if tgt is not None:
                m['deliveries'] += 1
                deliver_body[i] = tgt
            if feed:
                m['feed_deaths'] += 1
                # the pile round the apex (our longest other dragon) when this feeder drops
                others = [(len(b), j) for j, b in g.bodies.items() if g.team[j] == ours]
                if others:
                    L, apex = max(others)
                    ah = g.bodies[apex][0]
                    pile = sum(1 for p in g.pearls if max(min(abs(p[0] - ah[0]), W - abs(p[0] - ah[0])),
                                                          min(abs(p[1] - ah[1]), H - abs(p[1] - ah[1]))) <= 3)
                    pile_at_feed.append((pile, len(body)))
            # pearls dropped: fill in on the next events; remember the body now
            for j, p in enumerate(body):
                if j % 2 == 0: drops[p] = (rnd, 'deliver' if tgt is not None else 'feed' if feed else 'death', len(body), tgt)
            continue
        _, rnd, i, ate = ev
        for p in ate:
            eaten_at[p].append((rnd, i))
            if p in drops:
                r0, kind, L, tgt = drops.pop(p)
                drop_fate[(kind, 'ours' if g.team.get(i) == ours else 'enemy')] += 1
                if kind == 'deliver':
                    m['deliver_eaten_by_target' if i == tgt else 'deliver_eaten_by_other_ours' if g.team.get(i) == ours
                      else 'deliver_eaten_by_enemy'] += 1
                    if i == tgt: m['deliver_eat_delay'] += rnd - r0
                drop_eaters.append((kind, i))
        if g.team.get(i) != ours or i not in g.bodies: continue
        body = g.bodies[i]
        head = body[0]
        ind = g.ind.get(i, '')
        role, _, mode = ind.partition(':')
        prev = hist[i][-1] if hist[i] else None
        hist[i].append((rnd, head, len(ate), ind, len(body)))
        m['turns'] += 1
        # --- pockets: entries, follow-ins
        now = set(st.pocket_of.get(head, ()))
        before = in_pocket.get(i, set())
        if prev is not None:
            for k in now - before:
                if i in births and births[i][0] == rnd: continue
                occ = {j: r for j, r in pocket_occupants[k].items() if j != i and j in g.bodies}
                m['pocket_entries'] += 1
                if occ:
                    m['follow_ins'] += 1
                    follow_events.append((rnd, i, k, dict(occ)))
                pocket_occupants[k][i] = rnd
        elif now:
            for k in now: pocket_occupants[k][i] = rnd
        for k in before - now:
            pocket_occupants[k].pop(i, None)
        in_pocket[i] = now
        # --- hazard mouths: someone else stepping onto a live mouth in its entry direction
        for r in raise_by_round.get(rnd, ()):
            live_hz[(r[2], r[3])] = (r[0], rnd)
        if prev is not None:
            for (mouth, d), (owner, r0) in list(live_hz.items()):
                if rnd - r0 > 12: del live_hz[(mouth, d)]; continue
                if head == mouth and i != owner and prev[1] == g.step(mouth, DI[(d + 2) % 4]):
                    informed = any(j == i and rr <= rnd for (j, rr) in hz_recv.get((mouth, d), ()))
                    hz_violations.append((rnd, i, owner, mouth, informed, len(body)))
        # --- chambers
        c = st.chamber_of.get(head)
        if i in chamber_visit and chamber_visit[i][0] != c:
            c0, r0, a = chamber_visit.pop(i); visits.append((i, c0, r0, rnd, a, 'left'))
        if c is not None:
            if i not in chamber_visit:
                chamber_visit[i] = [c, rnd, 0]
                born_here = i in births and births[i][0] >= rnd - 1
                if prev is not None and not born_here:
                    m['chamber_entries'] += 1
                    inside = [j for j, b in g.bodies.items() if j != i and g.team[j] == ours and
                              any(st.chamber_of.get(q) == c for q in b)]
                    if inside: m['chamber_entries_occupied'] += 1
                    # a settled occupant: its head has been in this chamber for 8+ rounds
                    if any(j in chamber_visit and chamber_visit[j][0] == c and rnd - chamber_visit[j][1] >= 8 for j in inside):
                        m['chamber_entries_settled'] += 1
                    if st.chambers[c]['barren']: m['barren_entries'] += 1
            chamber_visit[i][2] += len(ate)
            others = sum(1 for j, b in g.bodies.items() if j != i and g.team[j] == ours and st.chamber_of.get(b[0]) == c)
            m['chamber_turns'] += 1
            if others: m['chamber_crowded_turns'] += 1
            if st.chambers[c]['barren']: m['barren_chamber_turns'] += 1
        # --- portal-tile loitering (outside small chambers)
        if st.portal_tile[head] and c is None and not ate:
            m['portal_tile_turns'] += 1
        # --- dispersion: friendly bodies beside a non-alpha head; single file
        if role == 'Neutral' and mode not in ('feed', 'ambush', 'portal'):
            m['disp_turns'] += 1
            gocc = g.occ
            def friend_seg(q):
                j = gocc.get(q)
                return j is not None and j != i and g.team.get(j) == ours and g.bodies[j][0] != q
            m['disp_adjacent'] += sum(1 for d in DI if friend_seg(g.step(head, d)))
            if prev is not None and len(body) > 1:
                d = next((dd for dd in DI if g.step(body[1], dd) == head), None)
                if d is not None:
                    a1 = g.step(head, d); a2 = g.step(a1, d)
                    if (friend_seg(a1) or friend_seg(a2)) and prev[1] == body[1] and len(hist[i]) >= 3:
                        # moved straight last turn too
                        pp = hist[i][-3][1]
                        if g.step(pp, d) == prev[1]: m['single_file_turns'] += 1

    # --- after the walk: loops (period and loose), per dragon
    for i, hh in hist.items():
        n = len(hh)
        for t in range(n):
            mode = hh[t][3].partition(':')[2]
            if mode in LOOP_MODES: continue
            if t >= 15:
                w = [h[1] for h in hh[t - 15:t + 1]]
                ate16 = sum(h[2] for h in hh[t - 15:t + 1])
                rounds_ok = hh[t][0] - hh[t - 15][0] == 15
                if rounds_ok and not ate16:
                    if len(set(w)) <= 7: m['loop_loose_turns'] += 1
                    for per in range(2, 9):
                        if all(w[-1 - k] == w[-1 - k - per] for k in range(per)):
                            m['loop_periodic_turns'] += 1; break
    # --- hazard outcomes: did the owner walk back out past the mouth (not trapped)?
    for (owner, r0, mouth, d, at, L) in hz_raise:
        outside = g.step(mouth, DI[(d + 2) % 4])
        hh = [h for h in hist.get(owner, ()) if h[0] > r0]
        walked = any(h[1] == outside for h in hh[:60])
        struct = (outside, mouth) in st.entry
        tree = struct and st.pockets[st.entry[(outside, mouth)]]['tree']
        hz_outcome.append((owner, r0, walked, struct, tree))
    # dedupe raises by (owner, mouth)
    seen = {}
    for o in hz_outcome:
        seen.setdefault((o[0], o[1] // 1000), o)
    uniq = {}
    for (owner, r0, walked, struct, tree) in hz_outcome:
        key = (owner,)
        uniq.setdefault((owner, r0), (walked, struct, tree))
    episodes = collections.defaultdict(list)
    for (owner, r0, mouth, d, at, L) in hz_raise:
        episodes[(owner, mouth, d)].append(r0)
    m['hazard_raises'] = len(hz_raise)
    m['hazard_episodes'] = len(episodes)
    ep_rows = []
    for (owner, mouth, d), rs in episodes.items():
        outside = g.step(mouth, DI[(d + 2) % 4])
        r0 = min(rs)
        walked = any(h[1] == outside for h in hist.get(owner, ()) if r0 < h[0] <= r0 + 60)
        struct = (outside, mouth) in st.entry
        ep_rows.append((walked, struct))
        m['hazard_ep_walked_out'] += walked
        m['hazard_ep_structural'] += struct
        m['hazard_ep_recv'] += len({j for (j, rr) in hz_recv.get((mouth, d), ())})
    m['hazard_violations'] = len(hz_violations)
    m['hazard_violations_informed'] = sum(1 for v in hz_violations if v[4])
    # follow-ins that trapped the occupant: occupant died in the pocket within 40 rounds
    for (rnd, i, k, occ) in follow_events:
        pass
    m['died_in_pocket'] = sum(died_in_pocket.values())
    # --- chamber visits
    for (i, c, r0, r1, a, how) in visits + [(i, v[0], v[1], 500, v[2], 'end') for i, v in chamber_visit.items()]:
        b = st.chambers[c]['barren']
        m['barren_visits' if b else 'chamber_visits'] += 1
        m['barren_visit_rounds' if b else 'chamber_visit_rounds'] += r1 - r0
        if how == 'died': m['barren_visit_deaths' if b else 'chamber_visit_deaths'] += 1
        if not b and a == 0 and r1 - r0 >= 20: m['chamber_idle_visits'] += 1
    # --- feeding
    m['feedholds'] = len(diag['feedhold'])
    m['feedhold_dragons'] = len({p[0] for p in diag['feedhold']})
    m['feedhold_nomove'] = sum(1 for p in diag['feedhold'] if p[-1] == '-1')
    for (kind, who), n in drop_fate.items(): m['drop_%s_%s' % (kind, who)] += n
    m['deliver_pearls'] = sum(1 for p in drops.values() if p[1] == 'deliver') + drop_fate[('deliver', 'ours')] + drop_fate[('deliver', 'enemy')]
    m['drop_feed_total'] = sum(1 for p in drops.values() if p[1] == 'feed') + drop_fate[('feed', 'ours')] + drop_fate[('feed', 'enemy')]
    m['drop_death_total'] = sum(1 for p in drops.values() if p[1] == 'death') + drop_fate[('death', 'ours')] + drop_fate[('death', 'enemy')]
    fin = [(len(b), j) for j, b in g.bodies.items() if g.team[j] == ours]
    apex = max(fin)[1] if fin else None
    m['drop_feed_by_final_apex'] = sum(1 for k, j in drop_eaters if k == 'feed' and j == apex)
    m['final_longest'] = max(fin)[0] if fin else 0
    if apex is not None:
        ah = g.bodies[apex][0]
        m['end_pile_round_apex'] = sum(1 for p in g.pearls if max(min(abs(p[0] - ah[0]), W - abs(p[0] - ah[0])),
                                                                 min(abs(p[1] - ah[1]), H - abs(p[1] - ah[1]))) <= 3)
    m['pile_at_feed_sum'] = sum(p for p, _ in pile_at_feed)
    m['pile_at_feed_n'] = len(pile_at_feed)
    m['pile_at_feed_big'] = sum(1 for p, L in pile_at_feed if p >= 5)
    # --- alpha memory episodes: consecutive rounds on the same target
    eps = []
    cur = {}
    for p in diag['amem']:
        try: i, r, t = int(p[0]), int(p[1]), xy(p[2])
        except (ValueError, IndexError): continue
        e = cur.get(i)
        if e and e[1] == t and r - e[2] <= 12: e[2] = r
        else:
            if e: eps.append(tuple(e) + (i,))
            cur[i] = [e and e[0] or r, t, r]
            cur[i][0] = r
    eps += [tuple(e) + (i,) for i, e in cur.items()]
    m['amem_turns'] = len(diag['amem'])
    m['amem_episodes'] = len(eps)
    for (r0, t, r1, i) in eps:
        hh = hist.get(i, ())
        got = any(h[1] == t and h[2] for h in hh if r0 <= h[0] <= r1 + 15)
        other = eaten_at.get(t)
        other = other is not None and any(r0 <= r <= r1 + 15 and j != i for (r, j) in other)
        m['amem_ate' if got else 'amem_taken_by_other' if other else 'amem_abandoned'] += 1
        m['amem_turns_ate' if got else 'amem_turns_other' if other else 'amem_turns_abandoned'] += 0
    # alpha pearls eaten (indicator Alpha:) per 100 alpha turns
    for i, hh in hist.items():
        for h in hh:
            if h[3].startswith('Alpha:'):
                m['alpha_turns'] += 1; m['alpha_ate'] += h[2]
    # --- cycles
    m['cycle1'] = len(diag['cycle1']); m['cycle2'] = len(diag['cycle2'])
    # --- reservations
    m['resvrecv'] = len(diag['resvrecv'])
    # --- result
    res = g.rp.result
    m['win'] = 1 if res.get('winner') == ours else 0
    m['games'] = 1
    return m


def _one(path):
    try:
        return dict(analyze(path, path[:-7].rsplit('_', 1)[1]))
    except Exception as ex:
        print('ERR', path, ex, file=sys.stderr)
        return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    cols = []
    for d in args:
        tot = collections.Counter()
        cache_f = os.path.join(d, 'audit.json')
        per = json.load(open(cache_f)) if os.path.exists(cache_f) and '--fresh' not in sys.argv else {}
        todo = [os.path.join(d, f) for f in sorted(os.listdir(d)) if f.endswith('.replay') and f not in per]
        if todo:
            import multiprocessing
            with multiprocessing.Pool(int(os.environ.get('AUDIT_J', '4'))) as pool:
                for f, r in zip(todo, pool.map(_one, todo)):
                    if r is not None: per[os.path.basename(f)] = r
        for f, v in per.items(): tot.update(v)
        json.dump(per, open(cache_f, 'w'))
        cols.append((d, tot, per))
    if '--by-map' in sys.argv:
        keys = [k for k in sys.argv if k.startswith('--k=')]
        for kk in keys:
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
    print('%-28s' % 'metric' + ''.join('%18s' % os.path.basename(d.rstrip('/'))[-17:] for d, _, _ in cols))
    for k in keys:
        print('%-28s' % k + ''.join('%18s' % t.get(k, 0) for _, t, _ in cols))


if __name__ == '__main__':
    main()
