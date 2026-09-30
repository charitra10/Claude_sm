# Strategy v5.9e: do the v5.3 modules do what they were built for?

v5.3 added eight switchable modules (`strategyV_5_3.md`). Each was kept or dropped on its win-rate ablation, and nobody
checked whether it still did its job. v5.9 has grown about 50 modules on top of them since. This audit measures each v5.3
module's intended effect directly, on replays and traces, with the module on and off. Where a module does not do its job,
it finds out why and fixes it in `v5.9/main.cpp` (the "v5.9e" flag block below `PORTAL_RESIDENCY`). The v5.2 modules are
audited separately (`strategyV_5_9d.md`, another branch). The two change sets merge cleanly.

## Method

- **Builds:** `audit53/mkvariant.sh` makes scratch copies of v5.9 with one flag switched and `DIAG` trace points added
  (`audit53/instrument.py`; these compile to nothing in a normal build).
- **Games:** each variant plays v5.8 on the 13 ladder maps (help, big_empty, default_small, Colosseum and arena are off
  the ladder and skipped), seeds 1-3, both sides: 78 games per variant, the same games for every variant.
- **Metrics:** `audit53/metrics.py` measures what happened on the board, from the replays. `audit53/diagstats.py` measures
  what the trigger points did, from the traces. Summaries are in `audit53/results/`.
- **Win rates:** measured separately with `seedbench.py` on fresh seeds (101-108), in `bench59e/`.

## Verdicts

| module | built to | measured (on vs off, 78 games each) | verdict |
|---|---|---|---|
| `F_REPEL` | spread non-alpha heads apart | clumping 0.322 vs 0.339 (−5%); coverage 0.59 vs 0.58; on dilemma it *raises* clumping (0.35 vs 0.27) | works, weakly; no change |
| `F_CYCLE2` | break loose loops, head away from them | loops are 0.2% of turns either way; breakouts were steered by other modules' targets and fired on deliberate waiting | **fixed** (`F_BREAKOUT_HOLD`) |
| `F_PORTAL_FIX` | stop dragons hesitating at portals | crossings 84.3 vs 75.1 a game (+12%; queen_of_spades_but_she_ages +56%, schooltime +30%); 36 preemptions and 3 releases a game | works |
| `F_ALPHA_PORTAL` | the big alpha takes a free portal early and hands its role on | the "alpha" before round 50 is 2-6 long in v5.9; 0 of 13 named heirs ever got the role (sonar stopped by kelp) | premise obsolete; **fixed** the handover (`F_HEIR_BEAM`) |
| `F_STRADDLE` | never split with the body across a portal | 152 vs 223 such splits (child > 2); a child born straddling was not detected | **fixed** (`F_STRADDLE_SEEN`) |
| `F_HANDOVER` | the rear child of an alpha's L−2 split takes the role | alpha handovers succeed as often without it (0.67 vs 0.65: v5.5's `F_MANTLE` does this); its real effect is elsewhere, see below | redundant for its purpose; kept for its side effect |
| `F_LONG_PORTAL` | long dragons stay out of portals | crossings by 8+-long dragons 0.85 vs 2.60 a game; but 62% of its forced splits repeat at the same spot | works; one known gap, not fixed |
| `F_HAZARD_RR` | relay every live hazard, not only the freshest | teammates reached per hazard 11.8 vs 8.7 (+35%); entries into live hazard mouths 635 vs 735 (−14%) | works |

`F_FANOUT` ships off and was not audited.

## Findings per module

### F_REPEL (repulsion between non-alpha heads)
The scorer and the exploration target do push non-alpha heads apart, but only on moves decided by the scorer or by
exploration. Pearl, portal and other targeted moves take the fast path and never see the field. v5.9 also has several
other spreading rules: a linear friend penalty in the scorer, `F_DISPERSE`, `F_PAIR_SEP`, and the crowd rules of
`F_HOTSPOT2`. So the marginal effect is small:

