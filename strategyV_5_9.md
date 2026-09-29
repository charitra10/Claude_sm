# Strategy v5.9: ladder replay analysis and the fixes it led to

v5.9 is `v5.8/main.cpp` plus the six requested changes of the v5.9 brief, which came from the ladder battle
`battle-M488787-replays/` (10 games of v5.8 against one opponent, 2 won). Each change sits behind an `F_*` flag in the
"v5.9 modules" block of `v5.9/main.cpp`; v5.8 is untouched. Raw results are in `bench58/N1_*`, `N2_*`, `val59.jsonl`.

**Bottom line:** v5.9 is at **parity with v5.8** and no stronger against the older versions. On fresh seeds 601-608,
all 14 ladder maps, both sides (896 games, no errors or timeouts) it scores **51.8%** (116/224) vs v5.8, 69.2% (155/224)
vs v5.6, 79.0% (177/224) vs v5.5, 68.3% (153/224) vs v5.4, each about ±3. (v5.8 on seeds 401-408: 74.1 / 77.7 / 72.3.)
The six items fix the specific behaviours in the replays, but none of them moves self-play results beyond noise except
early feeding (+5/192), exit clearing (+3) and the chamber-exit fix (+4); the requested residency change measured -9 and
was reverted (item 5). Self-play against our own old versions does not reproduce the ladder opponent that beat v5.8 8-2.

## Screening (seeds 501-508, 12 ladder maps without help / big_empty, 192 games each vs v5.8)

| build | score | notes |
|---|---|---|
| all six as first written (residency 1) | 93 | schooltime 4/16 |
| - farm split and quick farms | 94 | trauma 13 vs 7, portals 9 vs 12 |
| - early feed | 88 | |
| - relaxed kamikaze | 96 | |
| - enemy chambers | 93 | |
| - exit clearing | 90 | |
| residency back to 2 | **102** | schooltime 9, slithery 14, stronghold 11; portals 8 vs 12 |
| candidate: residency 2, farm split non-alpha only, small-dragon room check, chamber-exit fix | 93.5 | |
| - relaxed kamikaze | 94.5 | |
| + farm split for alphas | 97.5 | |
| - small-dragon room check | 95 | |
| - chamber-exit fix | 89.5 | |

Each row is about ±7 games; only the residency change stands out.

## Replay tooling

`.replay` files are packed Cap'n Proto; the schema was read off the replay viewer's generated code. `replay_tools/`
decodes them and rebuilds every dragon's body turn by turn (checked on three games: every pair of consecutive segments is
joined by a legal step or portal crossing). Turn numbers match the viewer's counter. See `replay_tools/README.md`.

## What the ten ladder games show

We are team A in all ten (only our dragons set indicator strings). The builds' sonar carries the 8183 farm tag: v5.8.

Our deaths by cause (what the head moved into), from `replay_tools/killers.py`:

| map | result | teammate | enemy | head-on, unattributed | kelp | self | deliberate feed |
|---|---|---|---|---|---|---|---|
| Autarky | L | 1 | 22 | 22 | 24 | 0 | 3 |
| Default | L | 9 | 33 | 35 | 2 | 0 | 2 |
| Devil | L | 4 | 15 | 14 | 0 | 5 | 0 |
| Portals | L | 241 | 0 | 51 | 160 | 33 | 45 |
| Prisoners Dilemma | W | 3 | 5 | 2 | 16 | 12 | 1 |
| Queen Of Spades | L | 2 | 10 | 19 | 0 | 1 | 6 |
| Schooltime | L | 16 | 31 | 37 | 8 | 0 | 5 |
| Slithery Fight | W | 513 | 41 | 24 | 266 | 69 | 115 |
| Trauma | L | 103 | 15 | 13 | 75 | 12 | 34 |
| Trophy | L | 2 | 39 | 36 | 0 | 0 | 0 |

Two kinds of loss: on the open maps the opponent simply wins the fights (enemy-caused deaths dominate); on the
corridor / chamber / farm maps (portals, slithery_fight, trauma) our own dragons kill each other far more than the enemy
does (894 friendly-fire deaths over the ten games against ~211 enemy-caused). Kelp deaths are mostly the 2-long heads left
at dead-end tips by rescue splits (deliberate).

