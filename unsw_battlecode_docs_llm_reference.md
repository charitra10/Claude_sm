# UNSW Battlecode — Comprehensive LLM Reference

> **Purpose:** LLM-ready technical reference distilled from the official UNSW Battlecode documentation at `https://game.battlecode.au/docs/`.
>
> **Important:** This file is a **comprehensive paraphrase/reference**, not a verbatim reproduction of the website. It is intended to preserve the technical facts, rules, limits, commands, protocol details, helper API, standard-library constraints, map format, API endpoints, and operational behavior an LLM needs when reasoning about a bot.
>
> **Documentation snapshot:** 2026-09-22.
>
> **Official documentation index:** https://game.battlecode.au/docs/overview

---

## 0. Documentation coverage

The official documentation navigation exposes these 22 documentation pages:

### Intro
1. Overview
2. Quickstart
3. Submitting via Website

### Game Rules
4. Structure
5. Game Map
6. Pearls
7. Kelp and Portals
8. Vision
9. Movement
10. Splitting
11. Sonar
12. Death

### Competing
13. Game Format
14. ELO System

### Advanced
15. CLI
16. Execution Order
17. Timeouts
18. Standard Library
19. Helper Reference
20. IO Protocol / Wire Protocol
21. Map Files
22. API

The site uses the terms **IO Protocol** in its navigation and **Wire protocol** as the page title.

---

# 1. Overview

## Core premise

The game is a two-team competitive snake-like game played by controlling deep-sea-dragon automata. Pearls are the only resource. A dragon consumes pearls to grow longer. At the end of a game, the team with the strongest surviving dragon according to the game's scoring rules wins.

## Team/program model

- A team may have **up to 64 living dragons simultaneously**.
- The team submits **exactly one program**.
- Each dragon runs its **own copy/instance of that program**.
- Dragons do **not** share program memory.
- The only built-in dragon-to-dragon communication mechanism is **sonar**.
- The documentation gives an approximate baseline of **~25 ms per dragon turn**, but explicitly says this is not the precise judge budget. The authoritative computational rules are in the Timeouts page: the judge uses CPU points, not a normal wall-clock allowance.

## Snake-like differences called out by the overview

Compared with classic Snake:

- Dragons can **split into two dragons**.
- Dragons communicate only via **long-distance sonar**.
- A dragon can perform **multiple moves within one turn**, paying for additional movement with body length.

## Recommended reading order

The site points beginners toward:

- Structure for full game rules.
- Quickstart for creating the first bot.
- IO/Wire Protocol and Execution Order for engine-level behavior.
- ELO System for ratings after ladder submission.

---

# 2. Quickstart

## Recommended language

The docs recommend **Python** for beginners. Python must be installed locally.

## Install toolkit

The toolkit is the `unswbc` Python package.

Preferred installation with `uv`:

```text
uv tool install unswbc
```

To upgrade later:

```text
uv tool upgrade unswbc
```

Fallback with pip:

```text
pip install unswbc
```

The toolkit requires **Python 3.11 or newer**. On Debian/Ubuntu/Homebrew, ordinary system `pip` may reject the install with `externally-managed-environment`; the docs recommend using `uv` in that case.

## Check the local machine

Running:

```text
unswbc
```

makes the toolkit inspect the machine and report what it finds, including:

- Python interpreter
- C compiler
- C++ compiler
- replay viewer/editor support

Missing prerequisites are accompanied by the corresponding installation step.

## Create a bot project

`unswbc init` supports:

- Python
- C
- C++

A Python project contains:

- `main.py` — bot program
- `helper.py` — helper library for engine protocol I/O
- `bot.toml` — project language and file list

A C project uses `helper.h` + `helper.c` in place of Python's helper module.

A C++ project uses `helper.hpp` in the corresponding place.

Maps are not placed inside each bot project. They are kept in a shared sibling `maps/` directory that multiple bots can use.

Example project creation command:

```text
unswbc init python mybot
```

After upgrading the toolkit, `unswbc maps` can add newly bundled maps to the local maps directory without replacing your own maps.

## Run a local match

`unswbc run`:

- takes a map path and two bot projects;
- rebuilds both bots;
- runs the match;
- writes a `.replay` file.

The same project can be supplied for both sides to play against itself.

Starter map: `arena.map`, an **11×11** map.

Example:

```text
unswbc run maps/arena.map mybot mybot
```

## Replay viewer

When using VS Code, Cursor, or VSCodium, `init` installs/sets up the replay viewer when it detects the editor. Opening the `.replay` file shows the match.

Manual installation command:

```text
unswbc vscode
```

The visualizer shows the **whole board** and **all bot output/log lines**. Bots themselves do not have access to either of those complete views.

## Authentication

Create an API key on the team page. Keys begin with `bc_`.

Set the key locally:

```text
unswbc auth set bc_...
```

`unswbc auth status` identifies the team associated with the currently configured key.

## Submit

The CLI submission command is:

```text
unswbc submit mybot
```

The command packages the project and uploads a new submission.

Options referenced by the docs:

- `-n` — submission/version name
- `-d` — record what changed

The website submission page explains the build lifecycle.

## Starter/game-loop concept

The starter bot has a loop conceptually equivalent to:

1. initialize the controller/game state from the engine;
2. read/update a new turn;
3. decide the current action in an `execute_turn`-style function;
4. end the turn and flush output.

The helper's `init()` consumes initial spawn/process setup. `update()` reads a new turn and returns false once the game is over or that dragon has died. `end_turn()` completes the turn.

The starter behavior is deliberately simple: it randomizes the four directions using a deterministic seeded generator, then moves toward the first neighboring tile that is not blocked by kelp or another dragon. The starter logic exists for Python, C, and C++ so the three starter bots behave equivalently.

## Beginner testing advice

The docs explicitly recommend testing on **multiple maps**, including maps you create yourself to probe edge cases and special situations. The CLI page contains the command set.

---

# 3. Submitting via Website

## Submission basics

Bots may be submitted through the website's Submissions page or via `unswbc submit`.

- A team must exist/be joined before submitting.
- One build is active at a time and participates in ranked play.
- Upload is a ZIP archive.
- `bot.toml` is required at the **top level** of the ZIP.
- ZIP size limit: **4 MB**.
- Submission rate limit: **12 submissions per hour**.
- The active submission can be changed **unlimited times**.
- Older submissions remain available and can be activated again.

## `bot.toml`

The configuration declares:

- the project language;
- which files should be included in the uploaded archive.

The language tokens used by the configuration are documented elsewhere as:

- `py` for Python
- `c` for C
- `c++` for C++

The API's upload field uses language names `python`, `c`, and `cpp`; do not confuse the API field spelling with the `bot.toml` spelling.

For Python, the documented example includes all Python files via an include glob. The helper file must be included, as must every other file needed for the build.

## Submission states

| State | Meaning |
|---|---|
| Processing | Build in progress |
| Ready | Built successfully, not currently playing |
| Active | Built successfully and currently used for ranked matches |
| Build failed | Compilation failed or ZIP/layout/configuration was invalid |

A submission becomes active as soon as it successfully builds, replacing the previous active submission. Any Ready build can later be activated.

## Diagnosing build failure

Before asking for help, verify:

1. `bot.toml` is at ZIP top level.
2. The language selected in the website form agrees with the project configuration.
3. `include` contains every required file, including the helper.
4. Unzip the archive into an empty directory and run a match from that directory. If it cannot run locally in that extracted layout, the server build is likely to fail for the same reason.

For unresolved server-side issues, the docs direct competitors to Discord and recommend quoting the submission/version number.

---

# 4. Structure

## Rounds and turns

- A game lasts **at most 500 rounds**.
- During each round, every currently living dragon gets a turn.
- A turn is one dragon's time step and can contain one or more actions internally.
- Dragons act in **ascending dragon-ID order**.

Therefore, on dragon ID `i`'s turn:

- every living dragon with a lower ID has already acted in that round;
- every dragon with a higher ID has not yet acted.

This ordering matters for movement conflicts, newly spawned children, and sonar timing.

## Scoring / terminal states

Possible game outcomes:

- Team A wins
- Team B wins
- Draw

A team is eliminated when **all of its dragons die**.

Once either team is eliminated, the game immediately ends, regardless of the round number.

- exactly one team eliminated → the other team wins;
- both teams eliminated in the same round → draw.

If neither team is eliminated by the end of **round 500**, tiebreaking proceeds in order:

1. team with the **longest living dragon**;
2. if tied, team with the **greatest total length across all dragons**;
3. if still tied, **draw**.

---

# 5. Game Map

## Geometry

Maps are 2D rectangular grids.

- Width: **10–64 cells inclusive**.
- Height: **10–64 cells inclusive**.
- Top-left cell is `(0,0)`.
- `x` increases to the right/east.
- `y` increases downward/south.
- North is upward.

A bot can query map dimensions through the helper's game/map-size API.

## Symmetry

Every competition map is guaranteed to be symmetric for fairness.

Allowed symmetry types:

