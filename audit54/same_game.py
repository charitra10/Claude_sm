#!/usr/bin/env python3
"""same_game.py DIR1 DIR2: for every game in both trace directories, are the two games identical? Compares the replays'
event streams (every move, split, death, pearl and sonar), so a flag whose ablation leaves every game identical provably
does nothing on those games."""
import glob, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: F401
import replay

a, b = sys.argv[1], sys.argv[2]
same = diff = 0
for f in sorted(glob.glob(os.path.join(a, '*.replay'))):
    g = os.path.join(b, os.path.basename(f))
    if not os.path.exists(g):
        continue
    ea = [e for e in replay.load(f).events if e[0] not in ('log', 'elog', 'ind')]
    eb = [e for e in replay.load(g).events if e[0] not in ('log', 'elog', 'ind')]
    if ea == eb:
        same += 1
        print(os.path.basename(f), 'identical')
    else:
        diff += 1
        k = next((i for i, (x, y) in enumerate(zip(ea, eb)) if x != y), min(len(ea), len(eb)))
        rnd = max((e[1] for e in ea[:k] if e[0] == 'round'), default=0)
        print(os.path.basename(f), f'DIFFERS from round {rnd}')
print(f'{same} identical, {diff} differ')
