#!/usr/bin/env python3
"""resume_bench.py CHALLENGER OPP [OPP...] --out FILE [--exclude a,b] [--seeds N] [--seed-base S] [-j J]

seedbench.py with resume: games already in --out (same challenger, opponent, map, seed, side) are skipped, the rest are
played and appended in seedbench's JSON-lines format. For runs cut short (a container restart loses a long benchmark).
"""
import argparse, concurrent.futures as cf, glob, json, os, queue, shutil, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import seedbench as sb

ap = argparse.ArgumentParser()
ap.add_argument("challenger"); ap.add_argument("opponents", nargs="+")
ap.add_argument("--out", required=True); ap.add_argument("--exclude", default="")
ap.add_argument("--seeds", type=int, default=8); ap.add_argument("--seed-base", type=int, default=1)
ap.add_argument("-j", type=int, default=4); ap.add_argument("--timeout", type=int, default=400)
a = ap.parse_args()
unswbc = shutil.which("unswbc")
drop = [k.lower() for k in a.exclude.split(",") if k]
maps = [m for m in sorted(glob.glob("maps/maps/*.map")) if not any(k in os.path.basename(m).lower() for k in drop)]
root = os.path.join(tempfile.gettempdir(), "seedbench_snap"); os.makedirs(root, exist_ok=True)
chal = sb.snapshot(a.challenger, root); opps = [sb.snapshot(o, root) for o in a.opponents]
done = set()
if os.path.exists(a.out):
    for line in open(a.out):
        r = json.loads(line)
        if r["chal_hash"] == chal[2] and r["outcome"] != "E":
            done.add((r["opponent"], r["map"], r["seed"], r["side"]))
tasks = [(o, m, s, side) for o in opps for m in maps for s in range(a.seed_base, a.seed_base + a.seeds) for side in "AB"
         if (o[0], os.path.basename(m)[:-4], s, side) not in done]
print(f"{len(done)} done, {len(tasks)} to play", flush=True)
if not tasks: sys.exit(0)
for _, d, _ in [chal] + opps: sb.build(unswbc, d, maps[0])
work = tempfile.mkdtemp(prefix="resume_bench_"); slots = queue.Queue()
for i in range(a.j):
    slot = {}
    for _, d, _ in [chal] + opps:
        slot[d] = os.path.join(work, f"s{i}", os.path.basename(d)); shutil.copytree(d, slot[d])
    slots.put(slot)

def job(t):
    (on, od, oh), m, seed, side = t
    s = slots.get()
    try:
        x, y = (s[chal[1]], s[od]) if side == "A" else (s[od], s[chal[1]])
        r = sb.run_game(unswbc, m, x, y, seed, a.timeout)
    finally:
        slots.put(s)
    return {"challenger": chal[0], "chal_hash": chal[2], "opponent": on, "map": os.path.basename(m)[:-4], "seed": seed,
            "side": side, "outcome": sb.outcome_of(r["winner"], side), **r}

with open(a.out, "a") as out, cf.ThreadPoolExecutor(a.j) as ex:
    for i, rec in enumerate(ex.map(job, tasks), 1):
        out.write(json.dumps(rec) + "\n"); out.flush()
        print(f"[{i}/{len(tasks)}] {rec['opponent']} {rec['map']} {rec['seed']} {rec['side']} -> {rec['outcome']}", flush=True)