- Overall clumping (another non-alpha head within 2 tiles) is 0.322 with it vs 0.339 without.
- It helps on default (0.23 vs 0.26), slithery_fight (0.28 vs 0.33) and trophy (0.32 vs 0.36).
- On dilemma it raises clumping (0.35 vs 0.27), yet dilemma won 6/6 with it vs 3/6 without.

v5.3 measured 0.57 → 0.26 against v5.2, when these other rules did not exist yet. The module is not broken, and there is
nothing to fix.

### F_CYCLE2 (loop breakout): fixed
**What happens:** loose loops (≤ 7 tiles in 16 turns, no food) are rare in v5.9: 0.18% of non-alpha turns with the
module on, 0.20% off. The breakout fired 4.4 times a game, and only 43% of breakouts got 5 tiles from where they started
within 10 rounds.

**Three causes:**
1. During a breakout, the v5.6-v5.8 errand targets steered on 826 of 2707 breakout turns: farm seeking, mirrored scouts,
   crowd leavers and convergence points. They ignore `cycle_break_until` and led the dragon back.
2. With nothing else to do, the dragon's target was `exploration_target()`. Its tile-age term (up to 56) swamps the pull
   toward the breakout point (at most 9).
3. 42% of breakouts broke deliberate waiting. The dominant target before the breakout was a rendezvous in 66 cases: a
   dragon circling a spawn cluster until its timer is due (`F_RENDEZVOUS`), which the breakout then ignores for 10
   rounds. Others were feeding (46) or lying in ambush.

**Fix (`F_BREAKOUT_HOLD`):**
- The four errand targets wait while a breakout runs.
- The patrol target during a breakout is the breakout point itself.
- `break_loop()` does nothing while the dragon has a rendezvous goal, is in the endgame feed, or was in ambush.

**Result (same 78 games):**

| | before | after |
|---|---|---|
| breakouts a game | 4.4 | 2.1 |
| errand-steered breakout turns | 826 | 23 |
| ate within 20 rounds of a breakout | 45% | 49% |
| looped again within 30 rounds | 9% | 6% |
| 5+ tiles away after 10 rounds | 43% | 40% |

The last row barely moves: most breakouts happen in mazes (trauma, schooltime), where 10 steps rarely add up to 5 tiles
of distance. The module still matters little because the loops it targets are rare.

**Coupling:** `breaking_cycle` in the fast path is gated on the v5.2 flag `F_CYCLE`, so switching `F_CYCLE` off also
silently switches off `F_CYCLE2`'s scorer steering. The v5.9d branch fixes this (`(F_CYCLE || F_CYCLE2)`), so this branch
leaves that line alone to avoid a merge conflict.

### F_PORTAL_FIX (portal hesitation fixes): works
- Crossings per game: 84.3 on vs 75.1 off. On queen_of_spades_but_she_ages 54.8 vs 35.2, schooltime 103 vs 79,
  queen_of_spades 51 vs 41.
- The "free portal beats a remembered pearl" rule fires 36 times a game.
- The residency release fires 3.2 times a game (`PORTAL_RESIDENCY` = 4 is gated by this flag).
- Deaths within 3 rounds of a crossing: 18.3% vs 16.7%, the price of using portals more.

One flaw, not fixed because its impact is tiny: rule 4 scores a portal whose far end was never seen "as open ground". It
does so on `step(here, d)`, the tile geometrically behind the portal edge rather than where the dragon comes out, so
`danger()` and the repulsion terms read the wrong tile.

### F_ALPHA_PORTAL (early alpha portal capture): handover fixed, premise obsolete
- It fired in 40 of 78 games: default, portals, trauma, stronghold, autarky, schooltime and trophy.
- The capturing "alpha" was 2-6 long every time. Since v5.4's round-0 cascade, the primary dragon is a short head, not
  the big alpha this module was designed for. A long alpha's split (`capsplit`) never happened.
- Of 35 role handovers, 22 named nobody, because no teammate was in view.
- The other 13 named heirs, and **none of them ever got the role**. The handover goes by sonar, which stops at kelp. For
  example, trauma_1_A round 34: three of the four beams stopped at the sender's own tile. The heir, off every straight
  line, never heard it, and no teammate relayed it (no handover packet was received in the whole traced game).