- vertical mirror;
- horizontal mirror;
- 180° rotation.

A dragon cannot conveniently ask the engine which symmetry type applies; the competitor is expected to infer or otherwise handle this from the map.

## Wrap-around topology

The map is topologically wrapped at its borders.

Moving beyond the east edge arrives from the west. Likewise, moving beyond the west/north/south edge wraps to the opposite side.

Visualizers depict this using dashed map borders. These wrap boundaries do not represent extra tiles; the equivalent opposite-side edges represent the same topology.

## Map composition

A map consists of:

- **tiles** (which may contain pearls, dragons, etc.);
- **edges between tiles** (which may be open, kelp, or portals).

Special edge behavior also exists on the map borders.

---

# 6. Pearls

## Resource

Pearls are the game's only resource and the only way for a dragon to increase its length.

## Pearl spawning

Each tile has a pearl countdown visible to any dragon that can inspect that tile.

Semantics:

- countdown = number of rounds until the tile next attempts to spawn;
- at the start of each round, countdown values decrease by 1;
- this tick happens before any dragon moves;
- when a countdown reaches zero, the tile attempts to spawn;
- the countdown resets **regardless of whether spawning succeeds**.

A spawn attempt succeeds only if the tile is empty:

- no dragon segment occupies it;
- no pearl already occupies it.

## Reset distribution

Every spawning tile has hidden map-defined parameters:

- `min_gap`
- `max_gap`

After each spawn attempt, the next countdown is sampled **uniformly and inclusively** from that range.

The gap parameters are **not directly visible to dragons**.

A tile with `max_gap = 0`:

- never spawns pearls;
- always reports pearl countdown `-1`.

## Symmetric spawning

On symmetric maps, a tile and its mirror image share the same countdown and therefore attempt to spawn at the same time.

At the spawn instant:

- if both mirrored tiles are free, both can receive a pearl;
- if one is blocked, the other can still spawn;
- one blocked mirror does not prevent the other from succeeding.

## Death interaction

When a dragon dies, it drops pearls from its body. See the Death section for exact placement/count.

---

# 7. Kelp and Portals

Kelp and portals are **edges**, not tiles. This includes edges along the map border.

## Kelp

Kelp is a barrier.

- A dragon cannot cross a kelp edge.
- Attempting to move across kelp kills the dragon with the `Hit Wall` reason.
- The dead dragon then follows normal pearl-dropping behavior.
- Kelp has no other special property; it is just a barrier/plant.

A normal safety check should therefore inspect the edge in the intended movement direction and, separately, whether the destination tile contains a dragon.

## Portals

Portals provide rapid traversal between two map edges.

Each portal has an ID which identifies its partner portal:

- IDs are non-negative integers.
- Exactly two portal edges share a given ID.

Portal constraints:

- the two connected portals have the **same orientation** (both horizontal or both vertical);
- both are on the map's symmetry line, or both are off it.

Those constraints avoid ambiguity under the map symmetry.

### Traversal

Portals are double-sided.

Entering from either side is legal and exits at the corresponding partner edge on the far side, with the orientation producing the appropriate emergence direction.

The docs give the horizontal example: entering a portal from the left causes the head to emerge from the right side of the partner portal, and vice versa. Vertical behavior follows the analogous orientation rule.

### Visibility

A dragon cannot see through a portal to the far side simply because the portal is visible.

The far-side area is only visible if it happens to fall inside the normal vision-radius rules from the dragon's current head position.

### Sonar

Sonar signals **do pass through portals**, unlike ordinary vision.

---

# 8. Vision

## Vision window

Each dragon sees only a **7×7** square centered on its head.

That means:

- total visible tiles = **49**;
- Chebyshev distance from head ≤ **3**;
- the window itself wraps around map edges because the board topology wraps.

## Portals and vision

Seeing a portal edge does not reveal the map on the opposite side of the portal.

The far side can only be seen under the normal geometric vision rule when it is actually within three tiles by the ordinary board distance used for visibility.

## Information provided for every visible tile

For each visible tile, the dragon receives:

- absolute board coordinates;
- current tile contents;
- whether a pearl is present;
- pearl spawn countdown;
- all four edges around the tile.

Dragon information includes visible segments from **all teams**, including the current dragon.

For each visible dragon segment, the helper/protocol exposes:

- team;
- dragon ID;
- whether the segment is a head;
- direction/facing of the segment.

Direction semantics:

- a head faces the direction it is currently heading;
- a body segment faces toward the next body segment toward its head.

## Coordinates

The coordinates in the vision data are already **wrapped board coordinates**, not local 0–6 window indices.

The helper returns `None`/equivalent when a queried position lies outside the 7×7 vision window.

## Practical navigation pattern

A simple visibility-aware bot can:

1. inspect the neighboring tile in each cardinal direction;
2. prefer a visible neighboring pearl;
3. otherwise choose a neighboring tile without a dragon;
4. avoid kelp edges and known collision squares.

This is an intentionally simple example pattern; it is not a recommended competitive strategy.

---

# 9. Movement

## Turn action model

Movement is an action.

Every dragon must output at least one action per turn. Only the eventual valid action that the engine accepts is applied.

The only other gameplay action type is **splitting**.

Actions are applied after the turn's command-reading phase completes.

## Standard movement

Each dragon can move one tile per ordinary move in one of the four cardinal directions:

- north;
- east;
- south;
- west.

The head moves first conceptually, and the rest of the body follows.

A dragon may move backwards relative to its current facing. This can cause its head to squeeze into its own neck and often results in death.

## Facing direction

For the head, facing is where the head is pointing.

For a body segment, facing points toward the next segment in the direction of the head.

## Eating pearls / growth

When the head enters a tile containing a pearl:

- the pearl is consumed automatically;
- the tail does **not** advance on that step;
- the dragon therefore gains one body segment / increases length by one.

Pearl consumption is the **only way to gain length**.

## Collisions

A dragon dies when its head moves into an obstacle, including:

- kelp;
- any segment of another dragon;
- its own body.

The collision condition is evaluated **before** ordinary movement/tail advancement.

Consequently, moving into the current location of one's own tail is fatal even if the tail would otherwise have moved away later on the same turn.

### Head-to-head

If the destination contains another dragon's head:

- both dragons die;
- this applies regardless of team.

If the destination contains an ordinary body segment of another dragon, only the moving dragon dies.

## Sprinting / multi-step movement

A dragon may provide several cardinal movement steps in one turn, potentially changing direction at each step.

Important consequences:

- collision checking happens **at every individual step**;
- pearl eating happens at every step;
- the intermediate board state matters;
- the same final destination can be safe for one step sequence and fatal for another.

The docs use `NNE` versus `ENN` as the conceptual example: both can target the same final area while differing in intermediate collision risk.

## Sprint cost

A multi-step move costs body length.

For `x` steps in a single turn:

**segments removed because of sprinting = `x - 1`**

So the first step is free from the sprint-specific body tax; each additional step costs one segment.

The helper's batch-move call transmits the whole direction sequence.

## Over-sprinting / death by sprint

A sprint longer than the dragon can afford is **not truncated safely**.

Instead:

- the dragon performs the steps it can pay for;
- when it reaches the next step it cannot afford, it pays for that step with its own life;
- this causes the dragon to die.

Execution Order contains the more exact per-step rule: after the first step, each additional step needs a dragon long enough to pay the sprint cost.

---

# 10. Splitting

## Basic split behavior

A dragon may split to create another dragon on the same team.

Minimum dragon length is **2**.

The child is built from the parent's **rear/tail segments in reverse order**:

- the parent's former tail becomes the child's head;
- the child faces away from the parent.

The parent keeps the front portion.

## IDs and process creation

The split child:

- receives the **next unused dragon ID**;
- takes its first turn later in the **same round**;
- is controlled by a **brand-new process/program instance**;
- does not inherit the parent's program memory or runtime state.

This means splitting is also a process-boundary event. The child should be treated as a fresh invocation of the bot.

## Legal split constraints

A split is legal only when all conditions hold:

1. Child length is at least **2**. Therefore `SPLIT 1` is always illegal.
2. Parent length after the split is at least **2**.
3. The team currently has fewer than **64 living dragons**.

Example: a dragon of length **6** can split off a child of length **2, 3, or 4**.

## Illegal split

Requesting an illegal split kills the parent with **no valid action**.

The helper exposes a legality-check operation so a bot can test before requesting the split.

---

# 11. Sonar

## Protocol requirement

The current sonar system is **protocol 3**. Protocol 3 adds:

- up to **four directed sonar beams per turn**, one each for N, E, S and W;
- **unsigned 64-bit** sonar payloads;
- **echo counts** reporting what each of the dragon's sonar beams stopped on.

Bots written for protocol 2 continue to run, but they cannot use the new directed/64-bit/echo features. New bots should use an up-to-date helper and protocol 3.

## Sending

Each turn a dragon may send **up to four sonars**, at most one in each direction: **N, E, S, W**. Each payload is an **unsigned 64-bit integer**. The engine casts the configured beams **after the dragon's action has been applied**, only if the dragon is still alive, and casts them in the fixed order **N, E, S, W**.

