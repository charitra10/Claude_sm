# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

C++ bots for **UNSW Battlecode** (a snake-like "dragons" game on a toroidal grid: pearls, kelp walls, portals, splitting, sonar). Each `vX.Y/` directory is a self-contained, submittable bot snapshot created by `unswbc init`. The newest is `v5.10/`, built on `v5.9/` (itself built on `v5.8/`, `v5.7/`, `v5.6/`, `v5.5/`, `v5.4/`, `v5.3/`, `v5.2/`, `v5.1/`, `v5/`, then `v4.14/`). Older versions are kept so new ones can be benchmarked against them. Leave them untouched and do new work in the latest version directory, or copy it into a new `vX.Y/` directory.

- `unsw_battlecode_docs_llm_reference.md`: the full game rules, wire protocol, and judge/timeout rules. Read the relevant section before you change game logic.
- `strategyV_14.md`: design spec for the v4.14 strategy (roles, sonar packet layout, the decision cascade, and the scorer math). Its links point to an old path (`~/Documents/battlecode/v4.14/main.cpp`), but the line numbers match `v4.14/main.cpp` here.
- `strategyV_5.md`: what v5 changed on top of v4.14, and the benchmark results behind each change. Its numbers predate seeded pearls and are optimistic.
- `strategyV_5_1.md`: what v5.1 changed on top of v5, seeded benchmark results, and a table of ideas that were tried and rejected.
- `strategyV_5_2.md`: v5.2's nine switchable modules (`F_*` flags at the top of `v5.2/main.cpp`), the two new sonar tags (8190 hazard, 8189 portal reservation), ablations, the kamikaze-threshold re-sweep, and fresh-seed results. Raw results are in `bench52/*.jsonl`.
- `strategyV_5_3.md`: v5.3's eight modules on top of v5.2 (repulsion field, loop breakout, portal fixes, early alpha portal capture and handover, the no-split-across-a-portal invariant, long-body portal avoidance, hazard rotation), the 8188 handover sonar tag, root causes, ablations and fresh-seed results. Raw results are in `bench53/*.jsonl`.
- `strategyV_5_4.md`: v5.4's ten modules on top of v5.3 (round-0 split cascade, reverse split for trapped dragons, chamber camping, the 2x2 portal loop, chained-room traversal, dead-cell portal barring, 2-long skirmishers, feeder gate with true alpha lengths, surplus feeders), the 8187 barred-portal sonar tag, ablations and fresh-seed results. Raw results are in `bench54/*.jsonl`.
- `strategyV_5_5.md`: v5.5's five modules on top of v5.4 (sonar-chokepoint and map dead-end gating, one camper per chamber, harvest behind the tail, round-0 cascade fix, alpha handover to the larger split child), the 8186 mantle sonar tag, root causes, ablations and paired results against v5.4. Raw results are in `bench55/*.jsonl`.
- `strategyV_5_6.md`: v5.6's five modules on top of v5.5 (dry chamber eviction, greedy dead ends with the split at the tip, re-entry after a forced portal exit, map-symmetry inference and mirrored-hotspot scouting), the 8185 scout sonar tag, the switched-off round-0 mantle cascade, ablations and paired results against v5.5. Raw results are in `bench56/*.jsonl`.
- `strategyV_5_7.md`: v5.7's 13 requested modules (three measured harmful and shipped off: the 33-unit kamikaze cap, barren zones, hybrid exploration) and the fixes for the ladder bugs in `a1.png`..`a8.png` (memory routing, portal roaming, the slithery corridor split loop, hotspot sharing, feeder unblocking), with ablations and fresh-seed results (parity with v5.4-v5.6). Raw results are in `bench57/*.jsonl`.
- `strategyV_5_8.md`: v5.8's 11 requested modules (isolation safety, dead-end farms with the tail harvest, early kills,
  chamber-entrance mirrors, the trophy symmetry-packet bug, feed scoring, straddle splits, portal yielding, rich-spot
  seeking, one camper per pearl patch) plus the endgame trap avoidance and feed unguard that carry most of the gain, the
  8183 farm sonar tag, and fresh-seed results (72-78% vs v5.4-v5.6 on ladder maps). Raw results are in `bench58/*.jsonl`.
