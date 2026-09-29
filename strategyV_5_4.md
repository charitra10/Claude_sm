# Strategy v5.4: what changed on top of v5.3, and why

v5.4 is `v5.3/main.cpp` plus ten switchable modules (`F_*` flags at the top of `v5.4/main.cpp`) and one
new sonar tag. Everything else is still described by `strategyV_5_3.md` and the earlier docs. Raw results
are in `bench54/*.jsonl`.

**Bottom line:** v5.4 beats v5.3 **58.8%** (381/648, ±1.9) and v5.1 **57.4%** (248/432, ±2.4) on
validation seeds. Counting the screening games too, it beats v5.3 56.7% (440/776). The screening seeds
1-4 gave only 46.1% (±4.4), an outlier about 2.6σ below the rest. No variant was picked on them, so they
are not discarded. No single module moves the win rate by more than the noise on its own. The requested
behaviours are in place and verified in match logs.

## How it was measured

`seedbench.py`, paired seeds, both sides, `-j 6`. The map set now has 18 maps: `portals` and
`slithery_fight` are new in `maps/maps/`.
- **Screening:** 16 maps (no `help`, `big_empty`), seeds 1-4: 128 games per variant (±4.4). All ablations
  ran here. No variant was chosen from these numbers; the build that was screened is the build that ships.
- **Validation:** all 18 maps, seeds 21-26 (`valid1.jsonl`) and 27-32 (`valid2.jsonl`) against v5.3
  and v5.1, plus seeds 5-10 (`valid3.jsonl`) against v5.3: 216 games per opponent per set.

Games are deterministic for a given seed: v5.4 with every v5.4 flag off scores exactly 4/8 on every map
against v5.3. The outcome is still chaotic: any behaviour change reshuffles which seeds are won, so 4
seeds per map (8 games) is far too few to judge a single map. Stronghold went 0/8 with only the feed gate
on, then 7/16 with the full build on 8 seeds.

## Results

| v5.4 against | seeds 21-26 | seeds 27-32 | seeds 5-10 | pooled validation | screening, seeds 1-4 (16 maps) |
|---|---|---|---|---|---|
| v5.3 | 58.3% (126/216) | 59.7% (129/216) | 58.3% (126/216) | **58.8%** (381/648, ±1.9) | 46.1% (59/128, ±4.4) |
| v5.1 | 54.6% (118/216) | 60.2% (130/216) | not run | **57.4%** (248/432, ±2.4) | not run |

For comparison, v5.3 scored 55.2% against v5.1 in its own validation, so v5.4's gain over v5.1 (57.4%) is
smaller than its head-to-head margin over v5.3.

Per map against v5.3, seeds 21-32 (wins out of 24): autarky 19, help 16, portals 17, queen_of_spades
17, trauma 17, dilemma 16, schooltime 15, trophy 15, default 14, slithery_fight 14, default_small 13,
small 13, arena 12, big_empty 12, devil 12, queen_of_spades_but_she_ages 12, Colosseum 11, stronghold 10.

Against v5.1: autarky 24/24, dilemma 24/24, slithery_fight 21/24, portals 20/24; weak on Colosseum 7/24 and
stronghold 7/24 (v5.3 had the same weakness against v5.1 on stronghold).

CPU in the judge sandbox: portals (seed 1) p50 4.5M, max 5.0M; big_empty (seed 1, up to 64 dragons a side)
p50 4.7M, p99 5.1M, max 6.5M points per turn. That is the same as v5.3; the budget is 100M.

### Ablation (screening, seeds 1-4, full build = 46.1%)

| module off | vs v5.3 | | module off | vs v5.3 |
|---|---|---|---|---|
| `F_SPAWN_CASCADE` | 46.9% | | `F_PORTAL_TRAP` | 47.7% |
| `F_REVERSE_SPLIT` | 43.8% | | `F_SKIRMISH` | 48.4% |
| `F_CAMP` | 47.7% | | `F_FEED_GATE` (+ true lengths) | 45.3% |
| `F_PORTAL_LOOP` | 45.3% | | `F_SURPLUS` | 46.9% |
| `F_CHAIN` | 47.7% | | all v5.4 flags off | 50% by construction |

All within ±4.4. On the four farm maps (stronghold, trauma, devil, dilemma; seeds 1-8, 64 games) the feed
gate with true alpha lengths scored 53.1%, the gate alone 51.6%, the true lengths alone 50.0% and neither
45.3%: weak evidence that the pair helps, so both stay on.

## Modules

