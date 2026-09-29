#!/usr/bin/env python3
"""trap_check.py DIR...: F_PORTAL_TRAP audit over portals traces (needs the xcross / deadcell / born DIAG lines).
Dead-cell entries split by: did the dragon know the portal was barred, did it have another legal move; walks into a
portal the team had already barred (by the dragon's age); and what newborns knew on their first turn."""
import collections, glob, os, re, sys
for d in sys.argv[1:]:
    c = collections.Counter()
    for f in glob.glob(os.path.join(d, 'portals*.log')):
        lines = open(f).read().split('\n')
        first = {}; born = {}
        for l in lines:
            m = re.match(r'DIAG (deadcell|deadportal) (\d+) (\d+) (\d+)', l)
            if m: first[int(m.group(4))] = min(first.get(int(m.group(4)), 999), int(m.group(3)))
            m = re.match(r'DIAG born (\d+) (\d+) (\d+) knows (\d+) msgs (\d+)', l)
            if m: born[m.group(1)] = (int(m.group(2)), int(m.group(4)))
        for l in lines:
            m = re.match(r'DIAG deadcell (\d+) ', l)
            if m: c['dead-cell entries'] += 1
            m = re.match(r'DIAG xcross (\d+) (\d+) pid (\d+) knew (\d) legal (-?\d+) ', l)
            if m:
                i, r, pid, knew, legal = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4), int(m.group(5))
                if pid in first and first[pid] < r:
                    c['crossed a portal the team had barred' + (' (knew, no other move)' if knew == '1' and legal <= 0 else
                      ' (knew, had other moves)' if knew == '1' else ' (did not know)')] += 1
        for i, (r, k) in born.items():
            if r > 0 and any(v < r for v in first.values()):
                c['newborns while the team knew a barred portal'] += 1
                c['... of them knowing none'] += k == 0
    print(os.path.basename(d.rstrip('/')), dict(sorted(c.items())))
