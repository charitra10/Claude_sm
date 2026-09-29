# Strategy v5.6: what changed on top of v5.5, and why

v5.6 is `v5.5/main.cpp` plus five switchable modules (`F_*` flags in the v5.6 block at the top of `v5.6/main.cpp`),
one new sonar tag, and one inherited v5.5 flag switched off (`F_MANTLE_CASCADE`, section 5). Everything else is
still described by `strategyV_5_5.md` and the earlier docs. v5.5 is untouched and was the baseline. Raw results are
in `bench56/*.jsonl`.

**Bottom line:** on fresh seeds 9-16, all 18 maps, both sides (576 games, no errors or timeouts), v5.6 scores
**50.3%** (145/288) against v5.4 and **55.6%** (160/288) against v5.3. v5.5 on exactly the same games: 39.8% and
51.9%. Paired, v5.6 wins 114 games v5.5 lost and loses 72 that v5.5 won (sign test z = 3.1). Most of that gain comes
from switching v5.5's round-0 mantle cascade off (section 5). The four requested changes on their own, with the
cascade still on, were +10/576 over v5.5 (z = 1.0, noise), though +18.5/160 on the five maps they target (default,
dilemma, autarky, both queens).

## How it was measured

`seedbench.py`, paired seeds, both sides, every map in `maps/maps`, against v5.4 and v5.3. Screening on seeds 1-4,
per-module ablations on seeds 5-8 (vs v5.4), the final comparison on fresh seeds 9-16. v5.5 was re-run on each seed
set (hash 9bf63bce, unchanged). Behaviour was checked with `BOT_DIAG` copies (`DIAG` lines, `unswbc run -v`).

## Results (final build 7f7408db, seeds 9-16, 16 games per cell)

| map | v5.6 vs v5.4 | v5.5 vs v5.4 | v5.6 vs v5.3 | v5.5 vs v5.3 |
|---|---|---|---|---|
| Colosseum | 8/16 | 8/16 | 8/16 | 11/16 |
| arena | 8/16 | 8/16 | 9/16 | 9/16 |
| autarky | 14/16 | 7.5/16 | 16/16 | 11/16 |
| big_empty | 6/16 | 4/16 | 4/16 | 6/16 |
| default | 10/16 | 7/16 | 10/16 | 4/16 |
| default_small | 6/16 | 8/16 | 6/16 | 8/16 |
| devil | 8/16 | 6/16 | 9/16 | 6/16 |
| dilemma | 12/16 | 4/16 | 16/16 | 15/16 |
| help | 4/16 | 1/16 | 6/16 | 6/16 |
| portals | 5/16 | 2/16 | 4/16 | 2/16 |
| queen_of_spades | 12/16 | 10/16 | 12/16 | 12/16 |
| queen_of_spades_but_she_ages | 8/16 | 7/16 | 10/16 | 8/16 |
| schooltime | 7/16 | 10/16 | 7/16 | 11/16 |
| slithery_fight | 3/16 | 4/16 | 12/16 | 12/16 |
| small | 12/16 | 12/16 | 11/16 | 11/16 |
| stronghold | 8/16 | 4/16 | 5/16 | 4/16 |
| trauma | 6/16 | 4/16 | 8/16 | 6.5/16 |
| trophy | 8/16 | 8/16 | 7/16 | 7/16 |
| **all 18** | **145/288 (50.3%)** | 114.5/288 (39.8%) | **160/288 (55.6%)** | 149.5/288 (51.9%) |

Each map cell is about ±2 games. Raw: `bench56/final_v56.jsonl`, v5.5 on the same games `bench56/fin_v55.jsonl`.
Target maps: default 10+10 vs 7+4, dilemma 12+16 vs 4+15, autarky 14+16 vs 7.5+11, queen_of_spades 12+12 vs 10+12,
queen_of_spades_but_she_ages 8+10 vs 7+8 (v5.6 vs v5.5, against v5.4 + v5.3).

