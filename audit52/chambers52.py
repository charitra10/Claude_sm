#!/usr/bin/env python3
"""chambers52.py DIR: per map, small-chamber visits by our dragons: entries, occupied entries, deaths by cause and length."""
import collections, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze
from sim import Game
d = sys.argv[1]
per = collections.defaultdict(collections.Counter)
for f in sorted(os.listdir(d)):
    if not f.endswith('.replay'): continue
    mp = f.rsplit('_', 2)[0]; side = f[:-7].rsplit('_', 1)[1]
    g = Game(os.path.join(d, f)); st = analyze.Static(g)
    per[mp]['chambers'] = len(st.chambers); per[mp]['barren_ch'] = sum(c['barren'] for c in st.chambers)
    per[mp]['ch_sizes'] = max([len(c['tiles']) for c in st.chambers] or [0])
    for ev in analyze.walk(g):
        if ev[0] == 'death' and g.team.get(ev[2]) == side:
            body = ev[7]
            if st.chamber_of.get(body[0]) is not None:
                mode = ev[4].partition(':')[2]
                per[mp]['death_' + ev[5].replace(' ', '_') + ('_feedmode' if mode == 'feed' else '')] += 1
                per[mp]['death_len%s' % ('2' if len(body) == 2 else '3' if len(body) == 3 else '4+')] += 1
for mp, c in per.items():
    print(mp, dict(c))
