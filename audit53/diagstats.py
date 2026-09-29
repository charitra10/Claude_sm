#!/usr/bin/env python3
"""diagstats.py DIR [DIR...]: what the v5.3 modules' trigger points did, from the DIAG traces (instrument.py) and replays.

  loop       breakouts started (a53loop) per game; away10: share whose head is >= 5 tiles from where it started 10 rounds
             later; reloop: share that start another breakout within 30 rounds; ate20: share that grow within 20 rounds;
             tgt: what the dragon steered for during its breakouts (DIAG tgt kinds)
  cap        alpha portal captures started (a53cap, distinct dragon), splits (a53capsplit), handovers (a53capho), handovers
             naming nobody (to -1); crossed: capturing dragons whose head crossed a portal within 8 rounds; heir: named heirs
             labelled Alpha 5 rounds later
  sacr       long dragons forced into a portal that split instead (a53sacrifice)
  hochild    4+ long split children that took the alpha role at birth; claimed: of those, also named by the parent (F_MANTLE)
  hz         hazards announced (a53hz, distinct mouth + round); rx: distinct teammates that learnt each (a53hzrecv);
             entries: our other heads stepping onto a live hazard's mouth in its entry direction (live: <= 12 rounds since
             its last announcement)
  pre / rel  F_PORTAL_FIX: a portal preempting remembered / upcoming food; residents released on arrival
"""
import collections, glob, gzip, os, re, sys

D = [(0, -1), (1, 0), (0, 1), (-1, 0)]


def parse(path):
    ev = collections.defaultdict(list)
    turns = collections.defaultdict(dict)  # id -> round -> (len, mode, pos)
    with gzip.open(path, "rt") as f:
        for line in f:
            p = line.split()
            if not p: continue
            k = p[0]
            try:  # lines from dragons writing at the same moment can come out cut or merged: skip those
                if k == "turn":
                    i, r, L, mode = int(p[1]), int(p[2]), int(p[3]), p[4]
                    x, y = map(int, p[5].split(","))
                    turns[i][r] = (L, mode, (x, y))
                elif k.startswith("a53") or k in ("tgt", "claim"):
                    int(p[1]); int(p[2])
                    if k == "tgt": p[3]
                    ev[k].append(p[1:])
            except (IndexError, ValueError):
                continue
    return ev, turns


def mapsize(mapname):
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(root, "maps/maps", mapname + ".map")) as f:
        for line in f:
            if line.startswith("MAP"): return int(line.split()[1]), int(line.split()[2])
    return 64, 64


def tdist(a, b, W, H):
    dx = abs(a[0] - b[0]); dy = abs(a[1] - b[1])
    return min(dx, W - dx) + min(dy, H - dy)