- Win rate: 59% on vs 58% off.

**Fix (`F_HEIR_BEAM`):** an heir must be a teammate that a straight beam from our head reaches within view. In the same
78 games none was, so the capture now always releases the role, which is what it did in practice anyway. The endgame
apex election refills the role. Because the "alpha" it moves is a short unit, a better use of this module needs a new
design (e.g. "the nearest unit takes a free chamber early"), not a fix.

### F_STRADDLE (no split across a portal): fixed
**What happens:** with the module on, 152 splits with a child longer than 2 still had the body across a portal (223 with
it off):
- 133 were rescue splits. v5.4's `F_REVERSE_SPLIT` deliberately allows those, because death is the alternative.
- 19 were not. Most came from a chain on slithery_fight: a 2×2 farm chamber at (57-58, 13-14) whose portal leads to
  (5,14). A 10-12-long dragon camping there was forced into the portal and split. Its rear child was born with its body
  across the portal and split again 2 rounds later: dragons 822, 838, 855, … in seed 3, one every 2 rounds, each 2-long
  head dying within 1-4 rounds.

**Why:** `straddling()` knew only three things: portals we crossed ourselves (`straddle_left`), a neck across a portal,
and a visible segment facing across one. A child born straddling never crossed anything, and the segment facing across
was out of view (11 tiles back).

**Fix (`F_STRADDLE_SEEN`):** walk the visible body from the head (`body_tiles()`). If the chain stops short of our length
at a segment within 2 tiles of the head, all its neighbours are in view, so the next segment must be behind a portal edge.

**Result (final build, same 78 games):** non-rescue straddle splits 19 → 3; long-portal splits on slithery_fight 25 → 6
(282 → 181 over all maps); rescue straddle splits 133 → 95.

### F_HANDOVER (the role passes to an L−2 split's rear child): redundant for its purpose, useful by accident
Since v5.5, `F_MANTLE` names the child in the parent's split-turn beam, which refracts out of the parent's own tail into
the child. A handover from an alpha succeeds with or without `F_HANDOVER`: the child still alive 3 rounds later is the
alpha in 67% vs 65% of cases. Most failures are children born in a sealed pocket, which the rule excludes by design.

What `F_HANDOVER` does in v5.9 is its birth rule: every 4+ long split child takes the role unless born in a pocket,
whoever its parent was. Its comment assumes only an alpha-sized body makes such a child. In v5.9, farm, choke and rescue
splits of ordinary 6+ long dragons make them too. The effect:

| | on | off |
|---|---|---|
| 4+ children of non-alpha parents that are Alpha 3 rounds later | 44% | 5% |
| rounds (20-380) with no alpha | 26% | 43% |
| dilemma: rounds with no alpha | 0.9% | 91% |
| dilemma wins | 6/6 | 3/6 |

So it is not doing what it says, but what it does looks useful. It is left as is. A win-rate test of restricting it to
alpha parents is the obvious next experiment, and it would probably lose dilemma.

