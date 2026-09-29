# Replay tools

Decode and analyse `.replay` files (ladder or local) without the VS Code viewer. The format is packed Cap'n Proto;
the schema was read off the viewer's generated code (`replay-viewer.vsix`, format version 2).

- `replay.py FILE...`: decode; prints map, event counts and result. `load(path)` returns the events as tuples.
- `sim.py`: `Game(path)` replays the events turn by turn and keeps every dragon's full body, teams, pearls and indicator
  strings. `for turn, rnd, dragon in g.run(): ...`, `g.render(x0, x1, y0, y1)` draws the board (A/a ours as team A,
  B/b theirs, o pearls, `|` `--` kelp, `P` portals). Turn numbers match the viewer's turn counter (0-based).
- `killers.py FILE...`: every death by cause (wall, self, ally/enemy body, ally/enemy head, feed).
- `chambers.py FILE`: the small sealed rooms behind portals; `occupancy.py FILE...`: who held each one, round by round,
  and how our holds ended.
- `portaldeaths.py FILE...`: our dragons that died on coming out of a portal, and what they hit.
- `curve.py FILE [step]`: units / longest / total length per team every `step` rounds.
- `economy.py FILE...`: per 50 rounds, pearls eaten, splits and units per team (a pearl that vanishes during a dragon's
  turn was eaten by it). `eatmap.py FILE R0 R1`: which team ate on each tile between two rounds (a map of who farms where).
- `trace.py FILE ID [T0 T1]`: one dragon's turns (round, length, head, tail, indicator). `board.py FILE --turn T | --round R
  [--box X0 X1 Y0 Y1] [--id I]`: the board at a turn with the heads in the box listed.
- `loops.py FILE [team] [window] [max]`: dragons whose head visits at most `max` tiles over `window` of their own turns.

Turn numbers quoted from the VS Code viewer drift away from these tools' turn counter over a game (6 early on, ~250 by
round 400 in M604172): find a situation by dragon ID, round and coordinates.

Which team is ours: the one whose dragons set indicator strings (`Alpha:` / `Neutral:` / `Kamikaze:`).
