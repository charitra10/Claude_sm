#!/usr/bin/env python3
"""run.py OUTDIR CHALLENGER OPPONENT --maps a,b --seeds 1-4 [-j 4]
Plays CHALLENGER (a BOT_DIAG build) against OPPONENT (plain) on each map/seed/side with -v; keeps the replay and the DIAG
lines (gzipped) as OUTDIR/<map>_<seed>_<side>.{replay,diag.gz}, plus results.jsonl."""
import argparse, concurrent.futures as cf, gzip, json, os, re, shutil, subprocess, sys, tempfile, time
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UNSWBC = shutil.which('unswbc') or os.path.expanduser('~/.local/bin/unswbc')

def build(bot):
    subprocess.run([UNSWBC, 'run', '--no-replay', '--seed', '0', os.path.join(ROOT, 'maps/maps/arena.map'), bot, bot],
                   capture_output=True, text=True, timeout=900)
    assert os.path.isfile(os.path.join(bot, '.unswbc-build', 'bot')), bot

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('out'); ap.add_argument('chal'); ap.add_argument('opp')
    ap.add_argument('--maps', required=True); ap.add_argument('--seeds', default='1-4')
    ap.add_argument('-j', type=int, default=4); ap.add_argument('--sides', default='AB')
    a = ap.parse_args()
    lo, hi = map(int, a.seeds.split('-'))
    os.makedirs(a.out, exist_ok=True)
    for b in (a.chal, a.opp): build(b)
    work = tempfile.mkdtemp(prefix='audit_')
    slots = []
    for i in range(a.j):
        d = {}
        for b in (a.chal, a.opp):
            t = os.path.join(work, 's%d' % i, os.path.basename(b.rstrip('/')) + ('_c' if b == a.chal else '_o'))
            shutil.copytree(b, t, symlinks=True); d[b] = t
        slots.append(d)
    tasks = [(m, s, side) for m in a.maps.split(',') for s in range(lo, hi + 1) for side in a.sides]
    tasks.sort(key=lambda t: t[0] not in ('help', 'big_empty', 'schooltime', 'slithery_fight'))
    free = list(range(a.j)); res_f = open(os.path.join(a.out, 'results.jsonl'), 'a')

    def job(t, k):
        m, s, side = t
        c, o = slots[k][a.chal], slots[k][a.opp]
        A, B = (c, o) if side == 'A' else (o, c)
        stem = os.path.join(a.out, '%s_%d_%s' % (m, s, side))
        t0 = time.time()
        p = subprocess.Popen([UNSWBC, 'run', '-v', '--seed', str(s), '-o', stem + '.replay',
                              os.path.join(ROOT, 'maps/maps/%s.map' % m), A, B], stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, errors='replace')
        tail = ''
        with gzip.open(stem + '.diag.gz', 'wt') as z:
            for line in p.stdout:
                if line.startswith('DIAG '): z.write(line[5:])
                elif 'wins after' in line or 'draw' in line.lower(): tail = line.strip()
        p.wait()
        mm = re.search(r'team\s+([AB])\s+wins\s+after\s+(\d+)', tail)
        win = mm.group(1) if mm else 'D'
        return t, k, {'map': m, 'seed': s, 'side': side, 'outcome': 'W' if win == side else 'D' if win == 'D' else 'L',
                      'round': int(mm.group(2)) if mm else 500, 'secs': round(time.time() - t0)}

    with cf.ThreadPoolExecutor(a.j) as ex:
        it = iter(tasks); pend = set()
        for _ in range(a.j):
            t = next(it, None)
            if t: pend.add(ex.submit(job, t, free.pop()))
        n = 0
        while pend:
            done, pend = cf.wait(pend, return_when=cf.FIRST_COMPLETED)
            for f in done:
                t, k, r = f.result(); free.append(k); n += 1
                res_f.write(json.dumps(r) + '\n'); res_f.flush()
                print('[%d/%d] %s %d %s -> %s r%d %ds' % (n, len(tasks), r['map'], r['seed'], r['side'], r['outcome'],
                                                        r['round'], r['secs']), flush=True)
                t2 = next(it, None)
                if t2: pend.add(ex.submit(job, t2, free.pop()))
    shutil.rmtree(work, ignore_errors=True)

if __name__ == '__main__':
    main()
