# v5.9e: do the v5.6 modules do what they were built to do?

Second part of the behavioural audit (the first, `strategyV_5_9d.md`, covered the v5.2 modules). The v5.6 modules were
kept on win rate within noise (`strategyV_5_6.md`: "they do what was asked (traces below)", checked on a few seeds). Here
each one is measured against its stated purpose on the replay's full board (ground truth) and the `DIAG` trace, in v5.9
as it is now, and switched off one at a time. Tool: `audit52/analyze56.py`; per-game numbers in `bench59d/audit56/`.

## Scope

| v5.6 flag | in this audit | why |
|---|---|---|
| `F_DRY_EVICT` | yes | unchanged since v5.6 |
| `F_CHOKE_GREEDY` | yes | still the core of dead-end diving (v5.7-v5.9 farm and tail rules wrap round it) |
| `F_REENTRY` | yes | unchanged since v5.6 |
| `F_SYMMETRY` | yes | inference and sharing unchanged (only the "unknown" bit of the packet was added in v5.8) |
| `F_MIRROR_SCOUT` | no | what it does was rewritten later: `F_HOTSPOT2`, `F_SCOUT_NEAR`, `F_SCOUT_NEARER` (v5.7) and `F_POCKET_SEEK` (v5.8) replaced its report rules, guards and dispatch |
| `F_MANTLE_CASCADE` | no | off |

## Method

`BOT_DIAG` copies of v5.9 (with the v5.9d fixes) against plain v5.8, the 12 ladder maps (autarky, default, devil, dilemma,
portals, queen_of_spades, queen_of_spades_but_she_ages, schooltime, slithery_fight, stronghold, trauma, trophy; not
default_small, Colosseum, arena, small, big_empty, help), seeds 1-3, both sides: 72 games per variant.

