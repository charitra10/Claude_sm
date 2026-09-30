# v5.9 audit of the v5.4 modules: do they do what they were built for?

The ten v5.4 modules (`F_SPAWN_CASCADE` .. `F_SURPLUS`, described in `strategyV_5_4.md`) were accepted on win rate, and
no single one of them ever moved it beyond noise. This audit asks a different question for each one: **in today's
`v5.9/main.cpp`, does the module still do the specific thing it was designed to do?** The answer was measured on traced
games (a `BOT_DIAG` copy of v5.9 against v5.8) and on the replays, not on win rate. Where a module failed its purpose, the
root cause was found and fixed behind a new flag, the behaviour was measured again, and win rates were re-checked at the end.

The v5.2/v5.3 modules are audited separately (`strategyV_5_9d.md`, another session), and the v5.5+ modules too
(`audit55/`). All changes here are in the new "Audit of the v5.4 modules" block of flags right under the v5.4 block, and
in the code those flags guard. Tools: `audit54/` (see its README). Raw win-rate results: `bench54audit/`.

**Bottom line.**
- Working as intended: `F_CHAIN` (halves ping-pong), `F_FEED_GATE` (+8 points of every feed drop reaches the apex),
  `F_TRUE_LEN` (small), `F_SPAWN_CASCADE` and `F_REVERSE_SPLIT` (apart from the defects below).
- Dead or shadowed: `F_CAMP` has **no effect at all** (every game is identical with it off; `F_CAMP2` overwrites both values
  it sets). The `F_SKIRMISH` ram **never fires** (0 times in 40 games; v5.9c's exchange strikes take those situations first).
  `F_SURPLUS` fires about once in two games.
- Not doing their job, now fixed: the round-0 cascade killed one of its own children on autarky every game
  (`F_CASCADE_EXIT`); a trapped feeder skipped its rescue split when the feed gate then refused its drop (`F_RESCUE_GATE`);
  the portal loop's campers were killed head-on by their own fresh children (`F_FAMILY_PORTAL`: parent-child head-ons
  129 -> 40 over 8 games); barred-portal news did not reach newborns (`F_BARRED_AIM`: uninformed walks into barred portals
  195 -> 77 over 4 games).
- Win rate: **no measurable change**, as expected for fixes this local. On fresh seeds 1001-1008 (12 ladder maps, both
  sides) the fixed v5.9 scores 61.7% against v5.8 and the original 63.5% on the same 192 games (42 games differ, 19-23,
  z = -0.6); fixed against original head-to-head 47.9%. On the two maps the portal fixes target (portals, slithery_fight),
  seeds 1009-1024, the fixed build scores 59.4% against 46.9% (20-12 of the games that differ, z = +1.4), after leaning the
  other way on seeds 1001-1008 (17 vs 22 of 32). Pooled over both sets: 55 vs 52 of 96. No improvement is claimed.

## Method

- `audit54/trace.py` builds a scratch copy of the bot with `#define BOT_DIAG` (optionally with flags overridden), plays
  it against v5.8 and keeps each game's `DIAG` lines, engine death lines and replay. A few `DIAG` lines were added for the
  audit (`xcross`, `deadcellknew`, `family`, `cascadewalk`, `surplus`, `feedhold`, ...); they are compiled out of the real
  build.
- Baseline traces: 10 maps (portals, slithery_fight, default, queen_of_spades, autarky, trauma, stronghold, dilemma, trophy,
  schooltime), seeds 1-2, both sides: 40 games. Targeted runs after each fix on the maps concerned.
- Only the 12 ladder maps are used (autarky, default, devil, dilemma, portals, queen_of_spades,
  queen_of_spades_but_she_ages, schooltime, slithery_fight, stronghold, trauma, trophy); arena, Colosseum, default_small,
  big_empty, help and small are off the ladder.
- Games are deterministic per seed. Stderr lines of different dragons interleave at random, so "same game" is decided by
  comparing replays (`audit54/same_game.py`).
- `replay_tools/sim.py` replays the pre-game `update` events as moves (each spawn body gets two phantom segments until they
  reach its tail); `audit54/common.py` drops those events for the audit scripts. `killers.py` labels a dragon rammed head-on
  on the enemy's turn by its own last action ("enemy body"); the audit uses the engine's cause instead.

## Module by module

