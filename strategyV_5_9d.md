# v5.9d: do the v5.2 modules do what they were built to do?

The v5.2 modules (`F_HAZARD` ... `F_ALPHA_MEMORY`, top of `v5.9/main.cpp`) were kept or dropped on win rate alone
(`strategyV_5_2.md`). This audit asks a different question for each one: does the behaviour it was written for actually
happen, measured on the full board of the replay (ground truth) and on the bot's own `DIAG` trace? Where a module did not
do its job, the cause was found and fixed in `v5.9/main.cpp` behind a new switch (block "v5.9d" after the v5.9c flags),
and measured again. Tools: `audit52/` (see its README). Per-game numbers: `bench59d/audit/*.json`.

## Method

- **Behaviour runs:** a `BOT_DIAG` copy of v5.9 (or of a variant) against plain v5.8, 12 ladder maps (the 14 without `help`
  and `big_empty`), seeds 1-2, both sides: 48 games per variant, one replay + trace each. For every v5.2 module, one run
  with that module switched off (`audit52/variant.py ... F_X=false`).
- Games are close to deterministic for a given seed (a variant that compiles to the same binary reproduced the baseline map
  by map), so on/off runs are paired. Metrics that depend on chaotic crowd dynamics still swing a lot: the number of
  follow-ins on slithery_fight ranged from 0 to 103 across five runs with identical hazard code.
- **Win rate:** `seedbench.py` with plain builds on fresh seeds (see the end).

## Results per module

48 games each (baseline v5.9 = "on"; the module switched off = "off").

| module | intended behaviour | measured (on / off) | verdict |
|---|---|---|---|
| `F_HAZARD` | a dragon trapped in a pocket announces its mouth; teammates don't follow it in | 6983 hazard episodes, only 1322 (19%) at the mouth of a real pocket; in 1110 (16%) the owner later walked back out past its own mouth (never trapped). Follow-ins 263 / 318, deaths in pockets 3438 / 3294 | **mostly false alarms** -> fixed (`F_HAZARD_CLOSED`) |
| `F_PORTAL_EVICT` | leave barren / crowded chambers at once | barren-chamber stays 86 / 455 visits, 350 / 9502 rounds, 7 / 116 deaths | **works** |
| `F_PORTAL_CLEAR` | don't loiter on portal tiles (blind exits) | turns idling on a portal tile 2.14% / 2.37% of turns (-10%); deaths coming out of a portal into a teammate 183 / 188 | works weakly; the collisions it targets are not caused by loitering (see below) |
| `F_PORTAL_RESERVE` | at most one dragon per small chamber | entries into a chamber a teammate has held 8+ rounds: 125 / 164 (default 43 / 97, queen_of_spades 51 / 30); 94% of such entrants never heard a reservation | **partly works** (see below; fixes tried, not shipped) |
| `F_DISPERSE` | spread over parallel lanes, no single file | friendly body segments beside a foraging head 0.223 / 0.242 per turn (-8%); single-file turns 4.8% / 5.4% (-11%) | works, weakly |
| `F_CYCLE` | detect periodic movement and break out | turns in a periodic loop with nothing eaten 488 / 477; loose loops 498 / 415; short loops (last 8 tiles with period <= 4, nothing eaten) 563 / 519. Fires 170 times, 16% on feeders circling on purpose | **redundant** with `F_CYCLE2`; its ablation was also coupled (fixed) |
| `F_FEED_ADAPT` | short feeders wait for a smaller pile than long ones | holds by feeders of length <= 3: 474 / 245; of length 6+: 27 / 69. Delivered pearls eaten by the apex they were meant for 52% / 49% | mechanism works; no effect on its goal |
| `F_FEED_BACKOFF` | hold (circle 3-5 tiles off) while the apex has a pile | its ablation was a **no-op** (see below). True ablation: 0 holds; delivered pearls eaten by the target apex 52% / 50%, by enemies 14 / 14 | mechanism works; no effect on its goal |
| `F_ALPHA_MEMORY` | the alpha returns to pearls it saw | of 9707 trips the alpha ate the pearl on 209 (2%); a teammate ate it first on 3305 (34%); 39% dropped after ~2 rounds; 16% already gone at the start. Alpha pearls per game 213 / 213 | **does not work** -> fixed (`F_AMEM_FIX`) |

### F_HAZARD: false alarms, and the fix

