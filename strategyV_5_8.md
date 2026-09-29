# Strategy v5.8: what changed on top of v5.7, and why

v5.8 is `v5.7/main.cpp` plus the 11 requested changes of the v5.8 brief (b1.png..b5.png) and four fixes found while
testing them, each behind an `F_*` flag in the "v5.8 modules" block at the top of `v5.8/main.cpp`. v5.7 is untouched.
Raw results are in `bench58/*.jsonl`.

**Bottom line:** on fresh seeds 401-408, all 14 ladder maps (help and big_empty included), both sides, 672 games with no
errors or timeouts, v5.8 scores **74.1%** (166/224) vs v5.6, **77.7%** (174/224) vs v5.5 and **72.3%** (162/224) vs v5.4,
each about ±3. v5.7 on the same 672 games: 57.6% / 58.0% / 53.1%. No map is below 50% against any opponent; the only map
where v5.8 is not ahead of v5.7 is trophy (27 vs 29 of 48). Raw: `bench58/val1.jsonl`, `bench58/val1_v57.jsonl`.

Most of the gain is not from the 11 requested changes on their own (57-66% on the screening seeds, parity with v5.7) but
from two endgame fixes found while testing them (`F_TRAP_AVOID`, `F_FEED_UNGUARD`: screening 62% -> 72-75%).

## Results

### Validation (seeds 401-408, 16 games per cell)

| map | vs v5.6 | vs v5.5 | vs v5.4 | | v5.8 pooled /48 | v5.7 pooled /48 |
|---|---|---|---|---|---|---|
| autarky | 10 | 16 | 16 | | 42 | 28 |
| big_empty | 9 | 10 | 9 | | 28 | 15 |
| default | 9 | 12 | 10 | | 31 | 28 |
| devil | 10 | 8 | 10 | | 28 | 25 |
| dilemma | 16 | 16 | 16 | | 48 | 40 |
| help | 15 | 14 | 13 | | 42 | 34 |
| portals | 12 | 15 | 10 | | 37 | 20 |
| queen_of_spades | 16 | 13 | 15 | | 44 | 39 |
| queen_of_spades_but_she_ages | 9 | 14 | 14 | | 37 | 33 |
| schooltime | 9 | 9 | 8 | | 26 | 24 |
| slithery_fight | 12 | 11 | 10 | | 33 | 10 |
| stronghold | 15 | 14 | 8 | | 37 | 20 |
| trauma | 15 | 12 | 15 | | 42 | 33 |
| trophy | 9 | 10 | 8 | | 27 | 29 |
| **all** | **166/224 (74.1%)** | **174/224 (77.7%)** | **162/224 (72.3%)** | | 502/672 | 378/672 |

### Screening (seeds 301-308, 12 ladder maps without help / big_empty, 576 games per row)

| build | vs v5.6 | vs v5.5 | vs v5.4 |
|---|---|---|---|
| v5.7 | 58.3% | 62.5% | 61.2% |
| v5.8, the 11 items (strict isolation) | 63.0% | 59.9% | 62.5% |
| same, isolation off | 57.3% | 65.6% | 64.1% |
| + `F_TRAP_AVOID`, `F_FEED_UNGUARD` (isolation off) | 71.9% | 72.9% | 75.0% |
| same, split cap off | -6 | +2 | -9 (games, paired) |
| + mild isolation + `F_SECTOR_FLIP` (shipped) | 67.7% | 79.2% | 69.8% |
| same, `F_SECTOR_FLIP` off | 69.3% | 76.6% | 70.8% |

Fight maps (trophy, arena, Colosseum, default_small, seeds 301-308, 192 games): isolation off 98, strict 87, shipped mild
version 97. Deprecated maps (arena, Colosseum, default_small, seeds 301-308, 144 games): v5.8 73, v5.7 61.

Per-module effects on maps where the module never fires are exactly zero (games are deterministic per seed): on the fight
maps F_FARM, F_PATCH_CAMP, F_POCKET_SEEK, F_STRADDLE_SPLIT, F_FEED_SCORE, F_CHAMBER_MIRROR changed nothing, F_EARLY_KILL
+2/160, F_PORTAL_YIELD +2/160. First farm screen (trauma, portals, stronghold, slithery, schooltime; seeds 101-108 vs
v5.6, 80 games): trauma 16/16 (v5.7 14/16), portals 11 (10), stronghold 10 (10), schooltime 6 (7), slithery 3 (6).