## The six items

1. **Farmers not splitting (autarky (35,0)).** The pocket was farmed by dragons 5-8 long, often alphas: the rear child
   of each tip split is born 4+ long, and v5.7's 8-round split cooldown for such rear children (and the alpha's growing
   phase) kept it long. Also only 2 of that pocket's 3 spawners refill every round (the third is [1,5]), so v5.8's farm
   detector ignored it. `F_FARM_SPLIT`: a non-alpha out of a dead end it farmed within 20 rounds splits 2 while longer
   than 4, cooldown and cap notwithstanding; `F_QUICK_FARM`: spawners refilling within 5 rounds count for farms.
   Splitting alphas too (`FARM_SPLIT_ALPHA`) stunts a farming apex (trauma 7 vs 13/16 in one run; noise in the other).
2. **Early consolidation (portals).** The replay confirms the premise: 0 of our deaths on portals were caused by the
   enemy (they sit in the next chamber behind kelp). `F_EARLY_FEED`: every dragon remembers the last round an enemy head
   could reach it within 3 steps (`danger() >= 280`, walls and portals in view respected); after `PEACE_WINDOW` = 150 calm
   rounds, from round `EARLY_FEED_ROUND` = 250 on, it starts feeding. A newborn starts with no calm rounds. Measured +5/192.
3. **Relaxed kamikaze.** `F_EARLY_KILL2`: a sure kill on an enemy 1-2 shorter than us, only with a teammate head within
   2 of the collision and no other enemy head within 3 of it (sprints keep the old rule). Measured -3 and -1 of 192
   (noise); shipped on as requested, easy to switch off.
4. **Losing small portal chambers (queen_of_spades), `replay_tools/occupancy.py`.** Our home chamber (bottom) was held
   rounds 9-131 by one camper, then lost when the camper, 2 long, walked into a chamber corner whose other exit its own
   split child blocked, then into a 1-tile nook, grew to 3 (too short to split out) and died; the enemy held it for the
   remaining 145 rounds. The enemy's chamber (top) was entered by us 6 times, each time dying inside within 1-5 rounds.
   On portals the 2x2 chambers change hands every 5-7 rounds by design (the loop), and our deaths inside them were
   mostly our own dragons meeting. Reproduced locally; fixes:
   - `F_TRAP_SMALL`: the room-aware move scoring of v5.8 (long dragons only) now covers short dragons, needing room for
     max(len + 2, 4) tiles;
   - `F_EVICT_EXIT`: a dragon leaving a crowded chamber could not step onto its portal tile (`escape_count()` lacked the
     "leaving" exemption `forward_escape_count()` has, so the tile looked like a dead end); it circled for 6 rounds and
     then killed itself on purpose;
   - `F_ENEMY_CHAMBER`: a chamber we saw an enemy come out of or go into is left alone for 100 rounds.
5. **Schooltime turn 73, (14,20) skips the portal next to it.** That dragon was a *portal resident*: it had come out of
   another portal where things spawn, and v5.2's residency rule bars residents from every portal except their own (it
   keeps chamber campers home); v5.7 frees a resident only after 8 idle rounds with nothing due within 10. The fix
   (residency only inside small chambers) was measured: 93 vs 102/192 against v5.8 (schooltime 4 vs 9, slithery 9 vs 14,
   stronghold 7 vs 11; only portals gained). So the behaviour is the price of a rule that pays; kept (`PORTAL_RESIDENCY`
   = 2, the analysis in the comment).
6. **Queen turn ~637, a dragon beside the portal kills a teammate coming out.** Our camper split child 21 inside the
   bottom chamber; children born in a chamber leave through its portal, so 21 came out at (3,3) and foraged in the tiny
   pocket right beside that exit; at round 67 it stepped onto the exit tile (4,3), and at round 68 the next chamber child
   (23) came out onto its neck. Neither can see across a portal. `F_EXIT_CLEAR`: the outer tiles of a small chamber's
   portal are for crossing only: -150 in the move scorer (-50 with a pearl on it), no direct step onto them, and no pearl
   targets there while someone is known inside. Measured +3/192.

