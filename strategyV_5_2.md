# Strategy v5.2: what changed on top of v5.1, and why

v5.2 is `v5.1/main.cpp` plus nine modules, one for each of the five requested improvements (some tasks
have two). Every module is a `constexpr bool F_*` switch at the top of `v5.2/main.cpp`, so it can be turned
off and benchmarked on its own. `strategyV_14.md`, `strategyV_5.md` and `strategyV_5_1.md` still describe
everything else.

## How it was measured

Same method as v5.1 (`seedbench.py`, explicit seeds, paired games, `-j 6`).
- **Screening:** 14 maps (no `help`, `big_empty`), seeds 1-8, both sides: 224 games per opponent (±3.3%).
  All module ablations and the threshold sweep ran on this set, so these numbers carry selection bias.
- **Validation:** fresh seeds 9-14 on all 16 maps: 192 games per opponent. These are the honest numbers.

## Results

Fresh seeds 9-14, all 16 maps, both sides, 192 games per opponent (`bench52/fresh.jsonl`):

| v5.2 against | v5.2 | v5.1 on the same kind of test (from `strategyV_5_1.md`) |
|---|---|---|
| v5.1 | **53.6%** (103/192, ±3.6) | 50% by definition |
| v5 | **68.2%** (131/192, ±3.4) | 63.0% |
| v4.14 | **70.8%** (136/192, ±3.3) | 63.0% |

The screening estimate against v5.1 was 61.6%. Most of the drop to 53.6% on fresh seeds is selection bias
(the screening seeds were used to choose among variants) plus the two big maps screening left out.

Per map (wins out of 12; v5.1 / v5 / v4.14):

| map | vs v5.1 | vs v5 | vs v4.14 | | map | vs v5.1 | vs v5 | vs v4.14 |
|---|---|---|---|---|---|---|---|---|
| autarky | 12 | 12 | 12 | | help | 7 | 10 | 11 |
| dilemma | 10 | 12 | 10 | | small | 6 | 8 | 9 |
| default | 8 | 10 | 11 | | trophy | 6 | 6 | 8 |
| Colosseum | 7 | 7 | 12 | | queen_of_spades_but_she_ages | 6 | 8 | 7 |
| trauma | 7 | 6 | 1 | | arena | 6 | 6 | 7 |
| default_small | 6 | 9 | 10 | | stronghold | 5 | 11 | 11 |
| devil | 6 | 7 | 7 | | queen_of_spades | 4 | 4 | 5 |
| big_empty | 4 | 8 | 9 | | schooltime | 3 | 7 | 6 |

The same validation with `F_FEED_ADAPT = false` scored 53.1% / 64.1% / 70.3%, so feed-adapt stays on.

CPU in the judge sandbox (big_empty, 64 dragons a side, seed 1): p50 4.7M, p99 5.0M, max 6.2M points per
turn (v5.1 in the same game: 4.6M / 5.0M / 6.1M). The new work is small, bounded floods (≤20 or ≤48 tiles)
and fixed-size arrays; no per-turn heap growth.

## Sonar protocol (unchanged layout, two new tags)

`alpha_packet64()` already fills all 64 bits. v5.2 adds no field. It reserves two values of the 13-bit
alpha-ID field, next to the existing 8191 "no alpha" sentinel:

| aid | Meaning | `px,py` | `r` | `alen` (8 bits) |
|---|---|---|---|---|
| 8190 `HAZARD_TAG` | a teammate is trapped in a pocket | mouth tile | round the owner last confirmed it | entry direction (2 bits) + valid bit |
| 8189 `PORTAL_TAG` | portal reservation / release | sender ID (12 bits, tie-break) | origin round | portal id (5 bits), small, barren, clear |

The enemy-alpha fields (`he`, `ex`, `ey`, `eage`) and the kamikaze bucket are filled as usual in tagged
packets, so those keep propagating. Decoders drop tagged packets before the alpha-tracking code, and a real
dragon ID never reaches 8189. Tagged packets take one beam per turn (rotating N/E/S/W). The exception is a
dragon that has just found itself trapped, which spends all four beams on its hazard. A beam fired into its
own body refracts out through the tail, back along the corridor toward the mouth.