`sealed_pocket()` treats visible dragon bodies as walls, and returns "trapped" at once when the head has no free tile ahead.
So a dragon stepping into a 1-wide corridor behind a teammate (portals' x = 14/15 columns: open at both ends) announced a
hazard, and everyone who heard it treated that corridor mouth as a trap for 12+ rounds (BFS skips it, the scorer charges
-600, `F_CHOKE2` still lets them in for 2+ pearls). On portals 534 of 553 hazard episodes (97%) were of this kind.

A first fix (the map's walls alone must seal the pocket: `sealed_pocket(..., static_only)`) also dropped useful hazards:
slithery_fight's top-edge pearl rooms have an inner loop, so walls alone never "seal" them, yet a room full of bodies is a
real trap while it is full. **`F_HAZARD_CLOSED`** keeps the dynamic test and adds `closed_behind(mouth, dir)`: the region
behind the mouth, walls only, must never lead back out except through the mouth (no other exit, usable portal, unknown
edge, or more than 48 tiles); loops inside are allowed.

| | v5.9 | walls must seal (tried) | **closed region (shipped)** | hazard off |
|---|---|---|---|---|
| hazard episodes | 6983 | 1605 | 1575 | 0 |
| at a real pocket mouth | 1322 (19%) | 1203 (75%) | 1245 (79%) | - |
| owner walked back out | 1110 (16%) | 41 | 23 | - |
| hazard packets received | 201690 | 88599 | 91102 | - |
| deaths in pockets | 3438 | 3347 | 3270 | 3294 |
| wins / 48 | 25 | 30 | 29 | 30 |

Follow-ins are too noisy to rank the variants (see Method). What the numbers do show: the fix removes almost every false
alarm and more than half of the hazard traffic on sonar (beams freed for other packets) without losing the hazards at real
pockets, and switching the hazard off entirely costs nothing measurable either.

### F_ALPHA_MEMORY: chasing the swarm's pearls, and the fix

`alpha_memory_target()` had none of the checks the generic memory (`remembered_pearl_target`, v5.7) has: it went for
pearls a nearer teammate was about to eat, ignored whether a way there was known, and re-chose every turn (the flip-flop
`F_REM_COMMIT` fixed for the generic memory). **`F_AMEM_FIX`**: when a remembered pearl is in view after this turn's BFS,
it is marked claimed if a teammate is nearer (`claimed()`); claimed pearls and pearls with no remembered route (or a route
more than 3 steps longer than straight) are skipped; the chosen pearl is kept until it is gone, claimed, reached or its
walk time + 3 rounds is up.

| | v5.9 | `F_AMEM_FIX` | memory off |
|---|---|---|---|
| trips | 9707 | 1503 | 0 |
| alpha ate the pearl | 209 (2.2%) | 162 (10.8%) | - |
| a teammate ate it first | 3305 (34%) | 410 (27%) | - |
| rounds per trip | 2-3 | 6.4 | - |
| alpha pearls eaten | 10253 | 10347 | 10227 |
| longest dragon (sum of 48 games) | 1570 | 1726 | 1690 |
| wins / 48 | 25 | 31 | 28 |

Still, most trips do not pay: what is left is mostly pearls far away (mean 11 tiles) that someone else ate out of view.

### F_PORTAL_RESERVE: reservations that nobody hears

With reservations on, 24% fewer entries into a settled chamber (125 vs 164), almost all of it on default (43 vs 97). But
94-96% of the dragons that still walked in had never heard a reservation for that portal. Two causes:
1. **Sonar stops at kelp.** A camper renews its reservation every 10 rounds on one rotating beam; from inside a walled
   chamber that beam nearly always ends on the chamber wall. Only a beam that runs straight through the portal gets out
   (sonar crosses portals).
2. **Chambers with two portals.** `enclosure_at()` records the first portal edge it finds. Default's nine chambers each
   have two, so only one was ever reserved; newcomers came in through the other (and the recorded one flipped between the
   two depending on where the flood started).

Tried: `F_RESERVE_AIM` (a camper sends the reservation on a beam that leaves through its portal whenever one does, every 3
rounds) and `F_RESERVE_ALL` (renew every portal of the chamber in rotation). Aiming raised reservations heard by 31% and
halved settled-chamber entries on queen_of_spades in a 4-game test, but over 48 games it moved them only 125 -> 120;
reserving every portal made default worse (90 vs 43 in 4 games). Both are in the code and **off**. On default most
"entries into an occupied chamber" turned out to be dragons walking the chain of dry chambers (`leave 1 dry 1` in the camp
trace), not campers being crowded, which is why only entries into chambers held 8+ rounds are counted.

### F_PORTAL_CLEAR: the collisions come from traffic, not loiterers

Portal-tile idling drops 10% with the module, but deaths from coming out of a portal into a teammate do not (183 vs 188;
117 of them on portals). On portals, 59 of 117 victims hit a teammate in `portal` mode: two of ours using the same portal
pair from opposite sides at once. `F_PORTAL_CLEAR` exempts dragons that are using the portal, by design, and
`F_PORTAL_YIELD` (v5.8) only sees the other side when it is in view. Not changed here.

### F_CYCLE: redundant, and its ablation measured two modules

`F_CYCLE2` (v5.3) catches loops of period <= 7 (<= 7 distinct tiles in 16 turns, idle > 8). `F_CYCLE` needs only two
periods, so it fires a few turns earlier on the same loops, and also on feeders circling the apex on purpose (28 of 170
firings), which `F_CYCLE2` exempts. Loop time is unchanged without it, even counting short loops of 8 turns. Its ablation was also wrong: the scorer's
breakout terms (noise, the -60 on recent tiles, the pull toward the breakout point) were guarded by `F_CYCLE` alone, so
switching `F_CYCLE` off also switched off `F_CYCLE2`'s breakout steering. Now guarded by `F_CYCLE || F_CYCLE2` (no change
to the shipped build, which has both on).

### F_FEED_BACKOFF / F_FEED_ADAPT: the ablation was a no-op

The hold was gated on `F_FEED_BACKOFF || F_FEED_ADAPT`, so switching off `F_FEED_BACKOFF` alone left holding on: the v5.2
ablation measured exactly 0.0, and the audit run with it off reproduced the baseline game for game. Now `F_FEED_BACKOFF`
alone gates holding and `F_FEED_ADAPT` only sets the pile limit (no change to the shipped build). With holding really off,
nothing measurable changes: delivered pearls reach the intended apex as often (52% / 50%). Only 154 of 1519 `:feed`
deaths are deliveries (`feeddrop`); the rest are protective suicides, unblocking and chamber evictions. 40% of deliveries
still drop onto a pile at or above the limit: holding stops in the last `LATE_FEED` rounds, and a hold that finds no safe
tile (61 of 558) falls through to the drop.

## Win rate

Plain builds, `seedbench.py`, fresh seeds 901-906, the 16 maps without `help` and `big_empty` (the ladder maps plus the
four small ones), both sides: 192 games per pairing. Raw: `bench59d/val_901.jsonl` (v5.9d), `bench59d/val_901_orig.jsonl`
(original v5.9, same games).

| pairing | score |
|---|---|
| v5.9d vs original v5.9 | **54.7%** (105/192, ±3.6) |
| v5.9d vs v5.8 | **65.1%** (125/192, ±3.4) |
| original v5.9 vs v5.8 (same seeds) | 59.6% (114.5/192, ±3.5) |

Paired over the same 192 games against v5.8, v5.9d won 29 that the original lost and lost 18 that it won (sign test
z = 1.6, p about 0.11). Head to head it is +4.7 points (z about 1.3). Both point the same way, neither is significant: treat
the fixes as behaviour fixes that do not cost games, with a likely small gain. Per map against v5.8 (v5.9d / original, of
12): queen_of_spades 10 / 5, queen_of_spades_but_she_ages 9 / 6, default 10 / 8, stronghold 6 / 5 (the maps with rooms
and chambers); slithery_fight 5 / 7, the rest within one game.

The 48-game behaviour runs (DIAG builds vs v5.8, seeds 1-2) gave: baseline 25, closed hazards 29, alpha memory fix 31
wins. Module-off runs there: hazard 30, portal evict 25, portal clear 27, reserve 26, disperse 25, cycle 28 (decoupled),
feed adapt 31, feed backoff 29 (the real ablation), alpha memory 28. One standard error is about 3.5 wins, so none of the
v5.2 modules is shown to win games on these maps: `F_PORTAL_EVICT` is the only one whose behaviour clearly matters.

CPU (judge sandbox, schooltime seed 1, v5.9d vs v5.8): p50 5.2M, p99 6.7M, max 7.6M points per turn (v5.8 in the same
game: 5.0M / 6.5M / 6.8M; budget 100M).

## Suggested next steps
- `F_CYCLE` can go (or stay): it changes nothing measurable. `F_FEED_BACKOFF` / `F_FEED_ADAPT` likewise; delivery losses
  come from the endgame (no holding in the last `LATE_FEED` rounds) and from failed holds, not from pile size.
- The portal reservation needs a way to reach entrants that works through walls; aiming through the portal was not enough.
  The collisions at portal exits on the portals map come from teammates crossing the same portal pair from both sides.
- Validate v5.9d on more seeds (and on help / big_empty) before submitting it over v5.9.

## Code changes in v5.9/main.cpp

- Flags (block "v5.9d"): `F_HAZARD_CLOSED` on, `F_AMEM_FIX` on, `F_RESERVE_AIM` off, `F_RESERVE_ALL` off.
- `closed_behind()` (hazard gate), `mark_amem_claims()` / `alpha_memory_pick()` and the claimed/route checks in
  `alpha_memory_target()`, `portal_beam_dir()` + `Enclosure::portal_mask` (reservation variants).
- Ablation fixes, no effect on the shipped build: `breaking_cycle` uses `F_CYCLE || F_CYCLE2`; holding uses
  `F_FEED_BACKOFF` alone.
- `DIAG` lines (compiled out of real builds): `hazard`, `hazrecv`, `feedhold`, `feeddrop`, `amem`, `cycle1`, `cycle2`,
  `resvrecv`, `resvaim`.
