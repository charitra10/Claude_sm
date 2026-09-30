# audit53: behavioural audit of the v5.3 modules in v5.9

These tools check whether each v5.3 module (`F_REPEL`, `F_CYCLE2`, `F_PORTAL_FIX`, `F_ALPHA_PORTAL`, `F_STRADDLE`,
`F_HANDOVER`, `F_LONG_PORTAL`, `F_HAZARD_RR`) does what it was built to do, measured on replays and traces rather than
on win rate. Findings and numbers are in `strategyV_5_9_audit53.md`. The summary outputs are in `results/`. The replays and traces
(about 270 MB per 78-game set) are not committed; regenerate them with the steps below.

- `mkvariant.sh SRC DEST [diag|instr] [FLAG=value ...]`: copy a bot and rewrite `constexpr` flags. `instr` also applies
  `instrument.py` and defines `BOT_DIAG`, so this is a scratch build only and must never be submitted.
- `instrument.py FILE`: inserts the audit's `DIAG` trace points after anchor strings (loop breakouts, alpha portal
  captures and handovers, long-dragon portal splits, handover children, hazards sent and received, portal preemption and
  release).
- `runreplays.py BOT OPP OUTDIR --seeds N`: plays BOT against OPP on the ladder maps, both sides, with seeded pearls. It
  keeps every replay and BOT's `DIAG` lines (`.diag.gz`). help, big_empty, default_small, Colosseum and arena are skipped
  because they are off the ladder.
- `metrics.py DIR... [--per-map]`: metrics taken from the replays, which is what actually happened on the board:
  - clumping and coverage
  - loose-loop share
  - portal crossings, including those by 8+-long dragons
  - splits with the body across a portal
  - alpha handovers at splits
  - rounds with no alpha
  - win rate
- `diagstats.py DIR...`: trace-based counts:
  - loop breakouts: how far the dragon gets in 10 rounds, whether it loops again, and what it steered for
  - alpha portal captures and heirs
  - long-dragon portal splits
  - handover children
  - hazard reach, and teammates entering live hazard mouths
- `events.py KIND DIR`: lists `strad` (straddle splits), `xlong` (long crossings) or `honone` (failed handovers), each
  with the dragon's trace line for that round.
- `cmp.py KEYS DIR... [--per-map]`: puts `metrics.py` numbers from several directories side by side.
- `resume_bench.py CHALLENGER OPP... --out FILE ...`: `seedbench.py` with resume. It skips games already in `--out`, so a
  benchmark cut short (for example by a container restart) continues where it stopped.
- `winrates.py FILE [--per-map]`: seedbench JSON-lines as a score per opponent (and per map), with one standard error.

Typical run (the opponent is v5.8, so every `DIAG` line comes from the bot under test):

```bash
S=/tmp/audit; audit53/mkvariant.sh v5.9 $S/on instr; audit53/mkvariant.sh v5.9 $S/noREPEL instr F_REPEL=false
python3 audit53/runreplays.py $S/on v5.8 $S/r/on --seeds 3 -j 4
python3 audit53/runreplays.py $S/noREPEL v5.8 $S/r/noREPEL --seeds 3 -j 4
python3 audit53/cmp.py clump,cover,win $S/r/on $S/r/noREPEL --per-map
python3 audit53/diagstats.py $S/r/on
```
