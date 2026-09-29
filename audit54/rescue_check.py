#!/usr/bin/env python3
"""rescue_check.py DIR [DIR...]: F_REVERSE_SPLIT audit over trace.py output (NAME.log + NAME.replay per game).

For every rescue split of the team under test (DIAG rescue): did the rear child live (rounds survived, and the mass it
carried), and did the 2-long head die soon (as intended)? Then the misses: our dragons >= 4 long that died by kelp, their
own body or another body (not a feed suicide, not head-on) — each such death is mass a rescue split might have saved.
For those, the last DIAG turn line says what the dragon was doing."""
import collections, glob, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import common  # noqa: F401  (patches sim's loader)
from killers import classify

RESCUE = re.compile(r'^DIAG rescue (\d+) (\d+) (\d+) (\d) (\d) child (\d+)')
DEATH = re.compile(r'^round (\d+): bot (\d+) \(team ([AB])\) died: (.*)')


def game(log, rep, side, verbose):
    rescues = []
    deaths = {}
    last_turn = {}
    for l in open(log):
        m = RESCUE.match(l)
        if m:
            rescues.append(tuple(map(int, m.groups())))
        m = DEATH.match(l)
        if m:
            deaths[int(m.group(2))] = (int(m.group(1)), m.group(4).strip())
        if l.startswith('DIAG turn '):
            p = l.split()
            if len(p) > 3 and p[2].isdigit():
                last_turn[int(p[2])] = l.strip()
    g, out, pr, rows = classify(rep)
    kids = {}
    for e in g.rp.events:
        if e[0] == 'split':
            kids.setdefault(e[1], []).append((e[2], len(e[6])))
    res = []
    for (i, r, L, alpha, strad, child) in rescues:
        # the split child is the first one this parent made at length `child` from this round on
        ch = None
        for cid, cl in kids.get(i, []):
            if cl == child and g.born.get(cid, -1) >= r:
                ch = cid
                break
        if ch is None:
            for cid, cl in kids.get(i, []):
                if cl == child:
                    ch = cid
        head_death = deaths.get(i, (999, ''))[0] - r
        child_life = (deaths.get(ch, (500, 'alive'))[0] - r) if ch is not None else -1
        res.append((i, r, L, alpha, strad, child, ch, head_death, child_life, deaths.get(ch, (500, 'alive'))[1]))
    # the engine's cause decides (killers.py attributes a death to the victim's last action, which mislabels a dragon
    # rammed head-on by an enemy on the enemy's turn as 'enemy body')
    misses = [row for row in rows if row[3] == side and row[5] >= 4 and row[4] != 'feed' and
              deaths.get(row[2], (0, ''))[1] in ('hit a wall', 'hit itself', 'hit another dragon')]
    return res, misses, last_turn


def main():
    verbose = '-v' in sys.argv
    tot = collections.Counter()
    life_hist = collections.Counter()
    miss_by = collections.Counter()
    for d in [a for a in sys.argv[1:] if a != '-v']:
        for log in sorted(glob.glob(os.path.join(d, '*.log'))):
            name = os.path.basename(log)[:-4]
            side = name[-1]
            res, misses, last_turn = game(log, log[:-4] + '.replay', side, verbose)
            n = len(res)
            lives = [x[8] for x in res]
            short = sum(1 for x in res if 0 <= x[8] <= 3)
            mass_short = sum(x[5] for x in res if 0 <= x[8] <= 3)
            print(f'{name:36} rescues {n:4d}  child died <=3 rounds: {short:4d} (mass {mass_short:4d})  '
                  f'misses (>=4 long, wall/self/body): {len(misses):3d} (mass {sum(m[5] for m in misses)})')
            tot['rescues'] += n; tot['short'] += short; tot['mass_short'] += mass_short
            tot['misses'] += len(misses); tot['miss_mass'] += sum(m[5] for m in misses)
            for x in res:
                life_hist[min(x[8] // 5 * 5, 50) if x[8] >= 0 else -1] += 1
            for m in misses:
                miss_by[m[4]] += 1
                if verbose:
                    print('   miss', m, '|', last_turn.get(m[2], ''))
            if verbose:
                for x in res:
                    if 0 <= x[8] <= 3:
                        print('   short-lived child', x)
    print('\nTOTAL', dict(tot))
    print('child lifetime histogram (rounds, 5-bins; -1 = child not found):', sorted(life_hist.items()))
    print('misses by cause:', dict(miss_by))


if __name__ == '__main__':
    main()