Each beam travels in a straight line from the head. Sonar:

- wraps around the board;
- passes through portals;
- stops at the first **kelp or dragon part** it encounters.

If a dragon part is hit, the payload is delivered to that dragon's sonar inbox. The sender itself can be the receiver.

A beam that reaches no dragon within **`WIDTH + HEIGHT` tiles** is lost.

### Direction is explicit

Protocol 3 no longer makes sonar direction implicit in the dragon's facing. The command supplies the direction explicitly:

```text
SONAR <N|E|S|W> <unsigned-64-bit-value>
```

The dragon's post-action facing still matters for the special self-body/refraction case described below, but a sonar beam is otherwise selected by its explicit command direction.

### Own-body refraction edge case

Dragons have extremely high refractive indices. If a sonar is sent **into the dragon's own body**, it undergoes Total Internal Refraction and exits through the tail, following the direction the tail points away from the body.

Example: a dragon facing east that issues a westward sonar sends that beam through its tail instead, in the direction the tail points.

### Duplicate direction

If two sonars are sent in the **same direction** in one turn, only the **last** one is sent. This is separate from having one sonar in each of the four distinct directions.

## Receiving and timing

Sonar messages are read at the **start of the recipient's next turn**. Because dragons act in ascending ID order:

- a receiver with **higher ID** than the sender can receive the message later in the **same round**;
- a receiver with **lower ID** than the sender receives it in the **following round**.

Several sonar messages may arrive before a turn and are exposed in the order the sonars were sent.

A sonar signal has **no sender ID or team metadata** attached to it. It can be received by a dragon on either team. Any team communication protocol, message addressing, sequencing, or sender identification must therefore be encoded by the bot itself.

## Loss/interception

A sonar contributes no received message when:

- the sender dies during its action/move, so its sonars are never cast; or
- the beam reaches no dragon within the `WIDTH + HEIGHT` limit; or
- another kelp/dragon part is encountered first and therefore stops/intercepts the beam.

The replay can still show a sonar ray even when it does not produce a received message.

## Echoes

Protocol 3 gives the **sender** echo information at the start of its next turn. For each sonar beam that was cast, the engine counts what caused that beam to stop. A beam that reached nothing contributes **no** count. The five counts are reported in this order:

1. `kelp`
2. `ally`
3. `ally_head`
4. `enemy`
5. `enemy_head`

Meaning:

| Echo field | Beam stopped because |
|---|---|
| `kelp` | it hit kelp |
| `ally` | it hit the body of a dragon on your team, including your own body |
| `ally_head` | it hit the head of a dragon on your team |
| `enemy` | it hit the body of a dragon on the opposing team |
| `enemy_head` | it hit the head of a dragon on the opposing team |

These are **aggregate counts**, not per-beam identities. They tell you how many sonar beams ended in each category, not which direction produced each category.

For example, if four beams are sent and two hit kelp, one hits an allied body, and one reaches nothing, the echo counts are `2 1 0 0 0`.

## Strategic implications for an LLM/bot

Protocol 3 substantially expands sonar as a communication and sensing primitive:

- Up to four payloads can be sent per turn, allowing multiple independent directions.
- 64-bit payloads allow much richer packed messages than protocol 2.
- Because delivery direction is explicit, a bot no longer needs to orient itself just to choose a sonar direction.
- Echoes provide categorical sensing about what lies along the sonar beams, but only as **aggregate counts**, not direction-tagged results.
- Because sender identity is absent, any multi-dragon protocol should encode a sender/role/sequence field into the payload when needed.
- Since echoes are returned to the sender on the next turn, they can be used as feedback for whether beams encountered friendly/enemy bodies or kelp.
- ID-ordered same-round vs next-round delivery still matters for coordination latency.

# 12. Death

## General death rule

A dragon dies when:

- its head collides with something that kills it;
- or its program fails to produce a valid action on its turn.

## Documented death reasons

| Reason | Trigger |
|---|---|
| Hit Wall | Move into kelp |
| Hit Self | Move into own body, including tail |
| Hit Other Body | Move into another dragon's non-head segment |
| Head to Head | Move into another dragon's head; both die |
| No Valid Action | Illegal/overlong sprint, illegal split, malformed/no action, or equivalent invalid turn |

## Mandatory action

A turn without a valid action is treated as suicide.

A dragon therefore dies if its program:

- crashes during the turn;
- prints nothing useful / no valid action;
- outputs malformed commands without ever producing a valid action;
- requests an illegal split;
- requests a sprint it cannot pay for.

Timeout behavior is special and is covered separately on the Timeouts page.

## Pearl dropping on death

On death, pearls crystallize from the dragon's body starting at the head and taking **every second segment**.

For length `L`, the number of dropped pearls is:

**`ceil(L / 2)`**

The pattern is:

- head segment → pearl;
- next body segment → skip;
- next segment → pearl;
- and so on.

Pearl countdowns on the affected tiles are **not changed** by the death drop.

---

# 13. Game Format

## Ranked vs unranked

### Ranked battle

- **5 games** per battle.
- Team ELO changes after the battle.
- Maps are chosen randomly by the competition server.
- Ranked battles include battles initiated directly by teams and autoscrim battles.

### Unranked battle

- Teams can choose the specific maps.
- One game is played for each chosen map.
- ELO and ladder ranking do not change.

The same team color is retained across every game in a battle, and replays are available afterward.

## Manual scrims

A team can request a battle against a particular leaderboard/team-profile opponent.

Battle outcomes are visible in the site's all-battles and per-team battle views.

All battles are publicly viewable, so teams should avoid leaking strategic information through publicly visible content.

### Requirements and restrictions

Both teams need an **active submission**.

For ranked manual battles:

- the ranked setting must be enabled on **both** teams;
- the target can be **no more than 50 rating points below** the requesting team;
- any opponent above the requesting team is targetable;
- the ranked setting has an **8-hour cooldown between flips**;
- you cannot start a second battle against a team while a previous battle against that team is still pending/running.

For manual games, the documented allowance is **60 games per hour**, equivalent to **12 five-game ranked scrims**.

## Autoscrim

Autoscrims are ranked battles and affect ELO.

The docs describe them as the majority of a typical team's games and therefore a major determinant of ladder movement.

Every **2 hours**, on the **UTC boundary**:

1. the ladder is sorted;
2. each eligible team is drawn an opponent from the **8 positions above and 8 positions below**;
3. the draw favors teams you have not played recently;
4. over a day, the system tends to expose a team to most of the surrounding ladder.

Eligibility:

- having an **active submission** is enough to enter autoscrim;
- the ranked switch under Settings applies only to manually requested ranked battles, not the autoscrim draw.

Each autoscrim is:

- a ranked **five-game series**;
- played on **five randomly chosen maps**;
- at most one autoscrim per team per draw round;
- does **not** consume the team's hourly manual-game allowance.

## Submitting a bot

Uploads cannot be deleted.

Exactly one submission is active at a time.

The active submission is what the game server uses for incoming/outgoing battles.

---

# 14. ELO System

## Rating basics

- One ladder.
- One ELO rating per team.
- Starting rating: **1500**.
- Ranked battles update both teams.
- Unranked battles do not.

## Meaning of rating

The rating is treated as a prediction of the expected share of games in a ranked five-game battle.

Important examples:

- equal ratings → predicted score **2.5 of 5**;
- a rating gap of **400 points** corresponds to an expected score of about **4.5 of 5** for the higher-rated team.

Rating changes compare actual battle performance against the expected share.

The two teams' rating changes are equal and opposite, so total ladder rating is conserved by a battle.

## Why winning can reduce rating

The actual quantity used is **fraction of games won**, not a boolean “won the battle” value.

Thus, in a five-game ranked battle:

- 3–2 = actual share 0.6;
- not 1.0.

Against a team 400 points lower:

- expectation is ~4.5/5;
- winning only 3–2 is still much worse than expectation;
- the higher-rated team can therefore lose rating despite winning the series;
- at that 400-point gap, even 4–1 is insufficient to gain rating; only a 5–0 sweep produces a gain.

The inverse applies to underdogs: outperforming expectation can produce a rating increase even when losing the battle overall.

## Formula

ELO uses:

- **K = 96**;
- update applied **once per battle**, not once per game.

For rating `RA` against opponent rating `RB`, expected score/share for A is:

`E = 1 / (1 + 10^((RB - RA)/400))`

Actual share is:

- fraction of battle games won;
- a draw counts as **0.5** for the actual-share calculation.

Rating change is:

`round(96 × (actual_share - expected_share))`

The two changes sum to zero.

The battle is settled only after **all games in the five-game series finish**, so a ranked five-game battle changes the rating once.

Autoscrims are ranked and therefore move rating.

## Example table from the docs

| Your rating | Opponent | Expected games of 5 | Result | Rating change |
|---:|---:|---:|---:|---:|
| 1500 | 1500 | 2.5 | 3–2 | +10 |
| 1500 | 1500 | 2.5 | 5–0 | +48 |
| 1900 | 1500 | 4.5 | 3–2 | −30 |
| 1500 | 1900 | 0.5 | 2–3 | +30 |

