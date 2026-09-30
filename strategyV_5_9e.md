# v5.9e: do the v5.5+ modules do what they were built to do?

The v5.5-v5.9 modules were kept or dropped on win rate alone. This audit asks a different question of each module that is
still switched on and not overridden later: does the behaviour it was written for actually happen? It is measured on the
full board of the replay (ground truth) and on the bot's own `DIAG` trace. Where a module does not do its job, the cause was
found and fixed in `v5.9/main.cpp` behind a new switch (block "v5.9e" after the v5.5 constants), and measured again.

The v5.2-v5.4 modules are audited separately (`strategyV_5_9d.md`, another branch); this audit uses that audit's
`run.py` / `variant.py` and imports its `analyze.py` for map structure (`audit55/analyze52.py`). Tools: `audit55/` (see its
README).

## Method

- **Behaviour runs:** a `BOT_DIAG` copy of v5.9 against plain v5.8 on the 12 ladder maps (autarky, default, devil, dilemma,
  portals, both queens, schooltime, slithery_fight, stronghold, trauma, trophy; not help, big_empty, arena, Colosseum,
  default_small, small), seeds 1-2, both sides: 48 games per variant, one replay and trace each. For each module one run
  with only that module switched off. The unmodified v5.9 is the baseline ("on").
- Games are paired by seed, but crowd dynamics are chaotic: team-level totals (pearls eaten, wins) over 48 games are noise
  at the +-5% level. The verdicts rest on per-event metrics of the behaviour itself.
- **Win rate:** `seedbench.py`, plain builds, fresh seeds 1001-1008, the same 12 maps, both sides, against v5.8.

## v5.5 modules

| module | intended behaviour | measured (on / off) | verdict |
|---|---|---|---|
| `F_CHOKE` | no entry into a dead end of the map unless it pays (since v5.7 `F_CHOKE2`: 2+ live pearls); forced to its mouth, split L-2 or go in for the pearls | dead-end entries with < 2 live pearls: 80 of 1776 (4.5%) / 138 of 639 (22%); of the 178 such entries in the baseline (nested pockets counted), 139 had no other free move. Pearls eaten in dead ends 5064 / 1883 | **works** |
| `F_CAMP2` | exactly one camper per paying chamber, yielding by ID; remembered spawn timers | chamber-rounds held by exactly one of ours 8025 / 7186; both campers leaving together 2 / 7. Two dragons both deciding to stay: 4% of held chamber-rounds, never longer than 12 rounds (the 10-round ID rule). The camper's "next pearl due" is exact or within 2 rounds of the truth in 95% of 5853 checks | **works** |
| `F_BACK_HARVEST` | split a 2-long child off the tail onto pearls the head cannot reach; the child should eat 2+ | tail-BFS geometry (v5.7): 153 of 213 children eat 2+ in 8 rounds. **Path-based fallback (the v5.5 geometry): 84 of 268 eat 2+, 117 eat nothing, 54 die before eating** | **fallback does not work** -> fixed (`F_HARVEST_FIX`) |
| `F_CASCADE_FIX` | every spawn body of 4+ cascades into 2-long units in round 0 | round-0 cascade splits 156 / 116; every round-0 dragon of 4+ cascaded (156 of 156) | **works** |
| `F_MANTLE` | an alpha's L-2 split hands the role to the rear child; the 2-long head never keeps it; the old ID is never fed | L-2 splits where the 2-long head stayed alpha: 0 / 227; the parent alpha again within 30 rounds 10 / 147; 4+ children left without the role 37 / 169; feed suicides into a demoted alpha 0 | **works** |

### F_BACK_HARVEST: the path-based fallback, and the fix

`back_harvest()` tries the v5.7 tail BFS first and, when that finds nothing, falls back to v5.5's geometry: it rebuilds
the tiles the tail just left from the head's recent path and counts pearls there that the head cannot reach soon, plus
out-of-view tiles that "refill every round" (`fast_obs`). Three things were wrong with it:

