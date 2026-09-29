# Strategy v5.10: the M604164 ladder battle, and the fixes it led to

v5.10 is `v5.9/main.cpp` plus the 13 requested changes of the v5.10 brief. Replays: `battle-M604164-replays/` (10 games
against one opponent, all lost), `Aut_1.replay` and `Trpy_1.replay` (we are team B in those two). Every change sits behind
an `F_*` flag in the "v5.10 modules" block at the top of `v5.10/main.cpp`; v5.9 and v5.8 are untouched. Raw results are in
`bench510/*.jsonl`.

RESULTS_PLACEHOLDER

## Replay tooling used

`replay_tools/` as before, plus five scripts written for this analysis: `economy.py` (pearls eaten, splits and units per
team per 50 rounds), `eatmap.py` (who ate on which tile), `trace.py` (one dragon's turns), `board.py` (the board at a turn)
and `loops.py` (dragons circling a few tiles). **The turn
numbers in the brief do not match the decoder's turn counter reliably** (the gap was 6 early in trauma and about 250 by
round 409), so every situation was located by dragon ID, round and coordinates.

The battle's bot sends the v5.9b tags (8180-8183) and its build matches `v5.9(QOL_ver).zip`, i.e. the current v5.9.

## Items 1-13: what the replays showed and what changed

