# Strategy v5.7: what changed on top of v5.6, and why

v5.7 is `v5.6/main.cpp` plus the 13 requested modules (tasks 1-13 of the v5.7 brief) and 12 fixes for the ladder
bugs in `a1.png`..`a8.png`, each behind an `F_*` flag in the v5.7 blocks at the top of `v5.7/main.cpp`. Three
requested modules measured harmful and ship switched off (tasks 4, 6 and 11), as does one part of the a2 fix (hashed
sectors). v5.6 is untouched and was the baseline. Raw results are in `bench57/*.jsonl`. The toolkit was upgraded from
unswbc 1.1.0 to 1.2.2 mid-way; seeded games replay identically on both (checked on trophy seeds 1-2), so all results
are comparable.

**Bottom line:** on fresh seeds 9-16, all 18 maps, both sides (864 games, no errors), v5.7 is at **parity** with the
three previous versions: **53.1%** (153/288) vs v5.6, **50.0%** (144/288) vs v5.5, **49.8%** (143.5/288) vs v5.4,
each ±2.9. The target ("comfortably beats v5.6, v5.5 and v5.4") is **not met**. Screening runs on seeds 1-8 reached
57-62% against v5.6, but that did not hold on fresh seeds: the same selection bias as in v5.1-v5.6.

What v5.7 does deliver: every ladder bug is reproduced, root-caused and fixed, with before/after traces below. The
fixes move games where the bugs were (trauma +12/48, slithery_fight +9/48, portals +8.5/48, autarky +4/48 over the
three opponents pooled) and lose about as much on small open maps (small -10, Colosseum -9, default_small -8,
arena -6), where a per-fix ablation on seeds 1-8 found no single cause (see "Small maps").

## How it was measured

`seedbench.py`, paired seeds, both sides. Screening and ablations on seeds 1-4 and 5-8 (16 maps, help and big_empty
excluded for speed, 128 games per run vs v5.6), validation on seeds 9-16 over all 18 maps against v5.6, v5.5 and
v5.4 (864 games). Behaviour was checked with `BOT_DIAG` copies against a `BOT_DIAG` copy of v5.6 (both teams trace
the same `DIAG turn` line), on seed 3 of ten ladder maps, with offline detectors for each bug pattern.

## Results

### Validation (seeds 9-16, 16 games per cell)

| map | vs v5.6 | vs v5.5 | vs v5.4 | | before the ladder fixes (pooled /48) | after (pooled /48) |
|---|---|---|---|---|---|---|
| Colosseum | 5 | 7 | 5 | | 26 | 17 |
| arena | 6 | 6 | 6 | | 24 | 18 |
| autarky | 5 | 9 | 10 | | 20 | 24 |
| big_empty | 8 | 6 | 3 | | 15 | 17 |
| default | 9 | 10 | 10 | | 29 | 29 |
| default_small | 7 | 9 | 9 | | 33 | 25 |
| devil | 5 | 6 | 5 | | 23 | 16 |
| dilemma | 16 | 8 | 16 | | 40 | 40 |
| help | 10 | 15 | 10 | | 43 | 35 |
| portals | 10 | 9 | 3.5 | | 14 | 22.5 |
| queen_of_spades | 13 | 11 | 15 | | 36 | 39 |
| queen_of_spades_but_she_ages | 10 | 11 | 12 | | 34 | 33 |
| schooltime | 7 | 5 | 10 | | 24 | 22 |
| slithery_fight | 8 | 4 | 6 | | 9 | 18 |
| small | 4 | 3 | 3 | | 20 | 10 |
| stronghold | 9 | 9 | 3 | | 18 | 21 |
| trauma | 15 | 10 | 11 | | 24 | 36 |
| trophy | 6 | 6 | 6 | | 19 | 18 |
| **all** | **153/288 (53.1%)** | **144/288 (50.0%)** | **143.5/288 (49.8%)** | | 451/864 | 440.5/864 |

Raw: `bench57/val_c2.jsonl` (final build), `bench57/val_c1.jsonl` (the 13 tasks, flags as shipped, before the ladder
fixes: 53.5% / 50.7% / 52.4%).

### The 13 requested modules (128 games vs v5.6 each)

Round 1 removes one module from the full build (seeds 1-4, full build 46/128); round 3 removes one from the base
with tasks 4 and 11 off (seeds 5-8, base 67/128).