### Ablations (seeds 5-8, 144 games each vs v5.4, an earlier build: before the symmetry fix and the uniform-map guard)

| variant | score | | variant | score |
|---|---|---|---|---|
| v5.6 (mantle cascade on) | 60.5 | | no `F_MIRROR_SCOUT` | 60 |
| no `F_DRY_EVICT` | 61.5 | | no `F_SYMMETRY` (nor scouting) | 65 |
| no `F_REENTRY` | 54 | | mantle cascade off | **72.5** |
| no `F_CHOKE_GREEDY` | 61 | | | |

Every task module is within noise on its own (±6 games): none of them is shown to help or hurt overall. They do
what was asked (traces below). The one clear effect was the mantle cascade, then confirmed on seeds 9-16. Per map:
mirrored scouting cost big_empty (0/8 vs 4/8 without symmetry), which led to the uniform-map guard.

Dilemma ablations (vs v5.4, 16 games each): v5.6 0/16, no `F_MANTLE` 13/16, no `F_CHOKE` 8/16, no round-0
cascade 8/16, no `F_CHOKE_GREEDY` 5/16 (seeds 1-8); no `F_MANTLE_CASCADE` 11/16, no `F_MANTLE` 11/16, no
`F_MANTLE` and no `F_CHOKE_GREEDY` 8/16, v5.6 0/16 (seeds 9-16).

## Modules

### 1. Dry chamber eviction (`F_DRY_EVICT`) — default
Root cause: default's centre is a chain of nine 4x4 rooms joined by portals (outside → R00 → R12 → R20 → R01 → R11
→ R21 → R02 → R10 → R22 → outside). Their tiles spawn every 1-769 rounds (the centre room 1-255), so a room holds
about one pearl every 25-50 rounds. v5.5 stayed in a room while it was dense and a pearl was due within 60 rounds,
and left only after 10 rounds without food. Traced (seed 1): id 4 sat 10+ rounds in each room it entered, and 50
rounds for a single pearl in one of them.

- `Enclosure` now counts `pearls_now` (in view, or remembered for tiles out of view) and `due_soon` (tiles whose
  known timer fires within `DRY_HORIZON` = 24 rounds). Utility = `pearls_now + due_soon`.
- Utility 0 (nothing lying there and nothing due within 24 rounds, or nothing spawns at all): the dragon leaves at
  once through the chamber's portal (no 10-round wait). Alphas are exempt, as for the old idle rule.
- On the way out it drops its hold (`camping`, `camp_portal`, `chamber_portal`, `loop_portal`, the chamber tiles)
  and, instead of v5.5's "free" notice (which invited the next dragon in), reserves the portal in its own name:
  teammates stay out for `RESERVE_TTL` (30) rounds, about as long as the room stays dry. It shuns the portal itself
  for 60 rounds.

