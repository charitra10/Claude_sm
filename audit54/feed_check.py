#!/usr/bin/env python3
"""feed_check.py DIR...: F_FEED_GATE / F_GATE_REACH / F_TRUE_LEN audit. Every feed suicide of the team under test (a dragon
that reversed into its own neck with a ':feed' indicator): the pearls its body dropped, and who ate them within 40 rounds
(the longest teammate near the drop = 'apex', another teammate, the enemy, or nobody). Also the length of that apex when
the feeder died: a drop beside a 2-3 long "apex" is what F_TRUE_LEN was built to stop.

Also the unit that ends the game longest: how much of its length came from feed drops."""
import collections, glob, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: F401
from sim import Game


def cheb(a, b, W, H):
    dx = abs(a[0] - b[0]); dy = abs(a[1] - b[1])
    return max(min(dx, W - dx), min(dy, H - dy))


def run(path, side):
    g = Game(path)
    track = {}
    feeds = []
    seen_dead = set()
    last_round = 0
    for t, r, d in g.run():
        last_round = r
        heads = {b[0]: i for i, b in g.bodies.items()}
        for tile in list(track):
            k = track[tile]
            if tile not in g.pearls:
                eater = heads.get(tile)
                if eater is None: who = '?'
                elif g.team.get(eater) != side: who = 'enemy'
                elif eater == feeds[k]['apex']: who = 'apex'
                else:
                    who = 'other ours'
                    ind = g.ind.get(eater, '')
                    OTHER[(ind.split(':')[-1] if ind else '?', 'alpha' if ind.startswith('Alpha') else 'non-alpha',
                           'len<=3' if len(g.bodies[eater]) <= 3 else 'len4-7' if len(g.bodies[eater]) < 8 else 'len8+')] += 1
                feeds[k]['eaten'][who] += 1
                del track[tile]
            elif r - feeds[k]['round'] > 40:
                feeds[k]['eaten']['left'] += 1
                del track[tile]
        for i, info in g.dead.items():
            if i in seen_dead: continue
            seen_dead.add(i)
            rd, cause, body, ind = info
            if g.team.get(i) != side or cause != 'hit self' or not ind.endswith(':feed'): continue
            drop = [p for j, p in enumerate(body) if j % 2 == 0 and p in g.pearls]
            near = [(len(b), j) for j, b in g.bodies.items() if g.team.get(j) == side and cheb(b[0], body[0], g.W, g.H) <= 4]
            apex = max(near)[1] if near else None
            alen = max(near)[0] if near else 0
            k = len(feeds)
            feeds.append(dict(round=rd, len=len(body), apex=apex, alen=alen, drop=len(drop), eaten=collections.Counter()))
            for p in drop: track[p] = k
    for tile, k in track.items(): feeds[k]['eaten']['left'] += 1
    return feeds


OTHER = collections.Counter()
tot = collections.Counter(); alen_hist = collections.Counter(); n_games = 0
for d in sys.argv[1:]:
    for f in sorted(glob.glob(os.path.join(d, '*.replay'))):
        side = os.path.basename(f)[-8]
        feeds = run(f, side)
        n_games += 1
        c = collections.Counter()
        for x in feeds:
            c['feeds'] += 1; c['dropped'] += x['drop']; c.update(x['eaten'])
            alen_hist['apex<=3' if x['alen'] <= 3 else 'apex 4-7' if x['alen'] < 8 else 'apex 8+'] += 1
            if x['apex'] is None: alen_hist['no teammate within 4'] += 1
        print(f'{os.path.basename(f):36}', dict(c))
        tot.update(c)
print('TOTAL', dict(tot))
if tot['dropped']:
    print('share of dropped pearls eaten by the apex %.0f%%, other ours %.0f%%, enemy %.0f%%, left %.0f%%' % tuple(
        100 * tot[k] / tot['dropped'] for k in ('apex', 'other ours', 'enemy', 'left')))
print('apex length at the drop:', dict(alen_hist))
print('who else ate the drops (mode, role, length):')
for k, v in OTHER.most_common(15): print('  ', v, k)