## Rating tiers

| Tier | Rating range |
|---|---:|
| Fishing Boat | 2800+ |
| Leviathan | 2500–2799 |
| Orca | 2200–2499 |
| Shark | 1900–2199 |
| Swordfish | 1600–1899 |
| Tunafish | 1300–1599 |
| Shrimp | 1000–1299 |
| Plankton | 0–999 |

---

# 15. CLI

The CLI command is `unswbc`.

The docs say `unswbc help <command>` exposes the options for a particular command.

## Installation

Preferred:

```text
uv tool install unswbc
```

Upgrade:

```text
uv tool upgrade unswbc
```

Pin a toolkit version example:

```text
uv tool install unswbc==0.3.0
```

Fallback:

```text
pip install unswbc
```

The docs repeat the Python 3.11+ requirement and the Ubuntu/Debian/Homebrew `externally-managed-environment` caveat.

## Commands

### `unswbc`

Lists available commands and checks for:

- Python;
- C compiler;
- C++ compiler;
- VS Code support;
- replay viewer.

### `unswbc init`

Creates a starter project in `c`, `cpp`, or `python` with helper code.

Maps live in the shared sibling `maps/` directory, not inside the bot directory.

Example:

```text
unswbc init python mybot
```

### `unswbc maps`

Adds bundled maps that are missing locally and leaves user-created maps alone.

Default directory: `maps`.

Alternative directory example:

```text
unswbc maps ../maps
```

### `unswbc run`

Runs one local game on a map between two bot projects, rebuilding them first and writing a replay.

The same bot path can be supplied twice.

Relevant flags:

- `-v` — show every round / verbose run output;
- `--sandbox` — price a Python bot using the judge's CPU-point sandbox locally.

Example:

```text
unswbc run maps/arena.map mybot mybot
```

### `unswbc vscode`

Installs the replay viewer/editor support for VS Code, Cursor, or VSCodium.

### `unswbc auth`

Stores/clears API credentials.

- `unswbc auth set bc_...`
- `unswbc auth status`
- `unswbc auth clear`

### `unswbc submit`

Archives and uploads a new submission.

Relevant options:

- `-n` — name the version;
- `-d` — record the change description.

Example:

```text
unswbc submit mybot
```

### `unswbc log`

Prints local toolkit errors useful for bug reports.

API keys are masked.

---

# 16. Execution Order

This page is critical for understanding race conditions and seemingly surprising replays.

Two principles dominate:

1. Dragons act one at a time in **ID order**.
2. A multi-step movement is a sequence of **separate movement steps**, not one atomic teleport.

## Round-level order

At the start of a round:

1. **Pearl ticking** happens.
   - Countdown decrements by one.
   - Processing order is top row to bottom row, and left to right within each row.
   - A zero countdown attempts to spawn if the tile is free.
   - Countdown resets whether spawn succeeds or fails.
2. Every living dragon acts in **ascending ID order**.
   - Dragons that already died this round are skipped.
   - A dragon created by a split is appended to the live list and later receives its turn in the same round.
3. The round ends.
4. The game ends if:
   - a team has no dragons;
   - or this was round 500.

## One-turn order

For a dragon's turn:

1. Its sonar inbox is supplied to the bot and emptied.
2. Default command state is initialized:
   - action = suicide/no valid action;
   - sonar = none;
   - indicator = none.
3. Engine reads bot stdout line-by-line until `ENDTURN`.
4. The selected action is applied.
5. Sonar is cast if configured and the dragon survived.

Conceptual output sequence:

- log/visual indicator commands;
- movement or split command;
- sonar command;
- `ENDTURN`.

## Command semantics

- `MOVE`, `SPLIT`, `SONAR`, and `INDICATOR` are last-write-wins command channels.
- The action/sonar/indicator values are not applied until command reading finishes.
- `LOG`, `DOT`, and `LINE` are applied immediately and in printed order.
- A line the engine cannot parse is skipped and logged by the engine; it does not replace the action already selected.

Thus, for repeated movement commands, the last one selected before `ENDTURN` is the movement that actually executes.

## Exact multi-step movement algorithm

For each step in the requested sequence, in order:

1. If it is **not the first step**, verify the dragon can pay the additional sprint cost. A length-2 dragon cannot pay for a second step and dies from having no valid payable action.
2. Set the dragon's facing to the current step direction.
3. Determine the destination across the currently faced edge:
   - kelp → death;
   - portal → exit from the corresponding partner edge;
   - otherwise → adjacent tile, with map wrapping.
4. Check the destination:
   - own body first;
   - then other dragons;
   - another dragon head causes the appropriate head-on death behavior;
   - any ordinary body segment kills the mover.
5. Move the head.
6. Eat a pearl if present.
7. Advance the tail unless a pearl was eaten.
8. If this was not the first step, remove one additional body segment to pay sprint cost.

A death on any step:

- discards all remaining requested steps;
- discards the turn's sonar;
- preserves all movement already completed earlier in the same turn.

## Own-tail rule

The self-collision test includes the current tail and occurs **before** tail movement.

Therefore the tail tile is blocked for that step.

Within a multi-step movement, the second step is evaluated against the body arrangement produced by the first step. So a tile that would be fatal as the first step can be safe later in the same sequence if the first step changed the body occupancy.

## Split execution

Split legality is checked before movement.

Conditions:

- child ≥ 2;
- parent after split ≥ 2;
- team count < `UNIT_LIMIT`.

An illegal split kills the parent without a valid action.

A successful child is the reversed parent rear section, gets the next ID, and runs later in the round as a fresh process.

## Sonar execution

Sonar is cast **after** action application and only while the dragon is alive. Protocol 3 allows up to four beams, one per direction N/E/S/W, and the engine casts the selected beams in exactly that order.

Each beam:

- starts from the head;
- uses the explicitly requested direction;
- wraps around the map;
- crosses portals;
- stops at kelp or the first living dragon part;
- may self-intercept, in which case the own-body refraction rule can send it out through the tail.

A receiver gets the 64-bit value in its sonar inbox at the start of its next turn.

ID-order consequence:

- higher receiver ID → same-round receipt;
- lower receiver ID → next-round receipt.

The sender also receives five aggregate echo counts on its next turn, corresponding to kelp, ally body, ally head, enemy body, enemy head. A beam that reaches nothing contributes no echo count.

## Death sequence

Upon death:

1. Record death reason.
2. Turn every second body segment (starting with the head) into a pearl.
3. Leave pearl countdowns unchanged.
4. Remove the dragon from the board.
5. Discard remaining movement steps and sonar for that turn.

For head-on collision, the documentation specifies that the other dragon's death sequence runs first, followed by the current dragon's sequence.

## Turn end conditions

A turn ends at the first of:

- bot prints `ENDTURN`;
- bot blocks trying to read its next turn;
- bot exits;
- bot reaches its execution limit.

If the turn ends normally via `ENDTURN` or the next-read blocking condition, its output is delivered to the engine.

If the bot exits or hits its limit:

- nothing is delivered;
- default suicide action remains;
- the dragon dies;
- the bot process is restarted as a fresh instance for any dragons the team still controls.

---

# 17. Timeouts

## Local run behavior

Normal `unswbc run` executes bots as ordinary local processes.

Current documented local limit: **10 seconds wall-clock per turn**.

If a bot:

- reaches the 10-second wall clock;
- or exits during a turn;

then the turn delivers no output, the dragon dies, and the process is restarted for any remaining dragons controlled by that program.

The docs explicitly warn that passing a normal local run does **not** prove judge-budget compliance.

## Local sandbox

Use:

```text
unswbc run --sandbox
```

for Python bots to be priced under judge-like CPU-point accounting.

With `-v`, the local sandbox reports the CPU points and memory used per turn.

C and C++ cannot currently be sandboxed locally according to the docs.

## Judge environment

The judge uses a **WebAssembly sandbox** and accounts for work in deterministic **CPU points**, not real elapsed time.

Because instruction pricing is fixed, machine load should not change a bot's budget result.

### Hard limits

| Resource | Limit |
|---|---:|
| CPU points per dragon per turn | 100,000,000 |
| Memory per dragon | 48 MB |
| Threads per dragon | 1 |
| CPU backstop | 1 s CPU |
| Wall backstop | 10 s wall |

`thread_spawn` is denied.

The backstop is mainly for bots that somehow evade/escape the normal meter.

A turn that exhausts its CPU points:

- is stopped;
- delivers no reply;
- kills the dragon for that turn.

Leave budget margin rather than aiming for exactly 100 million.

## First-turn cost

The first turn is **not free**.

The interpreter and NumPy are preloaded, but your own imports and initialization are charged.

Recommended design pattern:

- spread heavy precomputation across turns;
- or perform it lazily when needed.

## What counts toward a turn

The budget window runs from reading the round input through the flush in `end_turn()`.