- `strategyV_5_9.md`: analysis of the ladder battle in `battle-M488787-replays/` (death causes, chamber occupancy, the
  six reported situations) and v5.9's fixes for them; parity with v5.8 in self-play. Raw results in `bench58/N1_*`, `N2_*`,
  `val59.jsonl`. Its second half covers the second brief (v5.9b: 12 items from replays `M510524`..`M514761`) and the
  kamikaze work (v5.9c: exchange strikes, support, flank approach; 64/96 vs v5.8), with results in `bench59b/`.
- `strategyV_5_9e.md`: behavioural audit of the eight v5.3 modules in v5.9 (does each do what it was built for, measured on
  replays and traces, independently of win rate), the fixes in `v5.9/main.cpp` ("v5.9e" flag block below
  `PORTAL_RESIDENCY`: `F_STRADDLE_SEEN`, `F_BREAKOUT_HOLD`, `F_HEIR_BEAM`), and fresh-seed results. Tools and metric
  summaries in `audit53/` (see its README), raw results in `bench59e/`.
- `strategyV_5_10.md`: v5.10's 13 requested modules (`F_*` flags in the "v5.10 modules" block of `v5.10/main.cpp`) from the
  ladder battle in `battle-M604164-replays/` plus `Aut_1.replay` / `Trpy_1.replay` (we are team B in those two), the 8179
  guard sonar tag, forward-selection ablations, fresh-seed results, and the analysis of why all ten battle games were lost.
  Raw results in `bench510/`. Turn numbers quoted from the replay viewer do not match `replay_tools/`: locate by ID and round.
