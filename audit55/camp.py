#!/usr/bin/env python3
"""camp.py DIR [-v]: F_CAMP2 audit. Every time the last of our dragons leaves a small spawning chamber alive and stays out
for 15+ rounds, with a pearl lying in it or due within 12 rounds (ground truth): what the leaver's own `camp` line said
on its last turn inside (leave flag, crowded / idle / dry), and its mode. Also chamber-rounds held by 2+ of ours."""
import collections, gzip, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyze52 import Static  # noqa: E402
from sim import Game  # noqa: E402

AWAY = 15; SOON = 12
d = sys.argv[1]; verbose = '-v' in sys.argv
MIN = int(next((a[6:] for a in sys.argv if a.startswith('--min=')), '1'))
tot = collections.Counter(); ex = []
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    stem = f[:-7]; ours = stem.rsplit('_', 1)[1]; mp = stem.rsplit('_', 2)[0]
    camp = {}
    turnl = {}
    for l in gzip.open(os.path.join(d, stem + '.diag.gz'), 'rt', errors='replace'):
        p = l.split()
        if not p: continue
        if p[0] == 'camp' and len(p) > 10: camp[(int(p[1]), int(p[2]))] = p
        elif p[0] == 'turn' and len(p) > 5: turnl[(int(p[1]), int(p[2]))] = p
    g = Game(os.path.join(d, f)); st = Static(g)
    cds = []; r = 0
    for e in g.rp.events:
        if e[0] == 'round': r = e[1]
        elif e[0] == 'countdown': cds.append((r, (e[1], e[2]), e[3]))
    due = {}; ci = 0
    heads_at = []   # per round: {chamber: set(ids)}, pearls
    snaps = []
    last_r = -1
    for t, rnd, i in g.run():
        if rnd != last_r:
            while ci < len(cds) and cds[ci][0] <= rnd:
                due[cds[ci][1]] = cds[ci][0] + cds[ci][2]; ci += 1
            inside = collections.defaultdict(set)
            for j, b in g.bodies.items():
                if g.team[j] == ours:
                    c = st.chamber_of.get(b[0])
                    if c is not None and not st.chambers[c]['barren']: inside[c].add(j)
            pay = {}
            for c, ch in enumerate(st.chambers):
                if ch['barren']: continue
                now = sum(1 for q in ch['tiles'] if q in g.pearls)
                soon = sum(1 for q in ch['tiles'] if q not in g.pearls and 0 <= due.get(q, -10) - rnd <= SOON)
                pay[c] = (now, soon)
            snaps.append((rnd, dict(inside), pay, set(g.bodies)))
            last_r = rnd
    for k in range(1, len(snaps)):
        rnd, ins, pay, alive = snaps[k]
        _, pins, _, _ = snaps[k - 1]
        for c, who in pins.items():
            if len(st.chambers[c]['tiles']) < MIN: continue
            if len(who) >= 2: tot['crowded_rounds'] += 1
            if ins.get(c): continue
            if not all(j in alive for j in who): continue
            back = any(snaps[kk][1].get(c) for kk in range(k + 1, min(len(snaps), k + AWAY)))
            back12 = [kk for kk in range(k + 1, min(len(snaps), k + 13)) if who & snaps[kk][1].get(c, set())]
            if len(who) == 1:
                tot['lone_exit'] += 1
                if back12: tot['lone_exit_same_back12'] += 1; tot['lone_exit_back_rounds'] += back12[0] - k
            if back: tot['leave_returned'] += 1; continue
            now, soon = pay[c]
            key = 'lone' if len(who) == 1 else 'all'
            tot[key + '_leave'] += 1
            if now + soon == 0: continue
            tot[key + '_leave_paying'] += 1
            tot['map_' + mp] += 1
            for j in who:
                cl = camp.get((j, rnd - 1)) or camp.get((j, rnd - 2))
                tl = turnl.get((j, rnd - 1))
                mode = tl[4] if tl else '?'
                if cl is None:
                    why = 'nocampline'
                else:
                    flags = dict(zip(cl[5::2], cl[6::2]))
                    if flags.get('leave') == '1':
                        why = 'leave:' + ','.join(x for x in ('crowded', 'idle', 'dry') if flags.get(x) == '1') or 'leave:barren'
                    else:
                        why = 'stay-flag'
                tot['why_' + why] += 1
                tot['mode_' + mode] += 1
                if verbose: ex.append((stem, j, rnd, c, now, soon, why, mode, ' '.join(cl) if cl else ''))
for k in sorted(tot): print('%-34s %d' % (k, tot[k]))
for e in ex[:60]: print(e)