That is what verbose CPU accounting reports.

Work done after `end_turn()` but before the next round is read is charged to the **following turn**. Therefore doing “precompute for next turn” in this gap does not escape accounting.

## Instruction price table

Anything not specifically named in the instruction table costs **2 points**.

| WebAssembly operation/group | Points |
|---|---:|
| Constants; local/global get/set/tee; select; nop; drop; block/loop/end/else/unreachable | 1 |
| Comparisons (integer and float) | 1 |
| Integer/float add, subtract, multiply, bitwise, shifts, rotates, `clz`, `ctz`, `popcnt`, `abs`, `neg`, `ceil`, `floor`, `trunc`, `nearest`, `sqrt`, `min`, `max`, `copysign` | 1 |
| Numeric conversions: wrap, extend, truncate, convert, promote, demote, reinterpret | 1 |
| `ref.null`, `ref.is_null`, `ref.func` | 1 |
| Loads/stores, all widths | 2 |
| `memory.size` | 2 |
| `br`, `br_if`, `if`, `return` | 2 |
| Everything else, including all 128-bit SIMD instructions | 2 |
| Integer/float division and remainder | 3 |
| `br_table` | 3 |
| `call` | 4 |
| `call_indirect`, `call_ref`, `return_call`, `return_call_indirect`, `return_call_ref` | 6 |
| `memory.copy`, `memory.fill`, `memory.init` | 10 + 1 per 8 bytes |
| `table.get/set/size/copy/fill/init`, `data.drop`, `elem.drop` | 10 |
| `memory.grow`, `table.grow` | 50 |

Judge work performed on the bot's behalf is also charged.

### Charged host calls

| Call | Cost |
|---|---:|
| stdout/stderr write | 2,500,000 + 4,000 per byte |
| random-byte generation | 40,000 + 3 per byte |
| opening a file or looking up a path | 40,000 |

Reference costs from the docs:

- parsing one round in the Python helper ≈ **10 million points**;
- flood fill of a **32×32** map ≈ **19 million points in Python**;
- same flood fill ≈ **0.3 million points in C++**.

## Output cost

Each write to stdout/stderr costs:

**2.5 million points + 4,000 points per byte**

Helpers normally flush once per turn via `end_turn()`; reading the next turn also flushes.

A bot that explicitly flushes every printed line incurs the fixed 2.5-million-point write cost on every line, which is extremely expensive.

The judge presents stdout as a terminal (`isatty()` is true), so ordinary line buffering can make each line an individual write.

C and C++ helpers counter this by switching to a full buffer with `setvbuf()` during initialization, allowing an entire turn's output to be sent in one flush.

The docs' example illustrates why batching matters: ten log lines can cost ~2.5 million points batched, versus ~25 million if emitted as ten independently flushed writes.

### C/C++ buffering cautions

Do **not**:

- enable `std::unitbuf`;
- call `sync_with_stdio(false)` when relying on the helper's buffer;
- call `cin.tie(nullptr)` unless you take responsibility for flushing at `ENDTURN`.

For debugging, the docs recommend `std::clog` instead of `std::cerr`.

Python does not need these special buffering changes because its stdout is already fully buffered.

## Language compilation

C/C++:

- C17 / C++20;
- `-O2`;
- `-msimd128` in judge;
- own ZIP directory is placed on the include path.

Python:

- CPython 3.13;
- NumPy 2.5;
- no additional installed third-party packages.

## Determinism

Matches are designed to be reproducible.

Randomness is seeded per dragon and per game, and the virtual clock advances according to budgeted work.

`time.sleep()` does not consume real wall time in the ordinary sense; it advances the virtual judge clock by the requested sleep duration.

This lets timing APIs be used to observe CPU-point expenditure rather than machine-speed variability.

## Budget-per-turn vs per-tile

The CPU budget is per turn, not per movement tile.

A multi-step `MOVE` sequence can cover multiple tiles under one bot decision, while the movement itself consumes body length rather than separate CPU budgets per tile.

---

# 18. Standard Library

## Sandbox policy

The judge runs the bot in WebAssembly with:

- no network;
- no child processes;
- no writable disk.

Some modules that exist only to access prohibited facilities are not shipped at all. Others import successfully but fail at runtime when a forbidden operation is attempted.

## Language versions

| Language | Judge | Local machine selection |
|---|---|---|
| Python | CPython 3.13 + NumPy 2.5.3 | first working of `python3`, `python`, `py -3` |
| C | Clang 20 as C17 against wasi-libc | first working of `cc`, `gcc`, `clang`, `cl` |
| C++ | Clang 20 as C++20 against libc++ | first working of `c++`, `g++`, `clang++`, `cl` |

Both judge and local compile modes use:

- `-std=c17` or `-std=c++20`;
- `-O2`.

Judge adds `-msimd128` and includes the uploaded project directory on the include path.

Environment variables controlling local tool selection:

- `UNSWBC_PYTHON`
- `CC`
- `CXX`

The toolkit itself requires Python 3.11+ even if the bot language is C/C++.

## Python availability

The judge provides CPython 3.13 and NumPy 2.5.3 and no other third-party Python packages.

Pure-Python third-party dependencies must therefore be **vendored into the ZIP**.

The bundled Python modules explicitly listed by the docs are:

```text
__future__
abc
argparse
array
ast
atexit
base64
bdb
binascii
bisect
builtins
calendar
cmath
cmd
code
codecs
codeop
collections
colorsys
compileall
configparser
contextlib
contextvars
copy
copyreg
cProfile
csv
ctypes
dataclasses
datetime
decimal
difflib
dis
doctest
encodings
enum
errno
faulthandler
fcntl
filecmp
fileinput
fnmatch
fractions
functools
gc
getopt
getpass
gettext
glob
graphlib
hashlib
heapq
hmac
importlib
inspect
io
itertools
json
keyword
linecache
locale
logging
marshal
math
mmap
modulefinder
netrc
numbers
opcode
operator
optparse
os
pathlib
pdb
pickle
pickletools
pkgutil
platform
posix
posixpath
pprint
profile
pstats
pty
pwd
py_compile
pyclbr
queue
quopri
random
re
reprlib
rlcompleter
runpy
sched
secrets
select
selectors
shlex
shutil
signal
site
stat
statistics
string
stringprep
struct
symtable
sys
sysconfig
tabnanny
tarfile
tempfile
termios
textwrap
time
timeit
token
tokenize
tomllib
trace
traceback
tracemalloc
tty
types
typing
unicodedata
unittest
uuid
warnings
wave
weakref
zipapp
zipfile
zipimport
zoneinfo
```

Modules whose names start with underscore are omitted from that displayed list but still exist under CPython (examples mentioned: `_thread`, `_pyrepl`, `_collections_abc`, and others).

## Python modules not shipped

### No network

```text
socket
socketserver
ssl
http
urllib
xmlrpc
ftplib
smtplib
poplib
imaplib
telnetlib
wsgiref
ipaddress
email
cgi
cgitb
webbrowser
```

### Multiple processes/threads disallowed

```text
asyncio
multiprocessing
concurrent.futures
```

### Compression libraries not built

```text
zlib
bz2
lzma
gzip
```

### Parsers/data stores not built

```text
xml
sqlite3
dbm
plistlib
mimetypes
mailbox
importlib.metadata
```

### Terminal/display support removed

```text
curses
readline
tkinter
turtle
```

### Packaging/test tooling removed

```text
ensurepip
venv
distutils
lib2to3
test
idlelib
```

## Python modules present but constrained

### `threading`

- `Thread.start()` raises `RuntimeError: can't start new thread`.
- Locks/events still work.
- Timed acquire returns immediately because the clock is virtual.

### `subprocess`

- `Popen` raises `OSError` 58 / `ENOTSUP`.
- `os.fork` likewise fails.

### `shelve`

Opening a shelf fails because `dbm` is absent.

### `pydoc`

Does not import because it reaches for unavailable `urllib`.

### `zipfile` / `tarfile`

The modules import, but without `zlib` only stored/uncompressed members can be read. Deflated/gzipped/xz-compressed members fail.

## C and C++ environment

- Clang 20.
- C17 / C++20.
- wasi-libc and libc++.
- Every libc++ header except `<generator>`.
- POSIX headers shipped by the sysroot are present, including `<sys/socket.h>`, `<pthread.h>`, `<spawn.h>`.
- They may compile/link but runtime calls behind prohibited capabilities fail.

Examples:

- `socket()` → `ENOTSUP`.
- `std::thread` → throws `Resource temporarily unavailable`.
- `std::system` → returns `-1`.

No third-party C/C++ libraries are installed. Vendoring source code into the ZIP is the required approach for extra dependencies.

## Common runtime restrictions for both C and C++

| Area | Sandbox behavior |
|---|---|
| Network | socket calls return `ENOTSUP` |
| Processes/threads | `fork`, `exec`, `posix_spawn`, thread creation denied |
| Filesystem | read-only |
| Bot working directory | `/bot` |
| `/tmp` | not writable |
| Environment | five `PYTHON*` variables + `TERM=dumb` |
| `getpid()` | 1 |
| `os.cpu_count()` | 1 |