### 2. Greedy dead ends, split at the tip (`F_CHOKE_GREEDY`) — dilemma, autarky
Root cause: dilemma's 1-wide corridors (x = 14 and 17, three tiles that refill every round, then kelp) and autarky's
4-tile dead end. A 2-long dragon went in (v5.5's chokepoint rule allows that for live pearls), ate two pearls, and at
length 4 did its routine split: the 2-long child walked out, the 2-long head ate one more pearl and died at the tip
as a 3 (a 3-long dragon cannot split: the parent would be left with 1).

- When our head is in a dead end of the map itself (`sealed_pocket(..., static_only)` ahead of the head, not a
  portal chamber) and pearls lie ahead (now, or due within 4 rounds): no routine split, no back-harvest split, and
  the step goes to the nearest pearl ahead (`choke_step`).
- At the tip no move survives, so v5.4's rescue split fires on the last turn the dragon can act: the head keeps 2
  and dies there, the rear L-2 is born on the tail facing out of the corridor.
- Nothing left ahead, or no pearl ahead that the head can still reach (in a tree-shaped maze pocket the branches
  behind our neck are gone): split L-2 at once (`chokefinal`); walking on only drags the tail deeper.
- Forced to the mouth of a dead end with pearls in it (every other move is gone), a dragon goes in instead of
  splitting at the mouth (`chokeenter`): it then keeps L + pearls - 2 instead of L - 2.

Traced (dilemma seed 1): `chokehold` 2 → 3 → 4 → 5 at the tip, then `rescue ... 5`: the child is 3 long (v5.5: 2),
and no 3-long head dies at a tip.

### 3. Re-entry after a forced exit (`F_REENTRY`) — queen_of_spades
Root cause: the queen chamber's portal edge is internal to the chamber. A camper that runs out of moves takes the
portal (v5.4's forced-portal fallback), comes out on the far side as an ordinary dragon, and walks off.

- A camper that crosses its chamber's portal only because nothing else is left (the forced fallbacks, or a scorer
  choice with a single legal move) is marked `transit_portal`.
- On emergence it is `reclaim_portal` for `REENTRY_TTL` (12) rounds: first priority after the rescue split, it walks
  round the partner edge and crosses back (a snake cannot turn 180 degrees on the spot; the tightest loop is 4
  moves). `reentry_move` picks the crossing, from either side of the partner edge, that lands on a tile of the
  chamber it camped in (tiles saved while camping) and not on its own body (the last L head positions).
- Back inside, its camping record (`camp_since`) is still valid, so the one-camper rule treats it as the same
  camper.

Traced (queen maps, seeds 2-4, 6 games): 37 forced exits, 25 re-entries within 4-11 rounds, 5 timeouts, the rest
died or the game ended.

### 4. Map symmetry (`F_SYMMETRY`) and mirrored scouting (`F_MIRROR_SCOUT`), new tag `SCOUT_TAG` 8185
Detection (every dragon, until resolved, then never again): candidates are 180-degree rotation, x → W-1-x,
y → H-1-y and (square maps) x ↔ y. The docs allow only the first three; every map in `maps/maps` is exactly one
of them (`xy` = rotation, `y` = x → W-1-x, arena = y → H-1-y; Colosseum and big_empty are all of them).
- Each visible tile is compared with the remembered image of itself under each live candidate: wall / open /
  portal on every edge, and "never spawns", must match. Mirrored tiles share one pearl countdown, so two timers
  that were both still running must name the same round. Any mismatch kills the candidate for good.
- Evidence: +1 matching kelp edge, +2 matching portal edge, +4 a matching running timer (countdown >= 3) — each tile
  counts once per candidate, and only when its image differs from its image under every other live candidate.
  (First version: trophy's middle row maps to the same tiles under rotation and the x mirror, so both collected the
  same evidence and 4 of 42 dragons resolved rotation on an x-mirror map.) Resolved: score >= 8 with two timer
  matches and no other live candidate with any, or the only live candidate with score >= 3.
- Timers are shared only under the map's declared symmetry, not every symmetry of its layout: Colosseum, arena and
  big_empty are symmetric every way on paper, and timers still single out rotation there.
- Rotation and the mirrors also hold about the wrap seam, so dragons near the map edge see mirrored pairs from
  round 0: on default, dilemma and autarky dragons resolve in rounds 0-9.
- The resolved type is shared: every `SCOUT_TAG` packet carries it, and a dragon that knows it sends a type-only
  packet on one beam every 6 rounds. A receiver adopts it unless its own evidence contradicts it.

Hotspots (sender): the dense chamber we camp in when it pays now (`pearls_now + due_soon >= 2`), or at least 5 pearls
in view and 2.5x the dragon's running mean of pearls in view. Guards:
- distance: the mirror point must be at least 0.35 * min(W, H) away (a spot near the symmetry axis maps into its own
  enclosure);
- saturation: no reports while the running mean of pearls in view is 4 or more (help, where every tile respawns
  every 10-20 rounds), and none on a map that looks uniform (100+ tiles seen, none of them dead, under 3% kelp:
  big_empty and help). A pile of pearls there is chance, not a place; the first version cost big_empty 0/8 vs 4/8.
- flood control: one new spot per dragon per 10 rounds, none within 6 tiles of a spot already known, relays on one
  beam every third round, a new spot on two beams for two rounds.

Dispatch (receiver): a non-alpha dragon of length 2-3 that is not camping, resident, evacuating or feeding, and
has nothing to eat in view, walks to the nearest unclaimed spot (claims it on sonar, gives up after 2 * distance +
10 rounds).

### 5. Inherited: the round-0 mantle cascade is off (`F_MANTLE_CASCADE = false`)
Not one of the four requested changes, but the largest effect found. v5.5 is weaker than v5.4 overall (38.2% on
all maps, seeds 1-4) and loses dilemma 0/8. Ablating every v5.5 and v5.6 flag on dilemma (16 games each vs v5.4)
pointed at one: with `F_MANTLE_CASCADE` off dilemma went 0/16 → 11/16 (seeds 9-16) and 13/16 with all of
`F_MANTLE` off (seeds 1-8). On the round-0 cascade the alpha role was handed from child to child down the chain;
without it the primary keeps the role, as in v5.4. The rest of `F_MANTLE` (handover on every later L-2 split) stays
on. Paired on the same fresh games (seeds 9-16, both sides, all maps):

| | vs v5.4 | vs v5.3 | pooled |
|---|---|---|---|
| mantle cascade on | 128/288 (44.4%) | 146/288 (50.7%) | 274/576 (47.6%) |
| mantle cascade off | **147/288 (51.0%)** | **154/288 (53.5%)** | **301/576 (52.3%)** |

It also won on the two earlier seed sets (+2/144 on seeds 1-4, +12/144 on seeds 5-8, vs v5.4).

## Sonar protocol (layout unchanged, one new tag)

| aid | meaning | position field | alen |
|---|---|---|---|
| 8185 `SCOUT_TAG` | a mirrored hotspot, or only the map symmetry | spot (x, y) | sym (2 bits) \| value (5 bits, 0 = no spot) \| claimed (bit 7) |

The low 20 bits (enemy alpha, kamikaze bucket) keep their meaning, as in every other tag. 8185 is below the
existing tags and far above any dragon ID seen in a game (under 2000 on slithery_fight). The `DIAG collision`
check (any alpha ID >= 8185 reaching `remember_alpha`) never fired.

## Tracing
`DIAG` lines: `dryout`, `chokehold`, `chokefinal`, `chokeenter`, `forcedout`, `reclaimed`, `reclaimfail`, `symres`,
`symheard`, `symbad`, `scoutsend`, `scoutrecv`, `scoutclaim`, `scoutarrive`, `scoutgiveup`, `collision`, plus
v5.5's; `camp` now also prints `dry`, `now`, `due` and `size`. CPU in the judge sandbox (slithery_fight, seed 1):
p50 4.6M, p99 5.0M, max 5.3M points per turn (budget 100M), the same as v5.5.

## Known weak spots / next ideas
- The four task modules are each within noise over 144 games; a larger per-module ablation (500+ games) is needed
  to tell whether any of them earns its keep beyond the target maps.
- slithery_fight vs v5.4 (3/16) and schooltime (7/16 vs v5.5's 10/16 against v5.4) are the weakest maps.
- A snake cannot turn 180 degrees, so "immediate re-entry" is a 4-move loop round the partner edge; about 1 in 7
  forced exits times out (the loop is blocked or the chamber is occupied on our side).
- Symmetry could also fill in memory: walls and timers of every tile's mirror image are known once it is resolved.
  Not done (it changes routing everywhere).