def game_stats(diag, W, H):
    ev, turns = parse(diag)
    m = collections.Counter()
    tgt_by = collections.defaultdict(dict)
    for p in ev["tgt"]:
        tgt_by[int(p[0])][int(p[1])] = p[2]
    # loop breakouts
    starts = collections.defaultdict(list)
    for p in ev["a53loop"]:
        starts[int(p[0])].append(int(p[1]))
    for i, rs in starts.items():
        for j, r in enumerate(rs):
            m["loop"] += 1
            tr = turns.get(i, {})
            if r not in tr: continue
            p0, L0 = tr[r][2], tr[r][0]
            if r + 10 in tr:
                m["loop_t10"] += 1
                if tdist(tr[r + 10][2], p0, W, H) >= 5: m["loop_away"] += 1
            if any(r < r2 <= r + 30 for r2 in rs[j + 1:]): m["reloop"] += 1
            if any(tr.get(r + k, (0,))[0] > L0 for k in range(1, 21)): m["loop_ate"] += 1
            for k in range(1, 11):
                kind = tgt_by[i].get(r + k)
                if kind: m["ltgt_" + kind] += 1
    # alpha portal capture
    capd = {}
    for p in ev["a53cap"]:
        capd.setdefault(int(p[0]), int(p[1]))
    m["cap"] = len(capd)
    m["capsplit"] = len({p[0] for p in ev["a53capsplit"]})
    m["capho"] = len(ev["a53capho"])
    m["capho_none"] = sum(1 for p in ev["a53capho"] if p[5] == "-1")
    heirs = [(int(p[5]), int(p[1])) for p in ev["a53capho"] if p[5] != "-1"]
    m["sacr"] = len(ev["a53sacrifice"])
    # handover children
    claims = {(int(p[0]), int(p[1])) for p in ev["claim"]}
    for p in ev["a53hochild"]:
        if p[-1] == "1":
            m["hochild"] += 1
            if (int(p[0]), int(p[1])) in claims: m["hochild_claimed"] += 1
    # hazards
    hz = collections.defaultdict(list)  # (x, y, dir) -> announce rounds
    def xyd(p):
        try:
            x, y = map(int, p[3].split(","))
            return x, y, int(p[5])
        except (IndexError, ValueError):
            return None
    for p in ev["a53hz"]:
        if xyd(p): hz[xyd(p)].append((int(p[1]), int(p[0])))
    rx = collections.defaultdict(set)
    heard = collections.defaultdict(dict)  # hazard -> dragon -> first round it heard of it
    for p in ev["a53hzrecv"]:
        if xyd(p):
            rx[xyd(p)].add(int(p[0]))
            heard[xyd(p)].setdefault(int(p[0]), int(p[1]))
    m["hz"] = len(hz)
    m["hz_rx"] = sum(len(rx[k]) for k in hz)
    for (x, y, d), ann in hz.items():
        owners = {o for _, o in ann}
        rounds = sorted(r for r, _ in ann)
        for i, tr in turns.items():
            if i in owners: continue
            for r, (L, mode, pos) in tr.items():
                if pos != (x, y) or (r - 1) not in tr: continue
                prev = tr[r - 1][2]
                if ((prev[0] + D[d][0]) % W, (prev[1] + D[d][1]) % H) != (x, y): continue
                if any(0 <= r - a <= 12 for a in rounds):
                    m["hz_entry"] += 1
                    if i in heard.get((x, y, d), {}) and heard[(x, y, d)][i] < r: m["hz_entry_knew"] += 1
                    if L > 2: m["hz_entry_long"] += 1
    m["pre"] = len(ev["a53preempt"])
    m["rel"] = len(ev["a53release"])
    m["games"] = 1
    return m, capd, heirs


def main():
    for d in [a for a in sys.argv[1:] if not a.startswith("--")]:
        t = collections.Counter()
        for diag in sorted(glob.glob(os.path.join(d, "*.diag.gz"))):
            mp = os.path.basename(diag).rsplit("_", 2)[0]
            W, H = mapsize(mp)
            m, _, _ = game_stats(diag, W, H)
            t.update(m)
        g = t["games"] or 1
        f = lambda a, b: t[a] / t[b] if t[b] else float("nan")
        kinds = sorted(((k[5:], v) for k, v in t.items() if k.startswith("ltgt_")), key=lambda kv: -kv[1])
        print(f"{os.path.basename(d.rstrip('/')):12} n={t['games']} loop={t['loop'] / g:.1f}/g away10={f('loop_away', 'loop_t10'):.2f} "
              f"reloop={f('reloop', 'loop'):.2f} ate20={f('loop_ate', 'loop'):.2f} | cap={t['cap']} capsplit={t['capsplit']} "
              f"capho={t['capho']} none={t['capho_none']} sacr={t['sacr']} | hochild={t['hochild']} claimed={t['hochild_claimed']} "
              f"| hz={t['hz']} rx/hz={f('hz_rx', 'hz'):.2f} entries={t['hz_entry']} knew={t['hz_entry_knew']} long={t['hz_entry_long']} | pre={t['pre'] / g:.1f}/g rel={t['rel'] / g:.1f}/g")
        print("   breakout targets:", " ".join(f"{k}:{v}" for k, v in kinds))


if __name__ == "__main__":
    main()
