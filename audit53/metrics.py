#!/usr/bin/env python3
"""metrics.py DIR [DIR...]: behaviour metrics for the v5.3 modules, measured on replays (not on the bot's own beliefs).

Every replay in DIR is named <map>_<seed>_<side>.replay, side being the team of the bot under test. Per directory it prints
one summary row; --per-map adds a row per map. The metrics:

  clump      share of our non-alpha, non-feeding dragon-turns (rounds < 300) with another such head within 2 tiles (F_REPEL)
  cover      distinct tiles our heads visited before round 300, as a share of the map (F_REPEL)
  loop       share of our non-alpha dragon-turns that end a 16-turn window with <= 7 distinct head tiles, no growth, no
             feed / reside / alpha endgame in it (the F_CYCLE2 trigger); loopep: mean length of such runs (F_CYCLE2)
  xing       portal crossings by our heads per game; xlong: crossings by a dragon of length >= 8 (F_LONG_PORTAL, F_PORTAL_FIX)
  xdie       share of our crossings that die within 3 rounds of coming out
  strad      splits (child > 2) with the parent's body across a portal edge; strad2: the same with a 2-long child (F_STRADDLE)
  ho         alpha splits with a rear child >= 4: share where the child is Alpha 3 turns later and the parent is not
             (F_HANDOVER); ho_both / ho_none: both still alpha / neither
  ns4        splits by a non-alpha parent with a child >= 4; ns4a: share where that child is Alpha 3 rounds later
  noalpha    share of rounds 20..feed (380) with no dragon of ours labelled Alpha
  win        share of games won
"""
import collections, glob, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "replay_tools"))
from sim import Game, DI

FEED = 380
EVENTS = []  # (kind, replay, round, id, detail) for the straddle / long-crossing listings


def tdist(a, b, W, H):
    dx = abs(a[0] - b[0]); dy = abs(a[1] - b[1])
    return min(dx, W - dx) + min(dy, H - dy)


def mode_of(ind):
    return ind.split(":", 1)[1] if ":" in ind else ind


