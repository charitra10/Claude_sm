#!/usr/bin/env python3
"""
Seeded benchmark: one challenger against one or more opponents, on every map, both sides, several seeds.

Since toolkit 1.1 pearl respawns are random unless `--seed` is given, so a (map, side) pair no longer
replays the same game. This runner passes explicit seeds, so every candidate plays the same set of games
and results stay comparable between runs (seed s on map m is the same pearl sequence for everybody).

Each bot directory is snapshotted (sources only) and built once when the run starts, so you can keep
editing the bot while a benchmark is running. Results are appended as JSON lines to --out.

Usage:
  ./seedbench.py v5.1 v5 v4.14 --seeds 4 -j 10
  ./seedbench.py v5.1 v5 --map help,small --seeds 8
"""

import argparse
import concurrent.futures
import glob
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections import defaultdict

HEAVY = ("help", "big_empty", "schooltime")  # scheduled first so the tail of the run stays parallel


def snapshot(bot_dir, root):
    """Copy a bot's sources into root/<name>-<hash> and build it there once."""
    bot_dir = os.path.abspath(bot_dir)
    files = sorted(f for f in os.listdir(bot_dir)
                   if os.path.isfile(os.path.join(bot_dir, f)) and not f.startswith("."))
    digest = hashlib.sha256()
    for f in files:
        digest.update(f.encode())
        with open(os.path.join(bot_dir, f), "rb") as fh:
            digest.update(fh.read())
    name = os.path.basename(bot_dir.rstrip("/"))
    dest = os.path.join(root, f"{name}-{digest.hexdigest()[:8]}")
    if not os.path.isdir(dest):
        os.makedirs(dest)
        for f in files:
            shutil.copy2(os.path.join(bot_dir, f), dest)
    return name, dest, digest.hexdigest()[:8]


def build(unswbc, bot, some_map):
    res = subprocess.run([unswbc, "run", "--no-replay", "--seed", "0", some_map, bot, bot],
                         capture_output=True, text=True, timeout=600)
    if not os.path.isfile(os.path.join(bot, ".unswbc-build", "bot")):
        sys.exit(f"build failed for {bot}:\n{res.stdout}\n{res.stderr}")


def run_game(unswbc, map_path, a_dir, b_dir, seed, timeout):
    cmd = [unswbc, "run", "--no-replay", "--no-debug", "--seed", str(seed), map_path, a_dir, b_dir]
    t0 = time.time()
    try:
        res = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return {"winner": "TIMEOUT", "method": "timeout", "round": 0, "secs": time.time() - t0}
    out = res.stdout + "\n" + res.stderr
    m = re.search(r"team\s+([AB])\s+wins\s+after\s+(\d+)\s+rounds\s+\(([^)]+)\)", out)
    if m:
        return {"winner": m.group(1), "method": m.group(3).strip(), "round": int(m.group(2)),
                "secs": time.time() - t0}
    m = re.search(r"draw\s+after\s+(\d+)\s+rounds", out, re.IGNORECASE)
    if m:
        return {"winner": "DRAW", "method": "draw", "round": int(m.group(1)), "secs": time.time() - t0}
    tail = (res.stderr.strip() or res.stdout.strip()).splitlines()[-1:] or ["unknown error"]
    return {"winner": "ERROR", "method": tail[0], "round": 0, "secs": time.time() - t0}