### 1. Round-0 cascade (`F_SPAWN_CASCADE`): works, one defect fixed
Purpose: every spawn body of length >= 4 becomes 2-3-long units in round 0. Checked on the unit lengths at the end of
round 0 (`cascade_check.py`): every spawn is fully broken up on every map.

Defect: on autarky one of our dragons died in round 0 in **every game** (and the v5.8 opponent's too). The 14-long spawn
lies in a 1-wide column walled by kelp. The spec's premise was wrong: "they move after the lower IDs have moved away, which
frees that tail". No lower ID moves in round 0: each one spends its turn splitting. So the last child of the cascade (2
long, it has to walk) is born in the middle of the column, facing a sibling's tail with kelp on both sides, and dies.
Its parent (4 long) had a free sideways step.

Fix (`F_CASCADE_EXIT`): a cascade split whose child would be too short to split on (< 4) and born with no free step
walks instead, if it can, and splits from round 1. The child's exits are judged by `cascade_child_exits()`: the tile past
its tail is 4 away and out of view, but in round 0 the parent's tail is always next to the child's tail and has not moved,
so one unseen neighbour of the tail is known to be occupied unless a teammate is already seen there. Verified: autarky
seeds 1-2, both sides: no round-0 death of ours (`cascadewalk 20 4 18,11`), the opponent still loses its piece.

### 2. Reverse split for trapped dragons (`F_REVERSE_SPLIT`): works, one interaction fixed, one chain left
Purpose: a dragon with no move that survives splits L-2 so the rear lives. `rescue_check.py`: 5706 rescue splits in the 40
baseline games. Outside slithery_fight the rear child lives more than 3 rounds in **88%** (2123 splits); misses (dragons 4+
long dying by kelp, self or body without splitting) are 61 over 40 games, most of them newborn rescue children that
`F_SPLIT_ONCE` (v5.9b) lets die on purpose.

Defect: a trapped feeder beside its apex skips the rescue split ("it delivers instead"), but the delivery then asks the
feed gate, which refuses when the apex cannot reach the drop. The feeder fell through to a blind last move and died whole
(portals: 13 long, 7 pearls where the apex could not get them). Fix (`F_RESCUE_GATE`): the gate test moved into
`feed_gate_ok()`; a trapped feeder skips its rescue only when the gate would let it deliver. Rare (once in 40 games).

Open: on slithery_fight the rear lives more than 3 rounds in only 57% of 3583 rescues; 317 of ~350 short-lived children in
one game split again within 3 rounds, and 480 of that game's 811 rescues were by 10-14-long alphas diving into the row of
2x2 pockets at the top of the map. That is the entry rule (`F_CHOKE2`, v5.7: any length may enter a dead end with 2+
pearls) and the chain that `strategyV_5_10.md` item 7 fixes in v5.10 (`F_BIRTH_DEAD`, `F_CHAIN_ALPHA`); it was not touched
here to avoid duplicating that work.

### 3. Camping in portal chambers (`F_CAMP`): dead code
Purpose: stay in a chamber while its pearls are due; one camper per chamber. `F_CAMP` sets `idle` and `crowded`; the
`F_CAMP2` block (v5.5) a few lines later recomputes both unconditionally. With `F_CAMP` off every game is identical (4 of 4
replays, event for event). The flag now carries a comment saying so.

The intent is served by `F_CAMP2` + `F_DRY_EVICT`, and holds: in 9433 camp turns no camper left with a pearl due within 12
rounds except to yield to a teammate. One-camper-per-chamber is weaker than intended in the 16-tile chambers of default
and queen_of_spades (two campers in 5-21% of held chamber-rounds): they do not fit in the 7x7 view, and the v5.5 tie-break
takes 10 rounds or never fires when the two never see each other's heads. That is `F_CAMP2`'s rule: a lead for the v5.5
audit.

### 4. 2x2 portal loop (`F_PORTAL_LOOP`): was undermined, fixed
Purpose: after walking out of a dense chamber of <= 8 tiles, go round the partner edge and back in within `LOOP_TTL`.
`loop_check.py` on portals and slithery_fight: of 511 exits 264 (52%) got back in (6 rounds on average), 70 found a
teammate in the chamber, 14 were too long, and **128 (25%) died** within the 12 rounds.

