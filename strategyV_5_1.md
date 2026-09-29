# Strategy v5.1: what changed on top of v5, and why

v5.1 is `v5/main.cpp` plus three parameter changes. Each one was kept only because a seeded benchmark
showed it helped. `strategyV_14.md` and `strategyV_5.md` still describe everything else.

## How it was measured (and why v5's numbers no longer hold)

Since toolkit 1.1 the engine takes `--seed`, and pearl respawns are random unless you pass one. A
(map, side) pair is no longer one fixed game. The v5 notes' numbers came from the old fixed pearl
sequence and do not survive: re-measured on the current maps with random seeds, v5 beats v4.14
**60.2%**, not 72.7%. Several of v5's per-map results (stronghold 12/16, for example) were artifacts of
that one sequence.

- `seedbench.py` (repo root) plays a challenger against one or more opponents on every map, both sides,
  with explicit seeds. Every candidate therefore plays exactly the same games, and comparisons are
  paired. It snapshots and builds each bot at launch, so you can keep editing while it runs. It writes
  JSON lines and retries timed-out games one at a time instead of scoring them as losses.
- Maps: `maps/maps/` is the current set: the toolkit's 13 bundled maps plus `help`, `small` and
  `queen_of_spades_but_she_ages`. Four top-level files in `maps/` (`arena`, `big_empty`, `default`,
  `stronghold`) are **older versions**.
- Screening ran on 14 maps (without `help` and `big_empty`, which take half the CPU time) with seeds
  1-8, 224 games per opponent (±3.3%). Winners were checked on the big maps, then validated on fresh
  seeds.
- Run at most about 8 workers on a 16 GB machine. Twelve long games at once (stronghold, trauma, 64
  dragons a side) swap the machine, and one run crashed the session.

## Results

| v5.1 against | seeds 1-8, all 16 maps (256 games) | fresh seeds 9-14, all 16 maps (192 games) |
|---|---|---|
| v5 | 64.1% | **63.0%** |
| v4.14 | 71.9% | **63.0%** |
| v4.12 | n/a | **67.2%** |

The seeds 1-8 column is optimistic, because about 45 variants were screened on those seeds and the
best were kept. The fresh-seed column is the honest estimate. Pooled over seeds 1-14: 63.6% against v5
and 68.1% against v4.14. For reference, v5 itself scores 60.2% against v4.14 and 68.0% against v4.12
(seeds 1-4). CPU on big_empty (64 dragons a side, sandbox): p50 4.6M, p99 5.0M, max 6.3M points per
turn, the same as v5.

## Changes

### 1. Consolidate 60 rounds earlier (`feed_round()` − 60)
The swarm usually stops growing well before v5's consolidation round (total length flat for about 60
rounds on stronghold). A merged apex then keeps foraging on its own: 37 → 82 in the last 80 rounds in
one traced game, against 68 for v5's later merge. −30, −45, −60, −75, −90 and −120 were all tested:
−60 is the peak both before and after the other changes. It gains most on stronghold (up to 8/8), and
it is the reason length-decided games against v5 went to 66%.

### 2. Lower kamikaze threshold: 0.7× v5's table below `map_scale()` 20, 0.5× above
The unit count above which length-2 dragons turn kamikaze. Pressing an advantage earlier wins more
games. 0.7× everywhere gave +5.8 points against both v5 and v4.14. 0.5× gave +7.6 against v4.14 but
cost the small maps (small 12/16 → 8/16). The size split keeps both gains. 0.35× on larger maps did
better against v4.14 but worse against v5.

### 3. Adaptive regime triggers at a lead of 3 units or 1.3× (was 5 or 1.5×)
This mostly changes dilemma (against v5 8/16 → 16/16, against v4.14 +8) and
queen_of_spades_but_she_ages. On dilemma, splitting the long starting dragons creates exactly the
ID/unit-count gap this trigger reads. Changes 2 and 3 together: +8.9 points against v4.14 and +4.0
against v5 over v5.1-with-change-1.

## Tried and rejected (paired, 112-224 games per opponent)

| Idea | Result |
|---|---|
| Keep a long starting dragon (length ≥ 8) whole instead of splitting it | 43.8% against v5 (autarky 5/16) |
| Endgame feeders take value kills; active hunt of the enemy apex | no change (rarely triggers) |
| Newborns claim portals in their first 8 rounds (v4.14 behaviour) | −3.9 against v4.14, 50% against v5 |
| Mid-game alpha election when no alpha is known | −2.7 against v4.14 |
| Penalise steps whose exits another head can take ("exit safety"), in 5 variants | −2 to −9; stronghold +5 but devil −5 |
| Split L−2 when every move is a dead end (doom-split) / hold splits to farm pockets | 43.8% / 42.9% against v5 |
| v4.14's dead-end rule (only pockets under 5 tiles) / kamikazes ignore dead ends | 46.4% against v5 / neutral |
| Undo v5's trade gate on the corridor trap | −4.5 against v4.14 |
| Idle portal use only from barren ground | −8.0 against v4.14 (queen maps 0/8) |
| Feeder parity fixes (even-length feeders grab a pearl, split an odd 3 before dying) | neutral |
| General zero-loss squeeze (sprint so an enemy head has no exit) | neutral, then −7.1 against v5 |
| Alpha grows 60 rounds earlier / half or 1.5× unit cap before growth | neutral / −3.6 / mixed |
| Regime: min units 5, lead 2, lasting to v5's feed round; kamikaze homing radius 15 | −1.8, 0, −0.9, −8.9 against v5 |
| Kamikazes ambush even with a pearl in view / length-3 dragons as full kamikazes | −13.4 / −10.3 against v5 |
| v5's threshold wherever the view is ≥ 80% spawning tiles | −4.9 against v5 |
| Apex: no station-keeping near feeders / remembered pearls up to 14 away | neutral |
| Remember every spawn countdown and chase predicted pearls | −6.2 against v4.14 |

Lessons from the rejections:
- On farm maps (devil, stronghold, trauma, dilemma), dying in crowded fast-pearl corridors is part of
  harvesting them. Every attempt to cut "trapped" deaths also cut economy.
- Per-map results at 8-16 games are mostly chaos. The same map swung 1/8 ↔ 7/8 between near-identical
  variants, so only aggregate numbers are worth acting on.

## Known weak spots (fresh seeds, against v4.14)
- **queen_of_spades (4/12) and queen_of_spades_but_she_ages (3/12).** Side B loses 5 of 6 on
  queen_of_spades. v5.1's dragons stall on the outer ring while v4.14 takes the interior.
- **trauma (4/12).** All food is inside a portal-gated maze with an every-round dead-end corridor on
  each side. Portals keep travel direction while the map is mirrored, so the same idle-portal habit
  keeps v4.14's corridor children inside the maze and sends v5.1's back out to the barren start area.
- **default (4/12).**
