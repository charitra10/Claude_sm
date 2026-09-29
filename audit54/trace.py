#!/usr/bin/env python3
"""
trace.py: run a BOT_DIAG copy of a bot against an opponent on (map, seed, side) triples, in parallel, and keep what
the behaviour audit needs: the DIAG lines, the engine's death lines and the result (one .log per game), plus the replay.

The DIAG copy is built in a scratch directory from the bot's sources with `#define BOT_DIAG` prepended. Flags can be
overridden (`--set F_CAMP=false --set CAMP_HORIZON=10`), which rewrites the `constexpr` line in the copy only.

Usage:
  audit54/trace.py v5.9 v5.8 --map portals,default --seeds 1-4 --out /tmp/tr/base
  audit54/trace.py v5.9 v5.8 --map portals --seeds 1-2 --side A --set F_PORTAL_TRAP=false --out /tmp/tr/notrap

Every log line kept is one of:  DIAG ...  |  round R: bot I (team T) died: CAUSE  |  team X wins ... / draw ...
The DIAG team is always the bot under test (the opponent is a normal build); `side` says which team that is.
"""
import argparse, concurrent.futures, glob, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def diag_copy(bot, dest, sets):
    os.makedirs(dest, exist_ok=True)
    for f in os.listdir(bot):
        p = os.path.join(bot, f)
        if os.path.isfile(p) and not f.startswith('.'):
            shutil.copy2(p, dest)
    src = open(os.path.join(bot, 'main.cpp')).read()
    for kv in sets:
        k, v = kv.split('=')
        pat = re.compile(r'(constexpr\s+(?:bool|int)\s+' + re.escape(k) + r'\s*=\s*)[^;,]+')
        src, n = pat.subn(lambda m: m.group(1) + v, src, count=1)
        if n != 1:
            sys.exit(f'flag {k} not found')
    open(os.path.join(dest, 'main.cpp'), 'w').write('#define BOT_DIAG\n' + src)


def plain_copy(bot, dest):
    os.makedirs(dest, exist_ok=True)
    for f in os.listdir(bot):
        p = os.path.join(bot, f)
        if os.path.isfile(p) and not f.startswith('.'):
            shutil.copy2(p, dest)


def build(unswbc, bot, some_map):
    subprocess.run([unswbc, 'run', '--no-replay', '--seed', '0', some_map, bot, bot], capture_output=True, timeout=900)
    if not os.path.isfile(os.path.join(bot, '.unswbc-build', 'bot')):
        sys.exit(f'build failed: {bot}')


KEEP = re.compile(r'^(DIAG |round \d+: bot \d+ \(team [AB]\) died|team [AB] wins|draw)')


def play(unswbc, m, a, b, seed, log, replay, timeout):
    cmd = [unswbc, 'run', '-v', '--seed', str(seed), '-o', replay, m, a, b]
    with open(log, 'w') as out:
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1 << 20)
        for line in p.stdout:
            if KEEP.match(line):
                out.write(line)
        p.wait(timeout=timeout)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('bot')
    ap.add_argument('opponent')
    ap.add_argument('--map', required=True, help='comma-separated substrings of map names')
    ap.add_argument('--maps-dir', default=os.path.join(ROOT, 'maps/maps'))
    ap.add_argument('--seeds', default='1-4', help='a-b or comma list')
    ap.add_argument('--side', default='AB')
    ap.add_argument('--set', action='append', default=[])
    ap.add_argument('--out', required=True)
    ap.add_argument('-j', type=int, default=4)
    ap.add_argument('--timeout', type=int, default=900)
    a = ap.parse_args()
    unswbc = shutil.which('unswbc') or os.path.expanduser('~/.local/bin/unswbc')
    keep = a.map.split(',')
    maps = [m for m in sorted(glob.glob(os.path.join(a.maps_dir, '*.map')))
            if any(k == os.path.basename(m)[:-4] or (k.endswith('*') and os.path.basename(m).startswith(k[:-1]))
                   for k in keep)]
    if not maps:
        sys.exit('no maps (use exact names, or a prefix ending in *)')
    if '-' in a.seeds:
        lo, hi = map(int, a.seeds.split('-'))
        seeds = list(range(lo, hi + 1))
    else:
        seeds = [int(s) for s in a.seeds.split(',')]
    os.makedirs(a.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix='trace_', dir=os.environ.get('TRACE_TMP'))
    base_bot = os.path.join(work, 'bot0')
    base_opp = os.path.join(work, 'opp0')
    diag_copy(os.path.join(ROOT, a.bot) if not os.path.isabs(a.bot) else a.bot, base_bot, a.set)
    plain_copy(os.path.join(ROOT, a.opponent) if not os.path.isabs(a.opponent) else a.opponent, base_opp)
    build(unswbc, base_bot, maps[0])
    build(unswbc, base_opp, maps[0])
    slots = []
    for i in range(a.j):
        s = (os.path.join(work, f's{i}', 'bot'), os.path.join(work, f's{i}', 'opp'))
        shutil.copytree(base_bot, s[0])
        shutil.copytree(base_opp, s[1])
        slots.append(s)
    tasks = [(m, sd, side) for m in maps for sd in seeds for side in a.side]
    free = list(range(a.j))

    def job(t, k):
        m, sd, side = t
        bot, opp = slots[k]
        name = f'{os.path.basename(m)[:-4]}_s{sd}_{side}'
        pa, pb = (bot, opp) if side == 'A' else (opp, bot)
        play(unswbc, m, pa, pb, sd, os.path.join(a.out, name + '.log'), os.path.join(a.out, name + '.replay'),
             a.timeout)
        return k, name

    with concurrent.futures.ThreadPoolExecutor(a.j) as ex:
        it = iter(tasks)
        pend = set()
        for _ in range(a.j):
            t = next(it, None)
            if t: pend.add(ex.submit(job, t, free.pop()))
        while pend:
            done, pend = concurrent.futures.wait(pend, return_when=concurrent.futures.FIRST_COMPLETED)
            for f in done:
                k, name = f.result()
                free.append(k)
                res = [l for l in open(os.path.join(a.out, name + '.log')) if l.startswith(('team', 'draw'))]
                print(name, res[-1].strip() if res else '?', flush=True)
                t = next(it, None)
                if t: pend.add(ex.submit(job, t, free.pop()))
    shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