| task | flag | round 1 (off) | round 3 (off) | shipped | reading |
|---|---|---|---|---|---|
| 4 kamikaze only above 33 units | `F_KAM_CAP` | **64** (+18) | - | **off** | harmful: small maps never reach 33 units, so they never press a lead |
| 11 hybrid outward exploration | `F_HYBRID` | 50 (+4) | 59 -> **67** (+8, round 2) | **off** | harmful: explorers leave rich centres (devil 0/8 -> 4/8, autarky 1/8 -> 5/8) |
| 11, newborn-only / facing-based | `HYBRID_MODE` 2 / 3 | - | 62 / 61 (vs 67) | off | also harmful |
| 6 barren zones + mirrors | `F_BARREN` | 51 (+5) | **76** (+9) | **off** | harmful: on devil / autarky nearly every view is barren; local-only zones: 69 |
| 2 spawner rendezvous | `F_RENDEZVOUS` | 58 (+12) | 69 (+2) | on | noise in round 3 |
| 3 split cap above 33 units | `F_SPLIT_CAP` | 50 | 69 (+2) | on | neutral |
| 5 protected strikes | `F_PROTECT` | 50 | 58 (-9) | on | helps |
| 5 long-neutral assassins | `F_ASSASSIN` | - | 69 (+2) | on | neutral (rarely over the cap) |
| 1, 9 two-pearl dead-end rule | `F_CHOKE2` | 44 (-2) | 64 (-3) | on | leans helpful |
| 7 post-escape split cooldown | `F_SPLIT_COOL` | - | 66 (-1) | on | neutral |
| 8 portal-tile route cost | `F_PORTAL_COST` | 51 | 60 (-7) | on | helps |
| 10 mirrored spot out of view | `F_SCOUT_NEAR` | - | 70 (+3) | on | neutral |
| 12 tail-drop harvest | `F_TAIL_DROP` | - | 62 (-5) | on | leans helpful |
| 13 map scale (W+H)/2 | `SCALE_MODE` | 45 (-1) | 60 (-7) | 1 | helps |

Each 128-game number is about ±6 games; only differences above that are meaningful, and screening seeds flatter the
winner. Round 1 (seeds 1-4) and round 3 (seeds 5-8) are different games, so only the deltas compare.

### The ladder fixes (seeds 5-8, 128 games vs v5.6, one fix off at a time; base with all on: 66)

| bug | flag | off | reading |
|---|---|---|---|
| a2 hash + pair rule (first version) | `F_PAIR_SEP` | **84** (+18) | the hashed sectors were the cost |
| a2 split (base 73) | pair rule / hash / scout yielding off | 74 / **77** / 68 | hash off; pair rule kept (neutral); yielding kept |
| a3 memory routing | `F_ROUTE_MEM` | 65 | neutral |
| a3 commitment | `F_REM_COMMIT` | 71 | neutral |
| a8 portal roaming | `F_PORTAL_ROAM` | 70 | neutral |
| a6 one per chamber | `F_CHAMBER_ONE` | 66 | neutral |
| a5 feeder unblocking | `F_FEED_CLEAR` | 64 | neutral |
| a7 corridor loop | `F_CHOKE_LOOP` | 70 | neutral |
| a4 tail harvest | `F_TAIL_BFS` | 65 | neutral |
| a1 hotspot sharing | `F_HOTSPOT2` | 68 | neutral |

### Small maps (seeds 1-8, arena / small / Colosseum / default_small, 64 games each)

Base 34/64 (Colosseum 10, arena 9, default_small 11, small 4). With each ladder fix off: routing 31 (small 9, the
rest worse), commitment 31, pair rule 36, hotspot 34, scout yielding 33, and six fixes exactly 34 (they never fire
there). No fix explains the validation losses on small maps; `small` (16x8) was already weak before them (6/16 vs
v5.6 in `val_c1`).

## Ladder bugs (a1-a8.png): root causes and fixes

Traces: `DIAG` copies of v5.7 (team A) and v5.6 (team B), seed 3, ten ladder maps; counts are for our team.

