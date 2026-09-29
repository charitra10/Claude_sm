#!/usr/bin/env python3
"""wr.py FILE [FILE...]: seedbench win rates per file (draws count half), per map, and paired differences between files
on the same (map, seed, side) games."""
import collections, json, math, sys
data = {}
for f in sys.argv[1:]:
    d = {}
    for l in open(f):
        r = json.loads(l)
        d[(r['opponent'], r['map'], r['seed'], r['side'])] = {'W': 1.0, 'D': 0.5}.get(r['outcome'], 0.0)
    data[f] = d
maps = sorted({k[1] for d in data.values() for k in d})
print('%-30s' % 'map' + ''.join('%16s' % f.split('/')[-1][-15:] for f in data))
for m in maps:
    print('%-30s' % m + ''.join('%16s' % ('%.1f/%d' % (sum(v for k, v in d.items() if k[1] == m), sum(1 for k in d if k[1] == m)))
                                  for d in data.values()))
tot = []
for f, d in data.items():
    n = len(d); s = sum(d.values()); p = s / n if n else 0
    tot.append('%.1f/%d (%.1f%% +-%.1f)' % (s, n, 100 * p, 100 * math.sqrt(p * (1 - p) / n) if n else 0))
print('%-30s' % 'all' + ''.join('%16s' % t.split(' ')[0] for t in tot))
for f, t in zip(data, tot): print('  ', f, t)
fs = list(data)
for i in range(len(fs)):
    for j in range(i + 1, len(fs)):
        a, b = data[fs[i]], data[fs[j]]
        common = set(a) & set(b)
        wa = sum(1 for k in common if a[k] > b[k]); wb = sum(1 for k in common if b[k] > a[k])
        z = (wa - wb) / math.sqrt(wa + wb) if wa + wb else 0
        print('paired %s vs %s: %d common, first better %d, second better %d, z=%.2f' % (fs[i].split('/')[-1], fs[j].split('/')[-1], len(common), wa, wb, z))