### 1. Round-0 cascade (`F_SPAWN_CASCADE`)
In round 0 every non-growing dragon of length ≥ 4 splits off its rear `L-2`. The child is a new dragon
that acts later in the same round, so it does the same, and a length-L spawn becomes L/2 two-long dragons
before anyone has moved. v5.3 split 2 per turn and stood still while doing it (L/2 − 1 turns). The front
keeps its role: the ID-0/1 alpha stays alpha at length 2, as it would anyway before `threshold()`
(`Mode::Cascade` skips the handover logic of an L−2 split). Children alternate ends, so every other one
faces its sibling's tail. They move after the lower IDs have moved away, which frees that tail.

The adaptive kamikaze trigger now ignores rounds 0-1: a cascade creates a unit lead but no mass lead.

Verified (autarky, seed 1, team A): `id 0: SPLIT 12`, `id 12: SPLIT 10`, `id 14: SPLIT 8`, `id 15: SPLIT 6`,
`id 16: SPLIT 4`, `id 17: SPLIT 2`, then `id 18: MOVE N`, all in engine round 0. v5.3 needed 6 turns
for the same result.

### 2. Reverse split for trapped dragons (`F_REVERSE_SPLIT`) — scenario_one.png
`survival_moves()` counts the moves that do not kill us this turn, whatever residency or occupancy
preferences say. A portal we have never seen through counts; one known to drop us in a dead cell does not.
With none left, length ≥ 4, and the split legal, the dragon splits `L-2` before anything else (a feeder
beside its apex still delivers instead). The 2-long head stays in the trap. The rear L-2 is born on the tail
facing away from it.

Root causes in v5.3:
- The routine `split 2` (non-growing dragons, and alphas before `threshold()`) ran before the emergency
  check. A cornered dragon shed a 2-long child off its tail and kept its mass in the trap.
- The emergency `L-2` split went through `can_split_safe()`, and the v5.3 straddle invariant blocked it
  whenever the body crossed a portal. That is the picture: the tail sits at a portal and the head is
  cornered in a dead-end cell. A dragon with zero survival moves now splits across the portal. The
  invariant still holds for every other split.
- When `legal` was empty only because we were refusing a portal (resident/occupied), v5.3 split. v5.4 takes
  the portal instead (`no_way`), and `portal_target(force)` no longer skips residents.

Verified on portals (seed 2): 117 rescue splits, 82 of them straddling a portal (impossible in v5.3). In
93 the 2-long head died within 3 rounds as intended. Example: alpha id 0, length 8, straddling, sends
`SPLIT 6`; child id 11 is born length 6 at the tail, and the head dies next round (`hit a wall`).

### 3. Camping in portal chambers (`F_CAMP`) — 2x2portal.png
In a small sealed portal enclosure (≤ 20 tiles) where at least half the tiles spawn, v5.3 left after 10
rounds without food and no pearl due within 6 rounds, even with pearls a few rounds off. v5.4 reads the
countdowns of every tile in the chamber (`Enclosure::next_pearl`). It leaves only if nothing is due within
`CAMP_HORIZON` (30) rounds. Single occupancy now also covers two dragons that are both settled, not just a
newcomer: the shorter one (higher ID on a tie) leaves. Children born inside evacuate as before.

### 4. 2x2 portal loop (`F_PORTAL_LOOP`) — 2x2portal.png
The picture's chamber has its portal on the internal edge between two of its four tiles, so the chamber is a
U: you enter at one end, walk three tiles and are pushed out through the same edge from the other side.
`portals` (nine such chambers, pearls every 1-10 rounds) and `slithery_fight` (two, every 1-5) are built
this way. The partner edge sits in open ground, so the whole thing is one 8-tile cycle: 4 chamber tiles, the
exit tile, and the 2x2 square round the partner edge that leads back to the entry side.

State machine: while inside a dense chamber of ≤ `LOOP_CHAMBER` (8) tiles we record it (`chamber_portal`),
and its portal is ours to cross (`own_portal()` overrides residency and occupancy). When we come out
through it (not evicting), we stay free rather than turning resident, and for `LOOP_TTL` (12) rounds we walk
back round the partner edge and in again (`Mode::Loop`), as long as the body fits the cycle
(`L ≤ chamber size + 3`). The routine `split 2` fires when the whole body is out, so the camper stays
short enough to keep looping. Verified on portals (seed 2): 105 chamber exits, 343 loop moves, and 27 dragons
went round at least once.

### 5. Dead cells behind portals (`F_PORTAL_TRAP`)
The main cause of death on `portals` was not the U-chambers but pairs of 1x1 cells joined by a portal edge,
with the partner edge in open ground. Whoever crosses it lands in a cell whose only exit is back into its own
neck. v5.3 and v5.1 lost 110-150 dragons a game each to `hit a wall` there. The map is built so that each team
can see only the dead cells that threaten the other team.
- `dead_cell(p, d)`: emerging on p moving d, is the region behind p sealed, portal-free and a tree (no loop to
  turn in)? Checked statically for every visible portal edge: a portal with dead cells on both sides of one
  end is barred for good (the `barren` flag). Crossings into a known dead cell are refused by `is_step_safe`,
  `portal_target` and `survival_moves`.
