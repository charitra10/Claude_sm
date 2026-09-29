#!/usr/bin/env python3
"""events.py KIND DIR: list metrics.py events of KIND (strad, xlong) with the dragon's own trace line for that round (mode,
split child, ...) and a count by mode."""
import collections, glob, gzip, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import metrics
kind, d = sys.argv[1], sys.argv[2]
modes = collections.Counter()
for p in sorted(glob.glob(os.path.join(d, "*.replay"))):
    side = os.path.basename(p)[:-7].rsplit("_", 1)[1]
    metrics.EVENTS.clear()
    metrics.analyse(p, side)
    evs = [e for e in metrics.EVENTS if e[0] == kind]
    if not evs: continue
    want = {(e[3], e[2]) for e in evs}
    lines = {}
    with gzip.open(p[:-7] + ".diag.gz", "rt") as f:
        for line in f:
            q = line.split()
            if len(q) > 4 and q[0] == "turn":
                try: key = (int(q[1]), int(q[2]))
                except ValueError: continue
                if key in want: lines[key] = " ".join(q[3:])
    for e in evs:
        t = lines.get((e[3], e[2]), "?")
        modes[t.split()[1] if t != "?" else "?"] += 1
        print(os.path.basename(p), "r", e[2], "id", e[3], e[4], "|", t)
print(modes)