## Changes

### Task 1: trap enclosure detection and hazard propagation (`F_HAZARD`, ablation −8.0)
- **Entry tracking:** each turn `observe()` checks whether we stepped from open ground (3+ passable edges)
  into a corridor tile (≤2). If so it records that tile and the direction (`enc_mouth`, `enc_dir`).
- **Detection:** `sealed_pocket(here, facing)` (v5's pocket flood, run on our own head) says we cannot get
  out. If the recorded entry is recent (≤40 rounds) and close behind us, we add the hazard and broadcast
  it on all four beams. We re-announce every 3 rounds while still trapped.
- **Receivers** store up to 8 hazards and relay the freshest on one beam until it is 12 rounds old. The
  owner's refresh keeps it alive. A hazard forbids only *entering*: stepping onto the mouth in the entry
  direction. BFS (`paths`, `remembered_direction`) skips that edge, the fast path refuses it, and the scorer
  charges −600. The trapped dragon's split child walks out the other way unhindered, and it no longer meets
  a teammate head-on in a 1-wide corridor.
- **Emergency split:** v5.1 already splits `L−2` when no legal move is left (`legal.empty()`), and the child
  is born on the tail facing outward. The rejected-ideas table in `strategyV_5_1.md` shows that splitting
  *earlier* ("doom-split") cost 6 points, so this was deliberately left as is.
- On dilemma the hazards land exactly on the four dead-end farm mouths, (14,12)/(17,12) S and
  (14,3)/(17,3) N. Dilemma went to 16/16 against v5.1.

### Task 2: portal overhaul
- **2.1 / 2.2 Eviction and congestion (`F_PORTAL_EVICT`, −5.4):** `enclosure_at(here)` floods the region
  reachable without portals. It is a *small enclosure* if it is sealed, has ≤20 tiles and has a portal edge.
  Inside one we leave via its portal (bypassing the resident / occupied locks) when it is barren (no
  spawning tile), when an alpha-less dragon has had no pearl for 10 rounds with none due within 6, or when
  we are a newcomer (arrived ≤6 rounds ago) and a teammate is inside. A newcomer with no way out that is
  not longer than the teammate reverses into its own neck, dropping its pearls for the resident. On exit
  we broadcast a *clear* (with small/barren bits), shun that portal for 60 rounds and are no longer a
  resident. A *barren* portal is marked occupied for good by everyone who hears it, and dragons that know
  one re-announce it every 8 rounds. On dilemma, v5.1 dragons camped in the barren 3×3 rooms for ~90
  rounds and died there; v5.2 dragons leave within about 4 turns.
- **2.3 Clearance (`F_PORTAL_CLEAR`, −2.7):** tiles with a portal edge are where dragons emerge blind. The
  fast path will not step onto one unless it is the target or holds a pearl. The scorer charges −45, and
  exploration targets charge −40.
- **2.4 Single occupancy (`F_PORTAL_RESERVE`, −3.1):** a dragon within 3 steps of the portal it is
  heading for broadcasts a reservation (30-round TTL). A resident re-broadcasts one every 10 rounds.
  `portal_occupied()` now also honours other dragons' live reservations. When two dragons reach for the
  same portal within 3 rounds, the lower ID keeps it.

### Task 3: movement flow
- **Dispersion (`F_DISPERSE`, −1.8):** the scorer charges −7 per adjacent friendly body segment (at most 3).
  It also charges −20 for going straight when a teammate's body is 1-2 tiles ahead ("single file"), so the
  follower takes a parallel lane. Exploration targets are pushed away from friendly bodies too. Alphas,
  feeders, ambushes, portal moves and pearl steps are exempt.
- **Cycle breaking (`F_CYCLE`, −1.1):** if the last 2p positions repeat with period p (2 ≤ p ≤ 8) and we
  have eaten nothing for 16 rounds, we jump to a random new sector for 8 rounds. During that time we skip
  the fast path (except for visible pearls), add ±15 of deterministic noise and charge −60 for the last 8
  tiles.

### Task 4: `base_kamikaze_threshold` sweep
A sweep had already been done for v5.1 (`strategyV_5_1.md`): 1.0× (v5), 0.7× everywhere, 0.5× everywhere,
the 0.7×/0.5× split by map size, and 0.35× on large maps. The split won. The multipliers are now constants
(`KAM_SMALL_X10`, `KAM_LARGE_X10`), and the neighbours were re-swept on the v5.2 build (screening seeds,
224 games per cell):

| small × / large × | vs v5.1 | vs v4.14 |
|---|---|---|
| **0.7 / 0.5 (kept)** | 62.1% | 68.8% |
| 0.6 / 0.5 | 60.7% | 69.2% |
| 0.8 / 0.5 | 60.7% | 68.3% |
| 0.7 / 0.4 | 62.5% | 69.2% |
| 0.7 / 0.6 | 62.1% | 67.4% |

The surface is flat within noise (±1.5 points against ±3.2 standard error), so the v5.1 values stay.

### Task 5: feeders and the alpha
- **5.1 Length-adaptive suicide (`F_FEED_ADAPT`):** the first version delayed short feeders' *departure*
  (a quarter or half of the feeding window). It took stronghold from 8/16 to **1/16**: the apex ended at
  60 against 70, with undelivered dragons still alive. Consolidating early is what v5.1 learned wins
  stronghold. The kept version sends everyone at `feed_round()`. Length only decides how big a pile of
  pearls around the apex makes a feeder wait before dying: 9 for length ≥6 (almost never waits), 5 for
  4-5, 3 for length ≤3 and ex-kamikazes. Measured neutral (ablation +1.3, within noise).
- **5.2 Saturation backoff (`F_FEED_BACKOFF`, 0.0):** with the pile at or above the limit, the feeder circles
  3-5 tiles from the apex, off its pearls, out of its path and out of danger. It stops waiting once
  `500 − round ≤ length + 10`.
- **5.3 Alpha pearl memory (`F_ALPHA_MEMORY`, −3.1):** alphas keep a 16-slot queue of every pearl they have
  seen. An entry leaves when the tile is seen empty, after 80 rounds, or when a fresher sighting needs the
  slot. With nothing in view the alpha heads for the best remembered pearl within 24 tiles (score
  `40/(d+1) − 0.15·age`), routed by `remembered_direction`. The generic 4-8-tile memory is the fallback.

## Ablation (screening, full v5.2 = 61.6% against v5.1)

| Module switched off | vs v5.1 | Δ |
|---|---|---|
| hazard | 53.6% | −8.0 |
| portal evict | 56.2% | −5.4 |
| portal reserve | 58.5% | −3.1 |
| alpha memory | 58.5% | −3.1 |
| portal clear | 58.9% | −2.7 |
| disperse | 59.8% | −1.8 |
| cycle | 60.5% | −1.1 |
| feed backoff | 61.6% | 0.0 |
| feed adapt | 62.9% | +1.3 |

Single-module deltas under ±3 are within noise, although the games are paired.

## Known weak spots / next ideas
- **big_empty against v5.1 (4/12 fresh, 8/24 on seeds 15-26).** Switching off dispersion or alpha memory each
  gave 11/24 there. Gating both off above `map_scale()` 40 (big_empty, help, schooltime) went 40/72
  against the full build's 38/72 on the same seeds: big_empty +4, help −3, schooltime +1. That is not
  significant, so it was **not** kept.
- **trauma against v4.14 (1/12 fresh, 5/24 on seeds 15-26).** This was already a v5.1 weak spot (4/12).
  The portal modules are not the cause: with all three off it drops to 0/24, and without clearance to 1/24.
  Every loss is on length at round 500.
- **queen_of_spades (4-5/12) and schooltime (3/12 against v5.1).** These are the portal and maze maps
  where early portal access decides the economy.
- **Hazard false positives:** `sealed_pocket` treats visible dragon bodies as walls, so a crowd can make a
  dragon in a corridor look trapped for a turn. Requiring the head to sit on a ≤2-edge tile before
  announcing is the obvious next refinement. It has not been tested.