def analyse(path, team):
    g = Game(path)
    W, H = g.W, g.H
    m = collections.Counter()
    hist = collections.defaultdict(list)       # id -> [(round, head, len, ind)]
    loop_run = collections.Counter()
    loop_runs = []
    crossings = []                              # (id, round, len)
    pending_ho = []                             # (parent, child, round, childlen)
    turns_seen = collections.Counter()
    prev_ind = {}                               # indicator each dragon ended its last turn with
    visited = set()
    alpha_rounds = collections.defaultdict(set)
    last_round = 0
    rnd = 0; turn = -1; cur = None; pending = None

    def end_turn(i):
        b = g.bodies.get(i)
        if b is None or g.team.get(i) != team:
            return
        ind = g.ind.get(i, "")
        prev_ind[i] = ind
        turns_seen[i] += 1
        is_alpha = ind.startswith("Alpha")
        md = mode_of(ind)
        if is_alpha:
            alpha_rounds[rnd].add(i)
        h = b[0]
        hist[i].append((rnd, h, len(b), ind))
        if rnd < 300:
            visited.add(h)
        if not is_alpha:
            m["na_turns"] += 1
            if rnd < 300 and md != "feed":
                m["clump_n"] += 1
                for j, bj in g.bodies.items():
                    if j == i or g.team.get(j) != team: continue
                    ij = g.ind.get(j, "")
                    if ij.startswith("Alpha") or mode_of(ij) == "feed": continue
                    if tdist(h, bj[0], W, H) <= 2:
                        m["clump"] += 1
                        break
            w = hist[i][-16:]
            inloop = (len(w) == 16 and len({x[1] for x in w}) <= 7 and w[-1][2] <= w[0][2] and
                      not any(mode_of(x[3]) in ("feed", "reside") for x in w))
            if inloop:
                m["loop"] += 1
                loop_run[i] += 1
            elif loop_run[i]:
                loop_runs.append(loop_run[i]); loop_run[i] = 0

    for e in g.rp.events:
        k = e[0]
        if k == 'round':
            rnd = e[1]; last_round = rnd
        elif k == 'turn':
            if cur is not None: end_turn(cur)
            turn += 1; cur = e[1]
            # handovers waiting for their parties' later turns
            for ho in list(pending_ho):
                par, ch, r0, cl, was = ho
                if rnd - r0 >= 3:
                    pa = g.ind.get(par, "").startswith("Alpha") and par in g.bodies
                    ca = g.ind.get(ch, "").startswith("Alpha") and ch in g.bodies
                    if not was:
                        if ch not in g.bodies:
                            pending_ho.remove(ho)
                            continue
                        m["nsplit4"] += 1
                        if ca: m["nsplit4_alpha"] += 1
                        pending_ho.remove(ho)
                        continue
                    if ch not in g.bodies:  # the child died: no verdict on the handover
                        m["ho_dead"] += 1
                        pending_ho.remove(ho)
                        continue
                    m["ho"] += 1
                    if ca and not pa: m["ho_ok"] += 1
                    elif ca and pa: m["ho_both"] += 1
                    elif not ca and not pa:
                        m["ho_none"] += 1
                        EVENTS.append(("honone", path, r0, ch, (par, cl, g.ind.get(ch, ""))))
                    else: m["ho_parent"] += 1
                    pending_ho.remove(ho)
        elif k == 'tile':
            if e[3]: g.pearls.add((e[1], e[2]))
            else: g.pearls.discard((e[1], e[2]))
        elif k == 'ind': g.ind[e[1]] = e[2]
        elif k == 'action': pending = e
        elif k == 'update':
            _, i, facing, hx, hy, tx, ty = e
            body = g.bodies.get(i)
            if body is None: continue
            steps = pending[3] if pending and pending[1] == i and pending[2] == 'move' else facing
            h = body[0]
            for d in steps:
                if g.edge(h[0], h[1], d)[0] == 2 and g.team.get(i) == team:
                    crossings.append((i, rnd, len(body)))
                    if len(body) >= 8: EVENTS.append(("xlong", path, rnd, i, len(body)))
                n = g.dest(h, d)
                if n is None: break
                body.insert(0, n); h = n
            if body[0] != (hx, hy): body.insert(0, (hx, hy))
            if (tx, ty) in body:
                j = len(body) - 1 - body[::-1].index((tx, ty))
                del body[j + 1:]
            else:
                body[:] = [(hx, hy), (tx, ty)]
        elif k == 'split':
            _, par, ch, tm, fc, pb, cb = e
            old = g.bodies.get(par)
            if tm == team and old:
                strad = any(tdist(old[j], old[j + 1], W, H) != 1 for j in range(len(old) - 1))
                if strad:
                    m["strad2" if len(cb) == 2 else "strad"] += 1
                    if len(cb) > 2: EVENTS.append(("strad", path, rnd, par, (len(pb), len(cb))))
                m["splits"] += 1
                if len(cb) >= 4 and rnd > 0:
                    pending_ho.append((par, ch, rnd, len(cb), prev_ind.get(par, "").startswith("Alpha")))
            g.bodies[par] = list(pb); g.bodies[ch] = list(cb); g.team[ch] = tm; g.born[ch] = rnd
        elif k == 'death':
            if e[1] in g.bodies:
                g.dead[e[1]] = (rnd, e[2]); del g.bodies[e[1]]
    if cur is not None: end_turn(cur)
    loop_runs += [v for v in loop_run.values() if v]

    m["games"] = 1
    m["xing"] = len(crossings)
    m["xlong"] = sum(1 for x in crossings if x[2] >= 8)
    m["xdie"] = sum(1 for (i, r, L) in crossings if i in g.dead and g.dead[i][0] - r <= 3)
    m["loop_runs"] = len(loop_runs); m["loop_len"] = sum(loop_runs)
    m["cover_n"] = len(visited); m["area"] = W * H
    m["noalpha"] = sum(1 for r in range(20, min(FEED, last_round + 1)) if not alpha_rounds.get(r))
    m["noalpha_n"] = max(0, min(FEED, last_round + 1) - 20)
    res = g.rp.result if hasattr(g.rp, "result") else None
    m["win"] = 1 if (isinstance(res, dict) and res.get('winner') == team) else 0
    return m


def winner_of(path):
    return None


def summarise(label, ms):
    t = collections.Counter()
    for x in ms: t.update(x)
    f = lambda a, b: (t[a] / t[b]) if t[b] else float("nan")
    return (f"{label:28} n={t['games']:3d} clump={f('clump','clump_n'):.3f} cover={f('cover_n','area'):.2f} "
            f"loop={f('loop','na_turns'):.4f} loopep={f('loop_len','loop_runs'):.1f} xing={f('xing','games'):.1f} "
            f"xlong={f('xlong','games'):.2f} xdie={f('xdie','xing'):.3f} strad={t['strad']} strad2={t['strad2']} "
            f"splits={t['splits']} hodead={t['ho_dead']} ho={t['ho']} ok={f('ho_ok','ho'):.2f} both={f('ho_both','ho'):.2f} "
            f"none={f('ho_none','ho'):.2f} par={f('ho_parent','ho'):.2f} ns4={t['nsplit4']} ns4a={f('nsplit4_alpha','nsplit4'):.2f} "
            f"win={f('win','games'):.2f} noalpha={f('noalpha','noalpha_n'):.3f}")


if __name__ == "__main__":
    per_map = "--per-map" in sys.argv
    for d in [a for a in sys.argv[1:] if not a.startswith("--")]:
        allm = []; bymap = collections.defaultdict(list)
        for p in sorted(glob.glob(os.path.join(d, "*.replay"))):
            base = os.path.basename(p)[:-7]
            mp, seed, side = base.rsplit("_", 2)
            try:
                x = analyse(p, side)
            except Exception as ex:
                print("skip", p, ex, file=sys.stderr); continue
            allm.append(x); bymap[mp].append(x)
        print(summarise(os.path.basename(d.rstrip("/")), allm))
        if per_map:
            for mp in sorted(bymap): print("  " + summarise(mp, bymap[mp]))