Ground truth used by `analyze56.py`:
- *dry chamber*: a small chamber (<= 20 tiles, has a portal edge) with no pearl on it and no tile whose next spawn attempt
  (from the replay's countdown events) falls within 24 rounds, the module's own `DRY_HORIZON`;
- *dead end*: a tree-shaped pocket of the map (small side of a bridge edge, no loop); its *tip* is a tile with no pocket
  neighbour other than the one we came from;
- *symmetry*: the map's declared symmetry, checked against its tiles and edges.

## Results (72 games each: module on / off)

| module | intended behaviour | measured | verdict |
|---|---|---|---|
| `F_CHOKE_GREEDY` | in a dead end with pearls ahead: no split, eat, split L-2 at the tip; no 3-long head dies at a tip | 3-long dragons dying at a dead-end tip 502 / 2483; splits before the tip 178 / 2686, with pearls still ahead 391 / 3211; mean child length of those splits 4.2 / 2.4; longest dragon (sum) 2502 / 2171; wins 46 / 37 | **works, and matters** |
| `F_DRY_EVICT` | leave a dry chamber at once; reserve it so teammates stay out while it is dry | share of chamber turns spent in a dry chamber on default (the only map with such rooms) 27% / 40%; after a dry exit, a dragon came in within 30 rounds 411 times (373 of them another dragon) / 82 (off); 240 of the 411 came in while it was still dry | the leaving works; the reservation does not keep anyone out |
| `F_REENTRY` | a camper forced out through its chamber's portal walks straight back in | back in its chamber within 12 rounds: queen_of_spades 10 / 2, the aged queen 6 / 2; turns in paying chambers there +16%. Of 81 forced exits 28 re-entered, 20 died, 31 timed out: 16 of 25 succeed on the two queens, most failures are on slithery_fight (13 of 21), default (8 of 15) and portals (7 of 20), where the body no longer fits back in (a 2x2 room) and the attempt quietly lapses | **works where it was meant to**; failures elsewhere are harmless |
| `F_SYMMETRY` | infer the map's symmetry, share it on sonar | 6017 resolutions and 6010 adoptions from sonar, **0 wrong**; dragons know it on 78.5% of turns after round 10; newborns learn it 8.8 rounds after birth on average, 21% never; wins 46 / 52 with it off, longest dragon 2502 / 2686 | **does its job**; no measurable benefit to the team (see below) |

Win counts are DIAG builds against v5.8; one standard error is about 4 wins in 72.

### F_CHOKE_GREEDY
The one v5.6 module whose effect is large and clear. The 502 remaining 3-long tip deaths are mostly not in its scope: 353
of them are on portals, dragons that crossed a portal into a one-tile dead cell they could not see (`Neutral:trapped`,
hit wall) — the dead-cell barring of v5.4 (`F_PORTAL_TRAP`), audited separately.

### F_DRY_EVICT: the reservation half
When a dragon leaves a dry room it reserves the portal in its own name for 30 rounds (instead of v5.5's "free" notice).
The v5.2 audit already showed reservations rarely reach anyone: sonar stops at kelp, and default's rooms have two portals,
of which only one gets reserved. Tried here: `F_DRY_SKIP`, a dragon that remembers the room behind a portal as dry (every
tile known, no pearl, nothing due) does not take that portal. It never changed a game on default (dry turns and re-entries
identical to the baseline, 13 skips in 42 games): the dragons walking into dry rooms have never been inside them, and the
one that knows (the leaver) already shuns that portal for 60 rounds. Removed.

### F_SYMMETRY: right, but slow to reach newborns, and not worth anything yet
Inference is exact on every ladder map. Sharing is the weak part: a split child starts knowing nothing and waits for a
type-only packet (every knower sends one on one beam every 6 rounds). Tried: `F_SYM_HANDOFF` (a parent that knows it
hands it to its split child on the beams that refract into the child, unless a barred / mantle / farm hand-off uses them):
newborn lag 8.8 -> 7.0 rounds, never 21% -> 18%, known on 78.5% -> 82.3% of turns, but 42 wins against the baseline's 46.
Knowing the symmetry sooner buys nothing, because nothing that uses it pays: with `F_SYMMETRY` off the bot won 52 of 72.
Its main consumer is mirrored-hotspot scouting (7030 scout claims with symmetry, 3462 without), part of `F_MIRROR_SCOUT`
and its v5.7/v5.8 successors, which is out of this audit's scope. `F_SYM_HANDOFF` is in the code, **off**.

## Head-to-head (plain builds, fresh seeds)

v5.9 (as shipped) against itself with one module off, 12 ladder maps, both sides. Raw: `bench59d/val56_1001.jsonl`,
`bench59d/val56_sym_1005.jsonl`.

| v5.9 against | seeds | score |
|---|---|---|
| no `F_CHOKE_GREEDY` | 1001-1004 | **55.2%** (53/96, ±5.1); trauma 8/8, stronghold 6/8 |
| no `F_SYMMETRY` | 1001-1004 | 43.8% (42/96, ±5.1) |
| no `F_SYMMETRY` | 1005-1012 | 52.1% (100/192, ±3.6) |
| no `F_SYMMETRY`, pooled | 1001-1012 | 49.3% (142/288, ±2.9) |

`F_CHOKE_GREEDY` earns its keep (with the audit runs: 46 vs 37 of 72). `F_SYMMETRY` is neutral: the first 96 games
suggested dropping it, the next 192 did not. The DIAG audit runs put `F_DRY_EVICT` (46 / 46) and `F_REENTRY` (46 / 47)
within noise too.

**Bottom line:** all four v5.6 modules in scope do what they were written to do (the dry chamber reservation excepted),
and nothing needed fixing in their own logic. The two fixes tried (`F_DRY_SKIP`, `F_SYM_HANDOFF`) did not pay and are
not shipped, so the shipped v5.9 behaves exactly as v5.9d.

## Next steps
- On portals, 353 of our dragons (72 games) died 3 long in one-tile dead cells behind portals: that is `F_PORTAL_TRAP`
  (v5.4), in the v5.4 audit's scope.
- Mirrored-hotspot scouting (the main use of the symmetry) doubles scout claims without a measurable gain; an audit of the
  v5.7/v5.8 scouting modules (`F_HOTSPOT2`, `F_POCKET_SEEK`) is the place to look.
- Reservations (dry exits, one camper per chamber) need a way through walls; nothing tried so far reaches the entrants.

## Code changes in v5.9/main.cpp

- New block "v5.9e" after v5.9d: `F_SYM_HANDOFF` (off). With it off the shipped build behaves exactly as v5.9d.
- `F_DRY_SKIP` was tried and removed.
- New `DIAG` line `symhand`; `audit52/analyze56.py`.