| bug | root cause | fix | evidence (before -> after) |
|---|---|---|---|
| a7 split-and-die loop | slithery_fight's spawn corridor (row 7, x 16-23) has a refilling pocket at each end and the exit in the middle. A 4-long rear child was born with its tail in the other pocket, facing into it: forced in, ate 2, split at the tip, and so on every 4 rounds. v5.7's cooldown (task 7) made it worse by blocking the 2-splits that let v5.6 escape | `F_CHOKE_LOOP`: the body is tracked tile by tile from birth; a non-alpha whose tail lies in a 1-wide corridor sheds 2-long rear children at the tip (a 2-long piece's own tail is at the junction when it reaches a tip). Rear children of escape splits stay out of dead ends for 40 rounds. Voluntary entries that would doom the child are refused | rescue splits 1514 -> 1034, max on one tile 123 -> 53, wall deaths 792 -> 457; slithery vs all three 9/48 -> 18/48 |
| a3 circling a barren 2x2 | the remembered-pearl target was re-chosen every turn and flipped between two pearls on either side; out-of-view targets were routed by straight-line distance over the tiles in view only (map_scale > 26) | `F_ROUTE_MEM`: one BFS per turn over the remembered map (unseen tiles assumed open); remembered pearls only if reachable and at most 3 steps longer than straight. `F_REM_COMMIT`: keep the chosen pearl until gone, reached, or the way grows | 12-round loops in a 3x3: 209 -> 124 |
| a2 pairs travelling together, stuck in a corner | nearby foragers picked the same scout spot / rendezvous before either claim arrived; sibling IDs share `id / 2` sectors; the same straight-line routing as a3 | `F_SCOUT_NEARER` (leave a spot to a nearer teammate in view), `F_PAIR_SEP` (after 3 turns side by side with nothing to eat, the higher ID turns away and drops shared goals), routing as a3. `F_SECTOR_HASH` measured harmful, off | pairs with a shared scout target were most of the side-by-side turns; after the fix pairs are back to v5.6's level (81 -> 83) |
| a8 no portal though nothing to eat | residency was permanent once a dragon came out where anything spawns, and our own crossing marked the portal occupied for good | `F_PORTAL_ROAM`: residency ends after 8 idle rounds with no pearl in view and none due within 10; our own occupancy expires after 40 rounds outside small chambers; an idle dragon may use an occupied portal with no small chamber behind it | residency expiry fires ~100 times over the ten games; the remaining refusals are dragons that just came out of that portal (idle <= 8) |
| a1 trophy: everyone in one handle | the cup's cluster lies on the symmetry axis (its mirror is itself), so it was never reported; reports stopped once the mean of pearls in view reached 4; a claim never lapsed; crowds never left | `F_HOTSPOT2`: report the spot itself and its mirror, no saturation guard (uniform-map guard kept), claims lapse after 25 rounds, one member of a crowd whose pearls do not go round leaves per turn for a richer known spot. `F_SCOUT_NEAR` (task 10): the handles are 5 tiles apart over the wrap seam, so the old "0.35 x min(W, H)" guard rejected them | handle mirror reported and claimed from round 14 (trophy seed 1); trophy screening 7/8, validation 18/48 (was 19) |
| a5 alpha boxed in at round 472 | late feeders were held back ("hold" while the apex had pearls piled up, feed gates) and circled next to it; the protective suicide needed 12+ units, which feeding had already used up | `F_FEED_CLEAR`: a feeder touching the apex head, on its next tile, or within 2 while it has at most 2 exits, delivers at once; no holding in the last 30 rounds; no unit gate in the endgame | behaviour change only in endgames with crowded apexes |
| a6 two dragons into one chamber | the sonar reservation reaches lower IDs a round late, so two dragons decided in the same round | `F_CHAMBER_ONE`: a small chamber is taken if a teammate's body is crossing its portal now, or a teammate within 2 of the approach tile is nearer (tie: lower ID) | - |
| a4 no tail split for pearls behind the tail | the harvest only counted pearls on the tail's old path, rebuilt from our head path (broken by sprints and portals), and an alpha needed length 10 | `F_TAIL_BFS`: tail read off the visible body, pearls within 6 steps of it that the head cannot reach sooner; an alpha from length 8 | tail harvests ~60 per ten games |

## Modules (the 13 tasks)

1. **`F_CHOKE2` (tasks 1, 9):** any length may enter a dead end or a hazard mouth, and only with at least
   `CHOKE_MIN_PEARLS` = 2 live pearls inside (v5.5: only 2-long dragons, for any pearl). At the tip the rescue split
   leaves the 2-long head to die, so 2 pearls is break-even. A 2-long dragon no longer enters for a single pearl (p3.png).
2. **`F_RENDEZVOUS` (task 2):** clusters of 3+ tiles within 2 of each other whose countdowns end within 2 rounds, 4 to
   60 rounds out, are remembered (and their mirror, which shares the countdowns). A dragon with nothing to eat leaves
   when the walk takes about as long as the wait and no teammate in view is nearer.
3. **`F_SPLIT_CAP` (task 3):** no voluntary non-alpha split while the team has more than 33 units; emergency splits
   are unaffected, and splitting resumes below the cap. Tail harvests above the cap need 2 pearls.
4. **`F_KAM_CAP` (task 4, off):** kamikaze regime = more than 33 units, replacing the adaptive regime. Benchmarked as
   asked: -18/128. v5.6's regime stays.
5. **`F_PROTECT`, `F_ASSASSIN` (task 5):** every suicide strike (guaranteed kills, the skirmisher ram) needs a
   teammate head nearer the collision than any enemy head other than the victim. Above the cap a long neutral (5+)
   near our crowd rams an intruding head when 3 x its length >= ours; small teammates converge on the fight.
6. **`F_BARREN` (task 6, off):** views with no pearl and nothing due within 30 rounds become zones (with their mirror),
   shared on sonar tag 8184 and avoided by explorers. Harmful as measured; kept for reference.
7. **`F_SPLIT_COOL` (task 7):** a rear child of an escape split (born at length 4+) makes no voluntary split for 8
   rounds (p2.png). Voluntary re-splits within 3 rounds of birth: 306 -> 31. See a7 for its interaction with corridors.
8. **`F_PORTAL_COST` (task 8):** routes cost 4 extra per portal-edge tile without a pearl (Dijkstra over the tiles in
   view; kill and sprint logic keep plain step counts); loitering there costs 110 instead of 40-45 (campers exempt).
   Idle turns on portal tiles off the portals map: autarky 245 -> 89, default 115 -> 34.
9. See 1.
10. **`F_SCOUT_NEAR` (task 10):** a mirrored spot only has to lie out of view. The handles of trophy (p4.png) are now
    reported and claimed.
11. **`F_HYBRID` (task 11, off):** outward octant from the centre, offset {straight, left, right} by ID, never
    reversing across the seam. Harmful in all three variants.
12. **`F_TAIL_DROP` (task 12):** the back harvest also counts pearls within 2 of the tail; `F_TAIL_BFS` (a4) replaces
    its geometry.
13. **`SCALE_MODE` = 1 (task 13):** `map_scale()` = (W + H) / 2. Toroidal Manhattan distance grows with W + H, so the
    arithmetic mean tracks travel times (autarky 54x18: 36 instead of 31). Mirrors use W-1-x and H-1-y separately.

## Sonar protocol

One new tag, `BARREN_TAG` 8184 (position = zone centre, alen = symmetry | valid | rounds left / 4). It is only sent
with `F_BARREN` on, i.e. not in the shipped build. `SCOUT_TAG` packets now also carry spots themselves, not only
mirror images.

## CPU

Judge sandbox, help seed 1 (64x64, the largest map): p50 5.1M, p99 7.6M, max 8.6M points per turn (v5.6: max 7.4M;
budget 100M). slithery_fight seed 1: max 5.6M.

## Tracing

`DIAG` lines added: `turn` (every turn: id, round, length, mode, position, portal tile, camping, child, born, born
length, units), `tgt` (target kind and tile), `directfail`, `noport` (why a visible portal was not taken), `unreside`,
`pairsep`, `crowdgo`, `scoutsend`, `tailbfs`, `tailtrap`, `doomchild`, `unblock`, `rdvseen`, `rdvgo`, `rdvdone`,
`barren`, `assassin`, `converge`. The detectors used for the ladder bugs (pairs, 3x3 loops, split hot spots, portal
idling) are described above and are easy to rebuild from the `turn` line.

## Known weak spots / next ideas

- v5.7 is at parity with v5.4-v5.6 on fresh seeds. Screening gains (57-62%) did not transfer; validate every change on
  at least two fresh seed sets before shipping.
- Small open maps (small 16x8, Colosseum, arena, default_small) and devil are the weakest; `small` is 10/48 over three
  opponents. No single flag explains it.
- The ladder fixes are verified behaviour fixes that are net-neutral on win rate against our own older versions. The
  ladder opponents are different bots; the bugs they exposed (a1-a8) may matter more there than in self-play.