The bot receives access to its own uploaded files under `/bot`, but cannot write to disk in the sandbox.

## Virtual time

Time is not simply disabled; it is replaced by the judge's virtual clock.

At zero CPU expenditure the clock is:

`2026-01-01T00:00:00Z`

It then advances by:

- **1 nanosecond per CPU point spent**;
- plus explicit requested sleep duration.

This means one full 100-million-point turn corresponds to roughly **0.1 seconds** of virtual elapsed CPU time.

Useful consequences:

- `time.perf_counter` and `std::chrono::steady_clock` can measure budget consumption;
- `time.sleep()` returns without real waiting but advances virtual time;
- timing across a sleep is not useful for comparing CPU work;
- repeated runs of the same match produce the same clock readings.

## Randomness

The judge owns the random stream.

The docs state that:

- `os.urandom`;
- Python `random`;
- C++ `std::random_device`;
- and `std::mt19937` seeded from that source

all derive from a single reproducible stream **per dragon, per game**.

Therefore replaying the same match reproduces random choices.

`std::random_device::entropy()` returns **0**.

Python's `PYTHONHASHSEED` is fixed to **0**, so hashing and set/dict ordering are stable.

If different dragons should have different independent pseudo-random behavior, explicitly seed an application-level generator rather than assuming global randomness will separate them.

---

# 19. Helper Reference

The helper is the convenience library generated by `unswbc init`.

Key design point: the helper is **your code**. You may edit it or replace it entirely with your own wire-protocol implementation.

The documentation describes the helper shipped by the **current toolkit**. Older projects keep the helper they were created with, so a missing API call may mean the project needs to be regenerated from a newer `unswbc init`.

Python, C, and C++ expose the same conceptual API using language-specific naming.

## Fixed constants / engine properties

- maximum rounds: **500**
- vision window: **7×7**
- spawn length: **3**
- minimum dragon length: **2**

## Direction

Represents the four compass directions.

- `value()` — protocol letter `N`, `E`, `S`, or `W`.
- `get_direction_list()` — fresh list of N/E/S/W.
- `get_offset()` — one-step `(dx, dy)` with y increasing downwards.
- `get_opposite()` — reversed direction.
- `get_left()` — 90° anticlockwise.
- `get_right()` — 90° clockwise.

## Team

- `get_enemy_team()` — other team.

## Position

Represents `(x,y)` board coordinates.

- `add_dir(direction)` — one wrapped step in the supplied direction.
- `is_in_map()` — whether the coordinates are on the board; especially relevant to coordinates computed by the bot itself rather than guaranteed engine-visible positions.
- `is_in_vision()` — whether the position lies in the current 7×7 visible window.

## Game

Global board state shared conceptually by all dragons.

- `get_round_num()` — current round; game ends after 500 rounds.
- `get_map_size()` — `(width, height)`.
- `get_unit_limit()` — max simultaneously living dragons on one team.

## Vision

- `get_tiles()` — all **49** visible tiles in row-major order from top-left of the window.
- `get_tile(pos)` — tile at a board position, or `None` if outside the visible window.

## Tile

Represents one board square plus the edges around it.

- `edges()` — dictionary of the four edges keyed by direction.
- `get_edge(direction)` — edge crossed when stepping that way.
- `get_dragon()` — visible dragon segment on the tile, or `None`.
- `has_pearl()` — whether a pearl is present now.
- `get_pearl_time()` — rounds until next spawn attempt; `-1` if the tile never spawns.
- `get_position()` — absolute board position.

## DragonPart

Represents one visible segment.

- `get_position()`
- `get_id()` — owning dragon ID
- `get_team()`
- `get_dir()` — head faces its heading; body segment points toward head
- `is_head()` — whether segment is a head; entering another dragon's head causes a head-on collision result.

## EdgeType

Possible values:

- `EMPTY`
- `KELP`
- `PORTAL`

## Edge

Represents the boundary between two tiles.

- `is_passable()` — whether a dragon can cross; only kelp blocks crossing.
- `is_portal()` — whether crossing leads through a portal to its partner edge.
- `get_edge_type()` — EMPTY/KELP/PORTAL.
- `get_portal_id()` — shared portal ID; `-1` if not a portal.

## Controller

Represents the dragon controlled by the current process.

### Identity/state

- `get_length()` — current segment count, including head.
- `get_unit_count()` — living dragons on the team, including this dragon.
- `get_head()` — current head `DragonPart`.
- `get_id()` — dragon ID, also its turn-order position.
- `get_team()` — own team.
- `get_dir()` — head direction at the start of the current turn.
- `get_vision()` — current 7×7 vision object.
- `get_tiles()` — all 49 current visible tiles.
- `get_tile(pos)` — visible tile lookup, or `None` when outside the window.
- `get_position()` — current head position, already wrapped.

### Actions

- `make_move(direction)` — request a one-step move; if multiple action commands are sent, the applicable last action is used.
- `make_moves(directions)` — multi-step sprint, one board move per direction; `n` steps cost `n-1` segments, so the dragon must be long enough.
- `can_split(child_size)` — check whether the requested child size is legal this turn.
- `do_split(child_size)` — split tail/rear segments into a reversed child.

### Diagnostics/visualization

- `output_log(...)` — attach a log line to the turn in the replay; each line is expensive under judge accounting.
- `draw_indicator_dot(pos, r, g, b)` — draw a replay-only board dot; does not affect gameplay.
- `draw_indicator_line(start, end, r, g, b)` — replay-only line.
- `set_indicator_string(message)` — label this dragon with text for the turn.

### Sonar

- `get_sonar_messages()` — received sonar payloads since the previous turn, in send order. With protocol 3 these are **unsigned 64-bit** values.
- `get_sonar_echoes()` — returns the protocol-3 aggregate echo counts for the sonars your dragon sent on the previous turn. The fields correspond to `kelp`, `ally`, `ally_head`, `enemy`, and `enemy_head`.
- `send_sonar(direction, message)` — send an unsigned 64-bit payload explicitly in direction N/E/S/W. At most one sonar per direction is retained; a later send in the same direction overwrites the earlier one.
- Legacy `send_sonar(message)` — still supported for compatibility; sends a **32-bit** value in the dragon's facing direction. Prefer the directed protocol-3 form.

## Helper module-level functions

The helper module is imported as `unswbc` in the Python starter project.

- `init()` → `(Controller, Game)`; consumes the process's initial spawn/setup block.
- `update(controller, game_state)` → boolean; reads the next turn and returns false when the game is over or the dragon has died.
- `end_turn()` → finishes the turn and flushes printed output.

---

# 20. IO / Wire Protocol

## Protocol version

**Wire protocol version: 2.1.0**

The engine uses plain text:

- engine → bot via stdin;
- bot → engine via stdout.

The helper only parses/prints this protocol and can be replaced by a custom implementation.

Blank lines may be skipped.

The C and C++ helpers strip text after `#` as comments.

## Init block

Sent once whenever a bot process starts.

Conceptual structure:

```text
ID <dragon-id>
TEAM <A-or-B>
MAP <width> <height>
UNIT_LIMIT <limit>
```

Fields:

- `ID` — unique dragon ID within the game.
- `TEAM` — `A` or `B`.
- `MAP` — width then height.
- `UNIT_LIMIT` — maximum living dragons per team; normally/default 64.

Important process semantics:

- a split child receives a fresh init block;
- a restarted bot process receives a fresh init block;
- the init block marks a **process start**, not a new game.

## Per-turn round block

Sent once per turn.

### 1. Header

Fields occur in this order:

```text
ROUND <round>
DIR <N|E|S|W>
LENGTH <length>
UNIT_COUNT <living-team-dragons>
NUM_MSGS <count>
<sonar-value-0>
<sonar-value-1>
...
```

`NUM_MSGS` determines how many following lines hold **unsigned 64-bit** sonar values for protocol 3. A protocol-2 dragon only receives values up to `4294967295`; larger values are omitted rather than buffered for later.

In protocol 3, the header continues with an `ECHOES` line containing five counts in this order: `kelp ally ally_head enemy enemy_head`.

### 2. Visible tiles — 49 lines

After the header come **49 tile lines**, representing the 7×7 vision window row by row.

- row 0 = three tiles north of the head;
- column 0 = three tiles west of the head.

Each line has:

```text
x y hasPearl pearlIn
```

Where:

- `x`, `y` — already-wrapped absolute board coordinates;
- `hasPearl` — whether a pearl is present now;
- `pearlIn` — rounds to next spawn attempt; `-1` means never spawns.

### 3. Visible dragon bodies

Structure:

```text
DRAGON_BODIES <count>
<team> <id> <x> <y> <facing> <isHead>
...
```

There is one entry for every visible segment of every currently living dragon, including the current dragon.

- `team` — A/B;
- `id` — owning dragon ID;
- `x y` — board coordinates;
- `facing` — head heading or body direction toward head;
- `isHead` — head marker.