## Benchmark set

On the user's instruction, `small.map` (16x8, below the 10x10 minimum) is never used, and arena, Colosseum and
default_small (off the ladder) only serve as a regression check. "Ladder maps" below are the other 14; the screening runs
leave out help and big_empty (half the CPU time). Screening seeds 301-308; validation on fresh seeds.

## The 11 requested changes

1. **`F_ISOLATED`**: no teammate head in view and an enemy head within 5: the dragon only takes fair trades (the enemy at
   least as long as it), never ambushes or plays "bold", and treats enemy reach like an alpha does: no pearl or step an
   enemy head can reach next move (within 2 moves in the first 120 rounds, "stricter early"). The strict first version (no
   strikes at all, refuse anything within 2-3 moves) cost 11/192 on the fight maps (trophy 9 games out of 48); the shipped
   parameters (`ISO_STRIKES = 1`, `ISO_RISK = 360`, `ISO_RISK_EARLY = 320`) are neutral there (97 vs 98/192).
2. **`F_FARM`** (with 9): a *farm* is a dead end of the map (no loop the body fits round) holding at least `FARM_MIN` = 3
   spawners that refill every round (countdown at most 1 on two sightings). The calculation: a dive eats every fast tile
   (they are full again a round after a body leaves them) and costs 2 (the head left at the tip when we split L-2 there,
   which drops 1 pearl for the next dive), so a farm with F fast tiles nets F - 2 per dive. trauma and stronghold each
   have two 8-deep corridors with 6 fast tiles (6 pearls a round each, 12 of trauma's 13.3), portals has two 9-deep
   spine dead ends with 4, autarky / dilemma / slithery_fight pockets have 3. The chain:
   - at the tip, the rear child is L-2 (v5.7's a7 rule shed 2-long pieces whenever the tail was in a corridor; now only
     when the rear child would really be trapped, `tail_doomed()`);
   - the rear walks out, and when its tail is just off the spawners it splits a 2-long child off the tail
     (`farm_harvest()`), born facing back in: it eats the refilled corridor, splits at the tip, and so on;
   - the rear of an escape split may re-enter a farm despite v5.7's 40-round dead-end ban (that ban was for 2-pearl
     pockets, a net-zero loop);
   - a dead end with anyone in view inside is not entered (`pocket_busy`), and the corridor mouth of a farm a teammate
     works is not a place to stand (`farm_lane`): a head there trapped the rear child walking out;
   - farms and their mirror images (under the resolved symmetry, or every live candidate as a guess checked on arrival)
     go out on sonar tag 8183; idle dragons walk to one nobody has claimed (`F_FARM_SEEK`), through portals if needed
     (`F_ROUTE_PORTAL`: trauma's farms are only reachable through one).
   Effect: pearls eaten in trauma's corridor 55-100 -> 100-260 per game.
3. **`F_EARLY_KILL`**: from round 1, a non-alpha takes a sure kill (1-step, or a sprint paid for by the length gap) on an
   enemy at least as long as itself when a teammate head is within 3 of the collision (it eats both drops).
4. **`F_CHAMBER_MIRROR`**: a camper in a paying portal chamber reports the tile outside the *mirror image of its chamber's
   portal*, from which one step lands in the mirrored chamber (not the mirror of the chamber itself, which is unreachable
   on foot).
5. **Trophy / symmetry bug**: v5.7 reported hotspots before a dragon had resolved the symmetry, with symmetry field 0,
   and every receiver adopted 180-degree rotation from it (trophy is x -> W-1-x): mirrored spots went to the wrong place.
   Bit 6 of the scout packet now means "symmetry unknown" (the value field is 4 bits). The spawner-density spots of 10
   (below) also cover trophy's handles, where pearls rarely pile up.
6. **`F_FEED_SCORE`**: feeders pick the alpha by L - k/4 (a 19-long alpha 10 away beats a 10-long one 5 away: 16.5 vs
   8.75) among alphas they can reach with time for the apex to eat; the current target is kept unless another beats it
   by 2 segments; in the endgame relays send the longest alphas first; an alpha merges into a longer one if the apex then
   has 12 rounds to eat the drop (v5.7 required its whole length, so late big alphas never merged).
7. **`F_STRADDLE_SPLIT`** (b1.png): v5.3's "no split while the body straddles a portal" also blocked the 2-long voluntary
   split. On the portals map dragons cross a portal every few rounds, and a 7-long dragon went dozens of rounds without
   splitting. A 2-long child is born on the side we came from, facing away from the portal: allowed.
8. **`F_PORTAL_YIELD`** (b2.png): a portal edge with a teammate head on either side facing it (a lower ID, or any one when
   we are not yet at it), or a teammate body crossing it, is not targeted (both would land in the same chamber).
9. See 2 (b3.png): the dragon lying along trauma's corridor with its tail at the mouth is the harvest moment; v5.7 could
   not see its tail (4 tiles out of view) and a freshly born rear child was barred from splitting for 8 rounds.
10. **`F_POCKET_SEEK`** (b4.png): a 5x5 with at least 6 good spawners (a pearl, or due within 100 rounds) and 2.5x the
    density seen so far is reported as a rich spot (and its mirror), before pearls pile up; this also works on maps where
    every tile spawns now and then (v5.7's "uniform map" guard silenced trophy). In slithery_fight the dragon spawned
    beside the bottom-right pocket now reports and camps it by round 12.
11. **`F_PATCH_CAMP`** (b5.png): a patch is a connected group of 4-36 spawners with at most 3 ways out (schooltime's rooms,
    trophy's handles). Whoever was in it first keeps it (two arriving together: the lower ID); the others leave and keep
    out of its tiles for 40 rounds; the camper stays while a pearl is due within 40 rounds.

## Fixes found on the way

- **`F_TRAP_AVOID`**: long dragons (5+) score each move by the room left after it, counting their own body as it frees up
  from the tail (the segment k from the tail is gone after k moves); the view's edge, a portal or 14 tiles count as a way
  out. v5.7 scored only visible empty area capped at 10, and long dragons (endgame apexes, farm divers) coiled into walls
  and died in rescue splits (stronghold seed 301: 135 rescues, apex 42; with it: 91 rescues, apex 91).
- **`F_FEED_UNGUARD`**: a dragon that just took the alpha role in a split (every rescue split hands it on) waited 40
  rounds before feeding; it now feeds an apex known to be 4+ longer at once.
- **`F_SECTOR_FLIP`**: exploration waypoints were fixed compass points round the map centre, so the two teams explored
  differently. On devil the left-side team's waypoints lay in its own barren strip (v5.6 against itself, seed 301: 284 vs
  1232 pearls eaten depending on the side). Waypoints now point away from where the dragon was born.
- `memory_bfs()` crossing portals used `dead_cell()`, which shares its scratch queue: an endless loop (every dragon timed
  out). It now has its own queue.

## Sonar protocol

`FARM_TAG` 8183: position = a fast spawner of the farm, origin = round seen (or claimed), alen = entry direction (2 bits) |
fast spawners (4 bits) | speculative mirror (bit 6) | claimed (bit 7). `SCOUT_TAG` 8185: bit 6 = symmetry unknown, value
now 4 bits.

## CPU

Judge sandbox, seed 1: schooltime max 6.9M, slithery_fight max 6.3M points per turn (budget 100M).

## Known weak spots / next ideas

- trophy is the one map where v5.8 does not beat v5.7 (27 vs 29 of 48): an elimination fight over 2.4 pearls a round,
  decided largely by who reaches the cup through its portal first.
- schooltime (26/48) and devil vs v5.5 (8/16) are the weakest remaining cells. `F_SECTOR_FLIP` measured neutral overall
  (it fixes devil's side asymmetry but costs a few games on default).
- The farm chain only runs when the harvest fires as the rear walks out; congestion at a farm's mouth still kills
  divers. The mirror farm on the enemy side (trauma: 127 steps away) is almost never reached in time.
- Screening seeds 301-308 were used for every decision; only 401-408 are clean.