1. **Reachability.** Nothing checked that the child could walk to the counted pearls. Example (queen_of_spades_but_she_ages
   seed 1, dragon 2, round 71): the child was born on a tail tile with kelp on two sides, the parent's head on the third
   and its own neck on the fourth, and died on its first turn. 15 of 268 children had no free first move.
2. **Speculative tiles.** In a run with the reachability fix, 566 of the 757 pearls the fallback counted were out-of-view
   "fast" tiles; 167 of 272 splits rested on them alone, and those children ate nothing 48% of the time.
3. **Teammates.** Devil's centre is a jam of our own dragons: the pearls behind a tail are as near to three teammates as to
   the child, which is born into the jam (61 of 127 early child deaths are "hit other body").

`F_HARVEST_FIX`: the fallback is replaced by `reach_harvest()`: the tail is the visible one or the tracked body's (no guess
across a sprint or a portal), a BFS from the tail (not back through the neck, our own segments are walls) counts visible
pearls within `HARVEST_REACH` = 6 steps that the head cannot reach about as soon, and there must be a free first step.
Out-of-view tiles do not count (`HARVEST_UNSEEN = false`). In both harvest paths, a pearl a teammate head in view reaches in
fewer moves than the child is not counted (`harvest_taken()`).

Five maps where the harvest fires (devil, slithery_fight, portals, queen_ages, schooltime; seeds 1-2, 20 games):

| | fallback splits | child ate 2+ | ate nothing | tail-BFS splits | child ate 2+ |
|---|---|---|---|---|---|
| v5.9 | 240 | 35% | 41% | 191 | 77% |
| reachability only (all 12 maps, 48 games) | 301 | 36% | 49% | | |
| reachability, own body as walls | 272 | 42% | 44% | | |
| same, visible pearls only | 70 | 36% | 40% | | |
| + teammate check, unseen tiles on | 241 | 51% | 37% | 177 | 79% |
| **+ teammate check, visible only (shipped)** | 50 | **54%** | **32%** | 312 | **80%** |

## v5.6 modules

| module | intended behaviour | measured (on / off) | verdict |
|---|---|---|---|
| `F_DRY_EVICT` | leave a chamber at once when no pearl lies in it and none is due within 24 | turns our heads spend in dry spawning chambers 1094 / 1862 (-41%) | **works** |
| `F_CHOKE_GREEDY` | in a dead end with pearls ahead: no split; split L-2 only at the tip | 3-long heads dying in dead ends 393 / 1615; 2-splits in a dead end with pearls in it 243 / 1032; rear child of the tip split 4.4 / 2.5 long on average | **works** |
| `F_REENTRY` | a camper pushed out of its chamber goes straight back in | forced exits (the camper's own last decision was "stay"): back within 12 rounds 14 of 43 / 1 of 24 | **works** (rare: ~1 a game) |
| `F_SYMMETRY` | infer the map's symmetry, share it | wrong resolutions (against the replay's shared countdowns) 0 of 7820; 77% of dragon-turns know the right one | **works** |
| `F_MIRROR_SCOUT` | idle foragers walk to reported rich spots (and mirror images) and eat there | 4845 claims: 3171 dropped by `F_PAIR_SEP`, 296 timed out, 997 arrived; 72% of arrivals ate nothing in 10 rounds; 0.44 pearls per arrival against 0.48 at the scout's own rate | **does not work** -> fix tried (`F_SCOUT_FIX`), see below |

### F_MIRROR_SCOUT

Why arrivals do not pay:
- "Arrived" meant 2 tiles in a straight line, through kelp too: schooltime seed 1, dragon 227 "arrived" at a spot in a room
  across a wall and the wrap seam, and walked off.
- An arriving scout claimed the next spot at once (hopping), and `F_PAIR_SEP` (v5.7) dropped the goal of any dragon that
  walked beside a teammate for 3 turns: two thirds of all trips.