### 4. Horizontal edges

There are **8 lines of 7 edge symbols**.

These represent:

- the north edge of each tile row;
- then the south edge of the final tile row.

### 5. Vertical edges

There are **7 lines of 8 symbols**.

These represent:

- the west edge of each tile column;
- then the east edge of the final column.

## Edge symbols

| Symbol | Meaning |
|---|---|
| `.` | open edge |
| `w` | kelp |
| integer | portal, with that integer identifying the partner edge |

A parser must split on **whitespace**, because portal IDs may contain multiple digits.

## Reply format

Bot output consists of lines ending with:

```text
ENDTURN
```

followed by a flush.

One flush per turn is the efficient strategy.

### Commands

| Command | Arguments | Effect |
|---|---|---|
| `MOVE` | N/E/S/W letters | move/sprint; a sequence such as NNE is three steps |
| `SPLIT` | child segment count | split rear segments |
| `SONAR` | `N|E|S|W` plus unsigned 64-bit integer | send a directed sonar after action; one retained per direction |
| `INDICATOR` | text | set dragon label for this turn |
| `LOG` | text | attach text to this turn |
| `DOT` | x y r g b | draw replay-only dot |
| `LINE` | x1 y1 x2 y2 r g b | draw replay-only line |
| `ENDTURN` | none | stop command reading |

Movement/split/sonar/indicator values are selected using the last valid value supplied in their respective command channel before the turn ends; drawing commands execute immediately as output is read.

## Ending the process

When a dragon's game has ended, the engine closes stdin.

Reading another input line should therefore reach EOF and the process should exit.

The helpers also accept `ENDGAME`, although the current engine **does not send it**.

---

# 21. Protocol 3 / Sonar Upgrade

## Current protocol

The official protocol is now **version 3**. Protocol 3 adds **directed 64-bit sonar** and **echoes**. Protocol-2 bots still run, but they cannot use the new features.

## Protocol 2 vs Protocol 3

| Feature | Protocol 2 | Protocol 3 |
|---|---|---|
| Sending | `SONAR <value>`: one 32-bit value sent in the dragon's facing direction | `SONAR <N|E|S|W> <value>`: up to four 64-bit values, one per direction |
| Receiving | Only values ≤ `4294967295`; larger values are left out | All unsigned 64-bit values |
| Echoes | None | `ECHOES` line after sonar messages |

Both `SONAR` forms are accepted by the engine at every version. The protocol version controls what the engine sends back to the bot.

## Upgrading with the helper

Update the toolkit and run:

```text
unswbc update my_bot
```

The update replaces only helper files, keeps each old helper as a `.bak` file, and does not change the main source file. The current helper requests protocol 3 in `end_turn`.

Old sonar calls remain compatible:

- `send_sonar(message)` (C: `unswbc_send_sonar(message)`) sends a 32-bit value in the facing direction.
- `send_sonar(direction, message)` is the new directed form.
- `get_sonar_echoes` (C: `unswbc_sonar_echoes`) reads the five echo counts.
- C directed send is `unswbc_send_sonar_to`.

Sonar payloads in protocol 3 are 64-bit. In C++ or C code, change message containers/pointers from 32-bit types to 64-bit types as needed; for example, `std::vector<std::uint32_t>` → `std::vector<std::uint64_t>`, or use `auto`.

## Upgrading without a helper

To upgrade a custom wire-protocol reader:

1. Print `PROTOCOL 3` before `ENDTURN`.
2. The change applies **from the next turn onward**.
3. Read sonar values as **unsigned 64-bit integers**.
4. After the sonar messages, read the `ECHOES` line.
5. The five echo counts are ordered: `kelp`, `ally`, `ally_head`, `enemy`, `enemy_head`.

Example reply:

```text
SONAR E 1806
SONAR W 18446744073709551615
MOVE N
PROTOCOL 3
ENDTURN
```

A corresponding next-turn sonar input can contain:

```text
NUM_MSGS 1
90210
ECHOES 1 0 0 0 1
```

## Split/restart semantics

A dragon created by a split **keeps the protocol version of the dragon it split from**. A restarted bot process receives a new init block, but its protocol behavior still follows the dragon's selected version.

## Compatibility warning

A protocol-2 dragon never receives a sonar value above `4294967295`; the engine does not hold that value for a later protocol upgrade. If an older bot must receive your message, keep the payload within 32 bits.

---

# 22. Map Files

`.map` files are plain text with one directive per line.

The map editor generates them, but they can also be authored manually.

## Example conceptual structure

```text
MAP <width> <height>
SYMMETRY xy
TILE_COUNT <n>
TILE <x> <y> <minGap> <maxGap>
...
EDGE_COUNT <n>
EDGE <index> <kind> <portalId>
...
DRAGON_COUNT <n>
DRAGON <team> <segmentCount> <x> <y> <x> <y> ...
```

## `MAP width height`

Required and must be the first directive.

All subsequent coordinates/indices are validated against the declared bounds.

## `MAP_NAME name`

Optional.

Displayed by the visualizer and VS Code replay viewer.

## `SYMMETRY x | y | xy`

Optional.

This declares the map's mirror relationship:

- `x`
- `y`
- `xy`

Mirrored tiles share a pearl countdown and attempt to spawn together, so symmetry is fair in both geometry and timing.

## `TILE x y minGap maxGap`

Defines a tile's pearl spawn range.

After each spawn attempt, next countdown is sampled uniformly from **inclusive** `[minGap, maxGap]`.

Special case:

- `maxGap = 0` → never spawns.

The counts declared by:

- `TILE_COUNT`
- `EDGE_COUNT`
- `DRAGON_COUNT`

must exactly match the number of corresponding records.

## `EDGE index kind portalId`

Edge kinds:

| Kind | Meaning | Portal ID |
|---:|---|---:|
| 0 | open | `-1` |
| 1 | kelp | `-1` |
| 2 | portal | paired non-negative ID |

Portal constraint in map files:

- exactly two edges share each portal ID;
- those two portal edges have the same orientation.

### Edge-index ordering

The file lays out alternating rows of:

- horizontal edge rows (north side of each tile row);
- vertical edge rows (west side of each tile column).

Each row is `width + 1` entries.

The total indexing span is described by:

`(2 * height + 1) * (width + 1)`

The docs recommend using the **map editor** as the practical way to construct these indices correctly.

## `DRAGON team segmentCount x y x y ...`

- `team = 0` → Team A.
- `team = 1` → Team B.
- Then segment count.
- Then coordinates in **head-first order**.

Requirements:

- all segments are adjacent;
- every coordinate is on the board;
- no overlap with another dragon;
- minimum length = 2.

Dragon IDs are assigned in the **order dragons appear in the file**, beginning at **0**.

That file order is also the initial turn order.

---

# 23. API

## General model

Everything a team can do through Submissions and Battles, plus publicly readable site information, is available as JSON through the API.

Team settings, membership, and account-level changes remain website-only.

## Base URL

```text
https://game.battlecode.au/api/v1
```

## API keys

Each team member creates one key on the Team page.

The key acts as that member:

- uploads are attributed to that member;
- challenges are attributed to that member.

The key is shown only once when created.

Creating a new key replaces the old key.

Leaving the team deletes the key.

The team leader can revoke another member's key.

Conceptual authorization header:

```text
Authorization: Bearer bc_...
```

## Limits

Per-key request limit:

- **120 requests/minute** normally.

Special expensive endpoints:

- `/leaderboard`
- `/ratings`

limit:

- **30 requests/minute**.

When over a limit:

- HTTP status **429**;
- `Retry-After` header communicates retry timing.

An unknown API path under `/api/v1` returns **404 JSON**, not an HTML page.

Error bodies contain both HTTP status semantics and an error message. Example shape:

```json
{"error":"That API key is not valid. Make a new one on your team page."}
```

## `/me`

`GET /me`

Returns information about the current user, team, and key context.

## `/team`

`GET /team`

Returns team information including:

- members;
- record;
- rank;
- pending join requests.

## Submissions endpoints

### `GET /submissions`

Returns every submission/version with its:

- wins;
- draws;
- losses.

### `POST /submissions`

Uploads a new version.

Fields documented:

- `name`
- `language`
- `description`
- `zip`

Accepted API language values:

- `python`
- `c`
- `cpp`

Submission ZIP limit:

- **4 MB**.

Submission rate:

- **12 versions/hour/team**.

### `GET /submissions/:id`

Returns one submission including its **build log**.

### `GET /submissions/:id/download`

Downloads the uploaded ZIP.

### `POST /submissions/:id/activate`

Makes a particular submission the active one.

## Battles endpoints

### `GET /battles?limit=50`

Returns your battles, newest first.

The API permits up to **200** in this listing.

### `POST /battles`

Challenges a team.

Request body fields:

- `teamId`
- `ranked`
- `mapIds` (optional)

Ranked battles are five-game series.

The same site limits apply:

- one pending battle per opponent;
- 60 games/hour.

### `GET /battles/:id`

Returns a battle and its games.

For unplayed battles it can also include queue position.

