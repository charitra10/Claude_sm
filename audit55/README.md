# audit55: behavioural audit of the v5.5+ modules

Does each module do what it was written to do, independently of win rate? Measured on the full board of the replay
(ground truth) and on the bot's own `DIAG` trace. Results and fixes: `strategyV_5_9e.md`.

`run.py` and `variant.py` are copied from `audit52/` (the v5.2-v5.4 audit on another branch); `analyze52.py` is that
audit's `analyze.py`, imported here for its map structure (`Static`: pockets behind bridge edges, small chambers) and its
replay walk.

- `variant.py SRC DEST [--diag] [F_X=false ...]`: copy a bot with `BOT_DIAG` on and flags changed.
- `run.py OUTDIR CHALLENGER OPPONENT --maps a,b --seeds 1-2 [-j 4]`: challenger (DIAG) vs a plain opponent, both sides,
  keeping `<map>_<seed>_<side>.replay` and `.diag.gz`.
- `analyze55.py DIR...`: v5.5 metrics per run directory (chambers, dead-end entries, mantle handovers, harvest children).
- `analyze56.py DIR...`: v5.6 metrics (symmetry vs the replay's shared countdowns, scout trips, dry chambers, splits in
  dead ends, forced exits and re-entry).
- `harvest.py DIR KEY [-v]`: every `harvest` / `tailbfs` / `farmharvest` split: what the child ate in 8 rounds, how it died.
- `boxed.py DIR KEY`: was the harvest child born with a free first move.
- `camp.py DIR [-v]`, `crowd.py DIR`, `camptimer.py DIR`: F_CAMP2 (lone campers leaving a paying chamber and why; 2+ of
  ours in one chamber; the camper's remembered spawn timers against the truth).
- `choke.py DIR [-v]`: dead-end entries with fewer than 2 live pearls, and whether the entrant had another move.
- `alpha2.py DIR`: alphas acting at length 2 after round 0, by cause. `kept.py DIR`: alphas keeping the role after an
  L-2 split.
- `show.py REPLAY ID R0 R1 [--also=ID]`: the board round a dragon, turn by turn.

The ladder maps used: autarky, default, devil, dilemma, portals, queen_of_spades, queen_of_spades_but_she_ages,
schooltime, slithery_fight, stronghold, trauma, trophy (not help, big_empty, arena, Colosseum, default_small, small).
