# Strategy v5.5: what changed on top of v5.4, and why

v5.5 is `v5.4/main.cpp` plus five switchable modules (`F_*` flags at the top of `v5.5/main.cpp`) and one new
sonar tag. Everything else is still described by `strategyV_5_4.md` and the earlier docs. Raw results are in
`bench55/*.jsonl`. v5.4 is untouched and was the baseline.

**Bottom line:** v5.5 is at parity with v5.4 on the six target maps: **66.8%** (385/576) against v5.3 and v5.1
pooled, vs **67.7%** for v5.4 on the same games. Of the paired games whose result differs, v5.5 wins 103 and v5.4
wins 108 (z = -0.3). Against v5.3 alone v5.5 is ahead: 65.3% vs 62.5%, driven by dilemma (43/48 vs 30/48). Against
v5.1 it is behind: 68.4% vs 72.9%, mostly slithery_fight (24/48 vs 38/48). All five requested behaviours are in
place and verified in match logs. They fix real defects, but two of them (the round-0 cascade and the alpha
handover) trade win rate on slithery_fight, and the strict dead-end rule wins dilemma and costs trauma.

## How it was measured

`seedbench.py`, paired seeds, both sides, against v5.3 and v5.1. Only the maps the tasks target were played:
`autarky`, `dilemma`, `queen_of_spades`, `queen_of_spades_but_she_ages`, `trauma` and `slithery_fight` (the
`queen_of_spades` filter matches both queen maps). Seeds 1-12 and 21-32, 24 games per map per opponent. v5.4 was
re-run on exactly the same games, because its source changed after `bench54/` was recorded (hash 03002ddf, not
50090302). Behaviour was checked with `BOT_DIAG` copies (`DIAG` lines on stderr, `unswbc run -v`).

## Results (final build, 576 paired games)

| map | v5.5 vs v5.3 | v5.4 vs v5.3 | v5.5 vs v5.1 | v5.4 vs v5.1 |
|---|---|---|---|---|
| autarky | 38/48 | 36/48 | 47/48 | 48/48 |
| dilemma | **43/48** | 30/48 | 47/48 | 48/48 |
| queen_of_spades | 30/48 | 31/48 | 27/48 | 23/48 |
| queen_of_spades_but_she_ages | 22/48 | 23/48 | 25/48 | 26/48 |
| slithery_fight | 32/48 | 30/48 | 24/48 | **38/48** |
| trauma | 23/48 | **30/48** | 27/48 | 27/48 |
| all six | **65.3%** (188/288, ±2.8) | 62.5% (180/288, ±2.9) | 68.4% (197/288, ±2.7) | **72.9%** (210/288, ±2.6) |

Raw: `bench55/final_v55.jsonl`; v5.4 on the same games: `target_v54`, `target2b_v54`, `slith2_v54`. Each
map cell is ±3.4 games.

### Ablations (earlier builds, 96 games per cell, the same seeds)

| module off | slithery_fight | trauma | dilemma | queen | queen_ages | autarky |
|---|---|---|---|---|---|---|
| none (v5.5 before the election fix) | 50 | 47 | 91 | 58 | 45 | 85 |
| `F_CHOKE` | 51 | **61** | **62** | 66 | 57 | 87 |
| `F_MANTLE` | 60 | 53 | 95 | 58 | 45 | 84 |
| `F_CASCADE_FIX` | 61 | | | | | |
| `F_MANTLE_CASCADE` | 56 | | | | | |
| `F_BACK_HARVEST` | | 54 | 91 | 58 | 45 | 85 |
| `F_CAMP2` | | 47 | | | | |
| v5.4 (reference) | 68 | 57 | 78 | 54 | 49 | 84 |

The chokepoint rule is what wins dilemma (+29/96), and it costs the maze maps (queen, queen_ages, trauma). The
round-0 cascade fix and the alpha handover each cost about 10/96 on slithery_fight. Each cell is ±5.

