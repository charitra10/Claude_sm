# audit52: behavioural audit of the v5.2 modules

Tools used for `strategyV_5_9d.md`: they check whether each module does what it was written to do, from the replay
(the full board, i.e. ground truth) and the bot's own `DIAG` trace, independently of win rate.

- `variant.py SRC DEST [--diag] [F_X=false ...]`: copy a bot with `BOT_DIAG` on and flags (or int constants) changed.
- `run.py OUTDIR CHALLENGER OPPONENT --maps a,b --seeds 1-2 [-j 4]`: plays the (DIAG) challenger against a plain opponent
  on both sides with `unswbc run -v`, keeping `<map>_<seed>_<side>.replay` and the `DIAG` lines (`.diag.gz`).
- `analyze.py DIR [DIR ...] [--fresh] [--by-map --k=metric ...]`: one column of metrics per run directory (cached in
  `DIR/audit.json`; `--fresh` recomputes). Static structure comes from the map itself: *pockets* are the small side
  (<= 48 tiles) of a bridge edge in the tile graph (portals included), *chambers* are regions of <= 20 tiles joined by open
  edges that have a portal edge (`SMALL_ENCLOSURE`).
- `hazards.py REPLAY [1]`: every hazard episode: is its mouth a real pocket entry, did the owner walk back out (so it was
  never trapped), how it ended.
- `amem.py REPLAY...`: alpha-memory trips: who ate the remembered pearl (the alpha, a teammate, an enemy, nobody) or
  whether it was already gone when the alpha set off.
- `occupied.py REPLAY...`: entries into a small chamber a teammate has held for 8+ rounds: family ties, whether the entrant
  had heard a reservation for that portal.
- `chambers52.py DIR`: chamber deaths by map, cause and length.

Main metrics (our team only):

| metric | module | meaning |
|---|---|---|
| `hazard_episodes`, `hazard_ep_structural`, `hazard_ep_walked_out` | F_HAZARD | distinct (owner, mouth) hazards; at a real pocket entry; owner later stood on the tile outside its own mouth |
| `follow_ins`, `died_in_pocket` | F_HAZARD | a head crosses into a pocket another of ours is in; our deaths inside pockets |
| `barren_visits`, `barren_visit_rounds`, `barren_visit_deaths` | F_PORTAL_EVICT | stays in chambers that never spawn |
| `portal_tile_turns`, `portal_exit_death_ally` | F_PORTAL_CLEAR | turns ending on a portal-edge tile outside chambers without eating; deaths coming out of a portal into a teammate |
| `chamber_entries_settled` | F_PORTAL_RESERVE | entries into a chamber a teammate has been in for 8+ rounds |
| `disp_adjacent / disp_turns`, `single_file_turns` | F_DISPERSE | friendly body segments beside a foraging head; following a teammate's body straight on |
| `loop_periodic_turns`, `loop_loose_turns`, `cycle1`, `cycle2` | F_CYCLE | turns in a periodic / <= 7-tile loop with nothing eaten for 16 rounds; detector firings |
| `feedholds`, `deliveries`, `deliver_eaten_by_target` | F_FEED_* | holds; real feeder drops (`feeddrop`); their pearls eaten by the apex they were meant for |
| `amem_*`, `alpha_ate` | F_ALPHA_MEMORY | trips and outcomes; pearls eaten by alpha-indicator dragons |
