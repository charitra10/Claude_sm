"""Shared setup for the audit scripts: import replay_tools, and drop the pre-game 'update' events, which sim.Game (and
killers.classify) would otherwise replay as moves (every spawn body gained two phantom segments until they reached the
tail). The replay_tools files themselves are left untouched."""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'replay_tools'))
import replay, sim

_load = replay.load


def _clean_load(path):
    rp = _load(path)
    k = next((i for i, e in enumerate(rp.events) if e[0] == 'round'), 0)
    rp.events = [e for e in rp.events[:k] if e[0] != 'update'] + rp.events[k:]
    return rp


sim.load = _clean_load