## Known weak spots / next ideas

- Friendly fire on corridor and chamber maps (see the table): the biggest remaining leak, not addressed yet beyond items
  4 and 6.
- On the open maps the opponent in this battle wins the fights outright; self-play benchmarks against v5.4-v5.8 do not
  reproduce that opponent.

---

# v5.9, second brief (ladder replays M510524-M514761) and the kamikaze brief (PD, Default)

The same `v5.9/` directory now carries two more rounds of changes, marked `v5.9b` and `v5.9c` in the code. Every item is
behind a flag in the "v5.9 second brief" block (and the kamikaze block after it) at the top of `v5.9/main.cpp`. The
v5.9 of the first half of this file is kept in the benchmarks as "old v5.9". Raw results: `bench59b/*.jsonl`.

**Bottom line:** the current `v5.9/` (both briefs, the kamikaze work below, flank approach) wins 64 of 96 games against
v5.8 over all 12 ladder maps (seeds 21-28, one replay per game), against 54 for the same build without the flank
approach; on the 5-map subset (autarky, default, devil, dilemma, trophy) old v5.9 won 15 of 40 and the current build
with flank 28 of 40. Against v5.7 (192 games, seeds 701-708) it scored 154 (80.2%) before the flank work; that screen is
at its ceiling for this bot. **Fresh-seed validation (seeds 801-808, all 14 ladder maps, both sides, 224 games each): the final v5.9 scores 63.4%
(142/224) against v5.8 and 78.1% (175/224) against v5.7; the v5.9 this work started from scores 50.0% (112/224) against
v5.8 on the same games** (+30 games, one sd about 7.5). Raw results: `bench59b/F_final.jsonl`, `F_base.jsonl`. CPU: max
8.7M of the 100M points a turn on big_empty (sandbox).

Trophy note: v5.8 against itself on trophy wins 24 of 32 as team B. The cause is ID-based jitter in the exploration
waypoints: B's bottom spawn keeps to the row of trophy's only portal (on the symmetry line, into the central cup) and finds
it by round 8; A's turns north. Only trophy has a portal on its symmetry line, so no trophy-specific fix was made.

## What the replays showed, and what changed (v5.9b)

