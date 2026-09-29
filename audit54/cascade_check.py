#!/usr/bin/env python3
"""cascade_check.py REPLAY...: F_SPAWN_CASCADE audit. For each team: the unit lengths at the end of round 0 (the cascade
should leave only 2- and 3-long units), and the dragons that died in round 0 with their cause.

Lengths are tracked from the split events (which carry both bodies) plus pearls eaten; sim.Game's bodies are not used
because it replays the pre-game 'update' events as moves."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'replay_tools'))
from sim import Game


def check(path):
    g = Game(path)
    ln = {i: len(b) for i, b in g.bodies.items()}
    team = dict(g.team)
    spawn = dict(ln)
    dead = []
    rnd = -1
    cur = None
    for e in g.rp.events:
        k = e[0]
        if k == 'round':
            rnd = e[1]
            if rnd >= 1:
                break
        elif k == 'turn':
            cur = e[1]
        elif k == 'tile' and not e[3] and cur is not None and cur in ln:
            ln[cur] += 1
        elif k == 'split':
            _, par, ch, tm, fc, pb, cb = e
            ln[par] = len(pb); ln[ch] = len(cb); team[ch] = tm
        elif k == 'death':
            dead.append((e[1], e[2], ln.pop(e[1], 0)))
    out = {}
    for tm in 'AB':
        out[tm] = (sorted(v for i, v in spawn.items() if team[i] == tm), sorted(v for i, v in ln.items() if team[i] == tm),
                   [d for d in dead if team.get(d[0]) == tm])
    return g.name, out


if __name__ == '__main__':
    for p in sys.argv[1:]:
        name, out = check(p)
        for tm, (spawn, lens, dead) in out.items():
            big = [l for l in lens if l > 3]
            print(f'{os.path.basename(p):40} {tm} spawn {spawn} -> {len(lens)} units, >3 long: {big}, died r0: {dead}')
