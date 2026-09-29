#!/usr/bin/env python3
"""loop_check.py DIR...: F_PORTAL_LOOP audit. For every 'loopout' (walked out of a dense chamber of <= 8 tiles), did the
same dragon get back into that chamber (a 'camp' line on the same portal) within LOOP_TTL rounds? If not: died, too long
to loop (length > chamber size + 3), or something else. Also how often another teammate took the chamber meanwhile."""
import collections, glob, os, re, sys

LOOP_TTL = 12
tot = collections.Counter()
for d in sys.argv[1:]:
    for f in sorted(glob.glob(os.path.join(d, '*.log'))):
        outs, camps, deaths, size = [], collections.defaultdict(list), {}, {}
        others = collections.defaultdict(set)
        for l in open(f):
            m = re.match(r'DIAG loopout (\d+) (\d+) (\d+) (\d+)', l)
            if m: outs.append(tuple(map(int, m.groups())))
            m = re.match(r'DIAG camp (\d+) (\d+) (\d+) len (\d+) .* leave (\d) .*size (\d+)', l)
            if m:
                i, r, pid, L, lv, sz = map(int, m.groups())
                camps[(i, pid)].append(r); size[pid] = sz
                others[(pid, r)].add(i)
            m = re.match(r'round (\d+): bot (\d+) \(team [AB]\) died: (.*)', l)
            if m: deaths[int(m.group(2))] = (int(m.group(1)), m.group(3))
        c = collections.Counter()
        for i, r, pid, L in outs:
            back = [x for x in camps[(i, pid)] if r < x <= r + LOOP_TTL + 1]
            if back:
                c['back in'] += 1
                c['rounds to re-enter %d' % (back[0] - r)] += 0
                c['sum_rounds'] += back[0] - r
                continue
            dr = deaths.get(i, (999, ''))[0]
            taken = any(j != i for rr in range(r, r + LOOP_TTL + 1) for j in others.get((pid, rr), ()))
            if dr <= r + LOOP_TTL: c['died (%s)' % deaths[i][1]] += 1
            elif L > size.get(pid, 4) + 3: c['too long to loop'] += 1
            elif taken: c['teammate took the chamber'] += 1
            else: c['not back'] += 1
        c = +c
        n = len(outs)
        print(f'{os.path.basename(f):34} loopouts {n:4d}', {k: v for k, v in c.items() if not k.startswith('rounds')})
        tot.update(c); tot['loopouts'] += n
print('TOTAL', dict(tot))