- Half the arrivals (494 of 1001) land where two or more of our dragons already are: since v5.7 (`F_HOTSPOT2`) a spot is
  reported by the dragon sitting in it. Arrivals on the enemy's half eat least (0.33 a trip).

`F_SCOUT_FIX`: arrival needs a walk of at most 4 moves; on arrival the scout forages 10 rounds before it may claim another
spot; the pair rule drops a scout goal only for the dragon farther from it. Result (48 games): trips arriving 793, pearls
per arrival 0.50 against 0.52 at the scouts' own rate. The trips are cleaner, but still do not reach anything richer than
where the scout was.

## v5.7 modules

Not measured (rare or not behavioural on their own): `F_ASSASSIN` (fires only over 33 units), `F_PORTAL_COST`,
`F_ROUTE_MEM`, `F_PORTAL_ROAM`, `SCALE_MODE`. Covered elsewhere: `F_CHOKE2` (the dead-end entry rule, see `F_CHOKE`), `F_TAIL_BFS` (see `F_BACK_HARVEST`: 77-80% of its
children eat 2+), `F_SCOUT_NEAR` / `F_SCOUT_NEARER` / `F_HOTSPOT2` (the scouting system, see `F_MIRROR_SCOUT`). `F_TAIL_DROP`
only changed the path-based harvest, which `F_HARVEST_FIX` replaces: dead code with the fix on. Off in v5.9 and not audited:
`F_KAM_CAP`, `F_BARREN`, `F_HYBRID`, `F_SECTOR_HASH`.

| module | intended behaviour | measured (on / off) | verdict |
|---|---|---|---|
| `F_SPLIT_CAP` | no voluntary non-alpha split while the team has more than 33 units | voluntary 2-splits by non-alphas over the cap (harvests and farm splits excluded, as designed): 0 | **works** (the gate holds) |
| `F_SPLIT_COOL` | the rear child of an escape split makes no voluntary split for 8 rounds | such splits within 8 rounds of birth: 0 | **works** |
| `F_FEED_CLEAR` | endgame feeders never box the apex in | our longest dragon (10+) dying after round 350 into our own bodies or with no move: 0 | **works** |
| `F_PROTECT` | a suicide strike only when a teammate is nearer the collision than any other enemy, so we eat the drops | pearls of our strikes' drops eaten by us 61% (2136 / 1362) / 54% (2236 / 1868); strikes 1037 / 1299 | **works** |
| `F_PAIR_SEP` | teammates travelling side by side with nothing to eat split up | it fires ~450 times a game and the pair does part (2.1 -> 4.9 tiles apart within 6 rounds, 18% together again); idle paired turns overall 14674 / 15062 (noise level): new pairs keep forming. Its side effect, cancelling two thirds of scout trips, is fixed by `F_SCOUT_FIX` | **mechanism works, no measurable effect** |
| `F_CHOKE_LOOP` | stop the split-and-die loop in slithery_fight's spawn corridor | slithery_fight: rescue splits 3618 / 3926, the busiest tile 373 / 491 (4 games); the busiest tiles are still the two corridor ends (16,7) and (23,7), 80-110 tip splits a game each: since v5.8 these are intended farm dives (they pay: 2.44 pearls eaten against 2.02 lost a dive on slithery_fight, 3.7 against 2.1 on trauma and stronghold) | **partial; superseded by `F_FARM`** |
| `F_REM_COMMIT` | keep the remembered pearl we set off for | turns whose remembered target changed from the turn before 15.7% / 18.3% | **works, weakly** |
| `F_CHAMBER_ONE` | two dragons never go into one small chamber in the same round | two of ours entering one chamber within a round 55 / 49. Default (18): 12 came in through the room's other portal (`chamber_taken()` only looks at the portal it is about to use; default's rooms have two). Portals (32): the 2x2 loop chambers, where re-entering is exempt by design | **no measurable effect** (not fixed here: it needs every portal of a room, which the v5.2 audit is changing, `F_RESERVE_ALL`) |
| `F_RENDEZVOUS` | be at a cluster of tiles due to spawn together when it spawns, and eat it | 8847 trips in 48 games; 19% on time; 83% ate nothing at the cluster, 0.34 pearls a trip. Pearls eaten by the team 33753 / 33689 | **does not work** -> fixed (`F_RDV_FIX`) |

