# Strategy v5.3: what changed on top of v5.2, and why

v5.3 is `v5.2/main.cpp` plus eight switchable modules (`F_*` flags at the top of `v5.3/main.cpp`) and one
new sonar tag. Everything else is still described by `strategyV_5_2.md` and the earlier docs.

**Bottom line:** v5.3 is not a significant improvement over v5.2. On 384 fresh-seed games it scores
52.3% (±2.5). It fixes real defects: splits while straddling a portal (≈43 per 20 games → 0), portal
lock-out after a single portal trip, and handover of the alpha role. It also cuts friendly clumping on
most maps. Only the repulsion field measurably moves the win rate.

## How it was measured

`seedbench.py`, paired seeds, both sides, `-j 6`. Raw results are in `bench53/*.jsonl`.
- **Screening:** 14 maps (no `help`, `big_empty`), seeds 1-8: 224 games (±3.3). Ablations and tuning
  ran here, so these numbers carry selection bias.
- **Selection:** the two finalists on seeds 9-14, all 16 maps: 192 games each (`fresh_pick.jsonl`).
- **Validation:** the chosen build on untouched seeds 15-20, all 16 maps, 192 games per opponent
  (`final.jsonl`). No timeouts, no errors, no crashes.

## Results (validation, seeds 15-20)

| v5.3 against | score | for reference: v5.2 on seeds 9-14 (`strategyV_5_2.md`) |
|---|---|---|
| v5.2 | **54.2%** (104/192, ±3.6) | 50% by definition |
| v5.1 | **55.2%** (106/192, ±3.6) | 53.6% |
| v5 | **61.5%** (118/192, ±3.5) | 68.2% |

Seeds 9-14 against v5.2 gave 50.5%, so the pooled figure against v5.2 is **52.3% (201/384, ±2.5)**. The
v5 figures come from different seed sets and are not paired, so "v5.3 is worse than v5.2 against v5" is
not established either.

Per map against v5.2 (wins out of 12, seeds 15-20): dilemma 12, default 9, trauma 9, Colosseum 7, arena 7,
default_small 7, schooltime 7, small 7, trophy 7, autarky 5, big_empty 5, devil 5, help 5, queen 5,
queen_of_spades 4, stronghold 3. Devil and autarky were weak on every seed set.

CPU in the judge sandbox (big_empty, seed 1, up to 64 dragons a side): p50 4.7M, p99 5.0M, max 5.8M points
per turn, the same as v5.2 (budget 100M).

## Behaviour measurements

These come from an instrumented copy (stderr lines, scratch only) playing v5.2: 10 maps × 2 seeds, compared
with an instrumented v5.2 in the same games.

| map | clump rate v5.2 → v5.3 | straddle splits v5.2 → v5.3 |
|---|---|---|
| default_small | 0.57 → 0.26 | 0 → 0 |
| default | 0.38 → 0.19 | 19 → 0 |
| Colosseum | 0.40 → 0.27 | 2 → 0 |
| trauma | 0.29 → 0.19 | 1 → 0 |
| dilemma | 0.11 → 0.07 | 0 → 0 |
| small | 0.23 → 0.16 | 0 → 0 |
| arena | 0.84 → 0.77 | 0 → 0 |
| stronghold | 0.54 → 0.51 | 10 → 0 |
| queen_of_spades | 0.19 → 0.17 | 11 → 0 |
| trophy | 0.28 → 0.29 | 0 → 0 |

*Clump rate* is the share of non-alpha dragon-turns with another non-alpha head within 2 tiles. Over all 20
games, splits with the body across a portal went from 43 (of 1422 splits) to 0 (of 1253).

Tight loops (≤5 distinct tiles in 13 turns) never occurred in either build. Loose loops (≤10 tiles in 21
turns without growing) stayed at 0-2% of dragon-turns. The loop breaker triggers, but it makes no
measurable difference to that metric or to the win rate.

## Root causes found

- **Portals ignored (Priority 2).** Instrumented v5.2 had a portal ≤4 steps away but did not take it
  mostly because `portal_occupied()` said so (812 turns on default, 318 on queen_of_spades), then
  because of a remembered pearl (401 on default). Behind that:
  1. Every dragon that went through any portal became a `resident` for good, even when it came out on
     open ground, and `is_step_safe()` refuses every portal edge to a resident.
  2. `occupy()` marked a portal occupied forever whenever a teammate was seen coming out of it.
  3. A remembered pearl 4-8 tiles away and up to 24 rounds old outranked a free portal 1 step away.
  4. The scorer skipped any portal whose far end had never been seen (`destination()` has no answer).
- **Cycle breaking (Priority 1).** v5.2 needed an exact period *and* 16 rounds without food. After it
  fired, remembered and upcoming pearls kept their +80 target pull, against ±15 noise and a −60 revisit
  penalty.