Root cause: the looper's own children. v5.4 designed the loop so that "the routine split 2 fires when the whole body is
out"; v5.8's `F_STRADDLE_SPLIT` then allowed a 2-split with the body across a portal. A camper or looper straddling its
chamber's portal splits a child on the far side (tail on the portal tile); the child, a fresh process, walks straight
back through the portal and lands on its parent on the landing tile. The portal probe cannot catch it: when the child
probes, the tile is empty; the parent (lower ID) steps onto it in the round the child crosses. `family_check.py`: 132
parent-child head-ons in the 40 baseline games, 101 of them straddle-split children on portals (about 25 a game, each
killing two dragons); in the other direction the parent split while walking out, the child was born inside, and the
looping parent walked back in onto it.

Fix (`F_FAMILY_PORTAL`), both sides:
- child: born with its tail on a portal tile, it keeps out of that portal for `FAMILY_TTL` (20) rounds (an evacuee born
  inside leaves at once; delaying it made it meet the returning parent: 8 -> 16 such head-ons);
- parent: a split with the body across a portal and the head outside the chamber gives the chamber to the child (it adopts
  it under `F_CAMP2`): no loop back in, and the parent keeps out of that portal for 20 rounds.

Result (portals + slithery_fight, seeds 1-2, both sides): parent-child head-ons 129 -> 57 (child side) -> 40 (both);
friendly head-on deaths on portals 68.8 -> 48.8 a game.

### 5. Chained rooms (`F_CHAIN`): works
Purpose: leave a room of default's chain by the portal not just used. `chain_check.py` reads every crossing from the
replays: the next crossing is back through the same portal within 30 rounds in **37.5%** of crossings with the flag off
and **19.9%** with it on (default, seeds 1-4, both sides, 8 games each), with fewer friendly deaths (18.8 vs 23.6 a game).

### 6. Dead cells behind portals (`F_PORTAL_TRAP`): detection works, the news did not spread, fixed
Purpose: never cross into a known dead cell; bar portals with dead cells on both sides and tell the team. On portals the
team found 4 such portals per game, yet made about 40 dead-cell entries per game, 36 of them through portals already
barred. Traced per crossing (`xcross`, `trap_check.py`, 4 games):
- Every crossing by a dragon that knew the portal was barred (147 over the 4 games) was made with **no other legal move**:
  the barred portal only counts as a way out because its far side is unseen. Entering and rescue-splitting a round later costs the same as
  splitting in place; left as is.
- ~195 crossings by dragons that **did not know**; **186 of 191** checked were born after the portal was barred, at a
  median age of 4 rounds. Parents knew the barred set in 527 of 536 splits, but only about 60% of the children did: the
  hand-off beam goes straight back into the neck and only refracts into the child when the body is straight at the split
  point, and the mantle and farm hand-offs overwrote it on the beams that did hit. Kelp walls on portals stop most relays.

Fix (`F_BARRED_AIM`): the hand-off is aimed with `beams_into_child()` and shares beams with the mantle / farm hand-offs
(one hitting beam each when two or more hit); the parent repeats it on the back beam for the two turns after a split; a
dragon that knows a portal leads into a dead cell (new bit 32 of the `BARRED_TAG` packet) relays the set every round
instead of every other. Result: uninformed crossings into barred portals 195 -> 77, dead-cell entries 207 -> 151, wall
deaths on portals 188 -> 156 a game (the aiming alone moved nothing; the repeat and the relay rate did).

### 7. 2-long skirmishers (`F_SKIRMISH`): the ram is shadowed
The ram (a 2-long rams a 3+ enemy head in reach) fired **0 times in 40 games**: `exchange_strike()` (v5.9c) runs earlier in
the cascade for every non-alpha and takes every strike worth taking by its value model; what it declines, the ram's
protection test declines too. The other half (2-longs ignore the enemy "certain death" filter) is live; it has no clean
behavioural signature and is left to the ablation below.

### 8. Feeder gate and true alpha lengths (`F_FEED_GATE`, `F_GATE_REACH`, `F_TRUE_LEN`): work
`feed_check.py` follows every pearl a feed suicide drops. Six feed-heavy maps (stronghold, trauma, schooltime, autarky,
portals, queen_of_spades), seeds 1-2, both sides, 24 games per row:

| build | drop eaten by the apex | by another teammate | by the enemy | never eaten | drops beside a 2-3 long "apex" | wins |
|---|---|---|---|---|---|---|
| current | **55%** | 33% | 2% | 10% | 52 | 16 |
| `F_FEED_GATE` off | 47% | 36% | 4% | 14% | 64 | 14 |
| `F_TRUE_LEN` off | 53% | 32% | 2% | 12% | 63 | 16 |
| `F_SURPLUS` off | 56% | 33% | 2% | 10% | 53 | 17 |