## Modules

### 1. Chokepoints and dead ends (`F_CHOKE`) — autarky, dilemma
- **Sonar chokepoints** (the `HAZARD_TAG` mouths trapped dragons broadcast): closed to every dragon longer than 2,
  and to a 2-long dragon unless live pearls lie in the pocket behind (`chokepoint_blocked()`, used by the BFS,
  `is_step_safe`, the remembered-map route and the scorer).
- **Dead-end corridors**: a dragon longer than 2 never enters one; a 2-long dragon only while a pearl lies in it now
  (`static_dead_end()`). "Dead end" means the map itself: `sealed_pocket(..., static_only)` ignores dragon bodies.
  The first version also counted bodies as walls, so 4-long dragons split ~200 times a game on slithery_fight in
  gaps between teammates that were gone a round later. Pockets closed only by bodies keep v5.4's rule.
- Dead-end steps are removed from the legal set. If they are the only moves left, a dragon of 4+ splits L-2 first
  (`chokesplit`), so the rear is born on its tail facing back out and only the 2-long head goes on; a 3-long dragon
  takes the least bad one.

On dilemma the 1-wide corridors respawn every round; on autarky there is a 4-tile dead end that does the same.
2-long dragons now farm them and split children out of the mouth.

### 2. One camper per chamber (`F_CAMP2`) — queen_of_spades, scenario_2.png
Root cause: the queen chamber (16 tiles behind portal 0 or 1, every tile spawning every 1-50 rounds) held two
dragons, and v5.4's rule was "the shorter one leaves". Lengths change during a round (the first mover eats), so
each dragon saw the other as longer and both left (seed 1, round 152: ids 62 and 76 both `leave 1`).
- Yielding is by ID only. A teammate first seen after we arrived never moves us. One that was inside when we came
  in, or that we have shared the chamber with for 10 rounds, moves us if its ID is lower. The lowest ID inside
  never leaves, so there is always exactly one camper once the others are out.
- Pearl timers of chamber tiles out of view are remembered (`Cell::spawn_at`): a 6x3 chamber never fits in the
  7x7 view, and v5.4 judged "nothing due soon" from the visible tiles only. The camper stays while the chamber is
  dense and a pearl is due within 60 rounds, or any pearl is due within 12.
- The camper is a resident (it does not wander through its portal) and does not join the endgame feeding; the
  children it splits off do.
- A child born in the chamber still evacuates to its parent, unless no lower ID is inside any more. It then
  adopts the chamber (v5.4 had two orphans walk out together).

Measured on queen_of_spades + queen_ages (12 games): chamber-rounds where every dragon inside decided to leave
went from 6 (v5.4, 8 games) to 0; a lone dragon leaving with a pearl due within 12 rounds from 86 to 15-26
(the rest yield to a lower-ID teammate passing through).

### 3. Harvest behind the tail (`F_BACK_HARVEST`) — trauma, scenario_3.png
trauma has 1-wide corridors (x = 11 and 36, y = 9-14) whose tiles respawn every round, so pearls appear on every
tile the tail leaves. A dragon that does not split routinely (a growing alpha, or a non-alpha whose routine split
is refused) now splits a 2-long child off its tail when that child should gain at least 2 pearls the head cannot
reach sooner. The body is rebuilt from the head's recent path (no sprint since). The pearls counted are the tiles
the tail just left, visible ones or ones that refill every round (`Cell::fast_obs`). The child is born on the
tail facing along that path, straight at them. Alphas need length 10 and 3 pearls, and it runs every 6 rounds
at most.

Traced on trauma (6 games): 18 harvest splits, every child ate within 6 rounds, 36 pearls in total (the estimate,
2 each). It never triggers on the other target maps, and turning it off moved trauma by +7/96 (noise level).