For the caller's own battles, it also carries a `log` describing why a game crashed or timed out.

### `GET /battles/:id/replay`

Returns the replay once the battle is finished.

The endpoint redirects to a **temporary download URL**.

Use redirect-following behavior, e.g. curl `-L`.

Important security detail:

- the redirected URL is separately signed;
- do **not** send the API key to the temporary download host;
- curl naturally drops the authorization header when moving between hosts;
- some clients, including Python's `urllib`, may preserve headers unexpectedly, causing the temporary download to be rejected.

## Example challenge request

Conceptual request:

```http
POST /api/v1/battles
Authorization: Bearer <KEY>
Content-Type: application/json
```

Body:

```json
{
  "teamId": 7,
  "ranked": false,
  "mapIds": [3]
}
```

A successful response is shown in the docs with a list containing a newly created battle ID.

## Public/read endpoints

### `GET /leaderboard`

Returns the ladder data used by the leaderboard page, including:

- rating history;
- members;
- institutions;
- prize eligibility.

### `GET /ratings`

Returns the same ladder/rating information with rating history but without the broader leaderboard metadata.

### `GET /teams`

Lists teams shown in Find a Team.

### `GET /teams/:id`

Returns a team's public profile and recent battles.

### `GET /tournaments`

Lists tournaments and their metadata.

### `GET /tournaments/:id`

Returns a tournament and its bracket/details.

### `GET /maps`

Returns maps currently in play, including their map text.

### `GET /queue`

Returns live judge capacity and queue length.

---

# 24. Cross-page rules that an LLM should treat as authoritative

These rules are spread across multiple pages and are easy to get wrong if only one page is read.

## Dragon count and spawning

- Maximum simultaneous team size = **64 living dragons**.
- Initial dragon/process and each split child are separate program instances.
- Split children obtain fresh process initialization and no parent memory.

## Length

- Starting/spawn length documented in helper constants = **3**.
- Minimum legal dragon length = **2**.
- Pearl eating adds exactly one segment.
- A sprint of `n` steps consumes `n-1` segments.
- A dragon that cannot pay a requested extra step dies rather than having the sprint quietly truncated.

## Vision

- exactly **7×7 = 49** tiles;
- Chebyshev radius 3;
- absolute wrapped coordinates;
- no automatic portal-side vision.

## Turn order

- Pearl countdowns tick first.
- Dragons then act in ascending ID order.
- Split children are appended and act later in the same round.

## Action application

- Bot output is read first.
- Action is applied after command-reading ends.
- Sprint steps execute sequentially.
- Each sprint step has its own collision and pearl checks.
- Remaining sprint steps and sonar are discarded if the dragon dies on an earlier step.

## Sonar

- protocol 3 supports up to four sonar payloads per turn, one per direction N/E/S/W;
- payloads are unsigned 64-bit;
- beams are cast after the action, in N/E/S/W order;
- direction is explicit rather than implicitly tied to current facing;
- beams pass through portals and board wrapping;
- beams stop at kelp or the first dragon part;
- self-interception invokes the own-body refraction/tail rule;
- duplicate sends in one direction are last-write-wins;
- higher-ID receiver can get a message later in the same round;
- lower-ID receiver gets it next round;
- sender identity/team is not carried;
- sender receives aggregate five-category echo counts on its next turn.

## Death

- no valid action = death;
- kelp/self/body/head-on have distinct death reasons;
- head-on kills both;
- death drops `ceil(length/2)` pearls starting from head and alternating;
- countdowns remain unchanged.

## Game end

- immediate when a team loses all dragons;
- otherwise after round 500;
- tiebreak by longest living dragon, then total team length, then draw.

## Judge cost model

- 100M CPU points/dragon/turn;
- 48 MB/dragon;
- 1 thread;
- 1 s CPU / 10 s wall backstop;
- stdout/stderr is exceptionally expensive;
- first turn is charged;
- deterministic virtual clock and RNG make matches reproducible.

---

# 25. High-value implementation traps / gotchas

The docs collectively imply these are common sources of bugs:

1. **Own tail is blocked before it advances.** Do not assume a normal Snake-style “tail moves away so I can enter it.”
2. **Multi-step movement is not atomic.** Check every intermediate square.
3. **Direction changes during a sprint matter.** Intermediate facing changes before destination computation and sonar direction.
4. **Sprint consumes body length.** A long move can destroy a short dragon even if the path is otherwise empty.
5. **Over-sprinting kills.** It does not simply shorten the requested sequence.
6. **A malformed/empty turn can suicide the dragon.** Always make sure one valid action is output.
7. **Split children are fresh processes.** Do not rely on global/static state being inherited.
8. **Sonar timing depends on IDs.** Same-round vs next-round delivery changes protocol design.
9. **Sonar has no sender metadata.** Encode identity/semantics yourself if necessary.
10. **Protocol 3 sonar is directional and 64-bit.** Up to one beam per N/E/S/W is retained, with last-write-wins for duplicate direction.
11. **Echoes are aggregate.** You learn counts for kelp, ally body, ally head, enemy body, enemy head, but not which direction caused each count.
12. **Portals affect movement and sonar but not ordinary far-side vision.**
13. **Pearls spawn based on hidden map-specific countdown distributions.** Bots see countdowns but not `minGap`/`maxGap`.
14. **Symmetric maps synchronize mirrored pearl attempts.**
15. **A dead dragon immediately drops pearls before being removed from the board.**
16. **A head-on collision kills both regardless of team.**
17. **The active submission changes automatically after a successful build.** Keep a known-good Ready version for quick rollback.
18. **Normal local timing is not judge timing.** A bot can pass 10 seconds locally and still exceed 100M CPU points in the judge.
19. **Logging can consume a large fraction of the CPU budget.** Batch output and avoid per-line flushes.
20. **C/C++ iostream settings can interfere with the helper's buffering.** Follow the timeout/buffering guidance.
21. **Network/process/thread/disk access is unavailable in the judge even if a header/module exists.**
22. **Map edges wrap.** Distance and navigation logic must use toroidal topology where appropriate.

---

# 26. Official source map

For an LLM using this file, the canonical source pages are:

- Overview — `https://game.battlecode.au/docs/overview`
- Quickstart — `https://game.battlecode.au/docs/quickstart`
- Submitting via Website — `https://game.battlecode.au/docs/submitting`
- Structure — `https://game.battlecode.au/docs/structure`
- Game Map — `https://game.battlecode.au/docs/map-info`
- Pearls — `https://game.battlecode.au/docs/pearls`
- Kelp and Portals — `https://game.battlecode.au/docs/kelp-and-portals`
- Vision — `https://game.battlecode.au/docs/vision`
- Movement — `https://game.battlecode.au/docs/movement`
- Splitting — `https://game.battlecode.au/docs/splitting`
- Sonar — `https://game.battlecode.au/docs/sonar`
- Death — `https://game.battlecode.au/docs/death`
- Game Format — `https://game.battlecode.au/docs/game-format`
- ELO System — `https://game.battlecode.au/docs/elo`
- CLI — `https://game.battlecode.au/docs/cli`
- Execution Order — `https://game.battlecode.au/docs/execution-order`
- Timeouts — `https://game.battlecode.au/docs/timeouts`
- Standard Library — `https://game.battlecode.au/docs/libraries`
- Helper Reference — `https://game.battlecode.au/docs/helper`
- IO/Wire Protocol — `https://game.battlecode.au/docs/protocol`
- Protocol Upgrade — `https://game.battlecode.au/docs/protocol-upgrade`
- Map Files — `https://game.battlecode.au/docs/map-files`
- API — `https://game.battlecode.au/docs/api`

---

# 27. Recommended LLM usage instructions

When using this file as context for Battlecode bot development, treat the following as hard constraints unless a newer official page explicitly supersedes them:

- Do not invent engine behavior that is not in this reference.
- Prefer the Execution Order page's sequencing over informal Snake assumptions.
- When reasoning about movement, model each sprint step independently.
- When reasoning about communication, model protocol-3 sonar as up to four directed 64-bit beams per turn, with dragon-ID-dependent delivery timing and aggregate echo feedback on the following turn.
- Treat sonar payloads as unsigned 64-bit in protocol-3 code; use explicit N/E/S/W directions and last-write-wins per direction.
- When reasoning about runtime feasibility, optimize for the judge's **100M CPU-point budget**, not ordinary local milliseconds.
- Assume network, child processes, multiple threads, and writable disk are unavailable in the judge.
- Remember that repeated matches are deterministic unless the bot deliberately introduces state/input outside the engine model.
- Treat visible-map data as limited to the current 7×7 window, even though coordinates are global/wrapped.
- Treat portals as movement/sonar conduits, not automatic vision conduits.
- Keep every turn's output valid and end it cleanly with `ENDTURN` when implementing a custom wire protocol.

---

## End of reference

This file is a comprehensive paraphrased companion to the official documentation snapshot dated 2026-09-24. For anything where exact wording or a later implementation update matters, consult the corresponding official source page listed above.
