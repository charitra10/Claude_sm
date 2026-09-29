#!/usr/bin/env python3
"""deaths.py DIR...: deaths of the team under test by cause (killers.py categories, the engine's own cause used to spot
head-ons suffered on the enemy's turn), summed over the games of each directory, plus pearls eaten per game."""
import collections, glob, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: F401
from killers import classify

only = next((a.split('=')[1] for a in sys.argv[1:] if a.startswith('--map=')), None)
for d in [a for a in sys.argv[1:] if not a.startswith('--')]:
    tot = collections.Counter(); n = 0; res = collections.Counter()
    for f in sorted(glob.glob(os.path.join(d, '*.replay'))):
        name = os.path.basename(f)[:-7]
        if only and not name.startswith(only): continue
        side = name[-1]
        g, out, pr, rows = classify(f)
        n += 1
        for (tm, cause), v in out.items():
            if tm == side: tot[cause] += v
        log = f[:-7] + '.log'
        w = [l for l in open(log) if l.startswith(('team', 'draw'))]
        res['win' if w and w[-1].startswith('team ' + side) else 'loss/draw'] += 1
    friendly = tot['ally head'] + tot['ally body']
    print(f'{d.rstrip("/").split("/")[-1]:10} games {n} {dict(res)}  per game: friendly {friendly / max(n, 1):.1f}  ' +
          '  '.join(f'{k} {v / max(n, 1):.1f}' for k, v in sorted(tot.items())))