### 4. Round-0 cascade fix (`F_CASCADE_FIX`) — slithery_fight, scenario_4.png
Root cause: a long spawn child is promoted to alpha on sight (length > 7), and the cascade's own units have
already reached `alpha_split_cap()` (12 on slithery_fight), so it counted as "growing" and skipped the cascade.
The 25-long spiral became `4 → SPLIT 23`, and then the 23-long child did a routine `SPLIT 2` a round later.
v5.5 ignores "growing" in round 0 and does not promote by length in round 0. Verified (slithery_fight, seed 1):
`id 4: SPLIT 23`, `id 18: SPLIT 21`, `id 24: SPLIT 19`, ... `id 32: SPLIT 3`, all in round 0.

This is what was asked for, but it costs slithery_fight about 10/96: v5.4 kept a big body alive early.

### 5. Alpha identity on split (`F_MANTLE`, new tag `MANTLE_TAG` 8186)
Root causes in v5.4:
- the round-0 cascade kept the role on the 2-long head (`Mode::Cascade` skipped the handover);
- the split-and-kamikaze path cleared `s.alpha` before `execute()` could announce it, so teammates kept the old ID
  (with its old length) as an alpha;
- stale reports of the old ID (length L) outranked the new rear child (length L-2) in `superior_alpha()`, so the
  new alpha walked off to feed its former self;
- the endgame apex election let a just-demoted 2-long head declare itself alpha again a round later.

Now, whenever an alpha splits and the rear child is the larger piece (every L-2 split: cascade, rescue,
kamikaze split, portal sacrifice, dead-end split), the head gives the role up and becomes a skirmisher.
- The child takes it if it is 4+ long (3+ in the round-0 cascade). The parent aims the claim: it simulates each
  beam (the one fired into its neck refracts out of its new tail) and puts the child's length on every beam that
  ends on the child's body. Only a newborn of exactly that length claims. Bends where no beam can reach fall back
  to v5.4's rule (a 4+ child born in round > 0 is an alpha).
- Every teammate that hears the tag drops the old ID as an alpha, and ignores reports of it from before the
  split (`demoted_alpha()`), so it is never fed again.
- A dragon never feed-suicides into one it can see whole that is no longer than itself, nor for 40 rounds after
  taking the role.
- A 2-3 long dragon, or one that handed the role over in the last 30 rounds, cannot win the apex election.

Evidence (stronghold + trauma, seeds 1-2, final build): 51 handovers, 26 children claimed; all 26 were alpha on
their first turn, none ever feed-suicided, no parent regained the role, and no alpha acted at length 2 after
round 0 (1776 alpha turns).

Two parts were tried and switched off: handing the role to 3-long children mid-game, and having a feeder longer
than its apex take the role (`F_MANTLE_PROMOTE`). Together they left 17-19 alphas in the endgame on
slithery_fight, and v5.5 fell to 31% there.

## Sonar protocol (layout unchanged, one new tag)

| aid | meaning | position field | alen |
|---|---|---|---|
| 8186 `MANTLE_TAG` | this ID gave the alpha role to its split child | old alpha ID (12 bits) | child length (0: relay, nobody claims) |

The parent sends it on every beam for 3 rounds; non-alphas relay the latest demotion every other round for 8 rounds.

## Tracing
`DIAG` lines: `claim`, `mantle`, `mantlebeam`, `mantleguard`, `camp`, `adopt`, `harvest`, `chokesplit`, plus
v5.4's. CPU in the judge sandbox (slithery_fight, seed 1): p50 4.6M, max 5.4M points per turn, the same as v5.4.

## Known weak spots / next ideas
- The strict dead-end rule for 3-long dragons costs the maze maps (queen, trauma). v5.4 let small dragons into
  pockets that had pearls or were 12+ tiles. Allowing 3-long dragons back into static pockets that have live
  pearls is the obvious next test; it breaks the letter of Task 1.
- slithery_fight: full decomposition in round 0 and the handover each cost about 10/96.
- Claims miss when the body bends at the neck and no head beam reaches the child (about 1 in 5 handovers).
