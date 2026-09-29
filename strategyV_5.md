# Strategy v5: what changed on top of v4.14, and why

v5 is `v4.14/main.cpp` plus the changes below. Every change was kept only if a benchmark showed it
helped. `strategyV_14.md` still describes everything that was not touched.

## How it was measured

The engine is deterministic: a given (map, side, bots) always replays the same game. So "more seeds"
means more maps. `make_map_variants.py` writes the 8 flip/transpose variants of each of the 11 bundled
maps into `maps_variants/`. The id variant replays the original game exactly, and the others play out
differently because of direction and ID tie-breaks. Each benchmark plays every variant from both sides:
**176 games per opponent**, which gives a standard error of about ±3.5%.

| Opponent | v4.14 (starting point) | **v5** |
|---|---|---|
| v4.14 | 50% (self-play) | **72.7%** (128–48) |
| v4.12 | 62.5% (44-game set) | **71.0%** (125–51) |

On just the 11 original maps (22 games, `./benchmark.py`), v5 beats v4.14 14–8 (63.6%) and v4.12
19–3 (86.4%). That sample is too small to tune on, which is why the variant set exists.

Per map against v4.14 (wins out of 16): Colosseum 15, arena 14, big_empty 15, default 14, schooltime 14,
stronghold 12, trophy 11, devil 10, default_small 9, queen_of_spades 7, trauma 7.

CPU in the judge sandbox (`unswbc run --sandbox`, big_empty, 64 dragons a side): p99 5.0M and max 6.0M
points per turn, against a 100M budget. That is the same as v4.14.

## Changes, in order of impact

### 1. Early economy: forage first, and remember pearls on every map (+13.6 points)
- **Newborns no longer race to claim a portal** in their first 8 rounds. They start foraging at once;
  only children born in a sealed nursery still escape through its portal. Colosseum went from 8/16 to 15/16.
- **Remembered-pearl targeting** (chasing pearls and imminent spawns seen outside the current 7×7 view)
  used to be switched off on maps with `map_scale() > 26`. It now runs on every map: default went from
  7/16 to 14/16, and big_empty, schooltime and stronghold also improved.

### 2. Endgame alpha consolidation into a single apex (length-decided maps: 50% → 69%)
v4.14 regularly finished with several big alphas (big_empty: [61, 53, 48, 48, 46]) or with none at all
(trauma: nobody to feed). Only the longest dragon counts at round 500.
- `feed_round()`: 415 on small maps, earlier on big ones (`500 - 1.5·n - 25`, so 385 on big_empty),
  giving feeders time to cross the map.
- **Alphas yield to superior alphas.** Superior means at least 2 longer, or within 1 and with a lower ID;
  the 2-segment margin stops near-equal alphas from both yielding. A yielding alpha feeds its whole body
  to the apex whenever `distance + own length` still fits in the rounds left (the apex needs about one
  round per segment to eat the trail).
- **Feeders go straight to the apex.** They target the longest reachable alpha, scored as `4·len − dist`,
  rather than the nearest, because every relay through an intermediate alpha loses half the mass. They
  deliver only to that alpha, anywhere inside its 7×7 view.
- **Apex rendezvous:** the apex walks toward a yielding alpha (length ≥5, within 24 tiles) instead of
  foraging away from it.
- **Apex election:** if no alpha is known when the endgame starts, the locally longest dragon declares
  itself alpha. Rival apexes then merge by the rule above.
- The apex ignores pearl "claims" by teammates in the endgame. Alpha lengths from sonar now overwrite
  older reports, so a split shows up as a shrink. Vision, which may see only part of a body, never
  lowers a tracked length.

### 3. Dead-end traps done properly (+4.6 points; arena 8/16 → 14/16)
- `is_dead_end_trap` used to call any pocket of 5+ tiles safe, and to treat every tile outside vision as
  an exit. A snake cannot reverse, so it dies in **any** exit-less region without a cycle it can circle,
  however big: a long 1-wide corridor full of pearls is a coffin. The new check floods the pocket (up to
  48 tiles) using remembered walls. It calls the pocket safe only if it finds an exit (a free portal,
  never-seen space, or a loop back past our neck) or a cycle in a region of at least length + 2 tiles.
- That strict rule is for dragons with mass to lose. Small dragons (length ≤3) are cheap, and maze
  branches are where much of the food is, so they only avoid tiny pockets (<5 tiles) and barren ones
  (<12 tiles, no pearls).
- **Kamikazes now respect dead ends too.** They used to skip every trap check and died in droves (30
  deaths in one corridor on stronghold). They still ignore the head-on "certain death" check, since a
  head-on is their job.
- **Split-child check:** a split child is born on our tail, facing away from us. `child_viable()`
  refuses a split that would spawn the child into a trap.

### 4. Combat (small, positive; each part rarely fires)
- **Zero-loss neck block** (`neck_block`): when an enemy head has exactly one exit, sprint *through*
  that tile so our neck, not our head, ends up on it. The enemy crashes into our body. v4.14's
  "corridor trap" put our head there, which is really a head-on 1-for-1, so that tier now only runs
  when the trade is worth it.
- **Value trades:** any non-alpha rams an enemy head at least 2 segments longer than itself, with a
  1-step collision or a 2–5 step sprint, as long as the team keeps at least 3 units. This is the
  "neutral kamikaze on a longer enemy" behaviour.
- **Split-and-kamikaze:** a dragon of length ≥5 with a longer enemy head within 3 tiles splits off its
  rear L−2 segments as a child, spawned away from the threat to vacuum the pearls afterwards. The
  2-segment front becomes a hunter (`hunter_until`) and rams the enemy's head. The same demotion
  applies after an emergency alpha split. In the benchmark this situation never came up (the results
  were identical game for game), so it is insurance, not a measured gain.

## Tried and rejected (all on the same 176-game set, compared with 72.7%)
| Idea | Result |
|---|---|
| Neutral dragons dodge the strike range of small enemy heads | 44% (costs far more foraging than it saves) |
| Skip pearls an enemy head is closer to | −5 points on elimination maps |
| Chase spawns up to 8 rounds ahead | −3.5 points |
| Newborns claim portals only when within 4 steps | −1.8 points |
| Portal claims expire after 40/80 rounds | no change |
| Never become a portal resident | 69.9% |
| Idle dragons stop walking to free portals | 55.1% (queen_of_spades 0/16) |
| Kamikaze regime triggers earlier (lead 3, ratio 1.3) / later (lead 8, ratio 2.0) | 69.3% / 68.8% |

## Known weak spots
- **queen_of_spades (7/16) and trauma (7/16).** Both maps hide their fastest pearls behind portals or in
  mazes. Which side wins there is decided mostly by early portal access, and v4.14 has the same side bias.
- **The engine's asymmetry.** Most losses are games the same side loses whichever of these bots plays it.
  Direction and ID tie-breaks settle the first ~30 rounds of economy.