### F_LONG_PORTAL (long dragons stay out of portals): works, one gap left
- Crossings by 8+-long dragons: 0.85 a game on vs 2.60 off.
- What remains is chamber campers in their 2×2 loop (`reside` / `loop`, v5.4's `F_PORTAL_LOOP`) and endgame feeders.

**The gap:** 175 of 282 of its forced splits (the 2-long head goes through, the rear stays) happened at a spot used 3+
times in the same game (stronghold 87/95, trauma 41/49). On stronghold, dragons in the maze around (12,14) make rescue
splits. Each rear child is born in the one-lane corridor (11,8)-(15,9)-(16,7), facing the portal at (16,7), and has no
other move. It splits there, and the next rescue child repeats this every ~6 rounds.

**Tried and removed (`F_LONG_CULDESAC`):** a portal-only cul-de-sac counts as a dead end for long dragons. Sacrifice
counts were identical with and without it (trauma 92 vs 91, stronghold 86 vs 86, schooltime 39 vs 43), because the
dragons are born there rather than walking in. A real fix would have to stop the rescue split from spawning its child
into such a corridor, which is v5.4's `F_REVERSE_SPLIT` and outside this audit.

### F_HAZARD_RR (relay every live hazard): works
- Distinct teammates that learnt each hazard: 11.8 with it vs 8.7 without.
- Teammate heads stepping onto a live hazard's mouth in its entry direction: 635 vs 735.
- Of those 635 entries, 523 were by dragons that had never heard of that hazard, so reach is still the limit.
- The other 112 heard it and went in anyway, which `F_CHOKE` / `F_CHOKE2` (v5.5 / v5.7) allow by design for 2-long
  dragons and for pearls.

The v5.9d branch changes which pockets count as hazards (`F_HAZARD_CLOSED`); this relay does not depend on that.

## Win rates (fresh seeds 101-108, 13 ladder maps, both sides)

Seeds 101-108 were not used in the audit. Games that decide themselves by seat (8/16 on a map) are common in the
mirror matchups. Raw results are in `bench59e/`; summarise them with `audit53/winrates.py FILE --per-map`.

| matchup | score | |
|---|---|---|
| v5.9e (all fixes) vs original v5.9 | 105/208, **50.5%** (±3.5) | `full_101.jsonl` |
| v5.9e without `F_STRADDLE_SEEN` vs original v5.9 | 104/208, 50.0% (±3.5) | `fx_noSEEN_101.jsonl` |
| v5.9e without `F_BREAKOUT_HOLD` vs original v5.9 | 105/208, 50.5% (±3.5) | `fx_noHOLD_101.jsonl` |
| v5.9e without `F_HEIR_BEAM` vs original v5.9 | 105/208, 50.5% (±3.5) | `fx_noHEIR_101.jsonl` |
| v5.9e vs v5.8 | 133.5/208, **64.2%** (±3.3) | `full_101.jsonl` |
| original v5.9 vs v5.8 (same seeds) | 129/208, **62.0%** (±3.4) | `orig_v58_101.jsonl` |

Per map against v5.8 (out of 16, v5.9e / original v5.9):

| map | v5.9e | orig | map | v5.9e | orig |
|---|---|---|---|---|---|
| autarky | 11.5 | 10 | schooltime | 9 | 9 |
| default | 10 | 11 | slithery_fight | 7 | 10 |
| devil | 8 | 8 | small | 14 | 14 |
| dilemma | 16 | 16 | stronghold | 7 | 10 |
| portals | 11 | 3 | trauma | 11 | 7 |
| queen_of_spades | 11 | 10 | trophy | 9 | 9 |
| queen_of_spades_but_she_ages | 9 | 12 | | | |

**Bottom line:** the fixes make the modules do what they were built for, but they do not measurably change the win rate.
Against the original it is 50.5%, and every single-fix ablation is 50.0-50.5%. Against v5.8 it is +2.2 points, which
is within noise. No improvement is claimed. The portals gain (11 vs 3 of 16) is the largest per-map difference and fits
the straddle and long-portal findings, but it is one map of 13, with slithery_fight and stronghold moving 3 games the
other way. Confirm it on more seeds before relying on it.

## Changes in `v5.9/main.cpp`

| flag | change |
|---|---|
| `F_STRADDLE_SEEN` | `straddling()` also detects a visible body chain that ends in view short of our length |
| `F_BREAKOUT_HOLD` | farm / scout / crowd / convergence targets wait during a loop breakout; the patrol target is the breakout point; `break_loop()` skips rendezvous waits, the endgame feed and ambushes |
| `F_HEIR_BEAM` | the early portal capture names only an heir a straight beam reaches (`beam_hits_friend()`) |

CPU in the judge sandbox (schooltime, seed 1, against the original v5.9): p50 5.9M, p99 6.7M, max 7.3M points a turn,
vs 4.9M / 6.7M / 7.1M for the original in the same game (budget 100M).