def outcome_of(winner, side):
    """The challenger's result: W/L/D, or E for an error or timeout."""
    if winner in ("A", "B"):
        return "W" if winner == side else "L"
    return "D" if winner == "DRAW" else "E"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("challenger")
    ap.add_argument("opponents", nargs="+")
    ap.add_argument("--maps-dir", default="maps/maps")
    ap.add_argument("--map", default=None, help="comma-separated substrings of map names to keep")
    ap.add_argument("--exclude", default=None, help="comma-separated substrings of map names to drop")
    ap.add_argument("--seeds", type=int, default=4, help="number of seeds per (map, side)")
    ap.add_argument("--seed-base", type=int, default=1, help="first seed (seeds are base..base+N-1)")
    ap.add_argument("-j", "--workers", type=int, default=8)
    ap.add_argument("--timeout", type=int, default=400)
    ap.add_argument("--out", default=None, help="JSON-lines file to append results to")
    ap.add_argument("--quiet", action="store_true", help="no per-game lines")
    args = ap.parse_args()

    unswbc = shutil.which("unswbc") or os.path.expanduser("~/.local/bin/unswbc")
    maps = sorted(glob.glob(os.path.join(args.maps_dir, "*.map")))
    if args.map:
        keep = [k.lower() for k in args.map.split(",")]
        maps = [m for m in maps if any(k in os.path.basename(m).lower() for k in keep)]
    if args.exclude:
        drop = [k.lower() for k in args.exclude.split(",")]
        maps = [m for m in maps if not any(k in os.path.basename(m).lower() for k in drop)]
    if not maps:
        sys.exit("no maps")

    root = os.path.join(tempfile.gettempdir(), "seedbench_snap")
    os.makedirs(root, exist_ok=True)
    chal_name, chal_dir, chal_hash = snapshot(args.challenger, root)
    opps = [snapshot(o, root) for o in args.opponents]
    for _, d, _ in [(chal_name, chal_dir, chal_hash)] + opps:
        build(unswbc, d, maps[0])

    work = tempfile.mkdtemp(prefix="seedbench_work_")
    slots = []
    for i in range(args.workers):
        slot = {}
        for name, d, _ in [(chal_name, chal_dir, chal_hash)] + opps:
            target = os.path.join(work, f"s{i}", os.path.basename(d))
            shutil.copytree(d, target)
            slot[d] = target
        slots.append(slot)

    tasks = []
    seeds = range(args.seed_base, args.seed_base + args.seeds)
    for opp_name, opp_dir, _ in opps:
        for m in maps:
            for seed in seeds:
                tasks.append((opp_name, opp_dir, m, seed, "A"))  # challenger plays team A
                tasks.append((opp_name, opp_dir, m, seed, "B"))
    tasks.sort(key=lambda t: not any(h in os.path.basename(t[2]) for h in HEAVY))

    print(f"challenger {chal_name}@{chal_hash} vs {', '.join(f'{n}@{h}' for n, _, h in opps)}; "
          f"{len(maps)} maps x {args.seeds} seeds x 2 sides = {len(tasks)} games, {args.workers} workers")

    free = list(range(args.workers))
    results = []
    out = open(args.out, "a") if args.out else None
    t_start = time.time()

    def job(task, slot_id):
        opp_name, opp_dir, m, seed, side = task
        s = slots[slot_id]
        a, b = (s[chal_dir], s[opp_dir]) if side == "A" else (s[opp_dir], s[chal_dir])
        r = run_game(unswbc, m, a, b, seed, args.timeout)
        return task, slot_id, r

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as ex:
        pending = set()
        it = iter(tasks)
        done_n = 0

        def submit_next():
            try:
                t = next(it)
            except StopIteration:
                return
            pending.add(ex.submit(job, t, free.pop()))

        for _ in range(args.workers):
            submit_next()
        while pending:
            finished, _ = concurrent.futures.wait(pending, return_when=concurrent.futures.FIRST_COMPLETED)
            for fut in finished:
                pending.remove(fut)
                (opp_name, _, m, seed, side), slot_id, r = fut.result()
                free.append(slot_id)
                mapname = os.path.basename(m)[:-4]
                outcome = outcome_of(r["winner"], side)
                rec = {"challenger": chal_name, "chal_hash": chal_hash, "opponent": opp_name, "map": mapname,
                       "seed": seed, "side": side, "outcome": outcome, **r}
                results.append(rec)
                if out:
                    out.write(json.dumps(rec) + "\n")
                    out.flush()
                done_n += 1
                if not args.quiet:
                    print(f"[{done_n:4d}/{len(tasks)}] {opp_name:8} {mapname:30} seed {seed:3d} side {side} "
                          f"-> {outcome} ({r['method']}, r{r['round']}, {r['secs']:.0f}s)", flush=True)
                submit_next()

    # Timeouts are almost always the machine swapping under many long games at once, not a bot hanging.
    # Re-run them one at a time with a longer limit rather than scoring them as losses.
    retry = [r for r in results if r["outcome"] == "E"]
    if retry:
        print(f"\nretrying {len(retry)} errored/timed-out games one at a time")
        opp_dirs = {n: d for n, d, _ in opps}
        for rec in retry:
            chal, opp = slots[0][chal_dir], slots[0][opp_dirs[rec["opponent"]]]
            a, b = (chal, opp) if rec["side"] == "A" else (opp, chal)
            r = run_game(unswbc, os.path.join(args.maps_dir, rec["map"] + ".map"), a, b, rec["seed"], args.timeout * 3)
            rec.update(r, outcome=outcome_of(r["winner"], rec["side"]))
            if out:
                out.write(json.dumps(rec) + "\n")
                out.flush()
            print(f"  retry {rec['opponent']} {rec['map']} seed {rec['seed']} side {rec['side']} -> {rec['outcome']}")

    if out:
        out.close()
    shutil.rmtree(work, ignore_errors=True)

    print(f"\n{chal_name}@{chal_hash}  ({time.time() - t_start:.0f}s)")
    for opp_name, _, h in opps:
        rs = [r for r in results if r["opponent"] == opp_name]
        per = defaultdict(lambda: [0, 0])
        for r in rs:
            per[r["map"]][1] += 1
            per[r["map"]][0] += {"W": 1, "D": 0.5}.get(r["outcome"], 0)
        score = sum(v[0] for v in per.values())
        n = len(rs)
        p = score / n if n else 0
        se = math.sqrt(p * (1 - p) / n) if n else 0
        errs = sum(1 for r in rs if r["outcome"] == "E" or r["winner"] == "TIMEOUT")
        print(f"  vs {opp_name}@{h}: {score:g}/{n} = {100 * p:.1f}% (±{100 * se:.1f})"
              + (f"  errors/timeouts: {errs}" if errs else ""))
        print("    " + "  ".join(f"{k} {v[0]:g}/{v[1]}" for k, v in sorted(per.items())))


if __name__ == "__main__":
    main()
