#!/usr/bin/env python3
"""winrates.py FILE.jsonl [--per-map]: seedbench results per opponent (a draw counts half), with one standard error."""
import collections, json, math, sys
recs = [json.loads(l) for l in open(sys.argv[1])]
by = collections.defaultdict(list)
for r in recs:
    if r["outcome"] == "E": continue
    s = {"W": 1.0, "D": 0.5, "L": 0.0}[r["outcome"]]
    by[(r["opponent"], "ALL")].append(s)
    by[(r["opponent"], r["map"])].append(s)
for (opp, mp), v in sorted(by.items(), key=lambda kv: (kv[0][0], kv[0][1] != "ALL", kv[0][1])):
    if mp != "ALL" and "--per-map" not in sys.argv: continue
    n = len(v); p = sum(v) / n; se = math.sqrt(p * (1 - p) / n) if n else 0
    print(f"{recs[0]['challenger']} vs {opp:10} {mp:30} {sum(v):6.1f}/{n:<4} {100 * p:5.1f}% (±{100 * se:.1f})")