| # | Replay | What happened | Change | Flag | Ablation (192 games vs v5.7, seeds 701-708) |
|---|---|---|---|---|---|
| 1 | (request) | Dragons outnumbered on the enemy's half stayed | Round-0 dragons record the spawn point and share it (`SPAWN_TAG` 8180); the enemy's spawn is its mirror image. A dragon ≥4 long with ≥2 enemy heads at least as long as it within 5 (more than teammates) on their half walks home for 15 rounds | `F_RETREAT`, `RETREAT_MODE = 2` | Broad version (any enemies, any length): −2 (default 5/16 vs 10/16 without it). Narrow version: 147, same as off |
| 2, 3 | (request) | Dragons came out of portals onto dragons (ours or theirs) | Before crossing, one beam goes alone through the portal (`PROBE_TAG` 8181); next turn's echo counts say ally / enemy / clear. A dragon on the line within 3 tiles of the far side (known kelp closes it) blocks the crossing: ally → the chamber is taken (task 3), enemy → held by them, split L−2 if forced out (task 2). Teammates the beam hits keep off the exit tile | `F_PORTAL_PROBE`, `F_PROBE_SPLIT` | **Off: −13.** Dropping the exit-lane penalty with probes on: +2 (noise, penalty kept) |
| 4 | M510524 round 112-114 | Dragon 112 gave up a pearl to a teammate 3 tiles away through kelp (5 moves; 112 needed 4). Its own route search also treated its tail as a wall | Who is nearer counts moves (walls, facing) for claims, strike protection and early kills (`moves_of`); our BFS crosses our own segments once they have moved off | `F_TRUE_MOVES`, `F_BODY_FREE` | Body freeing off: **−11**. Move counts off: +5 (mixed by map; kept) |
| 5 | M510533 round 12 | Dragon 5 traded head-on because a teammate was "3 away" from the drop; it faced away and never came | as 4: protection by moves, and the protector must be within 4 moves | `F_TRUE_MOVES` | (as 4) |
| 6 | M510532 rounds 45, 106, 154, 212, 276 | The same corridor (x = 11) five times: a rescue child born at the mouth, a teammate on the only exit (12, 8), so it split again, and so did its child | A fresh split child whose own child would have no way out does not split again; teammates keep off the mouth of a dead end a teammate is in (no pearl exemption) | `F_SPLIT_ONCE`, `F_MOUTH_CLEAR` | 0 and −1 (neutral) |
| 7 | M510532 round 418 | Our last dragon (26-long alpha) stepped next to a 2-long enemy with a pearl between them; it ate the pearl and sprinted into our head | Enemy sprint reach counts pearls on the way (for late-game alphas); our strikes may sprint through pearls too | `SPRINT_REACH = 1`, `F_PEARL_SPRINT` | Pearl sprints off: −4. Reach for everyone: −4; reach off: +3 (kept for late alphas as asked) |
| 8 | M510527 | More total length when feeding started, still lost on longest dragon | Feeders grab a pearl at even length (ceil(L/2) drop grows); earlier feeding tested | `F_FEED_EFF`, `FEED_SHIFT` | Parity grab off: −2. Feeding 30 rounds earlier: −2 (not adopted). Chamber-map feeding stays a weak spot |
| 9 | (request) | Residency kept dragons from using portals | Mode 4: a dragon that comes out onto wide open ground with no pearl in view and none due within 40 rounds is free to use another portal (not the one it came through) | `PORTAL_RESIDENCY = 4` | Mode 3 (due within 10): −5 vs mode 2 (schooltime's rooms spawn slowly). Mode 4: +1 vs mode 2 |
| 10 | M514761 round 13 | Our kamikaze stepped where the 11-long enemy kept two safe moves | Hunters take the step that leaves the enemy the fewest safe replies (≤1); long non-alphas (≥6) weigh enemy reach and split away | `F_HERD`, `F_LONG_GUARD` | Herding off: +1 (neutral). Guard off: **−8** |
| 11 | M510539 round 5 | Two teammates entered the x = 35 corridor from opposite ends | A dragon in a straight one-way corridor beams `LANE_TAG` (8182) ahead; nobody enters from the far end; vision check for bends | `F_LANE` | Off: −5 |
| 12 | M510540 round 4 | A 2-long dragon skipped a pearl: the enemy's head on the edge of view made it "at least 4 long" | Enemy lengths seen whole are remembered; the isolation rule leaves 2-long skirmishers alone | `F_ENEMY_LEN`, `ISO_MIN_LEN2 = 3` | Off: −1 (neutral) |

## The kamikaze brief (v5.9c)

`replay_tools/exchanges.py` measures every head-on collision: who moved, the lengths lost (at the start of the turn), and
which side ate the pearls the two bodies dropped. Over 17 ladder replays:

| Head-on exchanges | Count | Our length lost | Their length lost | Drops eaten us / them | Net for us |
|---|---|---|---|---|---|
| We moved into them | 169 | 411 | 498 | 345 / 164 | +268 (+1.6 each) |
| They moved into us | 499 | 1406 | 1294 | 409 / 1053 | −756 (−1.5 each) |
| Our own dragons colliding | 187 | 1018 | – | 517 / 27 | −1018 |

Our strikes pay but are rare; theirs are three times as frequent and win on the drops (their teammates stand by). So:
- `F_EXCHANGE` (PD round 21): any non-alpha strikes an enemy head it can reach this turn (one step, sprint, or a sprint
  paid by pearls) when the team's expected gain is ≥ `EXCH_MIN` (0.5): enemy length − our length − half the pearls eaten,
  plus our expected share of the drop minus theirs (our nearest teammate against their nearest other dragon, by moves).
  Checked before any split.
- `F_SUPPORT` (PD round 22): a 2-3-long hunter moves beside a teammate an enemy can ram this turn (sprint reach, pearls
  included) when our side would otherwise lose the race to the drop, instead of chasing another enemy.
- Local check (10 map-seeds vs v5.8): we start 239 exchanges (old v5.9: 201) and the enemy starts 157 (220); the exchange
  balance moves from +18 to +43.

| Build (192 games vs v5.7, seeds 701-708) | Score |
|---|---|
| before the kamikaze brief | 147 |
| **exchange + support** | **154 (80.2%)** |
| without exchange | 150 |
| without support | 149.5 |
| guard from length 4 (instead of 6) | 151 |

The Default item of the kamikaze brief (a dragon walking away from a portal) was withdrawn: in `Default.replay` both
dragons that passed (8, 7) took the portal.

## Follow-ups to the kamikaze brief

All measured on 96 games against v5.8 (12 ladder maps, seeds 21-28), one replay per game; the v5.7 screen no longer
separates builds.

| Build | Wins /96 | Our strikes n / net | Their strikes n / net | Exchange balance |
|---|---|---|---|---|
| exchange strikes + support, `EXCH_MIN` 0.5 | 54 | 2526 / +3403 | 1513 / −2582 | +821 |
| same, `EXCH_MIN` 1.5 | 53 | 2299 / +3621 | 1859 / −3032 | +589 |
| **+ flank approach (`F_FLANK`), 0.5** | 63 | 2568 / +3293 | 1435 / −2470 | +823 |
| + flank, 1.0 | 55 | 2529 / +3477 | 1592 / −2690 | +787 |
| + flank, 1.5 | 55 | 2204 / +3543 | 1746 / −2964 | +579 |
| **+ flank + skip outnumbered strikes (`F_EXCH_OUTNUM`): shipped** | **64** | 2597 / +3451 | 1486 / −2593 | +858 |
| + flank + learned drop share (`F_DROP_LEARN`) | 58 | 2507 / +3352 | 1436 / −2325 | +1027 |
| + flank + hunters eat drops first (`F_COLLECT`) | 60 | 2505 / +3041 | 1461 / −2534 | +507 |

- **Flank** (the user's idea): kamikazes come down the lane beside an enemy's path and ram from the side instead of face
  to face (where the enemy just turns away). The largest single gain of the round.
- **Threshold:** 1.5 looked better on the 5 open maps (+1.34 a strike against +0.98) but lost over all 12: on crowded maps
  (schooltime, slithery_fight) our strikes are worth +1.9 to +3.3 each and 1.5 skips paying ones. 2127 logged strikes: every
  band of predicted value paid in reality (even below 1.0: +0.30), except strikes where their heads outnumbered ours near
  the collision (27, net −3): hence `F_EXCH_OUTNUM`.
- **Why the enemy struck us more with 1.5:** more of our dragons stayed alive (220k against 170k dragon-turns in 40
  games); the strike rate per dragon-turn was the same (4.4 per 1000).
- **The drop-race model is map-dependent:** when our collector was nearer we ate 82-88% of the drop on default,
  schooltime and the queen maps, 72% on trophy, 53% on devil. Learning it online (`F_DROP_LEARN`) gave the best exchange
  balance but fewer wins; off for now, the lead to follow (a team-wide estimate over sonar).
- **Dodge** (`F_DODGE`, the defensive mirror of exchange strikes): a heavy version made dilemma 8/16 (16/16 without);
  shipped light (weight 15). Stronger forager dodge and ambush dodge: neutral, off.

## Tools

- `replay_tools/exchanges.py FILE...` (`-q` for totals only): the head-on exchange table above, per replay.
- `replay_tools/refresh_doc_lines.py`: refresh the `main.cpp:NNNN` references in `what_the_code_does.md`.
- Benchmark pitfalls met this round: parallel `unswbc run` games on one map finishing in the same second overwrite each
  other's replay (use `-o` with a unique file per game), and parallel runs on one bot directory race on its build cache
  (build once first). `seedbench.py` does both right.
- Turn numbers in the viewer run about 6 ahead of `sim.py`'s turn counter in these replays; rounds and coordinates match.

## Known weak spots after v5.9c

- Feeding on chamber maps (portals): about 20 dragons die "trapped" in 2x2 chambers far from the apex around the start
  of feeding; not solved.
- Friendly head-on collisions (−1018 over the 17 ladder games) remain the largest single leak.
- Portal probes only act firmly on chamber-like far sides (an echo from open ground can come from far away).
