#!/usr/bin/env python3
"""exchanges.py FILE...: every head-on collision (both dragons die): who moved into whom, the lengths lost, and which side
ate the pearls the two bodies dropped (followed for 40 rounds). Team A is assumed to be ours (check with killers.py).

Lengths are taken at the start of the turn (a sprint's own cost counts as lost); drops are the pearls that appeared during
that turn. Per exchange: net = (their length lost - our length lost) + (drop pearls we ate - drop pearls they ate), i.e. how
the length gap between the teams moved in our favour."""
import sys, collections
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from sim import Game

def run(path):
    g = Game(path)
    seen_dead = set(g.dead)
    track = {}        # drop tile -> exchange index
    ex = []
    lens = {i: len(b) for i, b in g.bodies.items()}
    prev_pearls = set(g.pearls)
    for t, r, d in g.run():
        heads = {b[0]: i for i, b in g.bodies.items()}
        for tile in list(track):
            k = track[tile]
            if tile not in g.pearls:
                eater = heads.get(tile)
                ex[k]['eaten'][g.team[eater] if eater is not None else '?'] += 1
                del track[tile]
            elif r - ex[k]['round'] > 40:
                ex[k]['eaten']['left'] += 1
                del track[tile]
        new = [i for i in g.dead if i not in seen_dead]
        seen_dead.update(new)
        h2h = [i for i in new if g.dead[i][1] == 'head to head']
        if len(h2h) >= 2:
            teams = [g.team[i] for i in h2h]
            e = {'round': r, 'mover': g.team.get(d), 'mover_id': d, 'ids': h2h, 'eaten': collections.Counter(),
                 'lenA': sum(lens.get(i, 0) for i in h2h if g.team[i] == 'A'),
                 'lenB': sum(lens.get(i, 0) for i in h2h if g.team[i] == 'B'),
                 'kind': ''.join(sorted(teams)), 'ind': g.dead[d][3] if d in g.dead else ''}
            ex.append(e)
            for tile in g.pearls - prev_pearls: track[tile] = len(ex) - 1
        prev_pearls = set(g.pearls)
        lens = {i: len(b) for i, b in g.bodies.items()}
    return g, ex

def summary(ex):
    per = collections.defaultdict(collections.Counter)
    for e in ex:
        key = ('friendly ' + e['kind']) if e['kind'] != 'AB' else ('we moved' if e['mover'] == 'A' else 'they moved')
        net = (e['lenB'] - e['lenA']) + (e['eaten']['A'] - e['eaten']['B']) if e['kind'] == 'AB' else -e['lenA']
        c = per[key]
        c['n'] += 1; c['lenA'] += e['lenA']; c['lenB'] += e['lenB']
        c['ateA'] += e['eaten']['A']; c['ateB'] += e['eaten']['B']; c['left'] += e['eaten']['left'] + e['eaten']['?']
        c['net'] += net; c['pos'] += net > 0; c['neg'] += net < 0
    return per

def show(label, per):
    print(label)
    for key in ('we moved', 'they moved', 'friendly AA'):
        c = per.get(key)
        if not c: continue
        print(f"   {key:11} n={c['n']:3d} our len lost {c['lenA']:4d} their len lost {c['lenB']:4d} drop eaten us/them/left "
              f"{c['ateA']}/{c['ateB']}/{c['left']}  net {c['net']:+d}  (+{c['pos']} / -{c['neg']})")

def main():
    tot = collections.defaultdict(collections.Counter)
    quiet = '-q' in sys.argv
    for path in sys.argv[1:]:
        if path.startswith('-'): continue
        g, ex = run(path)
        per = summary(ex)
        if not quiet: show(f"{g.name} {path.split('/')[-1]}: winner {g.rp.result.get('winner')}", per)
        for k, c in per.items(): tot[k].update(c)
    show('TOTAL', tot)

if __name__ == '__main__':
    main()