- **Straddle splits (Priority 2).** Nothing checked for them. `body_tiles()`, and so `child_viable()`,
  silently gives up when the body crosses a portal. A first version of v5.3's own counter was off by one
  when the head came out onto a pearl, which cannot be seen at execute time. Bookkeeping now happens in
  `observe()` from the real length change.
- **Emergency split (Priority 3).** The zero-move L−2 split already existed. However, an alpha that was not
  `growing` stayed alpha at length 2, and teammates kept treating ID 0/1 as alpha because that is hardcoded.

## Modules

| flag | what it does | screening ablation (Δ when off, full = 51.3%) |
|---|---|---|
| `F_REPEL` | scorer charges `REPEL_W·(5−d)²` for each non-alpha teammate head within 4; exploration targets push 10/tile (was 6) from them | **−4.9** (dilemma 16 → 6) |
| `F_FANOUT` (off) | 3 turns in a crowd of 2+ other heads within 3 → retarget 5-12 tiles away from their centroid | on: −3.2 on screening, so off |
| `F_CYCLE2` | loop = ≤7 distinct tiles in the last 16, no food for 8 rounds (not feeding, residing or alpha endgame). Breakout: 10 rounds heading away from the loop's centroid, remembered/upcoming pearls ignored, 16-tile revisit penalty, +10/step toward the breakout point | −0.2 |
| `F_PORTAL_FIX` | the four portal fixes above. Residency by `PORTAL_RESIDENCY` (2: released only if nothing in view spawns pearls); teammate-exit occupancy expires after 40 rounds unless the enclosure is known small; a free portal ≤3 away beats a remembered pearl; unseen-partner portals scored as open ground | +2.0 (with rule 1) |
| `F_ALPHA_PORTAL` | before round 50, an alpha that is the closest teammate to a free portal ≤6 away takes it. One step out it hands the role to the longest teammate in view, then walks in; a long alpha splits and sends only its 2-long head | +0.9 |
| `F_STRADDLE` | every split goes through `can_split_safe()`: no split while the neck came through a portal, a visible segment faces across one, or the segment counter is non-zero | +3.6 (kept: requested invariant) |
| `F_HANDOVER` | an L−2 split demotes the parent (even a non-growing alpha) to a hunter; a child born ≥4 long takes the alpha role unless born into a sealed pocket; ID 0/1 only counts as alpha while ≥3 segments are visible | 0.0 |
| `F_LONG_PORTAL` | length ≥ 8: no portal routing (`portal_target` returns nothing), −700 on portal moves in the scorer; forced into one, split L−2 and send the 2-long head through | +2.0 |
| `F_HAZARD_RR` | relay every live hazard in rotation (two beams when two or more are live), not only the freshest | −0.4 |

Every single-module delta except `F_REPEL` is within noise. `PORTAL_RESIDENCY` 0/1/2 scored 50.7 / 51.3 /
49.8% (noise).

### Repulsion tuning (screening, fan-out on unless noted)

| variant | vs v5.2 |
|---|---|
| weight 0 / 1 / 3 / 6 | 51.8 / 53.6 / 49.8 / 50.9% |
| fan-out off, weight 3 **(kept)** | 54.5% screening, **50.5%** on seeds 9-14 |
| fan-out off, weight 2 | 57.6% screening, **46.4%** on seeds 9-14 |
| fan-out off, no alpha-portal / long-portal | 53.6% |

The field is strongly map-dependent: dilemma needs it (5/16 without it → 16/16 at weight 3), while
default and trauma do better without it. The weight-2 result is a clear example of screening selection
bias.

## Sonar protocol (layout unchanged, one new tag)

| aid | meaning | `px,py` | `r` | `alen` |
|---|---|---|---|---|
| 8188 `HANDOVER_TAG` | the alpha role passes on | successor's ID (12 bits), 4095 = none named | round of the handover | old alpha's ID (≤254) |

The old alpha puts it on all four beams for 4 turns. Teammates relay it on one beam for 8 rounds, and
receivers drop it at age 10. Receivers remove the old alpha's track and set `primary_demoted` if it was
ID 0/1; the named teammate sets `alpha = true`. Like 8189/8190, it is decoded before alpha tracking, so no
real alpha is ever confused with it. v5.2's tags keep their meaning, and the enemy-alpha and kamikaze
fields are filled as usual in tagged packets. On a 16×16 map the first version lost most handovers,
because all four beams wrapped round the torus into the sender; the 4-turn repeat and 8-round relay
fixed that in the traced games.

## Known weak spots / next ideas
- **devil and autarky** were weak on every seed set (devil 2-6/12, autarky 2-5/12 against v5.2). Devil
  has no portals; only switching off all repulsion brought it back to v5.2's level (8/16), within noise.
  A map-feature gate for the repulsion field is the obvious next experiment, but beware overfitting.
- **stronghold 3/12 and queen_of_spades 4/12** against v5.2 on seeds 15-20 (7/12 and 7/12 on seeds 9-14),
  most likely noise.
- The alpha capture only triggers on Colosseum, default and trophy; elsewhere no free portal is near the
  alpha before round 50.
