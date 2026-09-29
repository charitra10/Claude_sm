#!/usr/bin/env python3
"""
Generate geometric variants of UNSW Battlecode maps for benchmarking.

The engine is deterministic, so every (map, side) pair always plays out the same
game. Flipping or transposing a map keeps its structure but changes which
directions/IDs win tie-breaks, giving genuinely different games to test on.

Usage: ./make_map_variants.py [--src maps] [--out maps_variants] [--transforms id,fx,fy,t,fx+fy,t+fx,t+fy,t+fx+fy]
"""

import argparse
import glob
import os


def parse(path):
    m = {"tiles": [], "edges": {}, "dragons": [], "symmetry": None, "name": ""}
    with open(path) as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            key = parts[0]
            if key == "MAP":
                m["w"], m["h"] = int(parts[1]), int(parts[2])
            elif key == "SYMMETRY":
                m["symmetry"] = parts[1]
            elif key == "MAP_NAME":
                m["name"] = line.strip()[len("MAP_NAME "):]
            elif key == "TILE":
                m["tiles"].append(tuple(int(v) for v in parts[1:5]))
            elif key == "EDGE":
                idx, typ, pid = int(parts[1]), int(parts[2]), int(parts[3])
                m["edges"][decode_edge(m["w"], idx)] = (typ, pid)
            elif key == "DRAGON":
                team, length = int(parts[1]), int(parts[2])
                cells = [(int(parts[i]), int(parts[i + 1])) for i in range(3, len(parts), 2)]
                m["dragons"].append((team, length, cells))
    return m


# An edge is ("N", x, y) = north edge of tile (x, y), y in 0..H, or
#            ("W", x, y) = west edge of tile (x, y), x in 0..W.
def decode_edge(w, idx):
    stride = 2 * (w + 1)
    y, off = divmod(idx, stride)
    return ("N", off, y) if off <= w else ("W", off - (w + 1), y)


def encode_edge(w, edge):
    kind, x, y = edge
    stride = 2 * (w + 1)
    return y * stride + x if kind == "N" else y * stride + (w + 1) + x


def transform(m, t):
    w, h = m["w"], m["h"]
    nw, nh = (h, w) if t == "t" else (w, h)

    def pt(x, y):
        if t == "fx":
            return (w - 1 - x, y)
        if t == "fy":
            return (x, h - 1 - y)
        if t == "t":
            return (y, x)
        return (x, y)

    def edge(e):
        kind, x, y = e
        if t == "fx":
            return ("N", w - 1 - x, y) if kind == "N" else ("W", w - x, y)
        if t == "fy":
            return ("N", x, h - y) if kind == "N" else ("W", x, h - 1 - y)
        if t == "t":
            return ("W", y, x) if kind == "N" else ("N", y, x)
        return e

    sym = m["symmetry"]
    if t == "t" and sym in ("x", "y"):
        sym = "y" if sym == "x" else "x"
    out = {
        "w": nw,
        "h": nh,
        "symmetry": sym,
        "name": f'{m["name"]} [{t}]',
        "tiles": [(*pt(x, y), a, b) for (x, y, a, b) in m["tiles"]],
        "edges": {edge(e): v for e, v in m["edges"].items()},
        "dragons": [(team, length, [pt(x, y) for (x, y) in cells]) for (team, length, cells) in m["dragons"]],
    }
    return out


def write(m, path):
    lines = [f'MAP {m["w"]} {m["h"]}']
    if m["symmetry"]:
        lines.append(f'SYMMETRY {m["symmetry"]}')
    lines.append(f'MAP_NAME {m["name"]}')
    lines.append(f'TILE_COUNT {len(m["tiles"])}')
    for x, y, a, b in sorted(m["tiles"], key=lambda r: (r[1], r[0])):
        lines.append(f"TILE {x} {y} {a} {b}")
    encoded = sorted((encode_edge(m["w"], e), v) for e, v in m["edges"].items())
    lines.append(f"EDGE_COUNT {len(encoded)}")
    for idx, (typ, pid) in encoded:
        lines.append(f"EDGE {idx} {typ} {pid}")
    lines.append(f'DRAGON_COUNT {len(m["dragons"])}')
    for team, length, cells in m["dragons"]:
        lines.append(f"DRAGON {team} {length} " + " ".join(f"{x} {y}" for x, y in cells))
    lines.append("END")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--src", default="maps")
    ap.add_argument("--out", default="maps_variants")
    ap.add_argument("--transforms", default="id,fx,fy,t", help="comma list of id, fx, fy, t, or +-joined compositions like t+fx")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    for path in sorted(glob.glob(os.path.join(args.src, "*.map"))):
        m = parse(path)
        base = os.path.splitext(os.path.basename(path))[0]
        for t in args.transforms.split(","):
            v = m
            for op in t.split("+"):
                v = transform(v, op)
            v["name"] = f'{m["name"]} [{t}]'
            write(v, os.path.join(args.out, f"{base}_{t.replace('+', '')}.map"))


if __name__ == "__main__":
    main()
