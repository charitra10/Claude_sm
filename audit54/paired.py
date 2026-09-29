#!/usr/bin/env python3
"""paired.py A.jsonl B.jsonl [--opp NAME]: two challengers' seedbench results on the same games (map, seed, side,
opponent): score of each, the games only one of them won, and a sign test on those."""
import collections, json, math, sys
a, b = sys.argv[1], sys.argv[2]
opp = next((x.split('=')[1] for x in sys.argv[3:] if x.startswith('--opp=')), None)
val = {'W': 1, 'D': 0.5}
def load(p):
    out = {}
    for l in open(p):
        r = json.loads(l)
        if opp and r['opponent'] != opp: continue
        out[(r['opponent'], r['map'], r['seed'], r['side'])] = val.get(r['outcome'], 0)
    return out
A, B = load(a), load(b)
keys = sorted(set(A) & set(B))
sa = sum(A[k] for k in keys); sb = sum(B[k] for k in keys)
onlya = sum(1 for k in keys if A[k] > B[k]); onlyb = sum(1 for k in keys if B[k] > A[k])
z = (onlya - onlyb) / math.sqrt(onlya + onlyb) if onlya + onlyb else 0
print(f'{len(keys)} paired games: A {sa:g} ({100 * sa / len(keys):.1f}%)  B {sb:g} ({100 * sb / len(keys):.1f}%)  '
      f'A better in {onlya}, B better in {onlyb}, sign test z = {z:+.2f}')
per = collections.defaultdict(lambda: [0, 0, 0])
for k in keys:
    per[k[1]][0] += A[k]; per[k[1]][1] += B[k]; per[k[1]][2] += 1
print('  ' + '  '.join(f'{m} {x[0]:g}-{x[1]:g}' for m, x in sorted(per.items())))