("Apex" = the longest teammate within 4 of the drop; most other eaters are other 8+ long alphas.) The gate refuses mostly
for reach (589 of 804 refusals: the apex cannot walk to the drop), then for an enemy as close (215).

### 9. Surplus feeders (`F_SURPLUS`): nearly idle
Feeders held back by a pile at the apex circled 393 times in 24 games; the value trade fired 15 times. It does what it says
when a trade exists, which is rare. No change.

## Win rates

All against v5.8, paired seeds, both sides. `seedbench.py`; raw results in `bench54audit/`.

**Validation, fresh seeds 1001-1008, the 12 ladder maps (192 games per row):**

| map | fixed v5.9 | original v5.9 | | fixed vs original |
|---|---|---|---|---|
| autarky | 10.5 | 11 | | 8 |
| default | 9 | 10 | | 8 |
| devil | 9 | 9 | | 8 |
| dilemma | 16 | 16 | | 8 |
| portals | 10 | 12 | | 9 |
| queen_of_spades | 12 | 10 | | 9 |
| queen_of_spades_but_she_ages | 10 | 10 | | 6 |
| schooltime | 11 | 9 | | 6 |
| slithery_fight | 7 | 10 | | 8 |
| stronghold | 7 | 7 | | 8 |
| trauma | 8 | 9 | | 8 |
| trophy | 9 | 9 | | 8 |
| **all** | **118.5/192 (61.7%)** | **122/192 (63.5%)** | | **92/192 (47.9%)** |

Paired against v5.8: 42 of the 192 games have different results, 19 won only by the fixed build and 23 only by the
original (sign test z = -0.62, noise). Head-to-head, the maps where no fix fires are exactly 8/16 (identical bots on a
symmetric map win each side once per seed).

**Targeted, portals + slithery_fight, fresh seeds 1009-1024 (64 games per row):**

| build | vs v5.8 | portals | slithery_fight |
|---|---|---|---|
| fixed v5.9 | **38 (59.4%)** | 19/32 | 19/32 |
| original v5.9 | 30 (46.9%) | 14/32 | 16/32 |
| fixed, `F_FAMILY_PORTAL` off | 35 (54.7%) | 14/32 | 21/32 |
| fixed, `F_BARRED_AIM` off | 40 (62.5%) | 21/32 | 19/32 |

Fixed against original, paired: 20-12 (z = +1.41). The single-fix ablations are within noise (`F_FAMILY_PORTAL` +3,
`F_BARRED_AIM` -2). Both stay on: they remove behaviour that is plainly wrong (a child ramming its parent, a newborn walking
into a portal the team already barred), and neither costs games measurably. `F_CASCADE_EXIT` only changes games on autarky
(8/16 head-to-head: it reshuffles which seeds are won), `F_RESCUE_GATE` fired once in 40 traced games.

CPU in the judge sandbox (trauma, seed 1, against v5.8): p50 5.0M, p99 5.9M, max 6.4M points per turn (budget 100M;
v5.8 in the same game: max 5.9M). The fixes add O(1) work per turn and one beam simulation per split.

## New flags

| flag | default | what it does |
|---|---|---|
| `F_CASCADE_EXIT` | on | round 0: a cascade child too short to split on is not born where it cannot move |
| `F_RESCUE_GATE` | on | a trapped feeder skips its rescue split only when the feed gate lets it deliver |
| `F_FAMILY_PORTAL` | on | a newborn with its tail on a portal tile keeps out of that portal; a parent splitting on the way out of a chamber leaves it to the child (`FAMILY_TTL` 20, `FAMILY_TTL_EVAC` 0) |
| `F_BARRED_AIM` | on | barred-portal hand-off aimed at the child, repeated for 2 turns, and relayed every round when a portal leads into a dead cell |

Sonar: `BARRED_TAG` (8187) bit 32 = "some of these portals lead into dead cells". Older builds ignore it.

## Leads for the next audits
- slithery_fight rescue chains: entry into 2x2 pockets by long alphas (`F_CHOKE2`), fixed in v5.10 by `F_BIRTH_DEAD` /
  `F_CHAIN_ALPHA`.
- `F_CAMP2`: two campers in 16-tile chambers that never see each other.
- Friendly body collisions on portals (about 190 a game) are the largest leak left there: mostly crossings onto a teammate
  standing unseen on the far side.