- `replay_tools/`: decoder and analysis scripts for `.replay` files (packed Cap'n Proto): full board per turn, deaths by
  cause, chamber occupancy. See its README.
- `maps/maps/`: the current map set (the toolkit's bundled maps plus `help`, `small`, `queen_of_spades_but_she_ages`, `portals` and `slithery_fight`). Four top-level files in `maps/` (`arena`, `big_empty`, `default`, `stronghold`) are older versions. `maps_variants/` holds flipped/transposed copies generated from the old set.

## Commands

The `unswbc` CLI (a Python package installed with `uv tool install unswbc`) builds and runs everything. There is no separate build system, linter, or test suite.

```bash
# Run one match (rebuilds both bots, writes a .replay you can open in the VS Code viewer)
unswbc run maps/arena.map v5 v4.14
unswbc run --no-replay maps/arena.map v5 v4.14   # skip the replay file

# Benchmark two bots on every map, playing both sides, in parallel
./benchmark.py v5 v4.14
./benchmark.py v5 v4.14 --map trophy --single-side -j 4 --timeout 120

# Pearl respawns are random unless --seed is given (toolkit >= 1.1), so a (map, side) pair is no longer
# one fixed game. Use seedbench.py: explicit seeds, the same games for every candidate, JSON-lines output,
# timeouts retried rather than scored as losses. Keep -j at about 8 on a 16 GB machine.
./seedbench.py v5.1 v5 v4.14 --seeds 8 -j 8 --out results.jsonl
./seedbench.py v5.1 v5 --exclude help,big_empty --seeds 8   # screening: those two maps are half the CPU time
unswbc run --seed 7 maps/maps/trophy.map v5.1 v5            # replay one seeded game

# Price a bot in the judge's CPU-point sandbox (works for C++ too); budget is 100M points per turn
unswbc run --sandbox --no-replay maps/big_empty.map v5 v4.14

# Submit (needs `unswbc auth set bc_...`)
unswbc submit v5 -n <name> -d "<what changed>"
```

Always pass both bot directories to `benchmark.py`, because its defaults (`v3`, `v2`) no longer exist. It builds each bot once in place and then gives every worker symlinks to it plus a private copy of `.unswbc-build/`. Each bot directory has a `.unswbc-build/` build cache, which its `.gitignore` excludes.

## Bot architecture

- **`helper.hpp`** is the engine-protocol library (namespace `unswbc`). It is the same file in every version, so don't edit it unless the protocol changes. It provides `Controller` (one dragon's view: head, length, 7×7 `Vision`, sonar echoes, `make_move`/`make_moves` for sprints, split, `send_sonar`, `set_indicator_string`), `Game`, `Position` (wraps around the map), `Direction`, `Tile`, and `Edge` (open, kelp, or portal). `init()`, `update()`, and `end_turn()` run the turn loop.
- **`main.cpp`** holds everything in `bot::Brain`. Each turn does `decide()` → `Action` (moves, split child, `Mode`), then `execute()`. `main()` catches every exception and falls back to moving straight ahead, so a bug inside `decide()` shows up only as odd behavior, never as a crash.
- **Every dragon is its own process.** A split creates a new process with a new sequential ID, so no memory is shared between dragons. `DragonState` (per-cell memory `Cell[MAX_CELLS]`, portals, sighted alphas) is private to each dragon. **Sonar is the only way teammates can coordinate.** `alpha_packet64()` packs friendly-alpha ID/position/length, enemy-alpha sightings, and a kamikaze-regime bucket into the 64-bit sonar payload, which `observe()` decodes.
- **Roles** depend on length and ID. The "Alpha" is the large primary dragon that grows and stays alive. "Neutral" dragons forage and feed the alpha. "Kamikaze" (length 2) and sprint-hunter (length 3) dragons hunt enemies. The adaptive kamikaze regime (`check_adaptive_kamikaze`) estimates the enemy's unit count from the gap between our ID and our unit count. From `feed_round()` (415, earlier on big maps), smaller alphas and feeders deliver their mass to the longest alpha (the apex) and then die on purpose.
- **Turn flow:** `observe()` updates memory from vision and sonar. `paths()` runs a BFS over the known map, including portals. Then `decide()` works through a fixed priority list (feeding and sacrifice, emergency split, `guaranteed_kill`, escape, food and portal targets, exploration). If nothing earlier applies, it falls back to a scorer that weighs `danger()`, `space()`, `is_dead_end_trap()`, and `is_enemy_certain_death()`.
- Thresholds scale with the map through `map_scale()`. Test changes on both small maps (`arena`, 11×11) and large ones.

## Judge constraints that affect the code

- Compiled as C++20 with `-O2` and run as WebAssembly. Time is charged in **CPU points** (one per instruction), not wall-clock time. Keep the zero-allocation style: fixed arrays such as `MAX_CELLS` and `BFS_Q` and sparse scratch arrays, with no per-turn heap allocation in hot loops.
- Every stdout/stderr write costs about 2.5M points. The helper sets up a full buffer with `setvbuf` and flushes once per turn. Don't use `std::endl` or explicit flushes, don't call `sync_with_stdio(false)` or `cin.tie(nullptr)`, and don't use `std::unitbuf`. Debug with `std::clog`, and use `set_indicator_string` for per-turn labels in replays.
- A game lasts at most 500 rounds. The submission ZIP must be 4 MB or smaller, and `bot.toml` must `include` every source file.

# Battlecode Development Rules

## Project Objective

This repository contains our Australian Battlecode bot.

The primary objective when modifying v5 is to improve its measured performance
against the designated baseline opponents, especially v4.14 and v4.12.

Never claim an improvement without empirical benchmark evidence.

## Required Reading

Before making strategic or behavioral changes:

1. Read `unsw_battlecode_docs_llm_reference.md` completely.
2. Read `strategyV_14.md` completely.
3. Inspect the relevant v5 source code.
4. Understand the existing implementation before replacing or refactoring it.

`unsw_battlecode_docs_llm_reference.md` is authoritative for game mechanics and rules.

`strategyV_14.md` describes the intended strategy and architecture, but the actual
source code is authoritative for what is currently implemented.

Do not invent game mechanics.