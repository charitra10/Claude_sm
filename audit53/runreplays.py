#!/usr/bin/env python3
"""runreplays.py BOT OPP OUTDIR [--maps a,b] [--seeds N] [--seed-base S] [-j J]

Plays BOT against OPP on every map (both sides, seeded), keeping each replay and BOT's DIAG lines (gzipped), for the
behaviour metrics in metrics.py. help, big_empty, default_small, Colosseum and arena (off the ladder)
are skipped unless named in --maps.
Files: OUTDIR/<map>_<seed>_<side>.replay and .diag.gz, where side is the team BOT played (A or B).
"""
import argparse, concurrent.futures as cf, glob, gzip, os, re, subprocess, sys

ap = argparse.ArgumentParser()
ap.add_argument("bot"); ap.add_argument("opp"); ap.add_argument("out")
ap.add_argument("--maps", default=None); ap.add_argument("--seeds", type=int, default=4)
ap.add_argument("--seed-base", type=int, default=1); ap.add_argument("-j", type=int, default=4)
ap.add_argument("--sides", default="AB")
a = ap.parse_args()
# Off the ladder (help, big_empty, default_small, Colosseum, arena): never benchmarked.
EXCLUDED = ("help", "big_empty", "default_small", "Colosseum", "arena")
os.makedirs(a.out, exist_ok=True)
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
maps = sorted(glob.glob(os.path.join(root, "maps/maps/*.map")))
if a.maps:
    keep = a.maps.split(",")
    maps = [m for m in maps if os.path.basename(m)[:-4] in keep]
else:
    maps = [m for m in maps if os.path.basename(m)[:-4] not in EXCLUDED]
# build each bot once, then give every worker its own copy (parallel runs of one directory race on its build cache)
import queue, shutil, tempfile
for b in (a.bot, a.opp):
    subprocess.run(["unswbc", "run", "--no-replay", "--seed", "0", maps[0], b, b], capture_output=True)
    if not os.path.isfile(os.path.join(b, ".unswbc-build", "bot")): sys.exit("build failed: " + b)
work = tempfile.mkdtemp(prefix="runreplays_")
slots = queue.Queue()
for i in range(a.j):
    d = {}
    for k, b in (("bot", a.bot), ("opp", a.opp)):
        d[k] = os.path.join(work, f"s{i}", k)
        shutil.copytree(b, d[k], symlinks=True)
    slots.put(d)

def one(job):
    m, seed, side = job
    name = f"{os.path.basename(m)[:-4]}_{seed}_{side}"
    rp = os.path.join(a.out, name + ".replay")
    if os.path.exists(rp) and os.path.exists(os.path.join(a.out, name + ".diag.gz")): return name, "cached"
    slot = slots.get()
    try:
        bots = [slot["bot"], slot["opp"]] if side == "A" else [slot["opp"], slot["bot"]]
        res = subprocess.run(["unswbc", "run", "-v", "-o", rp, "--seed", str(seed), m] + bots,
                             capture_output=True, text=True, timeout=900)
    finally:
        slots.put(slot)
    out = res.stdout + res.stderr
    tmp = os.path.join(a.out, name + ".diag.tmp")
    with gzip.open(tmp, "wt") as f:
        for line in out.splitlines():
            if line.startswith("DIAG "): f.write(line[5:] + "\n")
    os.replace(tmp, os.path.join(a.out, name + ".diag.gz"))
    r = re.search(r"(team [AB] wins after \d+ rounds \([^)]*\)|draw after \d+ rounds)", out)
    return name, r.group(1) if r else "?"

jobs = [(m, s, side) for m in maps for s in range(a.seed_base, a.seed_base + a.seeds) for side in a.sides]
jobs.sort(key=lambda j: 0 if any(h in j[0] for h in ("schooltime", "slithery", "stronghold")) else 1)
with cf.ThreadPoolExecutor(a.j) as ex:
    for name, r in ex.map(one, jobs):
        print(name, r, flush=True)