### F_RENDEZVOUS: late by design, and the fix

A trip started when the straight-line distance roughly matched the wait, and was allowed to arrive up to 15 rounds after
the spawn ("pearls stay until eaten"). Walls were ignored, so on maze maps the walk was much longer than the distance; the
trips that closed at the spot arrived 5.4 rounds after the spawn on average, when others had eaten the pile. 4349 of the
trips were dropped before closing (food in view comes first, which is right) and started again later.

`F_RDV_FIX`: the walk is the route over the remembered map (`memory_bfs()`, unseen tiles open), a trip starts only if it
arrives no more than `RDV_LATE` = 2 rounds after the spawn, is dropped as soon as it no longer can, and a teammate in view
nearer by moves takes it. Result (48 games, the other fixes off): 2223 trips, 40% on time, 0.51 pearls a trip (1.10 per
on-time trip), team pearls 34983.

## v5.8 modules

Covered elsewhere: `F_POCKET_SEEK` and `F_CHAMBER_MIRROR` (spots for the scouting system, see `F_MIRROR_SCOUT`), `F_FARM`'s
dead-end rules (see `F_CHOKE`). `F_EARLY_KILL` fired once in 48 games: v5.9c's exchange strikes (`F_EXCHANGE`, checked
earlier in the turn) take its cases, so it is superseded and not audited. Not measured: `F_STRADDLE_SPLIT`,
`F_PORTAL_YIELD`, `F_PATCH_CAMP`, `F_FEED_UNGUARD`, `F_SECTOR_FLIP`, `F_ROUTE_PORTAL`.

| module | intended behaviour | measured (on / off) | verdict |
|---|---|---|---|
| `F_FARM` (farm harvest) | walking out of a farm, split a 2-long child back into the refilled corridor | 381 farm harvests, 330 children ate 2+ in 8 rounds (3.8 pearls each; the wall deaths are the planned tip deaths). Dead-end dives pay on every map with farms (pearls eaten against length lost per dive: stronghold 3.8 / 2.1, trauma 3.7 / 2.1, autarky 3.0 / 2.0, slithery_fight 2.4 / 2.0) | **works**. Side effect: 290 of the 381 harvests are by alphas, mostly 4-7 long (2180 alpha turns at length 2); `FARM_SPLIT_ALPHA` was switched off in v5.9 for the same reason, but this path has no alpha floor. Not changed |
| `F_FARM_SEEK` | idle dragons walk to a known farm nobody works | of 776 trips that ended, 69 reached the farm (stronghold 0 of 237, trauma 0 of 55); 10% of trips ate 2+ there. Still, off: dead-end dives 1776 -> 1475, pearls eaten in dead ends 5064 -> 4271; team pearls unchanged (33753 / 33668) | **partial**: more farming, no more food overall. A fix (route length instead of straight-line distance, drop farms a teammate is diving: `F_FARMSEEK_FIX`) halved the trips but cut arrivals 69 -> 26 and paid trips 212 -> 74: **off** |
| `F_ISOLATED` | alone among enemies: fair trades only, respect their reach | deaths per isolated turn 3.8% / 4.3% (head-on 2.5% / 2.9%); lost trades 96 / 85 | **works, weakly** |
| `F_TRAP_AVOID` | long dragons do not coil into places they cannot turn round in | "no move survives" rescue splits at length 5+: 4774 / 4831, at 8+: 2319 / 2328; long-dragon wall / self deaths in the open 18 / 18 | **does not work** -> fixed (`F_TRAP_DIRECT`), see below |
| `F_FEED_SCORE` | feeders pick the alpha that will end longest | feed drops eaten by our final longest dragon 10.3% / 10.9%; feeders whose last pick was the final apex 84 / 125 of ~815 (most picked alphas that later handed their mass on in a split and died as the 2-long head) | **no measurable effect** |