- A dragon that comes out in a dead cell bars the portal. On its rescue-split turn every beam carries the
  news. The beam fired back into its own body refracts out of the tail straight into the child it just split
  off. That child is a fresh process standing next to the same portal, and it used to walk straight back in
  (95 of ~180 wall deaths in one traced game).
- New tag `BARRED_TAG` (8187): the whole set of barred portal ids as a 32-bit mask. It is relayed every 2
  rounds, burst on two beams when first learnt, and handed to every split child the same way. v5.3's barren
  relay only ever repeated the first barren portal a dragon knew.

Result on portals (seeds 1-4, team A): avoidable wall deaths (not counting the doomed 2-long rescue heads) fell
from 392 (v5.3) to 259; wall deaths right after a portal entry fell from 244 to 170. First entries cannot
be avoided: nothing on our side of the map shows where our portals lead.

### 6. Chained rooms (`F_CHAIN`) — daisy_chains.png
`default` is the daisy chain: a 3x3 grid of 4x4 rooms joined in one line by portals 10 → 2 → 3 → 4 → 5 → 6 →
7 → 8 → 9 → 11 (rooms spawn every 1-769 rounds, the centre faster). Each room has two portals, both on
internal edges. v5.3 ping-ponged: an idle resident evicts with `portal_target(force)`. That ignores the shun
timer and picks the nearest portal, which is the one it just came through (it stands next to it). v5.4 keeps
the last `CHAIN_MEMORY` (4) crossings. A portal used within 60 rounds costs `CHAIN_PENALTY` (12) extra steps
in every portal choice except the loop's, so a dragon leaves a quiet room by the other portal and works down
the chain.

### 7. 2-long skirmishers (`F_SKIRMISH`)
A 2-long non-alpha is worth one pearl dead. It ignores the enemy "certain death" filters when choosing pearls
and steps (dead ends still apply), and it rams any enemy head of visible length ≥ 3 (or the enemy's ID-0/1
dragon) that it can reach this turn, when we have ≥ 3 units. Chasing enemies that are not yet in reach was
not added: v5.1 measured "kamikazes ambush even with a pearl in view" at −13.4.

### 8. Feeder gate and true alpha lengths (`F_FEED_GATE`, `F_GATE_REACH`, `F_TRUE_LEN`)
Traced on autarky, most feeder suicides happened next to a **2-long** "apex". Sonar-reported alpha lengths
were floored at 8 (`std::max(8, alen)`), so an ID-0 alpha that had only just started growing looked like an
8-long apex to every feeder. v5.4 keeps real lengths (`F_TRUE_LEN`). A feeder now dies only if all of these
hold:
- the apex is real: ≥ 3 segments visible, or its body runs out of view;
- it can reach the drop: `F_GATE_REACH`, a neighbour of its head within gap + 2 of our BFS, or our own body;
- it has the time: `500 − round ≥ gap + 2`;
- no enemy head is as close to us as the gap.

Otherwise it keeps walking toward the apex.

### 9. Surplus feeders (`F_SURPLUS`)
Feeders the apex cannot use yet (its pile of pearls is at the limit, so v5.2 would have them circle) first
look for a value trade or a zero-loss corridor trap (`guaranteed_kill(false, true)`). Feeders on their way
also take those. The existing split-and-kamikaze (split L−2, 2-long head goes for a longer enemy head, the
rear collects) already covers the requested split-sprint trade.

## Sonar protocol (layout unchanged, one new tag)

| aid | meaning | payload |
|---|---|---|
| 8187 `BARRED_TAG` | portals nobody should enter | bits 0-31: one bit per barred portal id < 32; no other field |

It is decoded before the generic kamikaze and enemy-alpha fields, which it does not carry. The old
single-portal barren bit in `PORTAL_TAG` is still sent and honoured.

## Tracing

`#define BOT_DIAG` (build a scratch copy with it prepended) turns on `DIAG` lines on stderr: `cascade`,
`rescue`, `deadcell`, `deadportal`, `loopout`, `feedgate`, `ram`, `useportal`, `gotbarren`, `born`. They are
compiled out of the real build.

## Known weak spots / next ideas
- Colosseum and stronghold against v5.1 (7/24 each). Stronghold was also weak for v5.3.
- On `portals` each team still pays for first entries into its dead cells. Map symmetry could predict them
  (the mirrored edge of a dead cell we can see), but a bot cannot ask which symmetry a map has.
- The loop only helps chambers of ≤ 8 tiles. Bigger dense rooms rely on camping plus child evacuation.