| # | Replay / place | What happened (root cause) | Change | Flag |
|---|---|---|---|---|
| 1 | Trophy M604173 r30-150; Trpy_1 r40-230 | 5-9 of our dragons sat in one rich zone (the cup in M604173, the left handle in Trpy_1) for 100+ rounds while the enemy farmed the other two zones and the open ring. Trophy's income: cup 0.63, handles 0.63, the 582 background tiles 1.16 pearls a round. The v5.7 crowd rule only let a member leave when fewer pearls lay in view than dragons, and only toward a spot someone had reported (nobody ever visited the handles in M604173). | A rich zone (tiles in view we can reach with a pearl or due within 30 rounds) holds one dragon per 4 rich tiles (at least 2). With 3+ of us in it and more than that, the highest ID leaves: to a reported spot, else straight away from the zone, and ignores that zone's pearls for 25 rounds. | `F_ZONE_CAP` |
| 2 | Trauma M604172 r25-35 | Dragon 8 (4 long, alpha) split a 2-long child (9) into the x = 11 dead-end corridor to farm it, then 4 rounds later split another child (10) whose only way on was back into the same corridor. 10 blocked the corridor's exit at (12,8); 9's rescue split there chained 9 -> 11 -> 12 -> 13, all dead. `pocket_busy()` only counted dragons in view, and 9 was out of view. | A dragon remembers where it split children off (4 slots, 25 rounds); a dead end holding one of those birth tiles is busy, so a new child is not viable there and the region is not entered. | `F_KID_MEMORY` |
| 3 | Trauma M604172 dragon 4, r80-103 (and 4 of 6 local trauma games) | A 2-long portal *resident* circled the 2x2 block (24-25, 21-22) for 20+ rounds. Its target (a rendezvous) was routed over the remembered map *through* the portal beside it, which residents may not cross; residency never lapsed because pearls in view behind a wall counted as "due soon". | Remembered-map routes skip portals we may not cross right now (`portal_usable()`); only pearls we can reach keep a resident busy; a resident that holds no chamber and circles 4 tiles or fewer for 16 turns gives up residency and breaks out. Local check: the loop is gone in all 4 seeds that had it. | `F_RESIDENT_LOOP` |
| 4 | (request) | Two teammates going for the same pearl: the nearer took it. | Before the endgame, between a 3-long and a 2-long non-alpha (the 3-long one eats and splits at once), the 3-long one has the pearl unless it needs more than 1 move more. Death drops still go to the nearest. The first version (any longer dragon, 2 moves' slack) measured worse and was narrowed to this. | `F_LONG_FIRST` |
| 5 | Trauma r135-136 (dragons 55 and 59) | 55 (3 long) walked into the y = 8 corridor from its east end while alpha 59 (6 long) came along it from the west; 59 dodged into a side pocket and 55 died on its body. | Two of our dragons facing each other along a way with exactly one free step at every tile (up to 5 apart): the smaller one (shorter, then higher ID) reverses into its own neck at once, leaving its pearls in the bigger one's path. The bigger one reproduces exactly what the smaller one sees of it, and when it knows the smaller one will die it counts that body as gone for room, trap and teammate-safety checks (no split, no dodge into a pocket). The rule only fires when both sides can be sure. | `F_YIELD` |
| 6 | Trauma r409 (alpha 297, 28 long) | Threatened by an 11-long enemy, the alpha split L-2, but its tail lay 2 tiles from its head: the 26-long child was born in the same danger and split again at once (child 300 kept 24). | When our tail is within an enemy's reach too, keep a front just long enough to sprint into the nearest enemy head in reach (k + 1 segments for k steps, 2 for an adjacent head); the rest is the rear child (it moves later this same round, away from the threat, and takes the alpha role); the front hunts. Otherwise L-2 as before. | `F_FRONT_KILL` |
| 7 | Slithery M604171, (39..46, 19): ~250 dead heads | Two 3-tile dead ends on one row with a 2-wide junction between them. A 6-long dragon at one tip rescue-split L-2; the child was born in the other pocket, ate 2, split at its tip, and so on for 500 rounds, net 0. The v5.7 fix (shed 2 when the tail's way on is a dead end) never fired: the child had never seen the far pocket, and unseen tiles count as open. Locally a second chain appeared: a 10-13-long *alpha* shuttling between that pocket and the dead end at (20..21, 0..1) (alphas were exempt from the shed-2 rule, and a newborn's entry check read its path from `recent_path`, empty at birth). | A dragon born of a split at a farm tip (its parent says so on sonar) or whose tail still sits where it was born, facing ground it never saw, treats that way as the dead end it is (its parent sat stuck there). Alphas shed 2 too when the tail is doomed; the entry check reads the path off the body when `recent_path` is short. Locally (seed 5): hits at the two tips 92 + 88 -> 36 + 0; the remaining ones are single farm dives, not a chain. | `F_BIRTH_DEAD`, `F_CHAIN_ALPHA` |
| 8 | Queen M604169 r45 (dragons 6 and 16) | 16 left the top chamber onto (20,31) in the same round teammate 6 stepped onto (20,31) from outside to go in: head on. 16 *had* probed the turn before (its beam went out and hit teammate 12 two tiles on, an unbounded "ally" verdict that only costs 120 in the scorer), and 6's probe from (20,31) reached 16 that same round, but a probe only told its receiver to keep off one tile. | (a) A probe whose landing tile is our own head means a teammate stands right behind the portal beside us: we do not cross it for 2 rounds. (b) A probe echo from a far side we never saw counts as right behind the portal (hard block). (c) Every step onto a tile with a portal edge straight ahead probes it (not only "portal" moves), so a crossing has a probe the turn before. | `F_PORTAL_HANDSHAKE` |
| 9 | Queen M604169 | We held the top chamber r9-94 (camper 3 split a child out every ~10 rounds), then two enemy 2-longs came in and cornered it; the enemy held the bottom chamber all game and the top one after r94. We never contested theirs. | An enemy body seen going into a small chamber marks the tile it went in from (it comes back out there); so does a probe that finds an enemy right behind a portal. The tile goes out on sonar (`GUARD_TAG` 8179). One small dragon (2-3 long) of ours with nothing better to do circles the 2x2 block through that tile, so whatever comes out runs into its body. Claiming the paying chamber (and its mirror image) is v5.8's `F_CHAMBER_MIRROR` + scout dispatch. | `F_GUARD` |
| 10 | Dilemma M604168 r2 (dragon 10) | 10 (2 long) stepped E to (16,10), beside enemy 13 (2 long) with two more enemy heads near: an even trade, and their crowd eats both drops. (15,9) was out of every enemy's reach. The v5.9c dodge weighs an even trade at about 1.4 x 15 = 21 points, less than the exploration pull. | Where their heads outnumber ours (with us) within 4 moves of the tile and one can ram us there next turn, the strike risk counts one more unit and is tripled. | `F_DODGE_OUTNUM` |
| 11 | (request) | Chokeholds (dead ends) that pay. | Farms (dead ends that refill fast) and their mirror images are already found, shared and claimed (v5.8 `F_FARM`, `F_FARM_SEEK`). New: an enemy head seen inside a known farm's dead end marks its mouth for the guard of item 9. | `F_GUARD` |
| 12 | Aut_1 r474-486 (our 66-long apex) | At (47,6) the apex turned W into a pocket walled by its own body and spiralled until it had to split at r485. Its target was a pearl it remembered at (42,6), behind its own coil (x = 43): `alpha_memory_target()` never checked reachability, and `memory_route()` then heads for the nearest reachable tile, which was inside the coil. The room check (`escape_room()`) counted the edge of the view as a way out, so the pocket looked safe. | Our own body as tracked (in view or not) is a wall in the remembered-map BFS until its segment has moved off; the alpha's remembered pearls must be reachable that way. `escape_room()` goes on past the view over remembered ground (our body out of view a wall), and only never-seen ground counts as a way out. | `F_OWN_BODY_MEM`, `F_ROOM_MEM` |
| 13 | Trpy_1 r31 (our dragon 9 at (11,9), 4 long) | It split (routine, at 4). Enemy 7, now 2 long at (12,7), sprinted through the pearls at (12,8), (12,9) into our 2-long front: an even trade with its teammates beside the drop, and the pearls theirs. | Alone (no teammate head within 3 moves) with an enemy able to ram us this turn at a profit to it (strike risk >= 2), and not a hunter: sprint through the pearls beside us (2-4 steps, every pearl pays a step, at least 2 eaten and a net gain) before any split. Here: (11,8), (12,8), (12,9): 4 -> 5 in one turn. The first version fired on any enemy in reach and cost dilemma badly; it was narrowed to this. | `F_PEARL_GRAB` |


## Item 14: why we lost the ten games

We are team A in all ten. Seven games ended in elimination before round 230; three went to round 500 and were lost on
the longest dragon. Pearls eaten per team are counted from the replays (a pearl that disappears during a dragon's turn
was eaten by that dragon).

| Map | Result | Pearls eaten us / them | Peak units us / them | Their strikes / ours | Drops of their strikes eaten us / them | Where it was lost |
|---|---|---|---|---|---|---|
| Autarky | eliminated r203 | 80 / 228 | 17 / 54 | 17 / 7 | 1 / 44 | r50-150: out-eaten 25/59, 7/69 |
| Default | eliminated r178 | 50 / 187 | 13 / 42 | 15 / 8 | 2 / 33 | whole game: we explored, they camped the rooms |
| Devil | eliminated r88 | 27 / 197 | 9 / 33 | 11 / 4 | 2 / 19 | r0-88: they held the centre with its fast spawners |
| Portals | lost on length 38 vs 46 (r500) | 1457 / 1128 | 37 / 36 | 0 / 0 | - | our own collisions (70 head-ons, 401 length) and feeding |
| Prisoners Dilemma | eliminated r38 | 15 / 53 | 7 / 12 | 6 / 2 | 0 / 11 | rounds 0-25: lost 4 of 7 units in even trades |
| Queen of Spades | eliminated r137 | 47 / 147 | 8 / 36 | 11 / 2 | 3 / 19 | r50-100: they held both rich chambers |
| Schooltime | eliminated r225 | 60 / 271 | 12 / 64 | 22 / 3 | 9 / 54 | r100-200: they covered the whole map, we the top rows |
| Slithery Fight | lost on length 49 vs 66 (r500) | 1591 / 1657 | 40 / 64 | 30 / 24 | 54 / 52 | 513 friendly-fire and ~300 wall deaths; the (39..46,19) chain |
| Trauma | lost on length 15 vs 25 (r500) | 403 / 626 | 19 / 54 | 17 / 5 | 28 / 21 | r150-350: they had 31-51 units to our 13-14 |
| Trophy | eliminated r152 | 109 / 211 | 11 / 44 | 23 / 17 | 14 / 40 | r50-150: 5 of ours in the cup, theirs everywhere else |

**1. Economy (all but portals and slithery).** Both bots split the same way (4 -> 2 + 2 almost always), so the gap is
foraging. From about round 50 the opponent eats 2-7 times our pearls and its unit count runs away (30-64 by round
150-200 against our 5-15). Its dragons cover the whole map, including the sparse background tiles that are half of the
income on trophy and default; ours cluster:
- trophy: our 5-9 dragons in one rich zone (item 1); its dragons in both handles, the cup *and* the open ring (14-36 of
  them outside all three zones);
- schooltime: we stayed in the top rows (y 2-13); it ate in every room of the map;
- devil: it took the central block with the 1-round spawners in the first 50 rounds (68 pearls to our 18, then 129 to 9);
  ours stayed in the side corridors;
- default: our dragons spent most of the game 2 long and exploring ("disperse"); it camped the chained 4x4 rooms in the
  middle (their spawners run 1-255 rounds, the background 1-3849);
- dilemma: it split 26 times to our 10 in the first 38 rounds, on the 1-round spawners.

**2. Fights.** It starts 2-7 times as many head-on exchanges as we do and eats nearly every drop of them (44 of 47 on
autarky, 54 of 63 on schooltime): its teammates are next to the collision, ours are not. Its strikes come from the side
and with sprints through pearls (item 13 is one). Our strikes are profitable but rare. Most of its 2-for-2 trades are
even in length and won on the drop.

**3. Our own collisions on corridor and chamber maps.** Portals: 0 of our deaths were caused by the enemy; 241 by our own
bodies and heads and 169 by walls (most of those the 2-long heads left by rescue splits). Slithery: 513 friendly-fire
deaths, and the (39..46,19) chain alone produced ~250 dead heads (item 7). Trauma: 103 friendly and 75 wall deaths.
Items 2, 3, 5, 7 and 8 are the parts of this that the brief pointed at.

**4. Chambers (queen).** We held the top chamber rounds 9-94 (the camper split out a child about every 10 rounds), then two
enemy 2-longs came in through its portal and cornered the camper in a one-tile nook; the enemy held the bottom chamber all
game and the top one afterwards (items 8 and 9).

**5. Endgame (portals, slithery, trauma).** In the three games that went the distance we out-ate the enemy on portals and
matched it on slithery until round 150, and still lost on the longest dragon: rescue-split chains and friendly collisions
kept our apexes short (portals 38 vs 46; trauma 15 vs 25 after its 51 units fed its apex), and the opponent kills its
own farm heads with "no valid action" (337 on portals) instead of splitting, which keeps its mass in fewer, longer bodies.

**What would matter most next (not in this brief).** Coverage: spread foragers over the whole map (the background tiles),
and take the rich fixed zones (devil's centre, default's rooms, dilemma's fast spawners) in the first 30 rounds. Second,
strike support: be next to our own collisions to eat the drops, and dodge when theirs outnumber ours (item 10 is a first
step).

