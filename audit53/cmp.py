#!/usr/bin/env python3
"""cmp.py KEYS DIR [DIR...] [--per-map]: metrics.py numbers side by side. KEYS: comma-separated metric names (xing,clump,...)."""
import collections, glob, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import metrics
keys = sys.argv[1].split(","); per_map = "--per-map" in sys.argv
dirs = [a for a in sys.argv[2:] if not a.startswith("--")]
rows = collections.defaultdict(dict)
for d in dirs:
    by = collections.defaultdict(list)
    for p in sorted(glob.glob(os.path.join(d, "*.replay"))):
        mp, seed, side = os.path.basename(p)[:-7].rsplit("_", 2)
        x = metrics.analyse(p, side); by["ALL"].append(x)
        if per_map: by[mp].append(x)
    for mp, ms in by.items():
        line = metrics.summarise("x", ms)
        kv = dict(t.split("=", 1) for t in line.split() if "=" in t)
        rows[mp][d] = " ".join(f"{k}={kv.get(k, '?').strip()}" for k in keys)
names = [os.path.basename(d.rstrip("/")) for d in dirs]
for mp in sorted(rows, key=lambda m: (m != "ALL", m)):
    print(f"{mp:28}", " | ".join(f"{n}: {rows[mp].get(d, '')}" for n, d in zip(names, dirs)))