### F_TRAP_AVOID: the direct path never asks

The room check (`escape_room()`: room left after the move, counting our body as it frees up) is a term in the move scorer.
Most moves never reach the scorer: a pearl, a remembered pearl, a portal or a feeding target in reach takes the direct path,
which only checks that the next tile has one free neighbour. `F_TRAP_DIRECT`: for a dragon 5+ long, a direct step that
leaves less room than the scorer wants goes to the scorer instead. Result (48 games, the other fixes off): rescue splits
at length 8+ 2319 -> 2060 (-11%), at 5+ 4774 -> 4654; rescue splits of long dragons outside dead ends 2707 -> 2429.

## Win rate

`seedbench.py`, plain builds against v5.8, fresh seeds 1001-1008, the 12 ladder maps, both sides (192 games each). Raw:
`bench59e/wr1_*.jsonl`; `audit55/wr.py` prints the table and the paired counts.

| map | v5.9 | + harvest, scout fixes | same, scouting off | **+ rendezvous fix (all fixes)** |
|---|---|---|---|---|
| autarky | 11 | 11.5 | 11 | 13 |
| default | 10 | 9 | 12 | 11 |
| devil | 9 | 10 | 9 | 9 |
| dilemma | 16 | 16 | 14 | 16 |
| portals | 12 | 11 | 8 | 11 |
| queen_of_spades | 10 | 11 | 11 | 14 |
| queen_of_spades_but_she_ages | 10 | 12 | 12 | 11 |
| schooltime | 9 | 9 | 11 | 10 |
| slithery_fight | 10 | 6 | 9 | 8 |
| stronghold | 7 | 13 | 7 | 8 |
| trauma | 9 | 10 | 11 | 8 |
| trophy | 9 | 10 | 8 | 9 |
| **all** | **122/192 (63.5%)** | 128.5 (66.9%) | 123 (64.1%) | **128 (66.7%)** |

On this seed set, paired on the same games, the fixed build wins 29 games v5.9 loses and loses 23 that v5.9 wins
(z = 0.8). Switching scouting off (with the other fixes on) is no better (123), so scouting stays on with its fix.

**A second fresh seed set (1101-1108, 192 games each) went the other way:** v5.9 131/192 (68.2%), all fixes 115/192
(59.9%); paired, v5.9 better in 45 games, the fixed build in 29 (z = 1.9; queen_of_spades 14 -> 11, queen_ages 14 -> 10,
stronghold 11 -> 7). **Both sets together (384 games each): v5.9 253 (65.9%), fixed build 243 (63.3%); paired 68 / 58,
z = 0.9.** So the fixes make their modules do what they were written for, but they do not raise the win rate against v5.8,
and may cost a little. Runs with one fix switched off at a time (`bench59e/wr3_*`) and the final build with `F_TRAP_DIRECT`
(`bench59e/wr4_*`, not in the numbers above) are to find out whether one fix is responsible. Every fix is behind its own
flag, so any of them can be switched off without touching the rest.

## Code changes in v5.9/main.cpp

- Flags (block "v5.9e" after the v5.5 constants): `F_HARVEST_FIX`, `HARVEST_REACH`, `HARVEST_UNSEEN`, `F_SCOUT_FIX`,
  `SCOUT_ARRIVE`, `SCOUT_REST`, `F_RDV_FIX`, `RDV_LATE`.
- `reach_harvest()`, `harvest_taken()` (F_HARVEST_FIX); arrival, rest and pair-rule changes in `scout_goal()` and the
  `F_PAIR_SEP` block (F_SCOUT_FIX); `DragonState::scout_rest_until`; `rendezvous_target()` (F_RDV_FIX).
