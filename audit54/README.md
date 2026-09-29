# audit54: behavioural audit of the v5.4 modules in v5.9

Does each v5.4 module (`F_SPAWN_CASCADE` ... `F_SURPLUS`) still do what it was built for in `v5.9/main.cpp`, measured on
games rather than on win rate? The findings and fixes are in `strategyV_5_9_audit54.md`; these are the tools.

## Running traced games

`trace.py BOT OPPONENT --map a,b --seeds 1-4 [--side A] [--set F_X=false ...] --out DIR [-j 4]` builds a scratch copy of
BOT with `#define BOT_DIAG` (and any flag overrides), plays it against OPPONENT, and keeps per game `NAME.log` (only the
`DIAG` lines, the engine's death lines and the result) and `NAME.replay`. Game names are `map_sSEED_SIDE`; SIDE is the
team of the bot under test. Map names are exact (`queen_of_spades` does not match `..._but_she_ages`).

Stderr lines of different dragons interleave in random order, so two logs of the same game differ textually.
`same_game.py DIR1 DIR2` compares the replays' event streams instead: identical games mean the flag changed nothing.

## Checkers (each takes one or more trace directories)

| script | module | what it counts |
|---|---|---|
| `cascade_check.py REPLAY...` | `F_SPAWN_CASCADE` | unit lengths after round 0, round-0 deaths (lengths from split events) |
| `rescue_check.py [-v] DIR` | `F_REVERSE_SPLIT` | per rescue split: did the rear child live > 3 rounds; misses (4+ long deaths by kelp / self / body) |
| `loop_check.py DIR` | `F_PORTAL_LOOP` | per chamber exit: back in within `LOOP_TTL`, died, too long, chamber taken by a teammate |
| `family_check.py DIR` | loop / straddle split | head-ons between a parent and its split child born <= 5 rounds before |
| `chain_check.py DIR [--map=default]` | `F_CHAIN` | portal crossings from the replay; ping-pong = next crossing back through the same portal within 30 rounds |
| `trap_check.py DIR` | `F_PORTAL_TRAP` | dead-cell entries; crossings of portals the team had already barred (knew / did not know); newborn knowledge |
| `feed_check.py DIR` | `F_FEED_GATE`, `F_TRUE_LEN` | pearls dropped by feed suicides and who ate them (apex = longest teammate within 4 of the drop) |
| `deaths.py [--map=x] DIR...` | all | deaths per game by cause (killers.py categories), wins |

`common.py` patches `replay_tools/sim.py`'s loader so the pre-game `update` events are not replayed as moves (sim.Game
otherwise gives every spawn body two phantom segments until they reach the tail). `replay_tools/` itself is unchanged.

The DIAG lines these rely on (`rescue`, `camp`, `loopout`, `deadcell`, `deadcellknew`, `xcross`, `barrencross`,
`splitinfo`, `family`, `familyparent`, `cascadewalk`, `surplus`, `feedhold`, `feedgate`, `born`, `turn`) are compiled out
of the real build.
