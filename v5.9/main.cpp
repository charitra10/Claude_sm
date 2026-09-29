#include "helper.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Behaviour tracing for offline analysis: build a scratch copy with BOT_DIAG defined and run `unswbc run -v`.
// Never enabled in a submitted build (every stderr write costs 2.5M points in the judge).
#ifdef BOT_DIAG
#include <iostream>
#define DIAG(x) (std::clog << "DIAG " << x << '\n')
#else
#define DIAG(x) ((void)0)
#endif

namespace bot {
using namespace unswbc;

constexpr int MAX_CELLS = 4096, INF = 100000;
constexpr int BFS_Q = MAX_CELLS;
constexpr int SONAR_TTL = 36, SONAR_ID_MASK = 8191;
const auto DIRS = Direction::get_direction_list();

// Adaptive kamikaze regime: trade 1-for-1 once our unit lead is decisive. v5.1 triggers at a smaller lead
// than v5 (5 / 1.5x): with random pearl seeds, pressing an early lead wins far more often (dilemma 8/16 -> 16/16).
constexpr int REGIME_MIN_UNITS = 7, REGIME_LEAD = 3, REGIME_RATIO_X10 = 13;

// Kamikaze threshold as a fraction of base_kamikaze_threshold(), in tenths, below / from map_scale() 20.
constexpr int KAM_SMALL_X10 = 7, KAM_LARGE_X10 = 5;

// v5.2 modules. Each one can be switched off on its own so it can be benchmarked in isolation.
constexpr bool F_HAZARD = true;         // trapped dragons broadcast their pocket's mouth; others stay out
constexpr bool F_PORTAL_EVICT = true;   // leave barren / crowded portal enclosures, or die to free them
constexpr bool F_PORTAL_CLEAR = true;   // don't loiter on or route through tiles with a portal edge
constexpr bool F_PORTAL_RESERVE = true; // sonar reservations: at most one dragon per small enclosure
constexpr bool F_DISPERSE = true;       // soft repulsion from friendly bodies, break single-file queues
constexpr bool F_CYCLE = true;          // detect periodic movement and perturb out of it
constexpr bool F_FEED_ADAPT = true;     // long feeders deliver first, short ones later
constexpr bool F_FEED_BACKOFF = true;   // feeders hold while the apex already has pearls piled up
constexpr bool F_ALPHA_MEMORY = true;   // alphas keep a queue of pearls they saw and return for them

// v5.3 modules, switchable the same way.
constexpr bool F_REPEL = true;        // repulsion field between non-alpha heads; a crowd fans out to new ground
constexpr bool F_CYCLE2 = true;       // loop detection by distinct tiles visited, and a breakout away from the loop
constexpr bool F_PORTAL_FIX = true;   // portal hesitation fixes (residency, occupancy expiry, preemption, unseen ends)
constexpr bool F_ALPHA_PORTAL = true; // before ALPHA_PORTAL_ROUND the alpha takes a free portal and hands its role on
constexpr bool F_STRADDLE = true;     // never split while the body straddles a portal
constexpr bool F_HANDOVER = true;     // an L-2 split passes the alpha role to the rear child, the short front hunts
constexpr bool F_LONG_PORTAL = true;  // long dragons stay out of portals; forced in, only a 2-long head goes
constexpr bool F_HAZARD_RR = true;    // relay every live hazard in rotation, not only the freshest
constexpr bool F_FANOUT = false;      // (part of F_REPEL) a crowd of 3+ heads fans out to new ground
constexpr int REPEL_W = 3;            // repulsion weight: REPEL_W * (5 - d)^2 per non-alpha head within 4
constexpr int LONG_PORTAL_LEN = 8, ALPHA_PORTAL_ROUND = 50, FRIEND_PORTAL_TTL = 40;
// Who stays a portal resident on arrival: 0 everyone (v5.2), 1 only in a small sealed enclosure,
// 2 everyone except a dragon that comes out where nothing in view spawns pearls.
// v5.9 (task 5, schooltime round 73): residency is why that dragon walked past the portal (it had come out of another portal
// where things spawn). Mode 1 (only a small sealed chamber makes a resident) measured 93 vs 102/192 against v5.8 (schooltime
// 4 vs 9, slithery_fight 9 vs 14, stronghold 7 vs 11; only portals gained, 12 vs 8): kept at 2.
// v5.9b (task 9): 3 = as 2, and also free when we come out onto wide open ground with no pearl in view and none due within 10
// rounds; it still shuns the portal it came through for 20 rounds (no ping-pong). 4 = the same with nothing due within 40
// (schooltime's rooms spawn on long timers: mode 3 let dragons walk out of them, seed 702 lost with it, won with mode 2).
constexpr int PORTAL_RESIDENCY = 4;

// v5.4 modules, switchable the same way.
constexpr bool F_SPAWN_CASCADE = true; // round 0: split L-2 again and again, so every spawn body is 2-long units at once
constexpr bool F_REVERSE_SPLIT = true; // no move that survives: split L-2 (the 2-long head dies), even across a portal
constexpr bool F_CAMP = true;          // stay in a portal chamber while its pearls are due; one dragon per chamber
constexpr bool F_PORTAL_LOOP = true;   // walked out of a dense chamber: loop round the partner edge and go back in
constexpr bool F_CHAIN = true;         // leave a chamber by a portal we did not just use (chained rooms, no ping-pong)
constexpr bool F_PORTAL_TRAP = true;   // never cross a portal into a dead cell; a portal with dead cells on both sides is barred
constexpr bool F_SKIRMISH = true;      // 2-long dragons ignore enemy risk on pearls and ram any longer enemy head in reach
constexpr bool F_FEED_GATE = true;     // a feeder dies only beside a real, reachable apex that no enemy is closer to
constexpr bool F_GATE_REACH = true;    // (part of F_FEED_GATE) ...and only where the apex can walk to the drop
constexpr bool F_TRUE_LEN = true;      // track alphas' real lengths (v5.3 floored every sonar report at 8)
constexpr bool F_SURPLUS = true;       // feeders the apex cannot use yet take value trades instead of circling
constexpr int CAMP_HORIZON = 30, LOOP_CHAMBER = 8, LOOP_TTL = 12, CHAIN_MEMORY = 4, CHAIN_PENALTY = 12;

// v5.5 modules, switchable the same way.
constexpr bool F_CHOKE = true;        // dead ends and sonar hazard mouths: only a 2-long dragon enters, and only for live pearls
constexpr bool F_CAMP2 = true;        // exactly one camper per paying chamber (yield only to a lower ID), remembered spawn timers
constexpr bool F_BACK_HARVEST = true; // pearls behind our tail that the head cannot reach: split a 2-long child onto them
constexpr bool F_CASCADE_FIX = true;  // round 0: every spawn body cascades, however long (v5.4 skipped "growing" alphas)
constexpr bool F_MANTLE = true;       // an alpha's split hands the role to the larger rear child, never to the 2-long head
// v5.6: off. Handing the role down the round-0 cascade lost dilemma 0/16 to v5.4 (11-13/16 without it), and on fresh
// seeds 9-16 (576 games vs v5.4 and v5.3) the bot scores 52.3% without it, 47.6% with it.
constexpr bool F_MANTLE_CASCADE = false; // (part of F_MANTLE) the round-0 cascade passes the role down its chain too
constexpr bool F_MANTLE_PROMOTE = false; // (part of F_MANTLE) a feeder longer than the apex it reaches takes the role
constexpr int MANTLE_MIN_CHILD = 4;    // smallest split child that takes the alpha role (3 in the round-0 cascade)
constexpr int CAMP_DENSE_HORIZON = 60, CAMP_SOON = 12, HARVEST_COOLDOWN = 6, MANTLE_GUARD = 40;

// v5.6 modules, switchable the same way.
constexpr bool F_DRY_EVICT = true;    // leave a portal chamber at once when no pearl lies in it and none is due soon
constexpr bool F_CHOKE_GREEDY = true; // inside a dead end: no split while pearls lie ahead; split L-2 only at the tip
constexpr bool F_REENTRY = true;      // a camper pushed out through its portal (no other move) walks straight back in
constexpr bool F_SYMMETRY = true;     // infer the map's symmetry from what we see, and share it on sonar
constexpr bool F_MIRROR_SCOUT = true; // (needs F_SYMMETRY) report the mirror image of a rich spot; idle foragers go there
constexpr int DRY_HORIZON = 24, DRY_SHUN = 40, REENTRY_TTL = 12;
// Symmetry candidates: 180-degree rotation, x -> W-1-x, y -> H-1-y, and x <-> y (square maps only).
constexpr int SYM_ROT = 0, SYM_MX = 1, SYM_MY = 2, SYM_DIAG = 3, SYM_RESOLVE = 8;
constexpr int SCOUT_TTL = 60, SCOUT_MIN_PEARLS = 5, SCOUT_MIN_SPAWNERS = 4, SCOUT_GAP = 10, MAX_SCOUTS = 4;
constexpr int SCOUT_ALPHA_X100 = 35;   // the mirror point must lie at least 0.35 * min(W, H) away
constexpr double SCOUT_SATURATION = 4.0; // average pearls in view above which every spot is ordinary

// v5.7 modules, switchable the same way (task numbers from the v5.7 brief, see strategyV_5_7.md).
constexpr bool F_CHOKE2 = true;        // 1, 9: any length enters a dead end, but only for >= CHOKE_MIN_PEARLS live pearls
constexpr bool F_RENDEZVOUS = true;    // 2: remember clusters of tiles due to spawn together and be there on time
constexpr bool F_SPLIT_CAP = true;     // 3: no voluntary non-alpha split while the team has > UNIT_CAP units
// 4: the kamikaze regime is exactly "more than UNIT_CAP units". Off: 46 -> 64/128 vs v5.6 when switched off (seeds 1-4);
// small maps never reach 33 units, so they never pressed an early lead (the adaptive regime of v5.1 stays).
constexpr bool F_KAM_CAP = false;
constexpr bool F_PROTECT = true;       // 5: a suicide strike needs a teammate nearer the collision than any other enemy
constexpr bool F_ASSASSIN = true;      // 5: above the cap, long neutrals ram intruders near our crowd; small ones converge
// 6: barren zones (and their mirror images) on sonar; explorers stay out. Off: 67 -> 76/128 when switched off (seeds 5-8);
// on devil / autarky nearly every view is barren (tiles that never spawn), so it only diverted explorers and beams.
constexpr bool F_BARREN = false;
constexpr bool F_BARREN_SONAR = true;  // (part of F_BARREN) share zones on sonar; off: each dragon keeps its own
constexpr bool F_SPLIT_COOL = true;    // 7: a newborn L-2 rear (rescue / choke split) makes no voluntary split for a while
constexpr bool F_PORTAL_COST = true;   // 8: routes pay extra for tiles with a portal edge; loitering there costs more
constexpr bool F_SCOUT_NEAR = true;    // 10: a mirrored spot only has to lie outside our view (trophy: 5 tiles over the seam)
// 11: explore outward from the map centre, fanned {left, straight, right} by ID. Off: -8/128 in two rounds (devil 0/8,
// autarky 1/8 with it: explorers leave rich centres); the newborn-only and facing-based modes also lost (62, 61 vs 67).
constexpr bool F_HYBRID = false;
constexpr bool F_TAIL_DROP = true;     // 12: back harvest also takes pearls beside the tail, not only on its old path
// (part of F_HYBRID) 1: always, outward from the centre; 2: only for HYBRID_TTL rounds after birth (the fan-out after a
// split), then the v5.6 sector waypoints; 3: always, but "forward" is the way we face, not outward from the centre.
constexpr int HYBRID_MODE = 1, HYBRID_TTL = 20;
constexpr int SCALE_MODE = 1;          // 13: map_scale() = 0: sqrt(W * H) (v5.6), 1: (W + H) / 2, 2: max(W, H)
// v5.7 ladder fixes (a1..a8.png, see strategyV_5_7.md "Ladder bugs").
constexpr bool F_ROUTE_MEM = true;     // a2, a3: out-of-view targets are routed over the remembered map (all map sizes)
constexpr bool F_REM_COMMIT = true;    // a3: keep the remembered pearl we set off for (it flipped between two every turn)
constexpr bool F_PAIR_SEP = true;      // a2: teammates travelling side by side with nothing to eat split up
// a2: exploration sectors by a hash of the ID (by id / 2, siblings shared them). Off: 77 vs 73/128 without it (seeds 5-8),
// and the first version of the a2 fix (hash + pair rule) lost 18/128; the pair rule alone is neutral (74 vs 73).
constexpr bool F_SECTOR_HASH = false;
constexpr bool F_SCOUT_NEARER = true;  // a2: leave a scout spot to a teammate in view that is nearer to it
constexpr bool F_PORTAL_ROAM = true;   // a8: residency and our own portal occupancy expire; idle dragons may use portals
constexpr bool F_CHAMBER_ONE = true;   // a6: a teammate nearer the chamber portal, or crossing it now, has it
constexpr bool F_FEED_CLEAR = true;    // a5: endgame feeders never box the apex in; late feeders deliver, not hold
constexpr bool F_CHOKE_LOOP = true;    // a7: the rear of an escape split stays out of dead ends for a while
constexpr bool F_TAIL_BFS = true;      // a4: tail harvest finds the tail on the visible body and pearls reachable from it
constexpr bool F_HOTSPOT2 = true;      // a1: report rich spots themselves (and mirrors); crowded dragons go to them
constexpr int SCOUT_CLAIM_TTL = 25;
constexpr int ROAM_IDLE = 8, DEADEND_COOL = 40, PAIR_TURNS = 3, CROWD_HEADS = 2, LATE_FEED = 30;
constexpr int CHOKE_MIN_PEARLS = 2, UNIT_CAP = 33, SPLIT_COOL = 8;
constexpr int PROTECT_EXEMPT_LEN = 0;  // an enemy at least this long is worth a strike with nobody to harvest (0: never)
constexpr int ASSASSIN_MIN_LEN = 5, ASSASSIN_CROWD = 2;
constexpr int PORTAL_STEP_COST = 4, PORTAL_LOITER = 110;
constexpr int RDV_MIN = 3, RDV_MAX_WAIT = 60, MAX_RDV = 6;
constexpr int BARREN_HORIZON = 30, BARREN_TTL_CAP = 90, BARREN_R = 3, MAX_BARREN = 8, BARREN_PENALTY = 30;

// v5.8 modules (task numbers from the v5.8 brief, see strategyV_5_8.md), switchable the same way.
constexpr bool F_ISOLATED = true;       // 1: alone among enemies (no teammate head in view): only fair trades, avoid their reach
constexpr bool F_FARM = true;           // 2, 9: farm dead ends that refill fast (>= FARM_MIN pearls a dive), share them
constexpr bool F_FARM_SEEK = true;      // (part of F_FARM) idle dragons walk to a known farm (and its mirror image)
constexpr bool F_EARLY_KILL = true;     // 3: from round 1, a sure kill on an enemy at least as long, with a teammate near
constexpr bool F_CHAMBER_MIRROR = true; // 4: a camper reports the mirror image of its chamber's outside entrance
constexpr bool F_FEED_SCORE = true;     // 6: feed the alpha that ends longest (length vs distance), alphas merge upward
constexpr bool F_STRADDLE_SPLIT = true; // 7: a 2-split is fine with the body across a portal (the tail is where we came from)
constexpr bool F_PORTAL_YIELD = true;   // 8: a portal a teammate is taking or just took is not waited at
constexpr bool F_POCKET_SEEK = true;    // 10: rich spots seen far off (and their mirror images) pull idle foragers
constexpr bool F_PATCH_CAMP = true;     // 11: one dragon per small enclosed pearl patch (not only portal chambers)
constexpr bool F_TRAP_AVOID = true;     // long dragons score moves by the room left, counting our body as it frees up
constexpr bool F_FEED_UNGUARD = true;   // (endgame) a new alpha feeds a clearly longer apex at once (no MANTLE_GUARD wait)
// Exploration waypoints point away from where we were born (they were fixed compass points round the centre: on devil the
// left-side team's "west" waypoints lay in its own barren strip, the right side's crossed the pearl columns; v5.6 against
// itself ate 284 vs 1232 pearls on seed 301 depending on the side).
constexpr bool F_SECTOR_FLIP = true;
constexpr bool F_ROUTE_PORTAL = true;   // (part of F_FARM) routes over the remembered map cross portals we know both ends of
constexpr int FARM_MIN = 3, MAX_FARMS = 8, FARM_TTL = 150, FARM_CLAIM_TTL = 30;
constexpr int ISO_RADIUS = 5, ISO_EARLY = 120, EARLY_KILL_FRIEND = 3;
// (F_ISOLATED) enemy heads within ISO_RADIUS that make us "surrounded"; strikes while isolated: 0 none, 1 only on an enemy at
// least as long (it loses as much as we do), 2 as usual; shortest dragon the rule applies to.
// Strict version (no strikes, refuse what an enemy reaches in 2 steps / 3 early): 87 vs 98/192 without the rule on the
// fight maps (trophy, arena, Colosseum, default_small, seeds 301-308); this one (fair trades allowed, refuse what an enemy
// reaches next move / within 2 early): 97.
constexpr int ISO_MIN_ENEMIES = 1, ISO_STRIKES = 1, ISO_MIN_LEN = 2;
constexpr int ISO_RISK = 360, ISO_RISK_EARLY = 320; // (F_ISOLATED) danger() from which a pearl or a step is refused
constexpr int POCKET_MIN = 6, POCKET_DUE = 100; // (F_POCKET_SEEK) good spawners in a 5x5 that make a rich spot
constexpr int FEED_SWITCH = 2; // (F_FEED_SCORE) segments a new alpha must beat the one we feed by
// (F_FEED_SCORE) an alpha merges into a longer one if the apex then has at least this many rounds to eat the drop (v5.7 asked
// for the whole body length, so late in the game big alphas never merged; only the longest dragon counts, so every pearl the
// apex eats from the drop is a gain).
constexpr int FEED_EAT_MIN = 12;
constexpr int TRAP_MIN_LEN = 5, TRAP_NEED = 14, TRAP_W = 25;
constexpr int PATCH_MAX = 36, PATCH_EXITS = 3, PATCH_HOLD = 40, PATCH_DUE = 40;

// v5.9 modules (task numbers from the v5.9 brief, see strategyV_5_9.md), switchable the same way.
constexpr bool F_FARM_SPLIT = true;     // 1: out of a dead end we farmed, split 2 while longer than 4 (alphas too, before the endgame)
constexpr bool F_QUICK_FARM = true;     // 1: spawners refilling within 5 rounds count for farms (autarky's pocket has [1,5] tiles)
constexpr bool F_EARLY_FEED = true;     // 2: no enemy able to reach us for PEACE_WINDOW rounds: start feeding from EARLY_FEED_ROUND
constexpr bool F_EARLY_KILL2 = true;    // 3: sure kills on enemies 1-2 shorter too, with a teammate right beside the collision
constexpr bool F_ENEMY_CHAMBER = true;  // 4: a chamber we saw an enemy come out of (or go into) is theirs: keep out for a while
constexpr bool F_EXIT_CLEAR = true;     // 6: keep off the outer tiles of small-chamber portals: whoever leaves lands there blind
// 4 (queen_of_spades): room-aware moves for short dragons too, needing room for max(len + 2, 4) tiles. A 2-long camper walked
// into a chamber corner whose only other exit its own split child blocked, then into a 1-tile nook, grew to 3 and died.
constexpr bool F_TRAP_SMALL = true;
// 4 (queen_of_spades): a dragon leaving a crowded chamber could not step onto its portal tile: escape_count() ignored the
// "leaving" exemption forward_escape_count() has, so the tile counted as a dead end. It circled 6 rounds, then died on purpose.
constexpr bool F_EVICT_EXIT = true;
constexpr int FARM_SPLIT_WINDOW = 20, FARM_SPLIT_LEN = 4;
constexpr bool FARM_SPLIT_ALPHA = false; // (F_FARM_SPLIT) alphas too (trauma 7 vs 13/16: it stunts a farming apex)
constexpr int PEACE_WINDOW = 150, EARLY_FEED_ROUND = 250;
constexpr int EARLY_KILL2_GAP = 2, EARLY_KILL2_FRIEND = 2;
constexpr int ENEMY_CHAMBER_TTL = 100, EXIT_LANE_PENALTY = 150;

// v5.9 second brief (ladder replays M510524-M514761; task numbers from that brief, see strategyV_5_9.md), switchable the
// same way.
constexpr bool F_RETREAT = true;        // 1: outnumbered on the enemy's half of the map (by its symmetry): walk back home
constexpr bool F_PORTAL_PROBE = true;   // 2, 3: one beam through a portal before crossing it; its echo says who is behind
constexpr bool F_PROBE_SPLIT = true;    // 2: an enemy right behind the portal we are forced out through: split instead
constexpr bool F_PROBE_EXIT_FREE = false; // 2: with probes on, drop the exit-lane penalty of F_EXIT_CLEAR (ablation)
constexpr bool F_TRUE_MOVES = true;     // 4, 5: who is nearer a pearl or a drop counts moves (walls, facing), not distance
constexpr bool F_BODY_FREE = true;      // 4: our routes may cross our own body where it will have moved off by then
constexpr bool F_SPLIT_ONCE = true;     // 6: a fresh split child whose own child would have no way out does not split again
constexpr bool F_MOUTH_CLEAR = true;    // 6: keep off the mouth of a dead end a teammate is in (its rear child comes out there)
// 7: enemy sprint reach counts the pearls it eats on the way (each pays one more step): 0 off, 1 for late-game alphas (as
// asked), 2 for every dragon that weighs enemy reach.
constexpr int SPRINT_REACH = 1;
constexpr bool F_PEARL_SPRINT = true;   // 7 (ii): our strikes may sprint further by eating pearls on the way
constexpr bool F_FEED_EFF = true;       // 8: feeders grow to an odd length before they drop (ceil(L / 2) pearls)
constexpr bool F_HERD = true;           // 10: hunters step where the enemy they close on has the fewest safe replies
constexpr bool F_LONG_GUARD = true;     // 10: long non-alphas weigh enemy reach (and split away from it) like alphas
constexpr bool F_LANE = true;           // 11: one-way corridors: a dragon in one beams along it; teammates keep out
constexpr bool F_ENEMY_LEN = true;      // 12: remember enemy lengths seen whole; a half-seen enemy is not assumed 4 long
constexpr int ISO_MIN_LEN2 = 3;         // 12: the isolation rule leaves 2-long skirmishers alone (v5.8: 2)
constexpr int RETREAT_TTL = 15, RETREAT_ENEMIES = 2, PROBE_RANGE = 3, LONG_GUARD_LEN = 6, HERD_RANGE = 4;
constexpr int MAX_LANES = 6, MAX_AVOID = 4;
constexpr int RETREAT_MODE = 2; // 1: as first asked (any 2+ enemy heads); 2: only 4+ long dragons facing enemies as long as them
constexpr int FEED_SHIFT = 0; // 8: rounds earlier than v5.9 the endgame consolidation starts (the portals opponent: ~35)

// v5.9c (the kamikaze brief, PD and Default replays): strikes and positioning by the team's profit. See exchanges.py: in 17
// ladder games the enemy started 499 head-on collisions to our 169 and ate 1045 of their drops to our 401.
constexpr bool F_EXCHANGE = true;  // a non-alpha strikes (one step or a sprint, pearls paying) when the exchange pays the team
constexpr bool F_SUPPORT = true;   // small hunters stand by a teammate an enemy can ram, when we would lose the race to the drop
// Expected length gain an exchange strike needs. On 5 open maps 1.5 looked better (+1.34 a strike against +0.98); over all 12
// ladder maps (96 games vs v5.8) 0.5 wins as often (54 vs 53) with a higher exchange balance (+821 vs +589): on crowded maps
// (schooltime, slithery_fight) the drop-race estimate is too pessimistic and 1.5 skips strikes that pay.
constexpr double EXCH_MIN = 0.5;
constexpr int EXCH_FAR = 6, SUPPORT_MAX_LEN = 3, SUPPORT_RANGE = 4;
// The defensive mirror of F_EXCHANGE: non-alphas weigh an enemy strike on the tile they step to, by the enemy's own profit
// (its length lost against ours, and who wins the race to the drop). In the ladder replays our victims were mostly foraging
// or dispersing (204 of 499) or setting up an ambush (68) when an enemy sprinted into them.
constexpr bool F_DODGE = true;
constexpr bool DODGE_SKIRMISH = true; // 2-long non-hunters dodge too
constexpr double DODGE_W = 15.0, DODGE_DIRECT = 2.0; // scorer weight per unit of enemy gain; gain that sends a step to the scorer
// Weight for a forager heading for food (they were hit at -2.02 length each in 40 games vs v5.8), and for a kamikaze setting
// up an ambush (hit 188 times in those games); the defaults keep the plain DODGE_W and no ambush dodging.
constexpr double DODGE_FORAGE_W = 15.0, DODGE_AMBUSH_W = 0.0;
// Kamikazes approach an enemy head down the lane beside its path and ram it from the side; face to face on its line it
// just turns away. FLANK_LINE_PENALTY: what a step onto its line 1-3 tiles ahead costs an ambushing kamikaze.
constexpr bool F_FLANK = true;
// Exchange refinements: skip a strike where their heads outnumber ours near the collision; learn our real share of a drop
// race online (devil 53%, default 85% when our collector was nearer); hunters eat fresh death drops before hunting on.
// 96 games vs v5.8 over the 12 ladder maps, each on top of flank (63 wins, exchange balance +823): outnumbered 64 / +858 (kept);
// learned share 58 / +1027; eat drops first 60 / +507 (hunters walk into the enemy's strike zone: off); all three 61 / +725.
// On new seeds 31-38: outnumbered 53, learned 55, both 55 (the 64 vs 58 was noise, one sd is about 7 in 192): both shipped,
// the learned share for its better trades.
constexpr bool F_EXCH_OUTNUM = true, F_DROP_LEARN = true, F_COLLECT = false;
constexpr int DROP_TTL = 30, DROP_PRIOR = 8, COLLECT_RANGE = 4;
constexpr int FLANK_AHEAD = 4;
constexpr double FLANK_LINE_PENALTY = 35.0;
constexpr bool EXIT_CLEAR_ON = F_EXIT_CLEAR && !(F_PORTAL_PROBE && F_PROBE_EXIT_FREE);

// v5.9d: fixes from the behavioural audit of the v5.2 modules (audit52/, strategyV_5_9d.md), switchable the same way.
// F_HAZARD announced a "trap" whenever bodies ahead closed the flood, so most hazards (81% over 12 maps) marked 1-wide
// corridors open at both ends, and 16% of owners walked back out past their own mouth. Now the region behind the mouth must
// be closed by walls (no other way out; a loop inside is fine: a room crowded with bodies is still a trap while it is full).
constexpr bool F_HAZARD_CLOSED = true;
// F_ALPHA_MEMORY: of 9707 trips to a remembered pearl the alpha ate it on 209 (2%); a teammate ate it first on 3305 (34%)
// and 39% were dropped after about 2 rounds (re-chosen every turn). Alpha pearls per game were the same with it off. Now
// the alpha skips pearls a teammate is nearer to (as the generic memory does), needs a remembered way there, and keeps
// the pearl it set off for (F_REM_COMMIT's rule).
constexpr bool F_AMEM_FIX = true;
// F_PORTAL_RESERVE: 94% of our entries into a chamber a teammate already held were by dragons that never heard its
// reservation: the camper renewed it on one rotating beam, and sonar stops at kelp, so it hit the chamber walls. Now a
// camper sends it on the beam that leaves through its portal (sonar crosses portals) whenever one does. And default's nine
// chambers each have two portals, but only the first one found was ever reserved: newcomers came in by the other one.
constexpr bool F_RESERVE_AIM = false; // measured: settled-chamber entries 125 -> 120 over 48 games (off: 164): not shipped
constexpr bool F_RESERVE_ALL = false; // reserve every portal of the chamber we hold, in rotation (default: 90 vs 43 settled-chamber
                                     // entries in 4 games, worse: off)

// Sonar tags carried in the 13-bit alpha-ID field. 8191 already meant "no alpha"; real IDs never get
// near these values, and every other field of the packet keeps its usual meaning.
constexpr int HAZARD_TAG = 8190, PORTAL_TAG = 8189, HANDOVER_TAG = 8188;
// v5.4: every portal (id < 32) we know is barred, as a bitmask in the low 32 bits. This packet carries no
// other field, so it is decoded before the generic kamikaze / enemy-alpha fields are read.
constexpr int BARRED_TAG = 8187;
// v5.5: the alpha role passes to the split child. Position field = old alpha ID (12 bits), alen = child length
// (the newborn checks it is the one meant), origin = split round. Everybody else drops the old ID as an alpha.
constexpr int MANTLE_TAG = 8186;
// v5.6: map symmetry and mirrored hotspots. Position field = the mirrored spot, origin = round found, alen = symmetry
// type (2 bits) | spot value (4 bits since v5.8, 0: symmetry only, no spot) | symmetry unknown (bit 6, v5.8) | claimed (bit 7).
constexpr int SCOUT_TAG = 8185;
// v5.7: a barren zone. Position field = its centre, origin = round seen, alen = symmetry (2 bits, valid with bit 2: the
// receiver adds the mirror image too) | rounds it stays barren / 4 (5 bits, from bit 3).
constexpr int BARREN_TAG = 8184;
// v5.8: a farm (a dead end that refills fast). Position field = its first tile, origin = round last seen, alen = entry
// direction (2 bits) | fast spawners (4 bits, from bit 2) | speculative mirror (bit 6) | claimed (bit 7).
constexpr int FARM_TAG = 8183;
// v5.9b: a one-way corridor in use (F_LANE). Position field = its far end tile, origin = round, alen = the sender's heading
// (2 bits) | rounds until it is out (5 bits, from bit 2).
constexpr int LANE_TAG = 8182;
// v5.9b: a portal probe (F_PORTAL_PROBE). Position field = the tile the sender comes out on next turn, origin = round.
constexpr int PROBE_TAG = 8181;
// v5.9b: our spawn point (F_RETREAT). Position field = where a round-0 dragon of ours stood.
constexpr int SPAWN_TAG = 8180;
constexpr int HAZARD_TTL = 12, RESERVE_TTL = 30, SMALL_ENCLOSURE = 20;
constexpr int MAX_HAZARDS = 8, MAX_PEARL_MEM = 16;

inline int di(Direction d) {
    for (int i = 0; i < 4; ++i)
        if (DIRS[i] == d)
            return i;
    return 0;
}

enum class Mode { Disperse, Forage, Ambush, Escape, Portal, Reside, Return, Feed, Split, Kill, Trapped, Block,
                  Cascade, Rescue, Loop };

inline const char *name(Mode m) {
    static const char *names[] = {"disperse", "forage", "ambush", "escape", "portal",  "reside", "return", "feed",
                                  "split",    "kill",   "trapped", "block", "cascade", "rescue", "loop"};
    return names[static_cast<int>(m)];
}

struct Action {
    std::vector<Direction> moves;
    int child = 0;
    Mode mode = Mode::Forage;
    bool intentional_death = false;
};

struct Cell {
    int seen = -1, visited = -10000, pearl_round = -1;
    int spawn_at = -1;  // v5.5: round the tile next tries to spawn (a pearl on it: when seen), -1 never / unknown
    int fast_obs = 0;   // v5.5: consecutive sightings with a countdown of at most 1 (tiles that respawn every round)
    int quick_obs = 0;  // v5.9: consecutive sightings with a countdown of at most 5
    int sym_t = -2;     // v5.6: round of the next spawn attempt as last seen, -1 never spawns, -2 unknown
    int sym_done = 0;   // v5.6: bit k: this tile already counted as evidence for symmetry candidate k
    int held_until = -1000; // v5.8: a teammate camps the pearl patch this tile belongs to (F_PATCH_CAMP)
    bool has_dragon = false;
    std::array<int, 4> edge{{-2, -2, -2, -2}}; // unknown=-2, kelp=-1, open=0, portal=id+1
};

struct Portal {
    int id;
    std::vector<std::pair<Position, int>> ends;
    int occupied_until = -1;
    bool occupied = false;
    int reserved_until = -1, reserved_by = -1; // sonar reservation of the enclosure behind this portal
    bool small = false, barren = false;          // a small sealed enclosure lies behind it / it spawns nothing
    int shun_until = -1;                         // we just left its enclosure: don't walk straight back in
    bool trap_checked = false;                   // v5.4: both sides of this edge have been judged for dead cells
    int enemy_until = -1000;                     // v5.9: an enemy holds the chamber behind it (F_ENEMY_CHAMBER)
};

// The mouth of a sealed pocket: stepping onto `p` while moving in `dir` walks into it.
struct Hazard {
    Position p;
    int dir = -1, origin = -1000;
};

struct PearlMemory {
    Position p;
    int seen = -1000;
    bool claimed = false; // v5.9d (F_AMEM_FIX): when last seen, a teammate was nearer to it than we were
};

// v5.6: a mirrored hotspot some teammate reported (or we found).
struct Scout {
    Position p;
    int origin = -1000, value = 0;
    bool claimed = false;
    int claim_round = -1000; // v5.7: a claim lapses after SCOUT_CLAIM_TTL rounds (F_HOTSPOT2)
};

// v5.7: a cluster of tiles that will all try to spawn around round `t` (seen on their countdowns).
struct Rendezvous {
    Position p;
    int t = -1000, value = 0;
};

// v5.7: a zone with no pearl and no spawn due for a while (radius BARREN_R round `p`), until round `until`.
struct BarrenZone {
    Position p;
    int until = -1000;
};

// v5.8: a dead end that refills fast: entered on tile `p` moving `dir`, with `value` fast spawners inside.
struct Farm {
    Position p;                 // a fast spawner in it; `dir`: the way we step onto p from outside (step(p, dir + 2))
    int dir = -1, value = 0, seen = -1000;
    bool spec = false;          // the mirror image under a symmetry not yet confirmed
    int claim_round = -1000; // a teammate set off for it (or we did) that round
    int claim_id = -1;
};

struct AlphaTrack {
    int id, seen, len = 8;
    Position p;
};

struct DragonState {
    bool initialized = false, alpha = false, growing = false, resident = false, dispersing = false, evacuating = false;
    int born = 0, last_food = 0, sector = 0, home_portal = -1, pending_portal = -1, explore_timer = 0;
    int kamikaze_signal_round = -1000;
    int hunter_until = -1000; // set when a split leaves this dragon as a short head that should hunt
    Position sector_target;
    Position explore_target;
    Position return_tile;
    Position enemy_alpha_pos{0, 0};
    int enemy_alpha_seen = -1000;
    // Rolling memory of our own recent trajectory — used to escape tight oscillation loops
    std::vector<Position> recent_path;
    // Where we last walked from open ground (3+ passable edges) into a corridor, and in which direction.
    Position enc_mouth;
    int enc_dir = -1, enc_round = -1000;
    std::array<Hazard, MAX_HAZARDS> hazards{};
    int hazard_sent = -1000;
    bool evicting = false;
    int contend_portal = -1, contend_round = -1000; // the portal we are walking to and reserved
    int send_reserve = -1, send_clear = -1;          // portal to reserve / announce as free this turn
    int portal_arrival = -1000;                      // round we last came out of a portal
    int cycle_break_until = -1000;
    // v5.3 state
    int straddle_left = 0;                       // body segments still on the far side of a portal we took
    int moved_len = 0, moved_steps = 0, moved_cross = -1; // last turn: length before it, steps, last crossing step
    int crowd_turns = 0, fanout_until = -1000;   // turns spent in a crowd of teammates / fan-out in progress
    bool primary_demoted = false;                // our team's ID-0/1 dragon has given up the alpha role
    int handover_to = -1, handover_round = -1000; // alpha role passed to this teammate ID (relayed a few rounds)
    int handover_old = -1;
    int capture_portal = -1, capture_until = -1000; // committed to walking through this portal
    int hazard_rr = 0;
    // v5.4 state
    int chamber_portal = -1, chamber_round = -1000; // last round we were inside a dense portal chamber, and its portal
    int chamber_size = 0;
    int loop_portal = -1, loop_until = -1000;       // forced out of that chamber: go back in through the same portal
    int dead_alarm = -1;                            // we came out in a dead cell of this portal: warn on every beam
    int news_portal = -1, news_until = -1000;       // just learnt this portal is barred: pass it on at once
    std::array<std::pair<int, int>, CHAIN_MEMORY> used_portals{}; // (portal id, round) of our latest crossings
    // v5.5 state
    int camp_portal = -1, camp_since = -1000, camp_round = -1000; // chamber we sit in, since when, last round seen in it
    std::array<std::array<int, 3>, 8> camp_seen{};                // (teammate ID, first and last round seen in our chamber)
    bool camping = false;                                         // we are the one dragon holding a paying chamber
    int harvest_round = -1000, sprint_round = -1000;
    int mantle_round = -1000, mantle_from = -1;                   // we took the alpha role from our parent this round
    int mantle_send = -1000, mantle_child_len = 0;                // we gave it away: tell the child and the team
    std::array<std::pair<int, int>, 8> demoted{};                 // (ex-alpha ID, round it gave the role away)
    // v5.6 state
    int dry_portal = -1;                                // we are leaving this chamber because it is dry
    int transit_portal = -1, transit_round = -1000;     // a camper forced through its chamber's portal (no other move)
    int reclaim_portal = -1, reclaim_until = -1000;     // ...walk back in through it
    std::array<Position, SMALL_ENCLOSURE + 4> camp_tiles{}; // the chamber we camp in (to aim the way back in)
    int n_camp_tiles = 0;
    int sym = -1;                                       // resolved symmetry (SYM_*), -1 unknown
    std::array<int, 4> sym_score{}, sym_strong{};       // evidence for each candidate
    std::array<bool, 4> sym_bad{};                      // candidate contradicted by what we saw
    std::array<Scout, MAX_SCOUTS> scouts{};
    int scout_sent = -1000, scout_new = -1;             // last spot we found; slot to announce this turn
    int goal = -1, goal_until = -1000, claim_send = -1000; // slot we are walking to, until when; tell the team
    double pearl_ema = -1.0;                            // running mean of pearls in view
    int n_seen = 0, n_never = 0, n_kelp = 0;            // distinct tiles seen; of them never spawning; their kelp edges
    int n_good = 0;                                     // v5.8: of them, a pearl or due within POCKET_DUE at first sight
    // v5.7 state
    std::array<Rendezvous, MAX_RDV> rdv{};
    int rdv_goal = -1;                                  // rendezvous slot we are walking to
    std::array<BarrenZone, MAX_BARREN> barren{};
    int barren_new = -1, barren_sent = -1000, barren_rr = 0; // zone to announce, when we last found one, relay rotor
    int assassin_round = -1000;
    int born_len = 0;                                   // our length on our first turn
    Position rem_goal;                                  // remembered pearl we are walking to (F_REM_COMMIT)
    int rem_until = -1000;
    Position amem_goal;                                 // v5.9d (F_AMEM_FIX): the alpha's remembered pearl we set off for
    int amem_until = -1000;
    int resv_aim_round = -1000;                         // v5.9d (F_RESERVE_AIM): last aimed reservation
    int resv_rr = 0;                                    // v5.9d (F_RESERVE_ALL): next portal of our chamber to renew
    int pair_turns = 0;                                 // turns a teammate head has been beside ours (F_PAIR_SEP)
    int deadend_until = -1000;                          // no dead-end entry until then (F_CHOKE_LOOP)
    std::array<Position, 64> hist{};                    // our body, tail first (F_CHOKE_LOOP); hist_n < length: unknown
    int hist_n = 0;
    int explore_timer_h = 0;                            // rounds since the last hybrid retarget
    int heading = -1;                                   // exploration octant (DIR8), -1 none yet
    // v5.8 state
    std::array<Farm, MAX_FARMS> farms{};
    int farm_goal = -1, farm_goal_until = -1000;        // farm slot we are walking to
    int farm_new = -1, farm_sent = -1000, farm_rr = 0;  // farm to announce, when, relay rotor
    int farm_harvest_round = -1000, farm_claim_send = -1000, farm_claim_slot = -1;
    int farm_birth = -1;                                // born by a split at a farm's tip (our parent said so)
    int feed_id = -1;                                   // the alpha we are feeding (F_FEED_SCORE)
    // v5.9 state
    int farm_recent = -1000;                            // last round our head was in a dead end of the map (F_FARM_SPLIT)
    int threat_round = -1000;                           // last round an enemy head could reach us (F_EARLY_FEED)
    // v5.9b state
    Position spawn;                                     // one of our round-0 positions (F_RETREAT)
    bool spawn_known = false;
    int retreat_until = -1000;
    std::array<Hazard, MAX_LANES> lanes{};              // (far end tile, the way in we must not take, until round)
    std::array<Hazard, MAX_AVOID> avoid{};              // tiles a teammate comes out of a portal onto (until round)
    std::array<std::array<int, 3>, 16> elen_mem{};      // (enemy ID, length seen whole, round)
    std::array<std::pair<Position, int>, 24> drops{};  // v5.9c (F_DROP_LEARN): death-drop pearls we saw appear, and when
    int drop_us = 0, drop_them = 0;                     // who ate the ones we saw eaten
    Position probe_pos;                                 // F_PORTAL_PROBE: where we fired a probe from, which way, when
    int probe_dir = -1, probe_round = -1000;
    int probe_verdict = 0;                              // for (probe_pos, probe_dir) this turn: 0 clear, 1 ally, 2 enemy
    bool probe_bounded = false;                         // the dragon it found is within PROBE_RANGE of the far side
    int probe_plan_dir = -1;                            // decide(): probe this portal edge after this turn's move
    Position probe_plan_pos;
    int lane_dir = -1, lane_steps = 0;                  // execute(): beam a lane packet along this heading
    Position lane_end;
    // F_PATCH_CAMP: the patch we are in (by its centre), since when, whether a teammate was in it when we came, whether we
    // are its camper, and its tiles.
    Position patch_center;
    Position home;                                      // where we were born (F_SECTOR_FLIP)
    int patch_round = -1000, patch_enter = -1000;
    bool patch_crowded_on_entry = false, patch_camp = false;
    std::array<Position, 40> patch_tiles{};
    int n_patch_tiles = 0;
    std::array<std::pair<int, int>, 6> patch_mates{};  // (teammate ID, first round seen with us in the patch)
    Mode last_mode = Mode::Forage;
    std::array<PearlMemory, MAX_PEARL_MEM> pearl_mem{};
    std::array<Cell, MAX_CELLS> cells{};
    std::vector<Portal> portals;
    std::vector<AlphaTrack> alphas;
    std::vector<DragonPart> previous_heads;
};

class Brain {
    Controller &c;
    Game &g;
    int w, h, round = 0;
    Position here;
    std::vector<DragonPart> friends, enemies;
    bool sonar_enemy_alert = false;
    bool iso = false, iso_early = false; // v5.8 (F_ISOLATED): no teammate head in view, an enemy head near; early game

    std::array<int, MAX_CELLS> distance{}, first{}, predecessor{}, arrival{};
    std::array<int, MAX_CELLS> wcost{}, wfirst{}; // v5.7: portal-averse route cost and first step (tiles BFS reached)
    // v5.7 (a2, a3): shortest ways over the remembered map from our head (never-seen tiles assumed open), once per turn.
    mutable std::array<int, MAX_CELLS> mem_depth{}, mem_first{};
    mutable int mem_round = -1;
    mutable std::array<Position, MAX_CELLS> mem_q{}; // v5.8: its own queue (dead_cell() inside it reuses remembered_q)
    std::array<int, 64> reached{};
    int n_reached = 0;
    mutable std::array<int, MAX_CELLS> risk_cache{};
    mutable std::array<int, MAX_CELLS> danger_depth{};
    mutable std::array<bool, MAX_CELLS> space_seen{};
    mutable std::array<int, MAX_CELLS> remembered_first{};
    mutable std::array<Position, MAX_CELLS> remembered_q{};
    static constexpr int FARM_CAP = 48;
    mutable std::array<Position, FARM_CAP + 4> farm_q{};  // v5.8: tiles of the last dead_end_region()
    mutable std::array<int, FARM_CAP + 4> farm_dep{};

    std::vector<std::pair<int, int>> friend_len_cache, enemy_len_cache;
    std::vector<int> friend_alpha_cache;

  public:
    DragonState s;

    Brain(Controller &controller, Game &game_state) : c(controller), g(game_state) {
        std::tie(w, h) = g.get_map_size();
        danger_depth.fill(-1);
        space_seen.fill(false);
        mm_round.fill(-1);
        remembered_first.fill(-1);
    }

    int index(Position p) const { return p.y * w + p.x; }

    Position wrap(Position p) const {
        return {(p.x % w + w) % w, (p.y % h + h) % h};
    }

    Position step(Position p, int d) const {
        auto [x, y] = DIRS[d].get_offset();
        return wrap({p.x + x, p.y + y});
    }

    int delta(int a, int b, int size) const {
        int v = (b - a + size) % size;
        if (v > size / 2) v -= size;
        return v;
    }

    int dist(Position a, Position b) const {
        return std::abs(delta(a.x, b.x, w)) + std::abs(delta(a.y, b.y, h));
    }

    int chebyshev(Position a, Position b) const {
        return std::max(std::abs(delta(a.x, b.x, w)), std::abs(delta(a.y, b.y, h)));
    }

    // v5.6: the image of p under symmetry candidate k, and of direction d.
    Position mirror(Position p, int k) const {
        switch (k) {
        case SYM_ROT: return {w - 1 - p.x, h - 1 - p.y};
        case SYM_MX: return {w - 1 - p.x, p.y};
        case SYM_MY: return {p.x, h - 1 - p.y};
        default: return {p.y, p.x};
        }
    }

    int mirror_dir(int d, int k) const {
        auto [ox, oy] = DIRS[d].get_offset();
        int mx = k == SYM_ROT || k == SYM_MX ? -ox : k == SYM_MY ? ox : oy;
        int my = k == SYM_ROT || k == SYM_MY ? -oy : k == SYM_MX ? oy : ox;
        for (int e = 0; e < 4; ++e) {
            auto [ex, ey] = DIRS[e].get_offset();
            if (ex == mx && ey == my) return e;
        }
        return d;
    }

    bool sym_possible(int k) const {
        return k >= 0 && k < 4 && (k != SYM_DIAG || w == h);
    }

    // v5.9b (F_RETREAT): the map's symmetry as far as we know it (the first candidate not ruled out while unresolved).
    int side_sym() const {
        if (s.sym >= 0) return s.sym;
        for (int k : {SYM_ROT, SYM_MX, SYM_MY, SYM_DIAG})
            if (sym_possible(k) && !s.sym_bad[k]) return k;
        return SYM_ROT;
    }

    // The enemy spawned at the mirror image of our spawn: p lies on their half when it is clearly nearer to that.
    bool on_enemy_side(Position p) const {
        if (!s.spawn_known) return false;
        Position es = mirror(s.spawn, side_sym());
        if (dist(es, s.spawn) < 8) return false; // spawns near the symmetry line: no sides to speak of
        return dist(p, es) + 2 < dist(p, s.spawn);
    }

    Portal &portal(int id) {
        for (auto &p : s.portals)
            if (p.id == id) return p;
        s.portals.push_back({id, {}, -1, false});
        return s.portals.back();
    }

    std::pair<Position, int> canonical(Position p, int d) const {
        if (d == 2) return {step(p, 2), 0};
        if (d == 1) return {step(p, 1), 3};
        return {p, d};
    }

    std::optional<Position> destination(Position p, int d) const {
        int e = s.cells[index(p)].edge[d];
        if (e < 0) return {};
        if (e == 0) return step(p, d);
        auto from = canonical(p, d);
        for (const auto &port : s.portals)
            if (port.id == e - 1 && port.ends.size() == 2) {
                if (port.ends[0] != from && port.ends[1] != from) return {};
                auto to = port.ends[0] == from ? port.ends[1] : port.ends[0];
                return (d == 0 || d == 3) ? step(to.first, d) : to.first;
            }
        return {};
    }

    bool empty(Position p) const {
        const auto &cell = s.cells[index(p)];
        return cell.seen == round && !cell.has_dragon;
    }

    // Newer reports replace the length (it can shrink after a split); same-round reports keep the larger one.
    void remember_alpha(int id, Position p, int origin_round, int len = 8) {
        if (demoted_alpha(id, origin_round)) return;
        for (auto &a : s.alphas)
            if (a.id == id) {
                if (origin_round > a.seen) a = {id, origin_round, len, p};
                else if (origin_round == a.seen) a = {id, origin_round, std::max(len, a.len), p};
                return;
            }
        s.alphas.push_back({id, origin_round, len, p});
    }

    // v5.5: this ID gave the alpha role away at or after origin_round, so a report that old is stale.
    bool demoted_alpha(int id, int origin_round) const {
        if (!F_MANTLE) return false;
        for (const auto &d : s.demoted)
            if (d.first == id && origin_round <= d.second) return true;
        return false;
    }

    void note_demoted(int id, int when) {
        if (!F_MANTLE || id < 0) return;
        int slot = 0;
        for (int i = 0; i < static_cast<int>(s.demoted.size()); ++i) {
            if (s.demoted[i].first == id) { s.demoted[i].second = std::max(s.demoted[i].second, when); slot = -1; break; }
            if (s.demoted[i].second < s.demoted[slot].second) slot = i;
        }
        if (slot >= 0) s.demoted[slot] = {id, when};
        s.alphas.erase(std::remove_if(s.alphas.begin(), s.alphas.end(),
                                      [&](const AlphaTrack &a) { return a.id == id && a.seen <= when; }),
                       s.alphas.end());
    }

    int friendly_visible_length(int friend_id) const {
        for (const auto &p : friend_len_cache)
            if (p.first == friend_id) return p.second;
        return 0;
    }

    int enemy_visible_length(int enemy_id) const {
        for (const auto &p : enemy_len_cache)
            if (p.first == enemy_id) return p.second;
        return 0;
    }

    int team_zero_id_val() const {
        return (c.get_team() == Team::A) ? 0 : 1;
    }

    bool is_primary_alpha_id(int id) const {
        return id == team_zero_id_val() && !s.primary_demoted;
    }

    // A visible teammate counts as the primary alpha only while it still has the body of one: after a
    // handover or an emergency split the ID-0/1 dragon may be a 2-long hunter.
    bool primary_alpha_seen(const DragonPart &f) const {
        if (!is_primary_alpha_id(f.get_id())) return false;
        return !F_HANDOVER || friendly_visible_length(f.get_id()) >= 3 || chebyshev(here, f.position) >= 3;
    }

    bool is_alpha(int id) const {
        if (id == c.get_id()) return s.alpha;
        for (int aid : friend_alpha_cache)
            if (aid == id) return true;
        return false;
    }

    // v5.7 (SCALE_MODE 1): the effective dimension N_eff = (W + H) / 2. Travel on the torus is Manhattan, so the mean
    // distance between two tiles is (W + H) / 4: linear in W + H, which the arithmetic mean tracks and sqrt(W * H)
    // understates on long maps (autarky 54x18: 36 vs 31). Square maps are unchanged.
    int map_scale() const {
        if (SCALE_MODE == 1) return (w + h) / 2;
        if (SCALE_MODE == 2) return std::max(w, h);
        return static_cast<int>(std::sqrt(w * h));
    }

    // Units above which small dragons turn kamikaze. v5.1 trades much earlier than v5: 0.7x v5's table on
    // small maps and 0.5x from map_scale 20 up (0.5x everywhere cost small maps; 0.35x cost v5 games).
    int kamikaze_threshold() const {
        int t = base_kamikaze_threshold();
        return map_scale() < 20 ? t * KAM_SMALL_X10 / 10 : t * KAM_LARGE_X10 / 10;
    }

    int base_kamikaze_threshold() const {
        int n = map_scale();
        if (n <= 12) return 5;
        if (n < 20)  return 10;
        if (n <= 26) return 18;
        if (n <= 31) return 28;
        if (n <= 35) return 18;
        if (n <= 50) return 26;
        return 48;
    }

    int alpha_split_cap() const {
        int n = map_scale();
        return n <= 12 ? 42 : n < 20 ? 36 : n <= 26 ? 24 : n <= 35 ? 18 : n <= 50 ? 12 : 8;
    }

    bool is_adaptive_kamikaze_active() const {
        return round < feed_round() && (round - s.kamikaze_signal_round <= 28) && c.get_unit_count() >= 6;
    }

    // v5.7: above UNIT_CAP units the team stops splitting (non-alphas) and its small dragons turn kamikaze.
    bool over_cap() const {
        return c.get_unit_count() > UNIT_CAP;
    }

    bool is_kamikaze_regime() const {
        if (F_KAM_CAP) return round < feed_round() && over_cap();
        return round < feed_round() && (c.get_unit_count() > kamikaze_threshold() || is_adaptive_kamikaze_active());
    }

    // v5.7: no voluntary split for a non-alpha while the team is over the cap, nor for SPLIT_COOL rounds after being born
    // as the rear L-2 of an escape split (p2.png: the rear split again at once, before it had walked out).
    bool split_blocked() const {
        if (F_SPLIT_CAP && !s.alpha && over_cap()) return true;
        return F_SPLIT_COOL && s.born > 0 && round - s.born < SPLIT_COOL && s.born_len >= 4;
    }

    // v5.7: a suicide strike on `at` is worth it only if a teammate will eat the drop: some teammate head (not us) nearer to
    // it than every enemy head other than the victim.
    bool strike_protected(Position at, int victim, int elen) const {
        if (!F_PROTECT) return true;
        if (PROTECT_EXEMPT_LEN > 0 && elen >= PROTECT_EXEMPT_LEN) return true;
        int mine = INF, theirs = INF;
        // v5.9b (F_TRUE_MOVES, trophy round 12): moves, facing included. The teammate "3 away" from the drop faced away
        // from it and never came.
        for (const auto &f : friends) mine = std::min(mine, F_TRUE_MOVES ? moves_of(f, at) : dist(f.position, at));
        for (const auto &e : enemies)
            if (e.get_id() != victim) theirs = std::min(theirs, F_TRUE_MOVES ? moves_of(e, at) : dist(e.position, at));
        if (F_TRUE_MOVES && mine > 4) return false; // nobody close enough to count on
        return mine < theirs;
    }

    bool is_kamikaze() const {
        if (!s.alpha && c.get_length() <= 3 && round <= s.hunter_until) return true;
        return !s.alpha && !s.growing && is_kamikaze_regime() && c.get_length() == 2;
    }

    bool is_sprint_hunter() const {
        return !s.alpha && !s.growing && is_kamikaze_regime() && c.get_length() == 3;
    }

    void occupy(int id, int until = INF) {
        auto &p = portal(id);
        if (p.occupied && p.occupied_until > until) return;
        p.occupied = true;
        p.occupied_until = until;
    }

    int center_dist(Position p) const {
        return std::abs(p.x - w / 2) + std::abs(p.y - h / 2);
    }

    Position sector_waypoint(int sec) const {
        int cx = w / 2, cy = h / 2;
        bool large_map = (map_scale() > 35);
        int ring = (c.get_id() / 2 + sec) % 3;
        int rx = (ring == 0 ? std::max(2, w / 7) : (ring == 1 ? std::max(3, w / 4) : std::max(4, (w * 3) / 8)));
        int ry = (ring == 0 ? std::max(2, h / 7) : (ring == 1 ? std::max(3, h / 4) : std::max(4, (h * 3) / 8)));
        int d_rx = large_map ? std::max(2, (rx * 3) / 4) : rx;
        int d_ry = large_map ? std::max(2, (ry * 3) / 4) : ry;
        int jitter_x = (static_cast<int>((c.get_id() * 3 + 1) % 3)) - 1;
        int jitter_y = (static_cast<int>((c.get_id() * 5 + 2) % 3)) - 1;
        Position base;
        // v5.8 (F_SECTOR_FLIP): offsets mirrored so the "west" sectors point away from our birth side.
        int fx = 1, fy = 1;
        if (F_SECTOR_FLIP && s.initialized) {
            double ax = static_cast<double>(delta(cx, s.home.x, w)) / w, ay = static_cast<double>(delta(cy, s.home.y, h)) / h;
            if (std::abs(ax) >= std::abs(ay)) fx = ax < 0 ? -1 : 1;
            else fy = ay < 0 ? -1 : 1;
        }
        rx *= fx;
        d_rx *= fx;
        ry *= fy;
        d_ry *= fy;
        switch ((sec % 8 + 8) % 8) {
        case 0: base = {cx, cy - ry}; break;
        case 1: base = {cx, cy + ry}; break;
        case 2: base = {cx - rx, cy}; break;
        case 3: base = {cx + rx, cy}; break;
        case 4: base = {cx - d_rx, cy - d_ry}; break;
        case 5: base = {cx + d_rx, cy + d_ry}; break;
        case 6: base = {cx + d_rx, cy - d_ry}; break;
        default: base = {cx - d_rx, cy + d_ry}; break;
        }
        return {std::clamp(base.x + jitter_x, 2, std::max(2, w - 3)),
                std::clamp(base.y + jitter_y, 2, std::max(2, h - 3))};
    }

    // v5.7: hybrid exploration. Compass octants clockwise from north (map y grows southward).
    static constexpr int DIR8[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}};

    // The octant pointing from the map centre out through p (each axis scaled by the map's size on it, so a long map is
    // not all "east / west"). At the centre itself: the way we face.
    int outward_octant(Position p) const {
        double ax = static_cast<double>(delta(w / 2, p.x, w)) / w, ay = static_cast<double>(delta(h / 2, p.y, h)) / h;
        if (std::abs(ax) < 1e-9 && std::abs(ay) < 1e-9) {
            auto [ox, oy] = c.get_dir().get_offset();
            ax = ox;
            ay = oy;
        }
        int best = 0;
        double best_dot = -1e9;
        for (int k = 0; k < 8; ++k) {
            double nrm = (k & 1) ? 0.7071 : 1.0;
            double dot = (ax * DIR8[k][0] + ay * DIR8[k][1]) * nrm;
            if (dot > best_dot) { best_dot = dot; best = k; }
        }
        return best;
    }

    // The ID picks straight on, forward-left or forward-right, so a parent and its children fan out like a broom.
    int id_offset() const {
        static constexpr int OFF[3] = {0, -1, 1};
        return OFF[c.get_id() % 3];
    }

    Position octant_waypoint(int k) const {
        int reach = std::clamp(map_scale() / 2, 5, 12);
        int r = (k & 1) ? std::max(3, reach * 7 / 10) : reach;
        return wrap({here.x + DIR8[k][0] * r, here.y + DIR8[k][1] * r});
    }

    bool hybrid_on() const {
        return F_HYBRID && (HYBRID_MODE != 2 || round - s.born <= HYBRID_TTL);
    }

    // Next exploration heading. `blocked`: the last waypoint was not reached in time (or we looped): turn 90 degrees.
    // Otherwise head outward (offset by ID), but never back the way we were going: past the wrap seam "outward"
    // flips, and we carry straight on instead.
    void hybrid_retarget(bool blocked) {
        int cand;
        if (blocked && s.heading >= 0) {
            cand = (s.heading + ((c.get_id() & 1) ? 2 : 6)) % 8;
        } else {
            int fwd = outward_octant(here);
            if (HYBRID_MODE == 3) {
                auto [ox, oy] = c.get_dir().get_offset();
                for (int k = 0; k < 8; k += 2)
                    if (DIR8[k][0] == ox && DIR8[k][1] == oy) fwd = k;
            }
            cand = (fwd + id_offset() + 8) % 8;
            if (s.heading >= 0) {
                int diff = std::abs(cand - s.heading);
                if (std::min(diff, 8 - diff) >= 3) cand = s.heading;
            }
        }
        // A waypoint inside a known barren zone: the nearest octant (toward our fan side first) that is not.
        if (F_BARREN && in_barren(octant_waypoint(cand)))
            for (int k : {1, -1, 2, -2}) {
                int alt = (cand + ((c.get_id() & 1) ? k : -k) + 8) % 8;
                if (!in_barren(octant_waypoint(alt))) { cand = alt; break; }
            }
        s.heading = cand;
        s.sector_target = octant_waypoint(cand);
        s.explore_timer = s.explore_timer_h = 0;
    }

    int threshold() const {
        int n = map_scale();
        return std::clamp(480 - 6 * n, 120, 410);
    }

    // Endgame consolidation starts earlier on big maps so feeders can reach the apex in time. It starts
    // 60 rounds before v5's schedule: by then the swarm has usually stopped growing, and a merged apex
    // keeps foraging on its own (v5.1 benchmark: 54.6% vs v5 over 240 seeded games).
    int feed_round() const {
        int n = map_scale();
        return std::min(415, 500 - (3 * n) / 2 - 25) - 60 - FEED_SHIFT;
    }

    std::uint64_t alpha_packet64(int id, Position p, int origin_round, int alpha_len) const {
        std::uint64_t sig = (c.get_team() == Team::A) ? 1ULL : 2ULL;
        std::uint64_t aid = static_cast<std::uint64_t>(id & SONAR_ID_MASK);
        std::uint64_t r = static_cast<std::uint64_t>(std::clamp(origin_round, 0, 511));
        std::uint64_t px = static_cast<std::uint64_t>(p.x & 63);
        std::uint64_t py = static_cast<std::uint64_t>(p.y & 63);
        std::uint64_t alen = static_cast<std::uint64_t>(std::clamp(alpha_len, 0, 255));
        bool has_e = (round - s.enemy_alpha_seen <= 12);
        std::uint64_t he = has_e ? 1ULL : 0ULL;
        std::uint64_t ex = has_e ? static_cast<std::uint64_t>(s.enemy_alpha_pos.x & 63) : 0ULL;
        std::uint64_t ey = has_e ? static_cast<std::uint64_t>(s.enemy_alpha_pos.y & 63) : 0ULL;
        std::uint64_t eage = has_e ? static_cast<std::uint64_t>(std::clamp(round - s.enemy_alpha_seen, 0, 15)) : 0ULL;
        std::uint64_t kam_bucket = 0ULL;
        if (is_kamikaze_regime()) {
            int age = (c.get_unit_count() > kamikaze_threshold()) ? 0 : std::clamp(round - s.kamikaze_signal_round, 0, 27);
            kam_bucket = static_cast<std::uint64_t>(std::clamp(age / 4, 0, 6) + 1);
        }
        return (sig << 62) | (aid << 49) | (r << 40) | (px << 34) | (py << 28) |
               (alen << 20) | (he << 19) | (ex << 13) | (ey << 7) | (kam_bucket << 4) | eage;
    }

    // Hazard packet: the mouth of a pocket some teammate is trapped in. alen = entry direction | valid bit.
    std::uint64_t hazard_packet(const Hazard &hz) const {
        return alpha_packet64(HAZARD_TAG, hz.p, hz.origin, (hz.dir & 3) | 4);
    }

    // Portal packet: the sender's ID sits in the position field (it breaks ties between two dragons
    // reserving the same portal); alen = portal id (5 bits) | small | barren | clear.
    // v5.9d (F_RESERVE_AIM): a direction whose beam from `from` runs straight over open, empty tiles we know and out through
    // portal `pid` (sonar crosses portals), or -1.
    int portal_beam_dir(Position from, int pid) const {
        for (int d = 0; d < 4; ++d) {
            Position p = from;
            for (int k = 0; k < 24; ++k) {
                int e = s.cells[index(p)].edge[d];
                if (e == pid + 1) return d;
                if (e != 0) break;
                p = step(p, d);
                auto t = c.get_tile(p);
                if (!t || t->get_dragon()) break;
            }
        }
        return -1;
    }

    std::uint64_t portal_packet(int pid, bool clear) const {
        const auto &port = portal_const(pid);
        int me = c.get_id() & 4095;
        int bits = (pid & 31) | (port.small ? 32 : 0) | (port.barren ? 64 : 0) | (clear ? 128 : 0);
        return alpha_packet64(PORTAL_TAG, {me & 63, me >> 6}, round, bits);
    }

    // Handover packet: the alpha role passes from `old` (alen) to teammate `target` (ID in the position field).
    std::uint64_t handover_packet(int target, int old, int origin) const {
        return alpha_packet64(HANDOVER_TAG, {target & 63, (target >> 6) & 63}, origin, std::clamp(old, 0, 255));
    }

    // Mantle packet: `old` gave the alpha role to its split child of length `clen` (0: a relay, nobody claims).
    std::uint64_t mantle_packet(int old, int clen, int origin) const {
        return alpha_packet64(MANTLE_TAG, {old & 63, (old >> 6) & 63}, origin, std::clamp(clen, 0, 255));
    }

    std::uint32_t barred_mask() const {
        std::uint32_t m = 0;
        for (const auto &pp : s.portals)
            if (pp.barren && pp.id >= 0 && pp.id < 32) m |= 1U << pp.id;
        return m;
    }

    std::uint64_t barred_packet() const {
        std::uint64_t sig = (c.get_team() == Team::A) ? 1ULL : 2ULL;
        return (sig << 62) | (static_cast<std::uint64_t>(BARRED_TAG) << 49) | barred_mask();
    }

    // v5.6: a mirrored hotspot (value 0: only our symmetry, no spot).
    std::uint64_t scout_packet(Position p, int origin, int value, bool claimed) const {
        // v5.8: bit 6 marks "symmetry unknown". v5.7 reported spots before its symmetry was resolved with symmetry field 0,
        // and every receiver adopted 180-degree rotation from it (trophy is x -> W-1-x): mirrors went to the wrong place.
        int bits = (std::max(0, s.sym) & 3) | (std::clamp(value, 0, 15) << 2) | (s.sym < 0 ? 64 : 0) | (claimed ? 128 : 0);
        return alpha_packet64(SCOUT_TAG, p, origin, bits);
    }

    // Merge a reported spot into the table: the same spot (within 3 tiles) is refreshed, otherwise the oldest goes.
    int note_scout(Position p, int origin, int value, bool claimed) {
        int slot = 0;
        for (int i = 0; i < MAX_SCOUTS; ++i) {
            auto &sc = s.scouts[i];
            if (round - sc.origin <= SCOUT_TTL && dist(sc.p, p) <= 3) {
                sc.origin = std::max(sc.origin, origin);
                sc.value = std::max(sc.value, value);
                sc.claimed = sc.claimed || claimed;
                if (claimed) sc.claim_round = std::max(sc.claim_round, origin);
                return i;
            }
            if (i == s.goal) continue; // the spot we are walking to keeps its slot
            if (slot == s.goal || sc.origin < s.scouts[slot].origin) slot = i;
        }
        s.scouts[slot] = {p, origin, value, claimed, claimed ? origin : -1000};
        return slot;
    }

    // v5.7: rendezvous. The same cluster (within 2 tiles, due within 3 rounds of each other) only refreshes its slot.
    void note_rdv(Position p, int t, int value) {
        int slot = 0;
        for (int i = 0; i < MAX_RDV; ++i) {
            auto &r = s.rdv[i];
            if (r.value > 0 && chebyshev(r.p, p) <= 2 && std::abs(r.t - t) <= 3) {
                r.value = std::max(r.value, value);
                return;
            }
            if (i == s.rdv_goal) continue;
            if (slot == s.rdv_goal || r.value == 0 || (s.rdv[slot].value > 0 && r.t < s.rdv[slot].t)) slot = i;
        }
        if (slot == s.rdv_goal) return;
        s.rdv[slot] = {p, t, value};
    }

    // Tiles in view that will all try to spawn within 2 rounds of each other, 4 to RDV_MAX_WAIT rounds from now, and lie
    // within 2 tiles of one another: a pile of pearls on a timetable. (Its mirror image shares the timetable.)
    void find_rendezvous() {
        if (!F_RENDEZVOUS || round >= feed_round()) return;
        const auto &tiles = c.get_tiles();
        int best = 0, best_t = -1;
        Position best_p;
        for (const auto &a : tiles) {
            int ta = a.get_pearl_time();
            if (a.has_pearl() || ta < 4 || ta > RDV_MAX_WAIT) continue;
            int n = 0;
            for (const auto &b : tiles) {
                int tb = b.get_pearl_time();
                if (b.has_pearl() || tb < 0 || std::abs(tb - ta) > 2 || chebyshev(a.get_position(), b.get_position()) > 2)
                    continue;
                ++n;
            }
            if (n > best) { best = n; best_t = round + ta + 2; best_p = a.get_position(); }
        }
        if (best < RDV_MIN) return;
        note_rdv(best_p, best_t, best);
        if (s.sym >= 0) {
            Position m = mirror(best_p, s.sym);
            if (chebyshev(m, best_p) > 4) note_rdv(m, best_t, best);
        }
        DIAG("rdvseen " << c.get_id() << ' ' << round << ' ' << best_p.x << ',' << best_p.y << " t " << best_t << " n "
                        << best);
    }

    // The rendezvous to walk to now: leave when the walk is about as long as the wait (pearls stay until eaten, so up to
    // 15 rounds late still pays), not when a teammate in view is nearer to it.
    std::optional<Position> rendezvous_target() {
        if (!F_RENDEZVOUS) return {};
        if (s.rdv_goal >= 0) {
            auto &r = s.rdv[s.rdv_goal];
            int k = dist(here, r.p);
            if (r.value == 0 || round > r.t + 15 || (k <= 1 && round >= r.t)) {
                DIAG("rdvdone " << c.get_id() << ' ' << round << ' ' << r.p.x << ',' << r.p.y << " t " << r.t << " k " << k);
                r.value = 0;
                s.rdv_goal = -1;
            } else {
                return r.p;
            }
        }
        int pick = -1;
        double best = 0;
        for (int i = 0; i < MAX_RDV; ++i) {
            const auto &r = s.rdv[i];
            if (r.value < RDV_MIN) continue;
            int k = dist(here, r.p), wait = r.t - round;
            if (k < 2 || k > 20 || wait - k > 3 || k - wait > 15) continue;
            bool nearer = false;
            for (const auto &f : friends) nearer = nearer || dist(f.position, r.p) < k;
            if (nearer) continue;
            double sc = r.value / (k + 2.0);
            if (sc > best) { best = sc; pick = i; }
        }
        if (pick < 0) return {};
        s.rdv_goal = pick;
        DIAG("rdvgo " << c.get_id() << ' ' << round << ' ' << s.rdv[pick].p.x << ',' << s.rdv[pick].p.y << " t "
                      << s.rdv[pick].t << " v " << s.rdv[pick].value << " k " << dist(here, s.rdv[pick].p));
        return s.rdv[pick].p;
    }

    // v5.7: barren zones. A new zone within 2 tiles of a known one only extends it.
    int note_barren(Position p, int until) {
        int slot = 0;
        for (int i = 0; i < MAX_BARREN; ++i) {
            auto &z = s.barren[i];
            if (round < z.until && chebyshev(z.p, p) <= 2) {
                z.until = std::max(z.until, until);
                return -1;
            }
            if (z.until < s.barren[slot].until) slot = i;
        }
        s.barren[slot] = {p, until};
        return slot;
    }

    bool in_barren(Position p) const {
        if (!F_BARREN) return false;
        for (const auto &z : s.barren)
            if (round < z.until && chebyshev(p, z.p) <= BARREN_R) return true;
        return false;
    }

    // Nothing in view: no pearl, and no tile due to spawn within BARREN_HORIZON rounds. It stays that way at least until
    // the earliest countdown in view runs out. Under the resolved symmetry the mirror image shares every countdown.
    void find_barren() {
        if (!F_BARREN || round < 3 || round >= feed_round()) return;
        int soonest = BARREN_TTL_CAP;
        for (const auto &t : c.get_tiles()) {
            if (t.has_pearl()) return;
            int pt = t.get_pearl_time();
            if (pt >= 0 && pt <= BARREN_HORIZON) return;
            if (pt >= 0) soonest = std::min(soonest, pt);
        }
        int until = round + soonest;
        int slot = note_barren(here, until);
        if (slot < 0) return;
        if (s.sym >= 0) {
            Position m = mirror(here, s.sym);
            if (chebyshev(m, here) > 2 * BARREN_R) note_barren(m, until);
        }
        if (round - s.barren_sent >= 4) {
            s.barren_new = slot;
            s.barren_sent = round;
        }
        DIAG("barren " << c.get_id() << ' ' << round << ' ' << here.x << ',' << here.y << " until " << until << " sym "
                       << s.sym);
    }

    std::uint64_t barren_packet(const BarrenZone &z) const {
        int ttl4 = std::clamp((z.until - round) / 4, 0, 31);
        int bits = (std::max(0, s.sym) & 3) | (s.sym >= 0 ? 4 : 0) | (ttl4 << 3);
        return alpha_packet64(BARREN_TAG, z.p, round, bits);
    }

    // A rich spot in view: the paying chamber we hold (`chamber` spawners), or a cluster of pearls well above what we
    // usually see. Its mirror image is reported (once per spot, at most every SCOUT_GAP rounds) unless it lies near the
    // spot itself (a spot on the symmetry axis maps into its own enclosure) or pearls are everywhere (help, big_empty).
    // An open map where every tile spawns (big_empty, help): a pile of pearls there is chance, not a place.
    bool uniform_map() const {
        return s.n_seen >= 100 && s.n_never == 0 && 100 * s.n_kelp < 3 * 4 * s.n_seen;
    }

    void find_hotspot(int chamber) {
        // v5.7 (a1): without a symmetry the spot itself is still worth reporting, and a high running mean of pearls in view
        // (trophy's handles) no longer silences reports; the uniform-map guard still covers help and big_empty.
        if ((!F_HOTSPOT2 && s.sym < 0) || round - s.scout_sent < SCOUT_GAP || round >= feed_round() ||
            (!F_HOTSPOT2 && s.pearl_ema >= SCOUT_SATURATION))
            return;
        Position spot = here;
        int value = 0;
        // v5.8 (F_POCKET_SEEK, b4.png, trophy): a dense patch of good spawners (a pearl on it, or due within POCKET_DUE
        // rounds) is worth reporting before anything has piled up on it, even on maps where every tile spawns now and then.
        auto dense = [&]() {
            if (!F_POCKET_SEEK || s.n_seen < 40) return false;
            double bg = 25.0 * s.n_good / s.n_seen;
            int best = 0;
            Position bp = here;
            for (const auto &t : c.get_tiles()) {
                Position q = t.get_position();
                if (chebyshev(q, here) > 2) continue; // (a 5x5 partly out of view counts what we see of it)
                int k = 0;
                for (const auto &u : c.get_tiles())
                    if (chebyshev(u.get_position(), q) <= 2 && (u.has_pearl() || (u.get_pearl_time() >= 0 &&
                                                                                 u.get_pearl_time() <= POCKET_DUE)))
                        ++k;
                if (k > best) { best = k; bp = q; }
            }
            if (best < POCKET_MIN || best < 2.5 * bg) return false;
            spot = bp;
            value = best;
            return true;
        };
        if (chamber >= SCOUT_MIN_SPAWNERS) {
            value = chamber;
        } else if (dense()) {
            DIAG("densespot " << c.get_id() << ' ' << round << ' ' << spot.x << ',' << spot.y << " v " << value << " bg "
                              << 25.0 * s.n_good / std::max(1, s.n_seen));
        } else {
            if (uniform_map()) return;
            int n = 0, sx = 0, sy = 0;
            for (const auto &t : c.get_tiles())
                if (t.has_pearl() && !t.get_dragon()) {
                    ++n;
                    sx += delta(here.x, t.get_position().x, w);
                    sy += delta(here.y, t.get_position().y, h);
                }
            if (n < SCOUT_MIN_PEARLS || n < (F_HOTSPOT2 ? 1.5 : 2.5) * s.pearl_ema) return;
            spot = wrap({here.x + sx / n, here.y + sy / n});
            value = n;
        }
        value = std::min(value, 31);
        // v5.8 (F_CHAMBER_MIRROR): a paying chamber we hold is ours; what the team needs is the way into its mirror image: the
        // tile outside the mirror of the chamber portal's outer end, from which a step across it lands in that chamber.
        if (F_CHAMBER_MIRROR && chamber >= SCOUT_MIN_SPAWNERS) {
            if (s.sym < 0) return;
            auto door = chamber_entrance();
            if (!door) return;
            Position m = mirror(*door, s.sym);
            if (chebyshev(m, *door) <= 2) return;
            for (const auto &sc : s.scouts)
                if (round - sc.origin <= SCOUT_TTL && dist(sc.p, m) <= 3) return;
            s.scout_new = note_scout(m, round, value, false);
            s.scout_sent = round;
            DIAG("chambermirror " << c.get_id() << ' ' << round << " door " << door->x << ',' << door->y << " mirror " << m.x
                                  << ',' << m.y << " v " << value);
            return;
        }
        if (F_HOTSPOT2) {
            // v5.7 (a1): the spot itself goes on the table too (trophy's cup lies on the symmetry axis, so its mirror is
            // itself and it was never reported), then its mirror image when the symmetry is known and it lies elsewhere.
            bool known = false;
            for (const auto &sc : s.scouts)
                if (round - sc.origin <= SCOUT_TTL && dist(sc.p, spot) <= 6) known = true;
            int first_new = -1;
            if (!known) first_new = note_scout(spot, round, value, false);
            if (s.sym >= 0) {
                Position m = mirror(spot, s.sym);
                bool mknown = chebyshev(spot, m) <= 4;
                for (const auto &sc : s.scouts)
                    if (round - sc.origin <= SCOUT_TTL && dist(sc.p, m) <= 6) mknown = true;
                if (!mknown) {
                    int k = note_scout(m, round, value, false);
                    if (first_new < 0) first_new = k;
                }
            }
            if (first_new < 0) return;
            s.scout_new = first_new;
            s.scout_sent = round;
            DIAG("scoutsend " << c.get_id() << ' ' << round << " spot " << spot.x << ',' << spot.y << " v " << value << " sym "
                              << s.sym);
            return;
        }
        Position m = mirror(spot, s.sym);
        // v5.7: only "out of view" is required. The distance rule rejected trophy's handles, which are 5 tiles apart
        // across the wrap seam (x -> W-1-x maps x = 2 to x = 22 on a 25-wide map).
        if (F_SCOUT_NEAR ? chebyshev(spot, m) <= 4 : 100 * dist(spot, m) < SCOUT_ALPHA_X100 * std::min(w, h)) return;
        for (const auto &sc : s.scouts)
            if (round - sc.origin <= SCOUT_TTL && dist(sc.p, m) <= 6) return; // already known
        s.scout_new = note_scout(m, round, value, false);
        s.scout_sent = round;
        DIAG("scoutsend " << c.get_id() << ' ' << round << " spot " << spot.x << ',' << spot.y << " mirror " << m.x << ','
                          << m.y << " v " << value << " sym " << s.sym << " chamber " << chamber << " ema " << s.pearl_ema);
    }

    // v5.8: the outside tile from which a step across the portal of the small chamber we are in lands in it.
    std::optional<Position> chamber_entrance() const {
        auto enc = enclosure_at(here);
        if (!enc.small || enc.portal_id < 0) return {};
        int n = std::min(enc.size, SMALL_ENCLOSURE + 1);
        std::array<Position, SMALL_ENCLOSURE + 4> tiles;
        for (int i = 0; i < n; ++i) tiles[i] = remembered_q[i];
        auto inside = [&](Position p) {
            for (int i = 0; i < n; ++i)
                if (tiles[i] == p) return true;
            return false;
        };
        const auto &port = portal_const(enc.portal_id);
        if (port.ends.size() != 2) return {};
        for (const auto &end : port.ends) {
            Position t = end.first;
            int d0 = end.second; // the edge on t's north (0) or west (3) side
            Position u = step(t, d0);
            if (inside(t) || inside(u)) continue; // the chamber's own end
            // From t the edge is crossed moving d0, from u moving the other way.
            for (auto [a, m] : {std::pair<Position, int>{t, d0}, std::pair<Position, int>{u, (d0 + 2) % 4}}) {
                if (s.cells[index(a)].seen < 0) continue;
                auto dest = destination(a, m);
                if (dest && inside(*dest)) return a;
            }
        }
        return {};
    }

    // The mirrored spot we are walking to: kept until we get there or run out of time; otherwise the nearest one nobody
    // has claimed yet (we then claim it on sonar).
    bool scout_claimed(const Scout &sc) const {
        return sc.claimed && (!F_HOTSPOT2 || round - sc.claim_round <= SCOUT_CLAIM_TTL);
    }

    std::optional<Position> scout_goal(int min_dist = 3, int min_value = 0) {
        if (s.goal >= 0) {
            const auto &sc = s.scouts[s.goal];
            if (round > s.goal_until) {
                DIAG("scoutgiveup " << c.get_id() << ' ' << round << ' ' << sc.p.x << ',' << sc.p.y);
                s.goal = -1;
            } else if (dist(here, sc.p) <= 2) {
                int n = 0;
                for (const auto &t : c.get_tiles()) n += t.has_pearl();
                DIAG("scoutarrive " << c.get_id() << ' ' << round << ' ' << sc.p.x << ',' << sc.p.y << " pearls " << n);
                s.goal = -1;
                return {};
            } else {
                return sc.p;
            }
        }
        int best = -1, best_d = INF;
        for (int i = 0; i < MAX_SCOUTS; ++i) {
            const auto &sc = s.scouts[i];
            if (scout_claimed(sc) || sc.value == 0 || sc.value <= min_value || round - sc.origin > SCOUT_TTL) continue;
            int k = dist(here, sc.p);
            if (k < min_dist || k > (w + h) / 2) continue;
            // v5.7 (a2): a teammate in view that is nearer to it (a tie: the lower ID) takes it; we would walk there side
            // by side before its claim reached us.
            bool nearer = false;
            if (F_SCOUT_NEARER)
                for (const auto &f : friends) {
                    int kf = dist(f.position, sc.p);
                    if (!is_alpha(f.get_id()) && (kf < k || (kf == k && f.get_id() < c.get_id()))) nearer = true;
                }
            if (nearer) continue;
            if (k < best_d) { best_d = k; best = i; }
        }
        if (best < 0) return {};
        auto &sc = s.scouts[best];
        sc.claimed = true;
        sc.claim_round = round;
        s.goal = best;
        s.goal_until = round + 2 * best_d + 10;
        s.claim_send = round;
        DIAG("scoutclaim " << c.get_id() << ' ' << round << ' ' << sc.p.x << ',' << sc.p.y << " dist " << best_d
                           << " len " << c.get_length());
        return sc.p;
    }

    // Symmetry evidence from this turn's view: every visible tile against its image under each live candidate. Walls,
    // portals and "never spawns" must match exactly; mirrored tiles share one pearl countdown, so two timers that were
    // both running must name the same round. Stops for good once a candidate is resolved.
    void observe_symmetry() {
        if (!F_SYMMETRY || s.sym >= 0) return;
        for (const auto &t : c.get_tiles()) {
            Position p = t.get_position();
            const auto &cp = s.cells[index(p)];
            for (int k = 0; k < 4; ++k) {
                if (!sym_possible(k) || s.sym_bad[k]) continue;
                Position q = mirror(p, k);
                if (q == p) continue;
                const auto &cq = s.cells[index(q)];
                if (cq.seen < 0) continue;
                bool bad = false;
                int gain = 0, strong = 0;
                for (int d = 0; d < 4 && !bad; ++d) {
                    int e1 = cp.edge[d], e2 = cq.edge[mirror_dir(d, k)];
                    if (e1 == -2 || e2 == -2) continue;
                    if ((e1 < 0) != (e2 < 0) || (e1 > 0) != (e2 > 0)) bad = true;
                    else if (e1 < 0) ++gain;
                    else if (e1 > 0) gain += 2;
                }
                if (!bad && cp.sym_t != -2 && cq.sym_t != -2) {
                    if ((cp.sym_t == -1) != (cq.sym_t == -1)) bad = true;
                    else if (cp.sym_t >= 0 && round < cq.sym_t) { // q's countdown was still running: it is shared now
                        if (cp.sym_t != cq.sym_t) bad = true;
                        else if (cq.sym_t - round >= 3) { gain += 4; strong = 1; }
                    }
                }
                // Each tile (and its image) is evidence once per candidate; contradictions are checked every time. A tile
                // whose image is the same under another live candidate (trophy's middle row under rotation and the x
                // mirror) tells them apart no better than a coin: it is no evidence for either.
                bool shared = false;
                for (int j = 0; j < 4 && !shared; ++j)
                    shared = j != k && sym_possible(j) && !s.sym_bad[j] && mirror(p, j) == q;
                auto &done_p = s.cells[index(p)].sym_done, &done_q = s.cells[index(q)].sym_done;
                if (!bad && !shared && gain > 0 && !((done_p | done_q) >> k & 1)) {
                    s.sym_score[k] += gain;
                    s.sym_strong[k] += strong;
                    done_p |= 1 << k;
                    done_q |= 1 << k;
                }
                if (bad) {
                    s.sym_bad[k] = true;
                    DIAG("symbad " << c.get_id() << ' ' << round << ' ' << k << " at " << p.x << ',' << p.y << " score "
                                   << s.sym_score[k]);
                }
            }
        }
        // Resolved: the only live candidate with matching running timers (two of them, and enough evidence overall), or
        // the only candidate left alive.
        int alive = 0, last = -1, timed = 0;
        for (int k = 0; k < 4; ++k)
            if (sym_possible(k) && !s.sym_bad[k]) {
                ++alive;
                last = k;
                timed += s.sym_strong[k] > 0;
            }
        for (int k = 0; k < 4 && s.sym < 0; ++k) {
            if (!sym_possible(k) || s.sym_bad[k]) continue;
            bool strong = s.sym_score[k] >= SYM_RESOLVE && s.sym_strong[k] >= 2 && timed == 1;
            bool sole = alive == 1 && k == last && s.sym_score[k] >= 3;
            if (strong || sole) {
                s.sym = k;
                DIAG("symres " << c.get_id() << ' ' << round << ' ' << k << " score " << s.sym_score[k] << " strong "
                               << s.sym_strong[k] << " alive " << alive);
            }
        }
    }

    const Portal &portal_const(int id) const {
        static const Portal none{-1, {}, -1, false};
        for (const auto &p : s.portals)
            if (p.id == id) return p;
        return none;
    }

    bool add_hazard(Position p, int dir, int origin) {
        int slot = 0;
        for (int i = 0; i < MAX_HAZARDS; ++i) {
            auto &hz = s.hazards[i];
            if (hz.p == p && hz.dir == dir) {
                bool renewed = round - hz.origin > HAZARD_TTL;
                hz.origin = std::max(hz.origin, origin);
                return renewed;
            }
            if (hz.origin < s.hazards[slot].origin) slot = i;
        }
        s.hazards[slot] = {p, dir, origin};
        return true;
    }

    // v5.9b: a timed mark on a tile (a lane end, a portal exit): `dir` -1 means any way in, `origin` is the last round it holds.
    template <std::size_t N> void add_mark(std::array<Hazard, N> &marks, Position p, int dir, int until) {
        int slot = 0;
        for (int i = 0; i < static_cast<int>(N); ++i) {
            if (marks[i].p == p && marks[i].dir == dir) {
                marks[i].origin = std::max(marks[i].origin, until);
                return;
            }
            if (marks[i].origin < marks[slot].origin) slot = i;
        }
        marks[slot] = {p, dir, until};
    }

    // F_LANE: stepping onto n moving d walks into a corridor a teammate is coming along from the other end.
    bool lane_blocked(Position n, int d) const {
        if (!F_LANE) return false;
        for (const auto &l : s.lanes)
            if (round <= l.origin && l.p == n && l.dir == d) return true;
        return false;
    }

    // F_PORTAL_PROBE: a teammate comes out of a portal onto n now.
    bool avoided(Position n) const {
        if (!F_PORTAL_PROBE) return false;
        for (const auto &a : s.avoid)
            if (round <= a.origin && a.p == n) return true;
        return false;
    }

    // F_LANE: open (non-portal) ways on from p other than `back`; -1 if an edge is unknown or a portal.
    int corridor_exits(Position p, int back, int &out) const {
        int n = 0;
        for (int e = 0; e < 4; ++e) {
            if (e == back) continue;
            int edge = s.cells[index(p)].edge[e];
            if (edge == -2 || edge > 0) return -1;
            if (edge == 0) { ++n; out = e; }
        }
        return n;
    }

    // F_LANE: standing on p having arrived moving d, are we in a 1-wide corridor that leads on to open ground? `end` is its
    // last tile, `steps` how many moves take our head there, `straight` whether it runs in a straight line (a beam fired
    // along d then leaves it at the far end).
    bool corridor_ahead(Position p, int d, Position &end, int &steps, bool &straight) const {
        straight = true;
        steps = 0;
        Position cur = p;
        int dir = d;
        for (int k = 0; k < 24; ++k) {
            int nd = -1;
            if (corridor_exits(cur, (dir + 2) % 4, nd) != 1) return false; // cur must have exactly one way on
            Position nx = step(cur, nd);
            if (s.cells[index(nx)].seen < 0) return false;
            if (nd != d) straight = false;
            int nd2 = -1, n2 = corridor_exits(nx, (nd + 2) % 4, nd2);
            if (n2 <= 0) return false;                         // unknown edges, or a dead end (the dead-end rules own it)
            if (n2 >= 2) { end = cur; return steps >= 1; }     // nx is open ground: cur is the far end
            cur = nx;
            dir = nd;
            ++steps;
        }
        return false;
    }

    // F_LANE (vision): stepping onto n moving d enters a corridor with a teammate's head in it (or at its far end) coming
    // our way: one of the two would have to die. Also true for a lane mark a teammate beamed to us.
    bool lane_occupied(Position n, int d) const {
        if (!F_LANE) return false;
        if (lane_blocked(n, d)) return true;
        int out = -1;
        if (corridor_exits(n, (d + 2) % 4, out) != 1) return false;
        Position cur = n;
        int dir = d;
        for (int k = 0; k < 12; ++k) {
            if (auto t = c.get_tile(cur)) {
                auto part = t->get_dragon();
                if (part && part->get_team() == c.get_team() && part->get_id() != c.get_id() && part->is_head() &&
                    di(part->get_dir()) != dir)
                    return true; // facing back along the corridor toward us
            }
            int nd = -1, cnt = corridor_exits(cur, (dir + 2) % 4, nd);
            if (cnt != 1) return false;
            Position nx = step(cur, nd);
            if (s.cells[index(nx)].seen < 0) return false;
            // The tile past the far end: a teammate head there facing in comes through next.
            int nd2 = -1;
            if (corridor_exits(nx, (nd + 2) % 4, nd2) != 1) {
                if (auto t = c.get_tile(nx)) {
                    auto part = t->get_dragon();
                    if (part && part->get_team() == c.get_team() && part->get_id() != c.get_id() && part->is_head() &&
                        step(nx, di(part->get_dir())) == cur)
                        return true;
                }
                return false;
            }
            cur = nx;
            dir = nd;
        }
        return false;
    }

    // v5.9b (F_TRUE_MOVES): moves a head at `from` facing `fdir` needs to reach each tile of a (2 MR + 1)^2 window round our
    // head: no turning back on the first step, walls as remembered (unknown edges open), dragon bodies in view block (a tile
    // holding one ends a route). Portals are not followed.
    static constexpr int MR = 5, MW = 2 * MR + 1;
    int local_idx(Position p) const {
        int dx = delta(here.x, p.x, w), dy = delta(here.y, p.y, h);
        if (std::abs(dx) > MR || std::abs(dy) > MR) return -1;
        return (dy + MR) * MW + dx + MR;
    }
    void moves_map(Position from, int fdir, std::array<int, MW * MW> &out) const {
        out.fill(INF);
        int si = local_idx(from);
        if (si < 0) return;
        static std::array<Position, MW * MW> q;
        int lo = 0, hi = 0;
        q[hi++] = from;
        out[si] = 0;
        while (lo < hi) {
            Position p = q[lo++];
            int pi = local_idx(p);
            for (int d = 0; d < 4; ++d) {
                if (p == from && d == (fdir + 2) % 4) continue;
                int e = s.cells[index(p)].edge[d];
                if (e == -1 || e > 0) continue;
                Position n = step(p, d);
                int ni = local_idx(n);
                if (ni < 0 || out[ni] < INF) continue;
                out[ni] = out[pi] + 1;
                auto t = c.get_tile(n);
                if (t && t->get_dragon()) continue;
                q[hi++] = n;
            }
        }
    }
    mutable std::array<std::array<int, MW * MW>, 10> mm_cache{};
    mutable std::array<int, 10> mm_id{}, mm_round{};
    // Moves the dragon whose head is `f` needs to reach p (a straight-line guess outside the window).
    int moves_of(const DragonPart &f, Position p) const {
        int pi = local_idx(p);
        if (!F_TRUE_MOVES || pi < 0 || local_idx(f.position) < 0) return dist(f.position, p);
        int slot = -1, free_slot = 0;
        for (int i = 0; i < 10; ++i) {
            if (mm_round[i] == round && mm_id[i] == f.get_id()) { slot = i; break; }
            if (mm_round[i] != round) free_slot = i;
        }
        if (slot < 0) {
            slot = free_slot;
            moves_map(f.position, di(f.get_dir()), mm_cache[slot]);
            mm_id[slot] = f.get_id();
            mm_round[slot] = round;
        }
        int k = mm_cache[slot][pi];
        return k >= INF ? dist(f.position, p) + 6 : k; // walled off in the window: a long way round
    }
    // Our own moves to p: the BFS when it reached it, else straight-line distance.
    int my_moves(Position p) const {
        if (F_TRUE_MOVES && distance[index(p)] < INF) return distance[index(p)];
        return dist(here, p);
    }

    // v5.9b (F_ENEMY_LEN): enemy lengths seen with the whole body in view.
    void note_enemy_len(int id, int len) {
        int slot = 0;
        for (int i = 0; i < static_cast<int>(s.elen_mem.size()); ++i) {
            if (s.elen_mem[i][0] == id) { s.elen_mem[i] = {id, len, round}; return; }
            if (s.elen_mem[i][2] < s.elen_mem[slot][2]) slot = i;
        }
        s.elen_mem[slot] = {id, len, round};
    }
    // How long an enemy of which we see `visible` segments may be: seen whole now, the visible count; seen whole within 40
    // rounds, that length plus what it may have eaten since; otherwise the old guess of at least 4.
    int enemy_len_est(int id, int visible, bool partial) const {
        if (!partial) return visible;
        if (F_ENEMY_LEN)
            for (const auto &m : s.elen_mem)
                if (m[0] == id && round - m[2] <= 40) return std::max(visible, m[1] + 1 + (round - m[2]) / 6);
        return std::max(visible, 4);
    }

    // v5.9c (F_DROP_LEARN): a death drop appeared on p this turn.
    void note_drop(Position p) {
        int slot = 0;
        for (int i = 0; i < static_cast<int>(s.drops.size()); ++i) {
            if (s.drops[i].second > -1000 && s.drops[i].first == p) { s.drops[i].second = round; return; }
            if (s.drops[i].second < s.drops[slot].second) slot = i;
        }
        s.drops[slot] = {p, round};
    }

    // Drops we follow that are gone now: whoever's body lies on the tile ate it (the eater's head stepped on it, and its
    // body covers the tile for a while). Gone with nobody on it: unknown, forgotten. Untouched for 30 rounds: forgotten.
    void track_drops() {
        for (auto &d : s.drops) {
            if (d.second <= -1000) continue;
            if (round - d.second > DROP_TTL) { d.second = -1000; continue; }
            auto t = c.get_tile(d.first);
            if (!t || t->has_pearl()) continue;
            if (auto part = t->get_dragon()) (part->get_team() == c.get_team() ? s.drop_us : s.drop_them) += 1;
            d.second = -1000;
        }
    }

    // Our share of a drop when our collector is the nearer one: the 85% prior, pulled toward what this dragon has seen
    // (DROP_PRIOR sightings' weight). Devil measured 53%, default 85% (strikes logged over 96 games vs v5.8).
    double near_share() const {
        if (!F_DROP_LEARN) return 0.85;
        return (s.drop_us + 0.85 * DROP_PRIOR) / (s.drop_us + s.drop_them + DROP_PRIOR);
    }

    // v5.9c (F_COLLECT): the nearest death-drop pearl still lying in view within COLLECT_RANGE moves that no teammate is
    // nearer to: a hunter eats it before it goes on hunting.
    std::optional<Position> fresh_drop() const {
        std::optional<Position> best;
        int best_k = COLLECT_RANGE + 1;
        for (const auto &d : s.drops) {
            if (d.second <= -1000 || round - d.second > DROP_TTL) continue;
            auto t = c.get_tile(d.first);
            if (!t || !t->has_pearl() || t->get_dragon()) continue;
            int k = distance[index(d.first)];
            if (k < 1 || k >= best_k || claimed(d.first)) continue;
            best_k = k;
            best = d.first;
        }
        return best;
    }

    // True if stepping onto n while moving in d walks into a pocket a teammate is trapped in.
    bool hazard_entry(Position n, int d) const {
        if (!F_HAZARD) return false;
        for (const auto &hz : s.hazards)
            if (round - hz.origin <= HAZARD_TTL && hz.dir == d && hz.p == n) return true;
        return false;
    }

    // v5.5: a sonar hazard mouth is closed to everything longer than 2, and to a 2-long dragon unless live
    // pearls lie in the pocket behind it.
    bool chokepoint_blocked(Position n, int d) const {
        if (!hazard_entry(n, d)) return false;
        int pearls = 0, size = 0, active = 0;
        if (F_CHOKE2) { // v5.7: any length, for enough pearls to pay for the 2-long head left at the tip
            sealed_pocket(n, d, pearls, size, &active);
            return active < CHOKE_MIN_PEARLS;
        }
        if (!F_CHOKE || c.get_length() != 2) return true;
        sealed_pocket(n, d, pearls, size, &active);
        return active == 0;
    }

    int passable_edges(Position p) const {
        int k = 0;
        for (int d = 0; d < 4; ++d)
            if (s.cells[index(p)].edge[d] != -1) ++k;
        return k;
    }

    bool portal_adjacent(Position p) const {
        for (int d = 0; d < 4; ++d)
            if (s.cells[index(p)].edge[d] > 0) return true;
        return false;
    }

    // The region reachable from `start` without crossing a portal. `small` means it is sealed (every
    // boundary edge is known), has at most SMALL_ENCLOSURE tiles and contains a portal edge.
    struct Enclosure {
        bool small = false;
        int size = 0, spawners = 0, pearls_soon = 0, friend_parts = 0, longest_friend = 0, portal_id = -1;
        int longest_friend_id = -1, next_pearl = INF; // v5.4: that friend's ID; rounds until the next pearl is due
        std::array<int, 4> friend_ids{{-1, -1, -1, -1}}; // v5.5: distinct teammates with a segment inside
        int friend_heads = 0;                             // v5.5: bit i: friend_ids[i] has its head inside
        int pearls_now = 0, due_soon = 0;                 // v5.6: pearls lying in it; tiles due within DRY_HORIZON
        std::uint32_t portal_mask = 0;                    // v5.9d: every portal id (< 32) with an edge in it
    };

    Enclosure enclosure_at(Position start) const {
        Enclosure out;
        int lo = 0, hi = 0;
        bool sealed = true;
        remembered_q[hi++] = start;
        remembered_first[index(start)] = 0;
        while (lo < hi && sealed) {
            Position p = remembered_q[lo++];
            if (hi > SMALL_ENCLOSURE) { sealed = false; break; }
            for (int d = 0; d < 4; ++d) {
                int e = s.cells[index(p)].edge[d];
                if (e == -2) { sealed = false; break; }
                if (e > 0 && out.portal_id < 0) out.portal_id = e - 1;
                if (e > 0 && e - 1 < 32) out.portal_mask |= 1U << (e - 1);
                if (e != 0) continue;
                auto n = step(p, d);
                if (remembered_first[index(n)] >= 0) continue;
                remembered_first[index(n)] = 0;
                remembered_q[hi++] = n;
            }
        }
        out.size = hi;
        for (int i = 0; i < hi; ++i) {
            Position p = remembered_q[i];
            remembered_first[index(p)] = -1;
            auto t = c.get_tile(p);
            if (!t) {
                // v5.5: tiles out of view count by their remembered timers (a 6x3 chamber never fits in view).
                const auto &cell = s.cells[index(p)];
                if (F_CAMP2 && cell.spawn_at >= 0) {
                    ++out.spawners;
                    int due = std::max(0, cell.spawn_at - round);
                    out.next_pearl = std::min(out.next_pearl, due);
                    if (cell.spawn_at == cell.seen) ++out.pearls_now; // a pearl lay there when we last looked
                    else if (due <= DRY_HORIZON) ++out.due_soon;
                }
                continue;
            }
            if (t->has_pearl()) ++out.pearls_now;
            else if (t->get_pearl_time() >= 0 && t->get_pearl_time() <= DRY_HORIZON) ++out.due_soon;
            if (t->get_pearl_time() >= 0 || t->has_pearl()) ++out.spawners;
            if (t->has_pearl() || (t->get_pearl_time() >= 1 && t->get_pearl_time() <= 6)) ++out.pearls_soon;
            if (t->has_pearl()) out.next_pearl = 0;
            else if (t->get_pearl_time() >= 0) out.next_pearl = std::min(out.next_pearl, t->get_pearl_time());
            auto part = t->get_dragon();
            if (part && part->get_team() == c.get_team() && part->get_id() != c.get_id()) {
                ++out.friend_parts;
                for (int k = 0; k < 4; ++k) {
                    auto &fid = out.friend_ids[k];
                    if (fid < 0) fid = part->get_id();
                    if (fid != part->get_id()) continue;
                    if (part->is_head()) out.friend_heads |= 1 << k;
                    break;
                }
                int flen = friendly_visible_length(part->get_id());
                if (flen > out.longest_friend || (flen == out.longest_friend && part->get_id() < out.longest_friend_id)) {
                    out.longest_friend = flen;
                    out.longest_friend_id = part->get_id();
                }
            }
        }
        out.small = sealed && lo >= hi && out.portal_id >= 0;
        return out;
    }

    // Emerging onto p while moving in d: 1 if we can never get out again (the region behind p is sealed, has
    // no other portal edge and no loop to turn round in: a snake cannot reverse), 0 if we can, -1 if unknown.
    int dead_cell(Position p, int d) const {
        int back = (d + 2) % 4, lo = 0, hi = 0, edges2 = 0, verdict = 1;
        if (s.cells[index(p)].seen < 0) return -1;
        remembered_q[hi++] = p;
        remembered_first[index(p)] = 0;
        while (lo < hi && verdict == 1) {
            Position cur = remembered_q[lo++];
            for (int k = 0; k < 4 && verdict == 1; ++k) {
                if (cur == p && k == back) continue;
                int e = s.cells[index(cur)].edge[k];
                if (e == -2) { verdict = -1; break; }
                if (e > 0) { verdict = 0; break; }
                if (e != 0) continue;
                Position n = step(cur, k);
                if (s.cells[index(n)].seen < 0) { verdict = -1; break; }
                ++edges2;
                if (remembered_first[index(n)] >= 0) continue;
                if (hi >= 12) { verdict = 0; break; }
                remembered_first[index(n)] = 0;
                remembered_q[hi++] = n;
            }
        }
        for (int i = 0; i < hi; ++i) remembered_first[index(remembered_q[i])] = -1;
        if (verdict != 1) return verdict;
        return edges2 / 2 >= hi ? 0 : 1; // a tree has hi - 1 edges; one more closes a loop
    }

    // Crossing the portal on p's edge d leads into a dead cell (only when we know where it comes out).
    bool portal_into_dead_cell(Position p, int d) const {
        if (!F_PORTAL_TRAP || s.cells[index(p)].edge[d] <= 0) return false;
        auto dest = destination(p, d);
        return dest && dead_cell(*dest, d) == 1;
    }

    static std::uint32_t mix(std::uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return x;
    }

    bool enclosed_nursery() const {
        int lo = 0, hi = 0;
        bool has_portal = false;
        remembered_q[hi++] = here;
        remembered_first[index(here)] = 0;
        auto cleanup = [&]() {
            for (int i = 0; i < hi; ++i) remembered_first[index(remembered_q[i])] = -1;
        };
        while (lo < hi) {
            Position p = remembered_q[lo++];
            if (hi > std::min(49, w * h / 4)) { cleanup(); return false; }
            for (int d = 0; d < 4; ++d) {
                int e = s.cells[index(p)].edge[d];
                if (e == -2) { cleanup(); return false; }
                if (e > 0) has_portal = true;
                if (e != 0) continue;
                auto n = step(p, d);
                if (remembered_first[index(n)] >= 0) continue;
                remembered_first[index(n)] = 0;
                remembered_q[hi++] = n;
            }
        }
        cleanup();
        return has_portal;
    }

    void check_adaptive_kamikaze() {
        if (round >= feed_round()) return;
        // A round-0 cascade gives us many 2-long units, not a lead worth trading on: wait for real splits.
        if (round - s.born <= 1 && !(F_SPAWN_CASCADE && round < 2)) {
            int my_units = c.get_unit_count();
            int enemy_max = std::max(0, c.get_id() - my_units);
            if (my_units >= REGIME_MIN_UNITS &&
                (my_units - enemy_max >= REGIME_LEAD || (my_units >= 10 && my_units * 10 >= enemy_max * REGIME_RATIO_X10))) {
                s.kamikaze_signal_round = round;
            }
        }
        if (c.get_unit_count() > kamikaze_threshold()) {
            s.kamikaze_signal_round = round;
        }
    }

    void initialize() {
        s.initialized = true;
        s.home = here;
        s.threat_round = round; // a newborn has seen nothing yet: it needs a whole PEACE_WINDOW of its own
        s.born = round;
        s.born_len = c.get_length();
        if (F_CHOKE_LOOP && round > 0 && s.born_len >= 4) s.deadend_until = round + DEADEND_COOL;
        s.last_food = round;

        s.alpha = is_primary_alpha_id(c.get_id());
        // Only an L-2 split (emergency, threat or portal sacrifice) makes a child of 4 or more: it is the
        // rear of an alpha-sized body and inherits the role, as long as it was not born into a dead end.
        if (F_HANDOVER && round > 0 && c.get_length() >= 4 && c.get_id() > 1) {
            int pp = 0, psz = 0;
            s.alpha = !sealed_pocket(here, di(c.get_dir()), pp, psz);
            if (s.alpha) s.mantle_round = round;
        }
        s.evacuating = !s.alpha && c.get_id() > 1 && enclosed_nursery();

        // v5.7 (a2): sectors by a hash of the ID. By id / 2, teammates 2k and 2k + 1 chased the same waypoints.
        s.sector = F_SECTOR_HASH ? static_cast<int>(mix(static_cast<std::uint32_t>(c.get_id()) * 2654435761U) % 8)
                              : (c.get_id() / 2) % 8;
        s.sector_target = sector_waypoint(s.sector);
        s.explore_timer = 0;
        if (F_HYBRID) hybrid_retarget(false);
        s.dispersing = true;
        s.used_portals.fill({-1, -1000});
        s.elen_mem.fill({-1, 0, -1000});
        s.drops.fill({Position{0, 0}, -1000});
        if (round == 0) {
            s.spawn = here;
            s.spawn_known = true;
        }
        s.camp_seen.fill({-1, -1000, -1000});
        s.demoted.fill({-1, -1000});
        check_adaptive_kamikaze();
    }

    void decode_tag(std::uint64_t msg, int tag) {
        int origin = static_cast<int>((msg >> 40) & 511ULL);
        int age = round - origin;
        if (age < 0) return;
        int px = static_cast<int>((msg >> 34) & 63ULL), py = static_cast<int>((msg >> 28) & 63ULL);
        int bits = static_cast<int>((msg >> 20) & 255ULL);
        if (tag == SPAWN_TAG) {
            if (!F_RETREAT || s.spawn_known || px >= w || py >= h) return;
            s.spawn = {px, py};
            s.spawn_known = true;
            return;
        }
        if (tag == LANE_TAG) {
            if (!F_LANE || age > 3 || px >= w || py >= h) return;
            // Somebody walks a corridor toward its far end {px, py}: nobody steps onto that tile heading into it.
            add_mark(s.lanes, {px, py}, ((bits & 3) + 2) % 4, origin + ((bits >> 2) & 31) + 2);
            DIAG("lanerecv " << c.get_id() << ' ' << round << ' ' << px << ',' << py << " dir " << (bits & 3));
            return;
        }
        if (tag == PROBE_TAG) {
            if (!F_PORTAL_PROBE || age > 2 || px >= w || py >= h) return;
            add_mark(s.avoid, {px, py}, -1, origin + 2); // a teammate comes out there next turn
            DIAG("proberecv " << c.get_id() << ' ' << round << ' ' << px << ',' << py);
            return;
        }
        if (tag == SCOUT_TAG) {
            if (!F_SYMMETRY || age > SCOUT_TTL) return;
            int k = bits & 3;
            if (s.sym < 0 && !(bits & 64) && sym_possible(k) && !s.sym_bad[k]) {
                s.sym = k;
                DIAG("symheard " << c.get_id() << ' ' << round << ' ' << k);
            }
            int value = (bits >> 2) & 15;
            if (!F_MIRROR_SCOUT || value == 0 || px >= w || py >= h) return;
            DIAG("scoutrecv " << c.get_id() << ' ' << round << ' ' << px << ',' << py << " v " << value << " claimed "
                              << ((bits >> 7) & 1) << " age " << age);
            note_scout({px, py}, origin, value, (bits & 128) != 0);
            return;
        }
        if (tag == FARM_TAG) {
            if (!F_FARM || age > FARM_TTL || px >= w || py >= h) return;
            int value = (bits >> 2) & 15;
            bool claimed = (bits & 128) != 0;
            note_farm({px, py}, bits & 3, value, claimed ? -1000 : origin, (bits & 64) != 0, claimed ? origin : -1000,
                      claimed ? 4094 : -1);
            if (!claimed && origin == round && round == s.born) s.farm_birth = farm_near({px, py}, 0);
            DIAG("farmrecv " << c.get_id() << ' ' << round << ' ' << px << ',' << py << " v " << value << " claimed " << claimed
                             << " age " << age);
            return;
        }
        if (tag == BARREN_TAG) {
            if (!F_BARREN || age > 4 || px >= w || py >= h) return;
            int until = origin + ((bits >> 3) & 31) * 4, k = bits & 3;
            if (until <= round) return;
            Position zp{px, py};
            note_barren(zp, until);
            if ((bits & 4) && sym_possible(k)) {
                Position m = mirror(zp, k);
                if (chebyshev(m, zp) > 2 * BARREN_R) note_barren(m, until);
            }
            return;
        }
        if (tag == MANTLE_TAG) {
            if (!F_MANTLE || age > 10) return;
            int old = px | (py << 6);
            if (old == c.get_id()) return;
            note_demoted(old, origin);
            if (old == team_zero_id_val()) s.primary_demoted = true;
            // The split child: born this very round, as long as the parent says. The beam the parent fired into its
            // own body refracted out of its tail into us.
            if (round == s.born && origin == round && bits == c.get_length() && bits >= 2) {
                s.alpha = true;
                s.mantle_round = round;
                s.mantle_from = old;
                DIAG("claim " << c.get_id() << ' ' << round << " from " << old << " len " << c.get_length());
            }
            return;
        }
        if (tag == HANDOVER_TAG) {
            if (age > 10) return;
            int target = px | (py << 6);
            if (bits == team_zero_id_val()) s.primary_demoted = true;
            if (bits < 255)
                s.alphas.erase(std::remove_if(s.alphas.begin(), s.alphas.end(),
                                          [&](const AlphaTrack &a) { return a.id == bits && a.seen <= origin; }),
                           s.alphas.end());
            if (target == (c.get_id() & 4095)) {
                if (bits != c.get_id()) s.alpha = true;
            } else if (origin > s.handover_round) {
                s.handover_to = target; // pass it on for a few rounds so it reaches the new alpha
                s.handover_old = bits;
                s.handover_round = origin;
            }
            return;
        }
        if (tag == HAZARD_TAG) {
            if (!F_HAZARD || age > HAZARD_TTL || px >= w || py >= h || !(bits & 4)) return;
            if (add_hazard({px, py}, bits & 3, origin))
                DIAG("hazrecv " << c.get_id() << ' ' << round << ' ' << px << ',' << py << " dir " << (bits & 3) << " age " << age);
            return;
        }
        int sender = px | (py << 6), pid = bits & 31;
        auto &port = portal(pid);
        if (bits & 32) port.small = true;
        if (bits & 64) { // nothing to gain behind it: keep everyone out for good
            DIAG("gotbarren " << c.get_id() << ' ' << round << ' ' << pid);
            if (F_PORTAL_TRAP && !port.barren) note_news(pid);
            port.barren = true;
            port.occupied = true;
            return;
        }
        if (!F_PORTAL_RESERVE) return;
        if (bits & 128) { // the sender has left: the enclosure is free again
            if (origin + RESERVE_TTL >= port.reserved_until) {
                port.reserved_until = -1;
                port.occupied = false;
            }
            return;
        }
        if (age > RESERVE_TTL) return;
        // Two dragons reaching for the same portal at once: the lower ID keeps it.
        if (s.contend_portal == pid && round - s.contend_round <= 3 && (c.get_id() & 4095) < sender) return;
        if (origin + RESERVE_TTL > port.reserved_until) {
            if (round > port.reserved_until || port.reserved_by != sender)
                DIAG("resvrecv " << c.get_id() << ' ' << round << ' ' << pid << " by " << sender << " age " << age);
            port.reserved_until = origin + RESERVE_TTL;
            port.reserved_by = sender;
        }
    }

    void observe() {
        round = g.get_round_num();
        here = c.get_position();
        friends.clear();
        enemies.clear();
        risk_cache.fill(-1);

        auto echoes = c.get_sonar_echoes();
        sonar_enemy_alert = (echoes.enemy_head > 0 || echoes.enemy >= 2);

        auto cur_tile = c.get_tile(here);
        if (cur_tile && cur_tile->has_pearl()) s.last_food = round;

        friend_len_cache.clear();
        enemy_len_cache.clear();

        for (const auto &t : c.get_tiles()) {
            auto p = t.get_position();
            auto &cell = s.cells[index(p)];
            if (cell.seen < 0) { // v5.6, first sight: how open and uniform is this map?
                ++s.n_seen;
                s.n_never += t.get_pearl_time() < 0;
                s.n_good += t.has_pearl() || (t.get_pearl_time() >= 0 && t.get_pearl_time() <= POCKET_DUE);
                for (int d = 0; d < 4; ++d) s.n_kelp += !t.get_edge(DIRS[d]).is_passable();
            }
            // v5.9c (F_DROP_LEARN): a pearl where a dragon body lay last turn is (almost always) a death drop: follow it.
            bool was_dragon = cell.seen == round - 1 && cell.has_dragon;
            cell.seen = round;
            auto part = t.get_dragon();
            if ((F_DROP_LEARN || F_COLLECT) && was_dragon && !part && t.has_pearl()) note_drop(p);
            cell.has_dragon = (part != nullptr);
            cell.spawn_at = t.has_pearl() ? round : t.get_pearl_time() >= 0 ? round + t.get_pearl_time() : -1;
            cell.fast_obs = (t.get_pearl_time() >= 0 && t.get_pearl_time() <= 1) ? cell.fast_obs + 1 : 0;
            cell.quick_obs = (t.get_pearl_time() >= 0 && t.get_pearl_time() <= 5) ? cell.quick_obs + 1 : 0;
            cell.sym_t = t.get_pearl_time() >= 0 ? round + t.get_pearl_time() : -1;
            if (t.has_pearl() && !part) {
                cell.pearl_round = round;
            } else if (!t.has_pearl() && t.get_pearl_time() > 0 && t.get_pearl_time() <= 14 && !part) {
                cell.pearl_round = round + t.get_pearl_time();
            } else {
                cell.pearl_round = -1;
            }
            for (int d = 0; d < 4; ++d) {
                const auto &e = t.get_edge(DIRS[d]);
                cell.edge[d] = !e.is_passable() ? -1 : e.is_portal() ? e.get_portal_id() + 1 : 0;
                if (e.is_portal()) {
                    auto &port = portal(e.get_portal_id());
                    auto endpoint = canonical(p, d);
                    if (std::find(port.ends.begin(), port.ends.end(), endpoint) == port.ends.end())
                        port.ends.push_back(endpoint);
                }
            }
            if (part) {
                auto &cache = (part->get_team() == c.get_team()) ? friend_len_cache : enemy_len_cache;
                int id = part->get_id();
                bool found = false;
                for (auto &entry : cache)
                    if (entry.first == id) { ++entry.second; found = true; break; }
                if (!found) cache.push_back({id, 1});
                if (part->is_head() && id != c.get_id())
                    (part->get_team() == c.get_team() ? friends : enemies).push_back(*part);
            }
        }

        observe_symmetry();
        find_barren();
        find_rendezvous();
        find_farms();
        if (F_MIRROR_SCOUT) {
            int n = 0;
            for (const auto &t : c.get_tiles())
                if (t.has_pearl() && !t.get_dragon()) ++n;
            s.pearl_ema = s.pearl_ema < 0 ? n : 0.9 * s.pearl_ema + 0.1 * n;
        }

        // A portal edge with a dead cell on both sides kills whoever crosses its partner, from either side:
        // bar it for good and tell the team (the barren flag already means "never enter").
        if (F_PORTAL_TRAP)
            for (const auto &t : c.get_tiles())
                for (int d = 0; d < 4; ++d) {
                    Position p = t.get_position();
                    int e = s.cells[index(p)].edge[d];
                    if (e <= 0) continue;
                    auto &port = portal(e - 1);
                    if (port.trap_checked) continue;
                    int ahead = dead_cell(step(p, d), d), behind = dead_cell(p, (d + 2) % 4);
                    if (ahead < 0 || behind < 0) continue;
                    port.trap_checked = true;
                    if (ahead == 1 && behind == 1) {
                        if (!port.barren) note_news(port.id);
                        port.barren = port.occupied = true;
                        DIAG("deadportal " << c.get_id() << ' ' << round << ' ' << port.id);
                    }
                }

        for (const auto &f : friends) s.cells[index(f.position)].pearl_round = -1;
        for (const auto &e : enemies) s.cells[index(e.position)].pearl_round = -1;
        s.cells[index(here)].pearl_round = -1;

        auto by_id = [](const DragonPart &a, const DragonPart &b) { return a.get_id() < b.get_id(); };
        std::sort(friends.begin(), friends.end(), by_id);
        std::sort(enemies.begin(), enemies.end(), by_id);

        if (!s.initialized) initialize();
        check_adaptive_kamikaze();
        track_body();

        if (F_ENEMY_LEN)
            for (const auto &e : enemies)
                if (!body_leaves_view(e.get_id())) note_enemy_len(e.get_id(), enemy_visible_length(e.get_id()));
        if (F_DROP_LEARN || F_COLLECT) track_drops();

        // F_PORTAL_PROBE: last turn we fired one beam through the portal edge beside us (and nothing else, so the echo is
        // its alone). Kelp or nothing: the line behind the portal is clear. A dragon: someone is on it; it counts as right
        // behind the portal when known kelp closes that line within PROBE_RANGE tiles (a chamber).
        s.probe_verdict = 0;
        s.probe_bounded = false;
        if (F_PORTAL_PROBE && s.probe_round == round - 1 && s.probe_dir >= 0 && here == s.probe_pos) {
            auto ec = c.get_sonar_echoes();
            if (ec.ally + ec.ally_head > 0) s.probe_verdict = 1;
            else if (ec.enemy + ec.enemy_head > 0) s.probe_verdict = 2;
            if (s.probe_verdict > 0) {
                auto dest = destination(here, s.probe_dir);
                if (dest) {
                    // The way out of the partner edge: the same direction for W/N crossings, as destination() does.
                    Position p = *dest;
                    int k = 0;
                    while (k < PROBE_RANGE && s.cells[index(p)].seen >= 0 && s.cells[index(p)].edge[s.probe_dir] == 0) {
                        p = step(p, s.probe_dir);
                        ++k;
                    }
                    s.probe_bounded = k < PROBE_RANGE && s.cells[index(p)].seen >= 0 && s.cells[index(p)].edge[s.probe_dir] == -1;
                } else {
                    s.probe_bounded = true; // never seen the far side: behind a portal that is usually a chamber
                }
            }
            DIAG("probe " << c.get_id() << ' ' << round << " dir " << s.probe_dir << " verdict " << s.probe_verdict << " bounded "
                          << s.probe_bounded << " echo " << ec.kelp << ec.ally << ec.ally_head << ec.enemy << ec.enemy_head);
        }

        // Enemy Alpha tracking from direct vision
        for (const auto &e : enemies) {
            if (e.get_id() <= 1 || enemy_visible_length(e.get_id()) >= 6) {
                s.enemy_alpha_pos = e.position;
                s.enemy_alpha_seen = round;
            }
        }

        // Decode 64-bit Protocol 3 sonar mesh packets
        std::uint64_t team_sig = (c.get_team() == Team::A) ? 1ULL : 2ULL;
        for (auto msg : c.get_sonar_messages()) {
            if ((msg >> 62) != team_sig) continue;
            if (static_cast<int>((msg >> 49) & SONAR_ID_MASK) == BARRED_TAG) {
                for (int pid = 0; pid < 32; ++pid) {
                    if (!((msg >> pid) & 1ULL)) continue;
                    auto &port = portal(pid);
                    if (!port.barren) note_news(pid);
                    port.barren = port.occupied = true;
                }
                continue;
            }
            int kam_bucket = static_cast<int>((msg >> 4) & 7ULL);
            if (kam_bucket > 0 && round < feed_round()) {
                int est_origin = round - (kam_bucket - 1) * 4;
                if (est_origin <= round && round - est_origin <= 28 && est_origin > s.kamikaze_signal_round) {
                    s.kamikaze_signal_round = est_origin;
                }
            }
            if ((msg >> 19) & 1ULL) {
                int ex = static_cast<int>((msg >> 13) & 63ULL);
                int ey = static_cast<int>((msg >> 7) & 63ULL);
                int eage = static_cast<int>(msg & 15ULL);
                int eseen = round - eage;
                if (ex < w && ey < h && eseen <= round && eseen > s.enemy_alpha_seen) {
                    s.enemy_alpha_pos = {ex, ey};
                    s.enemy_alpha_seen = eseen;
                }
            }
            int alpha_id = static_cast<int>((msg >> 49) & SONAR_ID_MASK);
            if (alpha_id == HAZARD_TAG || alpha_id == PORTAL_TAG || alpha_id == HANDOVER_TAG || alpha_id == MANTLE_TAG ||
                alpha_id == SCOUT_TAG || alpha_id == BARREN_TAG || alpha_id == FARM_TAG || alpha_id == LANE_TAG ||
                alpha_id == PROBE_TAG || alpha_id == SPAWN_TAG) {
                decode_tag(msg, alpha_id);
                continue;
            }
            if (alpha_id >= SONAR_ID_MASK || alpha_id == (c.get_id() & SONAR_ID_MASK)) continue;
#ifdef BOT_DIAG
            if (alpha_id >= BARREN_TAG) DIAG("collision " << c.get_id() << ' ' << round << " tag " << alpha_id);
#endif
            int origin_round = static_cast<int>((msg >> 40) & 511ULL);
            int age = round - origin_round;
            if (age < 0 || age > SONAR_TTL) continue;
            Position p{static_cast<int>((msg >> 34) & 63ULL), static_cast<int>((msg >> 28) & 63ULL)};
            if (p.x >= w || p.y >= h) continue;
            int alen = static_cast<int>((msg >> 20) & 255ULL);
            // v5.4: keep the real length. The old floor of 8 made a 2-long alpha look like an apex worth feeding.
            remember_alpha(alpha_id, p, origin_round, F_TRUE_LEN ? std::max(2, alen) : std::max(8, alen));
        }

#ifdef BOT_DIAG
        if (round == s.born) {
            int nb = 0;
            for (const auto &pp : s.portals) nb += pp.barren;
            DIAG("born " << c.get_id() << ' ' << round << ' ' << c.get_length() << " knows " << nb << " msgs "
                         << c.get_sonar_messages().size());
        }
#endif
        // Remember visible alphas directly
        for (auto f : friends) {
            int id = f.get_id();
            int flen = friendly_visible_length(id);
            bool is_a = primary_alpha_seen(f) || flen > 7;
            if (!is_a) {
                for (const auto &a : s.alphas)
                    if ((a.id & SONAR_ID_MASK) == (id & SONAR_ID_MASK) && round - a.seen <= SONAR_TTL) {
                        is_a = true;
                        break;
                    }
            }
            if (is_a) {
                // Vision may only show part of the body, so never let it shrink the tracked length.
                int known = F_TRUE_LEN ? 2 : 8;
                for (const auto &a : s.alphas)
                    if (a.id == (id & SONAR_ID_MASK)) known = std::max(known, a.len);
                remember_alpha(id & SONAR_ID_MASK, f.position, round, std::max(known, flen));
            }

            int e = s.cells[index(f.position)].edge[(di(f.get_dir()) + 2) % 4];
            if (e > 0 && round > s.born) {
                bool changed = true;
                for (auto prev : s.previous_heads)
                    if (prev.get_id() == f.get_id() && prev.position == f.position) changed = false;
                if (changed) {
                    const auto &pp = portal_const(e - 1);
                    occupy(e - 1, (!F_PORTAL_FIX || pp.small) ? INF : round + FRIEND_PORTAL_TTL);
                }
            }
        }

        s.previous_heads = friends;

        // v5.9 (F_ENEMY_CHAMBER): an enemy coming out of a portal (its neck across the edge behind its head) or going into one
        // (a body segment facing across it): if a small chamber lies behind, the enemy holds it. Our dragons that walked into
        // queen_of_spades' enemy-held chamber died there six times in one game.
        if (F_ENEMY_CHAMBER)
            for (const auto &t : c.get_tiles()) {
                auto part = t.get_dragon();
                if (!part || part->get_team() == c.get_team()) continue;
                Position p = t.get_position();
                int d = part->is_head() ? (di(part->get_dir()) + 2) % 4 : di(part->get_dir());
                int e = s.cells[index(p)].edge[d];
                if (e <= 0) continue;
                auto &pp = portal(e - 1);
                auto dest = destination(p, d);
                if (!pp.small && !(dest && enclosure_at(*dest).small)) continue;
                if (pp.enemy_until < round + ENEMY_CHAMBER_TTL) {
                    DIAG("enemychamber " << c.get_id() << ' ' << round << ' ' << pp.id << " at " << p.x << ',' << p.y);
                    pp.enemy_until = round + ENEMY_CHAMBER_TTL;
                }
            }
        s.alphas.erase(std::remove_if(s.alphas.begin(), s.alphas.end(),
                                      [&](const AlphaTrack &a) { return round - a.seen > SONAR_TTL; }),
                       s.alphas.end());

        // Promote long survivors (>7) to Alpha and originate their own sonar.
        if (!s.alpha && c.get_length() > 7 && !(F_CASCADE_FIX && round == 0)) {
            s.alpha = true;
            s.growing = true;
        }

        friend_alpha_cache.clear();
        for (auto f : friends) {
            int id = f.get_id();
            bool is_a = primary_alpha_seen(f) || friendly_visible_length(id) > 7;
            if (!is_a) {
                for (const auto &a : s.alphas)
                    if ((a.id & SONAR_ID_MASK) == (id & SONAR_ID_MASK) && round - a.seen <= SONAR_TTL) {
                        is_a = true;
                        break;
                    }
            }
            if (is_a) friend_alpha_cache.push_back(id);
        }

        // Apex election: entering the endgame with no living alpha known, the locally longest dragon
        // declares itself alpha so the swarm has something to feed. Rival apexes then merge by length.
        if (!s.alpha && round >= feed_round() - 5 && friend_alpha_cache.empty()) {
            bool known = false;
            for (const auto &a : s.alphas)
                if (round - a.seen <= SONAR_TTL) known = true;
            bool biggest = true;
            for (const auto &f : friends) {
                int flen = friendly_visible_length(f.get_id());
                if (flen > c.get_length() || (flen == c.get_length() && f.get_id() < c.get_id())) biggest = false;
            }
            // v5.5: not a 2-3 long head, and not one that just handed the role to its split child (it has not heard the
            // child's own alpha packets yet, so it would take the role straight back).
            bool eligible = !F_MANTLE || (c.get_length() >= 4 && round - s.mantle_send > 30);
            if (!known && biggest && eligible) {
                s.alpha = true;
                s.growing = true;
            }
        }

        ++s.explore_timer;
        ++s.explore_timer_h;
        bool reached_wp = dist(here, s.sector_target) <= 3;
        if (reached_wp || s.explore_timer > std::clamp((w + h) / 2, 16, 64)) {
            s.dispersing = false;
            s.sector = (s.sector + 3) % 8;
            if (hybrid_on()) hybrid_retarget(!reached_wp);
            else s.sector_target = sector_waypoint(s.sector);
            s.explore_timer = 0;
        }

        int repeats = static_cast<int>(std::count(s.recent_path.begin(), s.recent_path.end(), here));
        if (repeats >= 2 && !s.resident) {
            s.sector = (s.sector + 3) % 8;
            if (!hybrid_on()) {
                s.sector_target = sector_waypoint(s.sector);
                s.explore_timer = 0;
            } else if (s.explore_timer_h > 3) {
                hybrid_retarget(true);
            }
        }
        // Enclosure entry: remember where we stepped from open ground into a corridor.
        if (F_HAZARD && !s.recent_path.empty()) {
            Position prev = s.recent_path.back();
            for (int d = 0; d < 4; ++d)
                if (s.cells[index(prev)].edge[d] == 0 && step(prev, d) == here &&
                    passable_edges(here) <= 2 && passable_edges(prev) >= 3) {
                    s.enc_mouth = here;
                    s.enc_dir = d;
                    s.enc_round = round;
                }
        }
        s.cells[index(here)].visited = round;

        s.recent_path.push_back(here);
        if (s.recent_path.size() > 24) s.recent_path.erase(s.recent_path.begin());

        // Loose loops: few distinct tiles over the last 16 turns with nothing eaten for 8 rounds. A snake
        // cannot reverse, so the tightest real loop is a 2x2 square; circling a pearl-less block of 6-7
        // tiles, or a near-periodic figure eight, slipped past the strict period test below.
        if (F_CYCLE2 && !s.resident && round >= s.cycle_break_until && round - s.last_food > 8 &&
            s.last_mode != Mode::Feed && s.last_mode != Mode::Reside && !(s.alpha && round >= feed_round())) {
            int n = static_cast<int>(s.recent_path.size());
            if (n >= 16) {
                int distinct = 0;
                for (int i = n - 16; i < n; ++i) {
                    bool dup = false;
                    for (int j = n - 16; j < i && !dup; ++j) dup = s.recent_path[j] == s.recent_path[i];
                    if (!dup) ++distinct;
                }
                if (distinct <= 7) {
                    DIAG("cycle2 " << c.get_id() << ' ' << round << " distinct " << distinct << " idle " << round - s.last_food);
                    break_loop(16);
                }
            }
        }
        // Oscillation: the last 2p positions repeat with period p and we ate nothing along the loop.
        if (F_CYCLE && !s.resident && round >= s.cycle_break_until && round - s.last_food > 16) {
            int n = static_cast<int>(s.recent_path.size());
            for (int per = 2; per <= 8 && 2 * per <= n; ++per) {
                bool periodic = true;
                for (int k = n - per; k < n && periodic; ++k)
                    if (s.recent_path[k] != s.recent_path[k - per]) periodic = false;
                if (!periodic) continue;
                DIAG("cycle1 " << c.get_id() << ' ' << round << " period " << per << " idle " << round - s.last_food);
                if (F_CYCLE2) { break_loop(2 * per); break; }
                std::uint32_t r = mix(static_cast<std::uint32_t>(c.get_id()) * 7919U + static_cast<std::uint32_t>(round));
                s.cycle_break_until = round + 8;
                s.sector = (s.sector + 1 + static_cast<int>(r % 7)) % 8;
                s.sector_target = sector_waypoint(s.sector);
                s.explore_timer = 0;
                break;
            }
        }

        bool from_chamber = false;
        if (s.pending_portal >= 0) {
            // Remember the crossing (chained rooms: don't turn straight back through it).
            auto oldest = std::min_element(s.used_portals.begin(), s.used_portals.end(),
                                           [](const auto &a, const auto &b) { return a.second < b.second; });
            *oldest = {s.pending_portal, round};
            from_chamber = F_PORTAL_LOOP && s.pending_portal == s.chamber_portal && round - s.chamber_round <= 2;
            // We came out in a dead cell: bar the portal for the team (a split lets the rear live to say so).
            if (F_PORTAL_TRAP && dead_cell(here, di(c.get_dir())) == 1) {
                auto &port = portal(s.pending_portal);
                port.barren = port.occupied = true;
                if (port.id < 32) s.dead_alarm = port.id;
                note_news(port.id);
                from_chamber = false;
                DIAG("deadcell " << c.get_id() << ' ' << round << ' ' << port.id << ' ' << c.get_length());
            }
        }
        if (s.pending_portal >= 0 && s.evicting) {
            // We left an enclosure on purpose: it is free again, and we are nobody's resident.
            auto &port = portal(s.pending_portal);
            port.occupied = port.barren;
            port.reserved_until = -1;
            port.shun_until = round + 60;
            // v5.6: a chamber we left because it is dry is not announced as free (that invites the next dragon in):
            // it is reserved in our name, which keeps teammates out for RESERVE_TTL rounds. Our own hold on it goes.
            if (F_DRY_EVICT && s.dry_portal >= 0) {
                s.send_reserve = s.pending_portal;
                port.shun_until = round + std::max(60, DRY_SHUN);
                s.camp_portal = s.chamber_portal = s.loop_portal = s.reclaim_portal = -1;
                s.camping = false;
                s.n_camp_tiles = 0;
                DIAG("dryout " << c.get_id() << ' ' << round << ' ' << s.pending_portal << " len " << c.get_length());
            } else {
                s.send_clear = s.pending_portal;
            }
            s.dry_portal = -1;
            s.resident = false;
            s.home_portal = -1;
            s.evicting = false;
            s.last_food = round;
            s.pending_portal = -1;
        }
        if (s.pending_portal >= 0) {
            s.portal_arrival = round;
            occupy(s.pending_portal);
            // v5.7 (a8): outside a small chamber our crossing holds the portal for FRIEND_PORTAL_TTL rounds, not for good.
            if (F_PORTAL_ROAM && !enclosure_at(here).small) portal(s.pending_portal).occupied_until = round + FRIEND_PORTAL_TTL;
            s.resident = !s.evacuating;
            // v5.2 made every portal user a resident for good, barring it from all portals. A dragon that
            // comes out somewhere with no food (PORTAL_RESIDENCY) stays free to use portals again; it only
            // avoids bouncing straight back through the one it took.
            bool release = PORTAL_RESIDENCY == 1 ? !enclosure_at(here).small
                         : PORTAL_RESIDENCY >= 2 ? spawners_in_view() == 0 : false;
            if (PORTAL_RESIDENCY >= 3 && !release && !enclosure_at(here).small) {
                int open = 0, soon = 0, horizon = PORTAL_RESIDENCY == 3 ? 10 : 40;
                for (const auto &t : c.get_tiles()) {
                    if (!t.get_dragon() && passable_edges(t.get_position()) >= 3) ++open;
                    if (t.has_pearl() || (t.get_pearl_time() >= 0 && t.get_pearl_time() <= horizon)) ++soon;
                }
                release = soon == 0 && open >= 25;
                if (release) DIAG("openarrival " << c.get_id() << ' ' << round << ' ' << s.pending_portal << " open " << open);
            }
            if (F_PORTAL_FIX && s.resident && release) {
                auto &pp = portal(s.pending_portal);
                pp.occupied = pp.barren;
                pp.shun_until = std::max(pp.shun_until, round + 20);
                s.resident = false;
            }
            // Walked out of a dense chamber (a 2x2 room forces that every few steps): stay free, and go back in
            // through the same portal once round its partner edge.
            if (from_chamber) {
                s.resident = false;
                s.loop_portal = s.pending_portal;
                s.loop_until = round + LOOP_TTL;
                DIAG("loopout " << c.get_id() << ' ' << round << ' ' << s.loop_portal << ' ' << c.get_length());
            }
            // v5.6: a camper that was pushed through its chamber's portal (no other move left) goes straight back in.
            if (F_REENTRY && s.pending_portal == s.transit_portal && round - s.transit_round <= 2) {
                s.resident = false;
                s.reclaim_portal = s.pending_portal;
                s.reclaim_until = round + REENTRY_TTL;
                DIAG("forcedout " << c.get_id() << ' ' << round << ' ' << s.pending_portal << " len " << c.get_length()
                                  << " at " << here.x << ',' << here.y);
            } else if (F_REENTRY && s.pending_portal == s.reclaim_portal) {
                DIAG("reclaimed " << c.get_id() << ' ' << round << ' ' << s.pending_portal << " len " << c.get_length());
                s.reclaim_portal = -1;
            }
            s.transit_portal = -1;
            s.home_portal = s.resident ? s.pending_portal : -1;
            s.return_tile = here;
            s.evacuating = false;
            s.last_food = round;
            s.pending_portal = -1;
        }

        if (s.alpha) {
            s.growing = (round > threshold() || c.get_unit_count() >= alpha_split_cap());
        } else {
            s.growing = (round >= 380 && c.get_unit_count() >= 14);
        }

        if (F_ALPHA_MEMORY && s.alpha) remember_pearls();

        // Crowding: two or more non-alpha teammate heads within 3 tiles for 3 turns running. Each member
        // heads off away from the others' centroid, so the pack fans out over different lanes.
        if (F_REPEL && F_FANOUT && !s.alpha && !s.resident && round < feed_round() && round >= s.fanout_until) {
            int n = 0, sx = 0, sy = 0;
            for (const auto &f : friends) {
                if (is_alpha(f.get_id()) || dist(here, f.position) > 3) continue;
                ++n;
                sx += delta(here.x, f.position.x, w);
                sy += delta(here.y, f.position.y, h);
            }
            s.crowd_turns = n >= 2 ? s.crowd_turns + 1 : 0;
            if (s.crowd_turns >= 3) {
                set_heading_away(-sx, -sy);
                s.fanout_until = round + 12;
                s.crowd_turns = 0;
            }
        }
        track_straddle();
    }

    // Segments still on the near side of the last portal we went through, from last turn's move and the
    // length it left us (a pearl on the far side is invisible when we step in, so this waits for the result).
    void track_straddle() {
        int len = c.get_length(), k = s.moved_steps;
        if (k > 0) {
            int eaten = std::max(0, len - s.moved_len + (k - 1));
            if (s.moved_cross >= 0) {
                int after = k - 1 - s.moved_cross; // steps taken after the crossing, each moving the tail
                s.straddle_left = len - 1 - std::max(0, after - eaten);
            } else {
                s.straddle_left -= std::max(0, k - eaten) + (k - 1);
            }
        }
        s.moved_steps = 0;
        s.moved_cross = -1;
        s.straddle_left = std::clamp(s.straddle_left, 0, std::max(0, len - 1));
    }

    int spawners_in_view() const {
        int n = 0;
        for (const auto &t : c.get_tiles())
            if (t.has_pearl() || t.get_pearl_time() >= 0) ++n;
        return n;
    }

    // Retarget exploration toward a point `reach` tiles away along (vx, vy) (any length, any sign). A zero
    // vector picks a side by ID so two dragons in the same spot split up.
    void set_heading_away(int vx, int vy) {
        if (vx == 0 && vy == 0) {
            int d = (di(c.get_dir()) + ((c.get_id() & 1) ? 1 : 3)) % 4;
            auto [ox, oy] = DIRS[d].get_offset();
            vx = ox;
            vy = oy;
        }
        int reach = std::clamp(map_scale() / 2, 5, 12);
        double len = std::sqrt(static_cast<double>(vx * vx + vy * vy));
        int tx = here.x + static_cast<int>(std::lround(reach * vx / len));
        int ty = here.y + static_cast<int>(std::lround(reach * vy / len));
        s.sector_target = wrap({tx, ty});
        s.explore_timer = 0;
        s.dispersing = true;
    }

    // Leave the loop the last `span` positions traced: head away from its centroid for 10 rounds.
    void break_loop(int span) {
        int n = static_cast<int>(s.recent_path.size());
        int sx = 0, sy = 0, k = 0;
        for (int i = std::max(0, n - span); i < n; ++i, ++k) {
            sx += delta(here.x, s.recent_path[i].x, w);
            sy += delta(here.y, s.recent_path[i].y, h);
        }
        s.cycle_break_until = round + 10;
        if (k > 0) set_heading_away(-sx, -sy);
    }

    // The alpha's pearl queue: every pearl it has seen and not yet seen gone. Entries leave the queue when
    // the tile is seen empty, after 80 rounds, or when a fresher sighting needs the slot.
    void remember_pearls() {
        for (auto &m : s.pearl_mem) {
            if (round - m.seen > 80) { m.seen = -1000; continue; }
            auto t = c.get_tile(m.p);
            if (t && (!t->has_pearl() || t->get_dragon())) m.seen = -1000;
        }
        for (const auto &t : c.get_tiles()) {
            if (!t.has_pearl() || t.get_dragon()) continue;
            Position p = t.get_position();
            int slot = -1, oldest = 0;
            for (int i = 0; i < MAX_PEARL_MEM; ++i) {
                if (s.pearl_mem[i].seen > -1000 && s.pearl_mem[i].p == p) { slot = i; break; }
                if (s.pearl_mem[i].seen < s.pearl_mem[oldest].seen) oldest = i;
            }
            if (slot < 0) slot = oldest;
            s.pearl_mem[slot] = {p, round, false};
        }
    }

    std::optional<Position> alpha_memory_target() const {
        std::optional<Position> best;
        double best_score = -1e20;
        for (const auto &m : s.pearl_mem) {
            if (m.seen <= -1000 || m.seen == round) continue; // still in view: food_target already judged it
            int d = dist(here, m.p);
            if (d < 1 || d > 24) continue;
            if (F_AMEM_FIX) {
                if (m.claimed || claimed(m.p)) continue;
                if (F_ROUTE_MEM) {
                    memory_bfs();
                    int md = mem_depth[index(m.p)];
                    if (md >= INF || md > d + 3) continue;
                    d = md;
                }
            }
            double score = 40.0 / (d + 1.0) - 0.15 * (round - m.seen);
            if (score > best_score) { best_score = score; best = m.p; }
        }
        return best;
    }

    // v5.9d (F_AMEM_FIX): pearls in view that a teammate is nearer to are left to it (needs this turn's BFS: after paths()).
    void mark_amem_claims() {
        for (auto &m : s.pearl_mem)
            if (m.seen == round) m.claimed = claimed(m.p);
    }

    // v5.9d (F_AMEM_FIX): keep the remembered pearl we set off for until it is gone, claimed, reached or late.
    std::optional<Position> alpha_memory_pick() {
        if (!F_AMEM_FIX) return alpha_memory_target();
        if (round <= s.amem_until && s.amem_goal != here) {
            for (const auto &m : s.pearl_mem)
                if (m.seen > -1000 && m.p == s.amem_goal && !m.claimed && !claimed(m.p) && mem_reachable(m.p))
                    return s.amem_goal;
        }
        s.amem_until = -1000;
        auto p = alpha_memory_target();
        if (p) {
            s.amem_goal = *p;
            int k = F_ROUTE_MEM ? (memory_bfs(), mem_depth[index(*p)]) : dist(here, *p);
            s.amem_until = round + k + 3;
        }
        return p;
    }

    // v5.6: a camper about to cross its chamber's portal only because nothing else is left: come straight back.
    void note_transit(int pid) {
        if (!F_REENTRY || !s.camping || s.evicting || pid < 0) return;
        s.transit_portal = pid;
        s.transit_round = round;
    }

    void note_news(int pid) {
        if (pid < 0 || pid >= 32) return;
        s.news_portal = pid;
        s.news_until = round + 1;
    }

    // The portal of the tiny chamber we are harvesting: ours to cross, whatever the occupancy flags say.
    bool own_portal(int id) const {
        if (F_REENTRY && id >= 0 && id == s.reclaim_portal && round <= s.reclaim_until) return true;
        return F_PORTAL_LOOP && id >= 0 &&
               ((id == s.chamber_portal && round - s.chamber_round <= 1 && s.chamber_size <= LOOP_CHAMBER) ||
                (id == s.loop_portal && round <= s.loop_until));
    }

    // Portals we crossed recently: leaving a room through one of them is going backwards.
    bool recently_used(int id) const {
        for (const auto &u : s.used_portals)
            if (u.first == id && round - u.second <= 60) return true;
        return false;
    }

    // v5.7 (a6): a small chamber behind this portal edge (p, d) is already being entered by a teammate: one of its bodies
    // is crossing the portal right now, or its head is within 2 of the approach tile and nearer to it than ours (a tie
    // goes to the lower ID). Sonar reservations reach lower IDs a round late, so both used to go in.
    bool chamber_taken(Position p, int d, int pid) const {
        auto dest = destination(p, d);
        bool small = portal_const(pid).small || (dest && enclosure_at(*dest).small);
        if (!small) return false;
        for (const auto &t : c.get_tiles()) {
            auto part = t.get_dragon();
            if (!part || part->get_team() != c.get_team() || part->get_id() == c.get_id() || part->is_head()) continue;
            if (s.cells[index(t.get_position())].edge[di(part->get_dir())] == pid + 1) return true;
        }
        int mine = dist(here, p);
        for (const auto &f : friends) {
            int k = dist(f.position, p);
            if (k <= 2 && (k < mine || (k == mine && f.get_id() < c.get_id()))) return true;
        }
        return false;
    }

    // v5.8 (b2.png): a teammate is taking this portal edge (p, d) now: its head sits on either side of the edge facing
    // across it (a lower ID, or one already facing it while we are not yet there), or one of its segments is crossing this
    // portal. Entering the same edge from the other side lands in the same chamber.
    bool portal_yield(Position p, int d, int pid) const {
        if (!F_PORTAL_YIELD) return false;
        Position q = step(p, d);
        for (const auto &f : friends) {
            int fd = di(f.get_dir());
            bool facing = (f.position == p && fd == d) || (f.position == q && fd == (d + 2) % 4);
            if (!facing) continue;
            if (f.get_id() < c.get_id() || here != p) return true;
        }
        for (const auto &t : c.get_tiles()) {
            auto part = t.get_dragon();
            if (!part || part->get_team() != c.get_team() || part->get_id() == c.get_id() || part->is_head()) continue;
            if (s.cells[index(t.get_position())].edge[di(part->get_dir())] == pid + 1) return true;
        }
        return false;
    }

    // v5.9 (F_EXIT_CLEAR, queen_of_spades turn ~637): n has a portal edge leading into a small chamber and lies outside it:
    // whoever leaves that chamber (a camper looping, its split children evacuating) lands on n or beside it without seeing
    // it first. `held`: we know someone is in there (a teammate's reservation or occupancy, or an enemy).
    bool exit_lane(Position n, bool *held = nullptr) const {
        if (!EXIT_CLEAR_ON || !portal_adjacent(n)) return false;
        for (int d = 0; d < 4; ++d) {
            int e = s.cells[index(n)].edge[d];
            if (e <= 0) continue;
            const auto &pp = portal_const(e - 1);
            auto dest = destination(n, d);
            bool small = pp.small || (dest && enclosure_at(*dest).small);
            if (!small) continue;
            if (enclosure_at(n).small) return false; // we are inside the chamber ourselves
            if (held)
                *held = (pp.occupied && (pp.barren || round <= pp.occupied_until)) || round <= pp.reserved_until ||
                        round < pp.enemy_until;
            return true;
        }
        return false;
    }

    // v5.7 (a8): an idle dragon (nothing eaten for ROAM_IDLE rounds) may take a portal that is only marked occupied, as
    // long as no small chamber lies behind it (occupancy exists to keep chambers to one dragon) and nobody reserved it.
    bool roam_portal(int id) const {
        if (!F_PORTAL_ROAM || round - s.last_food <= ROAM_IDLE) return false;
        const auto &pp = portal_const(id);
        return pp.id >= 0 && !pp.small && !pp.barren && !(F_PORTAL_RESERVE && round <= pp.reserved_until &&
                                                           pp.reserved_by != (c.get_id() & 4095));
    }

    bool portal_occupied(int id) const {
        if (own_portal(id) && !portal_const(id).barren) return false;
        for (const auto &p : s.portals)
            if (p.id == id)
                return (p.occupied && (!F_PORTAL_FIX || p.barren || round <= p.occupied_until)) ||
                       (F_PORTAL_RESERVE && round <= p.reserved_until &&
                                      p.reserved_by != (c.get_id() & 4095));
        return false;
    }

    void paths() {
        int n_cells = w * h;
        std::fill_n(distance.begin(), n_cells, INF);
        std::fill_n(first.begin(), n_cells, -1);
        std::fill_n(predecessor.begin(), n_cells, -1);
        std::fill_n(arrival.begin(), n_cells, -1);
        n_reached = 0;
        static std::array<Position, BFS_Q> q;
        // v5.9b (F_BODY_FREE, autarky round 112): our segment j from the head is gone after len - j moves, so a route may
        // cross it from then on (the BFS treated our own tail as a wall: the pearl just past it looked 11 steps away).
        static std::array<int, MAX_CELLS> free_at;
        static bool free_init = false;
        if (!free_init) { free_at.fill(-1); free_init = true; }
        std::vector<Position> body;
        if (F_BODY_FREE) {
            body = body_tiles();
            for (int j = 1; j < static_cast<int>(body.size()); ++j) free_at[index(body[j])] = c.get_length() - j;
        }
        int lo = 0, hi = 0;
        q[hi++] = here;
        distance[index(here)] = 0;
        while (lo < hi) {
            auto p = q[lo++];
            int pi = index(p);
            for (int d = 0; d < 4; ++d) {
                if (pi == index(here) && d == (di(c.get_dir()) + 2) % 4) continue;
                if (s.cells[pi].edge[d] != 0) continue;
                auto n = step(p, d);
                if (chokepoint_blocked(n, d)) continue;
                if (F_LANE && lane_blocked(n, d)) continue;
                int ni = index(n);
                auto t = c.get_tile(n);
                if (!t || distance[ni] != INF) continue;
                auto part = t->get_dragon();
                if (part && F_BODY_FREE && part->get_id() == c.get_id() && !part->is_head() && free_at[ni] > 0 &&
                    distance[pi] + 1 > free_at[ni])
                    part = nullptr; // it has moved off by the time we get there
                if (part && !(part->get_team() != c.get_team() && part->is_head())) continue;
                distance[ni] = distance[pi] + 1;
                first[ni] = pi == index(here) ? d : first[pi];
                predecessor[ni] = pi;
                arrival[ni] = d;
                if (n_reached < 64) reached[n_reached++] = ni;
                if (!part && hi < BFS_Q) q[hi++] = n;
            }
        }
        for (const auto &b : body) free_at[index(b)] = -1;
        if (F_PORTAL_COST) weighted_paths();
    }

    // v5.7: tiles with a portal edge are where dragons come out blind. Routes pay PORTAL_STEP_COST extra to step on one
    // (unless a pearl lies there); the last step into a target costs every route the same, so a portal or a pearl on such
    // a tile is still reached. Dijkstra over the tiles the BFS reached (all in view, at most 48).
    bool portal_costly(int ci) const {
        Position p{ci % w, ci / w};
        if (!portal_adjacent(p)) return false;
        auto t = c.get_tile(p);
        return !(t && t->has_pearl());
    }

    void weighted_paths() {
        static std::array<bool, 64> done;
        int hi = index(here);
        for (int i = 0; i < n_reached; ++i) {
            wcost[reached[i]] = INF;
            done[i] = false;
        }
        wcost[hi] = 0;
        // Relax from `here` first, then repeatedly settle the cheapest open tile.
        auto relax = [&](int from) {
            Position p{from % w, from / w};
            for (int d = 0; d < 4; ++d) {
                if (from == hi && d == (di(c.get_dir()) + 2) % 4) continue;
                if (s.cells[from].edge[d] != 0) continue;
                int ni = index(step(p, d));
                if (distance[ni] >= INF || ni == hi) continue;
                int cst = wcost[from] + 1 + (portal_costly(ni) ? PORTAL_STEP_COST : 0);
                int fd = from == hi ? d : wfirst[from];
                if (cst < wcost[ni] || (cst == wcost[ni] && fd == first[ni])) {
                    wcost[ni] = cst;
                    wfirst[ni] = fd;
                }
            }
        };
        relax(hi);
        for (int it = 0; it < n_reached; ++it) {
            int bi = -1;
            for (int i = 0; i < n_reached; ++i)
                if (!done[i] && wcost[reached[i]] < INF && (bi < 0 || wcost[reached[i]] < wcost[reached[bi]])) bi = i;
            if (bi < 0) break;
            done[bi] = true;
            int ci = reached[bi];
            auto t = c.get_tile({ci % w, ci / w});
            if (t && t->get_dragon()) continue; // an enemy head ends a route, as in the BFS
            relax(ci);
        }
    }

    // v5.7: what idling on a tile with a portal edge costs in the move scorer / exploration (a camper or resident lives
    // among such tiles and keeps the old weight).
    double loiter_penalty(int old_weight) const {
        if (!F_PORTAL_COST || s.camping || s.resident) return old_weight;
        return PORTAL_LOITER;
    }

    // The first step toward a target the BFS reached: the portal-averse one when that exists.
    int route_first(Position target) const {
        int ti = index(target);
        if (distance[ti] >= INF) return -1;
        if (F_PORTAL_COST && wcost[ti] < INF && wfirst[ti] >= 0) return wfirst[ti];
        return first[ti];
    }

    int escape_count(Position p) const {
        int n = 0;
        for (int d = 0; d < 4; ++d) {
            int edge = s.cells[index(p)].edge[d];
            if (edge == 0 && empty(step(p, d))) ++n;
            else if (edge > 0 && (!portal_occupied(edge - 1) || (F_EVICT_EXIT && (s.evacuating || s.evicting)))) ++n;
        }
        return n;
    }

    int forward_escape_count(Position p, int arr_d) const {
        int back_d = (arr_d >= 0) ? ((arr_d + 2) % 4) : -1;
        int n = 0;
        for (int d = 0; d < 4; ++d) {
            if (d == back_d) continue;
            int edge = s.cells[index(p)].edge[d];
            if (edge == 0) {
                Position nxt = step(p, d);
                if (empty(nxt) || s.cells[index(nxt)].seen < round) ++n;
            } else if (edge > 0 && (!portal_occupied(edge - 1) || s.evacuating || s.evicting)) {
                ++n;
            }
        }
        return n;
    }

    // A tile we can still walk on as far as we know: visible and empty, or remembered (walls are static).
    bool walkable(Position p) const {
        const auto &cell = s.cells[index(p)];
        if (cell.seen == round) return !cell.has_dragon;
        return true;
    }

    // True if arriving at p (moving in arr_d) seals us into a pocket we can never leave.
    // A snake cannot reverse, so it dies in any exit-less region that has no cycle long enough
    // for its body to circle; region size alone (e.g. a long 1-wide corridor) does not save it.
    // That strict rule is for dragons with mass to lose. Small dragons are cheap and regrow, and the
    // pockets (maze branches, fast-spawning corridors) are where much of the food is: they only avoid
    // tiny pockets and small barren ones. They grow and split a child back out (children face away
    // from the parent, i.e. toward the entrance), so usually only the stub is lost.
    bool is_dead_end_trap(Position p, int arr_d) const {
        int pearls = 0, size = 0, active = 0;
        if (!sealed_pocket(p, arr_d, pearls, size, &active)) return false;
        // v5.5: nothing longer than 2 goes into a dead-end corridor of the map, and a 2-long dragon only for pearls lying
        // there now. A pocket closed only by dragon bodies opens again as they move: judged as in v5.4.
        if (F_CHOKE) {
            int sp = 0, ssz = 0, sact = 0;
            if (sealed_pocket(p, arr_d, sp, ssz, &sact, true))
                return !choke_pays(pocket_live(p, arr_d, sact)) || entry_dooms_child(p, arr_d, ssz, sact) ||
                       pocket_busy(p, arr_d);
        }
        bool small = c.get_length() <= 3 && (!s.alpha || !s.growing);
        if (!small || size < 5) return true;
        return pearls == 0 && size < 12;
    }

    // v5.8: pearls in a dead end, counting its fast spawners out of view as full (F_FARM); `seen` is what we see there.
    int pocket_live(Position p, int d, int seen) const {
        if (!F_FARM) return seen;
        int n = 0, fast = 0, live = 0, busy = 0;
        if (!dead_end_region(p, d, n)) return seen;
        farm_stats(n, fast, live, busy);
        return std::max(seen, live);
    }

    // v5.5: `static_only` judges the map alone (dragon bodies do not count as walls): a dead-end corridor, not a gap
    // between bodies that will have moved on in a few rounds.
    // v5.5: a dead-end corridor of the map itself that this dragon may not enter (any length > 2; a 2-long dragon
    // when no pearl lies in it now).
    bool static_dead_end(Position p, int arr_d) const {
        int sp = 0, ssz = 0, sact = 0;
        return sealed_pocket(p, arr_d, sp, ssz, &sact, true) &&
               (!choke_pays(pocket_live(p, arr_d, sact)) || entry_dooms_child(p, arr_d, ssz, sact) || pocket_busy(p, arr_d));
    }

    // v5.7 (a7): our body, tail first. At birth the body is read off the view (it is in view up to length 7); after that
    // each new head position is appended and the list is cut to our length. A jump (portal, sprint) starts it over.
    void track_body() {
        if (!F_CHOKE_LOOP) return;
        int len = std::min(c.get_length(), 64);
        if (s.hist_n == 0 || round == s.born) {
            auto body = body_tiles();
            s.hist_n = 0;
            for (int i = static_cast<int>(body.size()) - 1; i >= 0 && s.hist_n < 64; --i) s.hist[s.hist_n++] = body[i];
            return;
        }
        if (s.hist[s.hist_n - 1] != here) {
            if (dist(s.hist[s.hist_n - 1], here) != 1) {
                s.hist[0] = here;
                s.hist_n = 1;
                return;
            }
            if (s.hist_n == 64) {
                std::copy(s.hist.begin() + 1, s.hist.end(), s.hist.begin());
                --s.hist_n;
            }
            s.hist[s.hist_n++] = here;
        }
        if (s.hist_n > len) {
            int drop = s.hist_n - len;
            std::copy(s.hist.begin() + drop, s.hist.begin() + s.hist_n, s.hist.begin());
            s.hist_n = len;
        }
    }

    // v5.7 (a7): our tail sits in a 1-wide corridor as far as we remember: a child born there (facing away from our body)
    // has at most one way on. Only judged when our whole body is known and the tail tile has been seen.
    bool tail_in_corridor() const {
        if (!F_CHOKE_LOOP || s.hist_n < c.get_length() || s.hist_n < 2) return false;
        Position tail = s.hist[0], neck = s.hist[1];
        const auto &cell = s.cells[index(tail)];
        if (cell.seen < 0) return false;
        int open = 0;
        for (int e = 0; e < 4; ++e)
            if (step(tail, e) != neck && cell.edge[e] >= 0) ++open;
        return open <= 1;
    }

    // Size of the rear child of an escape split: L - 2 (the rear keeps the mass), except for a non-alpha whose tail is in a
    // 1-wide corridor. There the child would be born facing along it, often into the next pocket (slithery_fight: a
    // 4-long child walked from one pocket's tip into the other's, split there, and so on, 100+ times a game). Shed a
    // 2-long child instead: we are stuck at the tip anyway and do it again next turn, so children are born one body tile
    // further forward each time, and the ones born at a junction walk out.
    int rescue_child() const {
        int len = c.get_length();
        if (F_CHOKE_LOOP && !s.alpha && len >= 5 && tail_in_corridor()) {
            // v5.8: only when the child would really be trapped there. A farm's corridor leads out to open ground: the rear
            // L-2 walks out and harvests the refilled corridor with its own tail (shedding 2-long pieces broke that chain).
            if (F_FARM && !tail_doomed()) return len - 2;
            return 2;
        }
        return len - 2;
    }

    // v5.9b (F_SPLIT_ONCE): free ways on right now for a child born on our tail (it faces away from our neck).
    int child_exits() const {
        Position tail, neck;
        if (!tail_and_neck(tail, neck)) return 1; // body unknown: assume it can go
        int n = 0;
        for (int e = 0; e < 4; ++e) {
            if (step(tail, e) == neck) continue;
            int edge = s.cells[index(tail)].edge[e];
            if (edge < 0) continue;
            if (edge > 0) { ++n; continue; }
            auto t = c.get_tile(step(tail, e));
            if (!t || !t->get_dragon()) ++n;
        }
        return n;
    }

    // v5.9b (F_SPLIT_ONCE, trauma rounds 45, 106, 212, 276): we were born in a rescue split a moment ago and are stuck
    // already; our own rescue child would be born with no way out either. It splits in turn, and so on: 12 -> 14 -> 15 -> 16,
    // four dead dragons. One split per corridor: we die as we are, and our drop stays for the farm.
    bool resplit_doomed() const {
        if (!F_SPLIT_ONCE || s.born <= 0 || round - s.born > 2 || s.born_len < 4) return false;
        bool doomed = child_exits() == 0;
        if (doomed) DIAG("splitonce " << c.get_id() << ' ' << round << " len " << c.get_length() << " born " << s.born);
        return doomed;
    }

    // v5.7 (a7): a child born on our tail now (facing away from our body) could only walk into walls or dead ends.
    bool tail_trapped() const {
        auto body = body_tiles();
        if (static_cast<int>(body.size()) != c.get_length() || body.size() < 2) return false;
        Position tail = body.back(), neck = body[body.size() - 2];
        int back = -1;
        for (int e = 0; e < 4; ++e)
            if (step(tail, e) == neck) back = e;
        for (int e = 0; e < 4; ++e) {
            if (e == back || s.cells[index(tail)].edge[e] != 0) continue;
            int pp = 0, psz = 0, pact = 0;
            if (!sealed_pocket(step(tail, e), e, pp, psz, &pact, true)) return false;
        }
        return true;
    }

    // v5.7 (a7): stepping from here onto p (moving d) into a dead end of `size` tiles holding `gain` pearls. At its tip we
    // split L-2 and the rear child is born on our tail, facing on along the way we came. Where will that tail be? Reading
    // our own path back: if the child there can only walk into walls or another dead end, the entry starts a chain
    // (slithery_fight's corridors have a pocket at each end: the child walks into the other one, and so on for good).
    bool entry_dooms_child(Position p, int d, int size, int gain) const {
        if (!F_CHOKE_LOOP || step(here, d) != p) return false;
        int k = c.get_length() + gain - size - 1; // our tail at the tip: k steps back along our path from here
        if (k < 0) return false;                  // the whole body fits in the pocket: the child is born facing out
        std::array<Position, 40> path;
        int n = 0;
        path[n++] = here;
        for (int i = static_cast<int>(s.recent_path.size()) - 1; i >= 0 && n < 40 && n <= k + 1; --i) {
            if (s.recent_path[i] == path[n - 1]) continue;
            if (dist(s.recent_path[i], path[n - 1]) != 1) return false; // a portal jump: can't tell
            path[n++] = s.recent_path[i];
        }
        if (k >= n) return false; // further back than we remember: assume open ground
        Position tail = path[k], toward = k == 0 ? p : path[k - 1];
        int back = -1;
        for (int e = 0; e < 4; ++e)
            if (step(tail, e) == toward) back = e;
        for (int e = 0; e < 4; ++e) {
            if (e == back || s.cells[index(tail)].edge[e] != 0) continue;
            int pp = 0, psz = 0, pact = 0;
            if (!sealed_pocket(step(tail, e), e, pp, psz, &pact, true)) return false; // a way out for the child
        }
        DIAG("doomchild " << c.get_id() << ' ' << round << " len " << c.get_length() << " gain " << gain << " size " << size
                          << " tail " << tail.x << ',' << tail.y);
        return true;
    }

    // May we walk into a dead end holding `live` pearls? v5.5: only a 2-long dragon, for any pearl. v5.7 (p3.png): any
    // length, for at least CHOKE_MIN_PEARLS. At the tip the rescue split leaves a 2-long head to die there, so fewer
    // pearls than that is a loss.
    bool choke_pays(int live) const {
        // v5.7 (a7): the rear of an escape split stays out of dead ends for DEADEND_COOL rounds. Pockets that refill every
        // round (slithery_fight) otherwise pulled it straight back in: in, eat 2, split at the tip, the head dies, the rear
        // walks out and turns back in, 100+ times a game at net zero.
        // v5.8: a farm (FARM_MIN pearls or more) pays even then: in, eat, split L-2 at the tip is a net gain.
        if (F_CHOKE_LOOP && round < s.deadend_until && !(F_FARM && live >= FARM_MIN)) return false;
        if (F_CHOKE2) return live >= CHOKE_MIN_PEARLS;
        return c.get_length() == 2 && live > 0;
    }

    // v5.9d (F_HAZARD_CLOSED): entering `mouth` moving d leads into a region the walls close off: the flood (walls only,
    // loops allowed) never gets back out except through the mouth, meets no usable portal, no unknown edge or tile, and
    // stays within 48 tiles. A corridor open at its far end is not closed, however many bodies fill it now.
    bool closed_behind(Position mouth, int d) const {
        constexpr int CAP = 48;
        static std::array<Position, CAP + 4> q;
        int back = (d + 2) % 4, lo = 0, hi = 0;
        Position outside = step(mouth, back);
        bool closed = true;
        q[hi++] = mouth;
        space_seen[index(mouth)] = true;
        while (lo < hi && closed) {
            Position cur = q[lo++];
            for (int k = 0; k < 4 && closed; ++k) {
                if (cur == mouth && k == back) continue;
                int e = s.cells[index(cur)].edge[k];
                if (e == -2) { closed = false; break; }
                if (e > 0) {
                    if (!portal_occupied(e - 1)) closed = false;
                    continue;
                }
                if (e != 0) continue;
                Position nx = step(cur, k);
                if (nx == outside || s.cells[index(nx)].seen < 0) { closed = false; break; }
                if (space_seen[index(nx)]) continue;
                if (hi >= CAP) { closed = false; break; }
                space_seen[index(nx)] = true;
                q[hi++] = nx;
            }
        }
        for (int i = 0; i < hi; ++i) space_seen[index(q[i])] = false;
        return closed;
    }

    bool sealed_pocket(Position p, int arr_d, int &pearls, int &size, int *active = nullptr, bool static_only = false) const {
        if (arr_d < 0) return false;
        int back_d = (arr_d + 2) % 4;
        Position pred = step(p, back_d);
        if (!static_only && forward_escape_count(p, arr_d) == 0) return true;

        constexpr int CAP = 48;
        static std::array<Position, CAP + 4> q;
        static std::array<int, CAP + 4> depth;
        int lo = 0, hi = 0, edges2 = 0;
        q[hi] = p;
        depth[hi++] = 0;
        space_seen[index(p)] = true;

        auto finish = [&](bool trapped) {
            size = hi;
            for (int i = 0; i < hi; ++i) {
                const auto &cell = s.cells[index(q[i])];
                if (cell.pearl_round >= 0 && cell.pearl_round <= round + 4 && round - cell.seen <= 12) ++pearls;
                if (active && cell.pearl_round >= 0 && cell.pearl_round <= round && round - cell.seen <= 12) ++*active;
                space_seen[index(q[i])] = false;
            }
            return trapped;
        };

        while (lo < hi) {
            Position cur = q[lo];
            int dep = depth[lo++];
            for (int d = 0; d < 4; ++d) {
                if (cur == p && d == back_d) continue;
                int edge = s.cells[index(cur)].edge[d];
                if (edge == -2) return finish(false);
                if (edge > 0 && (!portal_occupied(edge - 1) || s.evacuating || s.evicting)) return finish(false);
                if (edge != 0) continue;
                Position nxt = step(cur, d);
                if (nxt == pred) {
                    if (dep >= 2) return finish(false);
                    continue;
                }
                if (s.cells[index(nxt)].seen < 0) return finish(false);
                if (!static_only && !walkable(nxt)) continue;
                ++edges2;
                if (space_seen[index(nxt)]) continue;
                if (hi >= CAP) return finish(false);
                space_seen[index(nxt)] = true;
                q[hi] = nxt;
                depth[hi++] = dep + 1;
            }
        }
        // edges2 counts every in-region edge twice; a tree has exactly hi - 1 edges.
        bool has_cycle = edges2 / 2 >= hi;
        return finish(!(has_cycle && hi >= c.get_length() + 2));
    }

    bool is_enemy_certain_death(Position n, int arr_d) const {
        if (enemies.empty()) return false;
        int f_exits = forward_escape_count(n, arr_d);
        if (f_exits == 0) return true;

        for (const auto &e : enemies) {
            int e_opp = (di(e.get_dir()) + 2) % 4;
            int enemy_escapes = 0;
            Position forced_tile = e.position;
            for (int ed = 0; ed < 4; ++ed) {
                if (ed == e_opp) continue;
                int edge = s.cells[index(e.position)].edge[ed];
                if (edge < 0) continue;
                if (edge == 0) {
                    Position ep = step(e.position, ed);
                    if (ep == here || empty(ep)) { ++enemy_escapes; forced_tile = ep; }
                } else ++enemy_escapes;
            }
            if (enemy_escapes == 1 && forced_tile == n) return true;

            if (f_exits == 1 && dist(e.position, n) == 1) {
                int back_d = (arr_d >= 0) ? ((arr_d + 2) % 4) : -1;
                for (int d = 0; d < 4; ++d) {
                    if (d == back_d || s.cells[index(n)].edge[d] != 0) continue;
                    Position sole = step(n, d);
                    if (empty(sole) && dist(e.position, sole) <= 1) return true;
                }
            }
        }
        return false;
    }

    int danger(Position p) const {
        if (risk_cache[index(p)] >= 0) return risk_cache[index(p)];
        int score = 0;
        for (auto e : enemies) {
            int visible_length = 0;
            bool partial = false;
            for (const auto &t : c.get_tiles())
                if (t.get_dragon() && t.get_dragon()->get_id() == e.get_id()) {
                    ++visible_length;
                    if (std::abs(delta(here.x, t.get_position().x, w)) == 3 ||
                        std::abs(delta(here.y, t.get_position().y, h)) == 3)
                        partial = true;
                }
            int est = F_ENEMY_LEN ? enemy_len_est(e.get_id(), visible_length, partial)
                                  : (partial ? std::max(visible_length, 4) : visible_length);
            int budget = std::min(6, std::max(1, est - 1));
            if (pearl_reach_on()) {
                int k = sprint_reach(e, est, p);
                if (k < INF) score += 400 - 40 * k;
                else if (dist(p, e.position) <= 3) score += 4;
                continue;
            }

            static std::array<Position, BFS_Q> q;
            int lo = 0, hi = 0;
            q[hi++] = e.position;
            danger_depth[index(e.position)] = 0;
            int reach = INF;
            while (lo < hi) {
                auto cur = q[lo++];
                int n = danger_depth[index(cur)];
                if (cur == p) { reach = n; break; }
                if (n >= budget) continue;
                for (int d = 0; d < 4; ++d)
                    if (s.cells[index(cur)].edge[d] >= 0) {
                        auto next = destination(cur, d);
                        if (!next) continue;
                        auto to = *next;
                        auto tile = c.get_tile(to);
                        if (!tile) continue;
                        if (to == p) { reach = std::min(reach, n + 1); continue; }
                        if (tile->get_dragon() || danger_depth[index(to)] >= 0 || hi >= BFS_Q) continue;
                        danger_depth[index(to)] = n + 1;
                        if (hi < BFS_Q) q[hi++] = to;
                    }
            }
            for (int i = 0; i < hi; ++i) danger_depth[index(q[i])] = -1;
            if (reach < INF) score += 400 - 40 * reach;
            else if (dist(p, e.position) <= 3) score += 4;
        }
        risk_cache[index(p)] = score;
        return score;
    }

    // v5.9b (task 7): count pearls on enemy sprints for this dragon's danger().
    bool pearl_reach_on() const {
        if (SPRINT_REACH == 2) return true;
        return SPRINT_REACH == 1 && s.alpha && round >= feed_round() - 20;
    }

    // v5.9b (task 7): the fewest steps in which enemy head e (about `len` long) reaches p in one turn. Rules (execution order):
    // the first step is free; every later one needs length >= 3 and costs a segment; a pearl eaten on a step adds one first.
    // So a 2-long dragon with a pearl in front of it can take two steps (trauma round 418: 2 long, pearl at (6,3), then into
    // our alpha's head at (5,3)). Label-correcting search over (tile, spare segments), at most 7 steps, tiles in view.
    int sprint_reach(const DragonPart &e, int len, Position p) const {
        constexpr int MAXD = 7;
        static std::array<Position, 64> cur, nxt;
        static std::array<int, 64> cur_sp, nxt_sp;
        static std::array<int, MAX_CELLS> best;
        static bool init = false;
        if (!init) { best.fill(-1); init = true; }
        static std::array<int, 256> touched;
        int n_touched = 0, n_cur = 0, reach = INF;
        cur[n_cur] = e.position;
        cur_sp[n_cur++] = len - 2; // spare segments; the first step needs none
        for (int depth = 1; depth <= MAXD && n_cur > 0 && reach == INF; ++depth) {
            int n_nxt = 0;
            for (int i = 0; i < n_cur && reach == INF; ++i) {
                int sp = cur_sp[i];
                if (depth >= 2 && sp < 1) continue;
                for (int d = 0; d < 4; ++d) {
                    if (s.cells[index(cur[i])].edge[d] < 0) continue;
                    auto next = destination(cur[i], d);
                    if (!next) continue;
                    Position to = *next;
                    auto tile = c.get_tile(to);
                    if (!tile) continue;
                    if (to == p) { reach = depth; break; }
                    if (tile->get_dragon()) continue;
                    int nsp = sp - (depth >= 2 ? 1 : 0) + (tile->has_pearl() ? 1 : 0);
                    int ti = index(to);
                    if (nsp <= best[ti] || n_nxt >= 64) continue;
                    if (best[ti] < 0 && n_touched < 256) touched[n_touched++] = ti;
                    best[ti] = nsp;
                    nxt[n_nxt] = to;
                    nxt_sp[n_nxt++] = nsp;
                }
            }
            std::copy(nxt.begin(), nxt.begin() + n_nxt, cur.begin());
            std::copy(nxt_sp.begin(), nxt_sp.begin() + n_nxt, cur_sp.begin());
            n_cur = n_nxt;
        }
        for (int i = 0; i < n_touched; ++i) best[touched[i]] = -1;
        return reach;
    }

    // v5.9b (task 7 ii): our shortest sprint onto `target` (an enemy head) within one turn, eating pearls on the way to pay
    // for extra steps. Every tile before the target must be free in view (our own body included: it moves too late to
    // help much). Depth-first over at most 6 steps. Returns the moves (empty: none) and the pearls eaten.
    std::vector<Direction> pearl_sprint(Position target, int &eaten) const {
        std::vector<Direction> best;
        std::array<int, 6> path{};
        std::array<Position, 7> pos{};
        int best_k = 7, best_eat = 0;
        pos[0] = here;
        auto rec = [&](auto &&self, int depth, int spare, int prev, int ate) -> void {
            if (depth >= best_k - 1) return;
            for (int d = 0; d < 4; ++d) {
                if (d == (prev + 2) % 4) continue;
                if (depth >= 1 && spare < 1) return;
                if (s.cells[index(pos[depth])].edge[d] != 0) continue;
                Position n = step(pos[depth], d);
                bool seen_before = false;
                for (int i = 0; i <= depth; ++i) seen_before = seen_before || pos[i] == n;
                if (seen_before) continue;
                path[depth] = d;
                if (n == target) {
                    best_k = depth + 1;
                    best_eat = ate;
                    best.clear();
                    for (int i = 0; i <= depth; ++i) best.push_back(DIRS[path[i]]);
                    continue;
                }
                auto t = c.get_tile(n);
                if (!t || t->get_dragon()) continue;
                int pearl = t->has_pearl() ? 1 : 0;
                pos[depth + 1] = n;
                self(self, depth + 1, spare - (depth >= 1 ? 1 : 0) + pearl, d, ate + pearl);
            }
        };
        rec(rec, 0, c.get_length() - 2, di(c.get_dir()), 0);
        eaten = best_eat;
        return best;
    }

    int space(Position start) const {
        static std::array<Position, BFS_Q> q;
        int lo = 0, hi = 0;
        q[hi++] = start;
        space_seen[index(start)] = true;
        while (lo < hi) {
            auto p = q[lo++];
            for (int d = 0; d < 4; ++d)
                if (s.cells[index(p)].edge[d] == 0) {
                    auto n = step(p, d);
                    if (!space_seen[index(n)] && empty(n) && hi < BFS_Q) {
                        space_seen[index(n)] = true;
                        if (hi < BFS_Q) q[hi++] = n;
                    }
                }
        }
        for (int i = 0; i < hi; ++i) space_seen[index(q[i])] = false;
        return hi;
    }

    bool incoming() const {
        for (auto e : enemies)
            if (dist(e.position, here) <= 4 &&
                dist(step(e.position, di(e.get_dir())), here) < dist(e.position, here))
                return true;
        return false;
    }

    std::vector<Direction> route(Position target) const {
        std::vector<Direction> out;
        int at = index(target);
        while (at != index(here) && at >= 0 && arrival[at] >= 0) {
            out.push_back(DIRS[arrival[at]]);
            at = predecessor[at];
        }
        std::reverse(out.begin(), out.end());
        return out;
    }

    // Moves that do not kill us this turn, whatever our preferences (residency, portal occupancy) say. A portal
    // whose far side we have never seen counts; one that drops us in a dead cell does not.
    int survival_moves() const {
        int opp = (di(c.get_dir()) + 2) % 4, n = 0;
        for (int d = 0; d < 4; ++d) {
            int e = s.cells[index(here)].edge[d];
            if (d == opp || e < 0) continue;
            if (portal_into_dead_cell(here, d)) continue;
            auto dest = destination(here, d);
            if (!dest) { n += e > 0; continue; }
            auto t = c.get_tile(*dest);
            if (!t || !t->get_dragon()) ++n;
        }
        return n;
    }

    // v5.8 (F_TRAP_AVOID): room after stepping onto n (moving d): tiles reachable from it through free tiles in view, where
    // our own body frees up from the tail (the segment k from the tail is gone after k moves). The view's edge, a portal or
    // `need` tiles count as a way out. Long dragons walking along walls and their own coils died in rescue splits.
    int escape_room(Position n, int need) const {
        auto body = body_tiles();
        int len = c.get_length();
        static std::array<int, MAX_CELLS> free_at;
        static bool init = false;
        if (!init) { free_at.fill(-1); init = true; }
        for (int i = 0; i < static_cast<int>(body.size()); ++i) free_at[index(body[i])] = len - i;
        static std::array<Position, 64> q;
        static std::array<int, 64> dep;
        int lo = 0, hi = 0, room = need;
        q[hi] = n;
        dep[hi++] = 1;
        space_seen[index(n)] = true;
        bool out = false;
        while (lo < hi && !out) {
            Position p = q[lo];
            int k = dep[lo++];
            for (int e = 0; e < 4 && !out; ++e) {
                int edge = s.cells[index(p)].edge[e];
                if (edge < 0) continue;
                if (edge > 0) { out = true; break; }
                Position r = step(p, e);
                if (space_seen[index(r)]) continue;
                auto t = c.get_tile(r);
                if (!t) { out = true; break; }
                if (auto part = t->get_dragon()) {
                    if (part->get_id() != c.get_id() || free_at[index(r)] < 0 || free_at[index(r)] > k) continue;
                }
                if (hi >= 64) { out = true; break; }
                space_seen[index(r)] = true;
                q[hi] = r;
                dep[hi++] = k + 1;
                if (hi >= need) out = true;
            }
        }
        if (!out) room = hi;
        for (int i = 0; i < hi; ++i) space_seen[index(q[i])] = false;
        for (const auto &b : body) free_at[index(b)] = -1;
        return room;
    }

    // Some segment of this dragon sits on the edge of our view, so it may be longer than what we see.
    bool body_leaves_view(int id) const {
        for (const auto &t : c.get_tiles()) {
            auto part = t.get_dragon();
            if (!part || part->get_id() != id) continue;
            if (std::abs(delta(here.x, t.get_position().x, w)) == 3 || std::abs(delta(here.y, t.get_position().y, h)) == 3)
                return true;
        }
        return false;
    }

    // A 2-long non-alpha: worth one pearl dead, so it contests pearls and trades freely.
    bool skirmisher() const {
        return F_SKIRMISH && !s.alpha && c.get_length() == 2;
    }

    bool is_step_safe(int d, bool allow_enemy_head = false) const {
        int opp_dir = (di(c.get_dir()) + 2) % 4;
        if (d == opp_dir) return false;

        int edge = s.cells[index(here)].edge[d];
        if (edge < 0) return false;

        if (edge > 0) {
            if (!s.evicting && !own_portal(edge - 1) &&
                (s.resident || (portal_occupied(edge - 1) && !s.evacuating && !roam_portal(edge - 1))))
                return false;
            if (portal_into_dead_cell(here, d)) return false;
            auto exit = destination(here, d);
            if (exit) {
                auto t = c.get_tile(*exit);
                if (t) {
                    auto dragon = t->get_dragon();
                    if (dragon) {
                        if (dragon->get_id() == c.get_id()) return false;
                        if (dragon->get_team() == c.get_team()) return false;
                        if (!dragon->is_head()) return false;
                        if (!allow_enemy_head) return false;
                    }
                }
            }
            return true;
        }

        Position n = step(here, d);
        auto t = c.get_tile(n);
        if (!t) return false;
        if (F_CHOKE && !allow_enemy_head && chokepoint_blocked(n, d)) return false;
        auto dragon = t->get_dragon();
        if (dragon) {
            if (dragon->get_id() == c.get_id()) return false;
            if (dragon->get_team() == c.get_team()) return false;
            if (!dragon->is_head()) return false;
            if (!allow_enemy_head) return false;
        }
        return true;
    }

    // The single tile an enemy head can still move to (its only non-reversing, non-blocked exit), if any.
    // Our own head counts as an exit because the enemy could ram it.
    std::optional<Position> forced_exit(const DragonPart &e) const {
        int e_opp = (di(e.get_dir()) + 2) % 4;
        std::optional<Position> forced;
        int escapes = 0;
        for (int ed = 0; ed < 4; ++ed) {
            if (ed == e_opp) continue;
            int edge = s.cells[index(e.position)].edge[ed];
            if (edge < 0) continue;
            if (edge > 0) return {};
            Position ep = step(e.position, ed);
            if (s.cells[index(ep)].seen < round) return {};
            if (ep == here || empty(ep)) { ++escapes; forced = ep; }
        }
        if (escapes != 1) return {};
        return forced;
    }

    // Our visible body from head to tail: each segment faces the one in front of it.
    std::vector<Position> body_tiles() const {
        std::vector<Position> body{here};
        Position p = here;
        for (int i = 1; i < c.get_length(); ++i) {
            bool found = false;
            for (int d = 0; d < 4 && !found; ++d) {
                Position q = step(p, d);
                auto tq = c.get_tile(q);
                if (!tq || !tq->get_dragon() || tq->get_dragon()->get_id() != c.get_id()) continue;
                if (tq->get_dragon()->is_head()) continue;
                if (step(q, di(tq->get_dragon()->get_dir())) != p) continue;
                if (std::find(body.begin(), body.end(), q) != body.end()) continue;
                body.push_back(q);
                p = q;
                found = true;
            }
            if (!found) break;
        }
        return body;
    }

    // True while part of our body is still on the other side of a portal: the neck came through a portal
    // edge, a visible segment faces across one, or our own count of segments left behind is not used up.
    bool straddling() const {
        if (!F_STRADDLE) return false;
        if (s.straddle_left > 0) return true;
        if (s.cells[index(here)].edge[(di(c.get_dir()) + 2) % 4] > 0) return true;
        for (const auto &t : c.get_tiles()) {
            auto part = t.get_dragon();
            if (!part || part->get_id() != c.get_id() || part->is_head()) continue;
            if (s.cells[index(t.get_position())].edge[di(part->get_dir())] > 0) return true;
        }
        return false;
    }

    // Every split goes through here: legal for the engine, and never with the body across a portal.
    // v5.8 (b1.png): except a 2-long child. It is born on our tail, on the side of the portal we came from, facing away from
    // it: ground we just walked. On the portals map dragons cross a portal every few rounds, so a length-7 dragon went
    // dozens of rounds without a split.
    bool can_split_safe(int child) const {
        return c.can_split(child) && (!straddling() || (F_STRADDLE_SPLIT && child == 2));
    }

    // A split child is born on our tail facing away from us; refuse splits that spawn it into a pocket.
    bool child_viable() const {
        auto body = body_tiles();
        if (static_cast<int>(body.size()) != c.get_length() || body.size() < 2) return true;
        Position tail = body.back(), neck = body[body.size() - 2];
        int facing = -1;
        for (int d = 0; d < 4; ++d)
            if (step(neck, d) == tail) facing = d;
        if (facing < 0) return true;
        return !is_dead_end_trap(tail, facing);
    }

    // Pearls behind our tail that the head cannot turn back for (a 1-wide corridor refills the tiles our tail leaves):
    // split a 2-long child off the tail. It is born on the tail facing away from us, i.e. straight at them, and moves
    // later this same round. Worth it when the child should gain at least its own two segments.
    std::optional<Action> back_harvest() {
        if (!F_BACK_HARVEST || round <= s.born || round - s.harvest_round < HARVEST_COOLDOWN) return {};
        // v5.7: never while walking out after an escape split. Over the unit cap it is still worth its 2-long child.
        if (F_SPLIT_COOL && s.born > 0 && round - s.born < SPLIT_COOL && s.born_len >= 4) return {};
        int len = c.get_length();
        if (len < 4 || !can_split_safe(2)) return {};
        if (s.alpha && (len < (F_TAIL_BFS ? 8 : 10) || round >= feed_round() - 20)) return {}; // alphas: early, when long
        for (const auto &e : enemies)
            if (dist(e.position, here) <= 4) return {};
        if (F_TAIL_BFS)
            if (auto a = tail_bfs_harvest()) return a;
        if (round - s.sprint_round <= len + 1) return {};
        // Our body is the last `len` distinct head positions (no sprint since); older ones are where the tail has been.
        std::array<Position, 40> seq;
        int n = 0;
        for (int i = static_cast<int>(s.recent_path.size()) - 1; i >= 0 && n < len + 4 && n < 40; --i) {
            if (n > 0 && s.recent_path[i] == seq[n - 1]) continue;
            if (n > 0 && dist(s.recent_path[i], seq[n - 1]) != 1) break; // a portal jump: stop there
            seq[n++] = s.recent_path[i];
        }
        if (n < len + 1) return {};
        Position tail = seq[len - 1];
        if (auto tt = c.get_tile(tail)) {
            auto part = tt->get_dragon();
            if (!part || part->get_id() != c.get_id()) return {}; // our picture of the body is off
        }
        int gain = 0, need = s.alpha ? 3 : 2;
        for (int i = len; i < n && i < len + 4; ++i) {
            Position b = seq[i];
            auto tb = c.get_tile(b);
            if (tb) {
                if (tb->get_dragon()) break;
                // A pearl the head can reach about as soon is the head's, not the child's.
                if (tb->has_pearl() && distance[index(b)] > (i - len + 1) + 3) ++gain;
            } else if (s.cells[index(b)].fast_obs >= 3) {
                ++gain; // it refills every round and our tail left it at least a round ago
            }
        }
        // v5.7: pearls beside the tail too (within 2 tiles, off the path the tail came along), that the head cannot get
        // to much sooner than a child born on the tail could. Turning the head round would cost the whole body length.
        if (F_TAIL_DROP) {
            for (const auto &t : c.get_tiles()) {
                if (!t.has_pearl() || t.get_dragon()) continue;
                Position b = t.get_position();
                int k = dist(tail, b);
                if (k < 1 || k > 2) continue;
                bool on_path = false;
                for (int i = len; i < n && i < len + 4 && !on_path; ++i) on_path = seq[i] == b;
                if (on_path) continue;
                if (distance[index(b)] >= INF || distance[index(b)] > k + len / 2 + 2) ++gain;
            }
        }
        if (F_SPLIT_CAP && !s.alpha && over_cap()) need = std::max(need, 2);
        if (gain < need) return {};
        s.harvest_round = round;
        DIAG("harvest " << c.get_id() << ' ' << round << " len " << len << " gain " << gain << " alpha " << s.alpha
                        << " tail " << tail.x << ',' << tail.y);
        return Action{{}, 2, Mode::Split, false};
    }

    // v5.7 (a4): the tail read off the visible body (not rebuilt from our path, which a sprint or a portal breaks), and
    // pearls a 2-long child born there can walk to (up to 6 steps, through free tiles in view) well before the head
    // could. The child is born on the tail facing away from the body, so it turns toward them at once.
    std::optional<Action> tail_bfs_harvest() {
        auto body = body_tiles();
        int len = c.get_length();
        if (static_cast<int>(body.size()) != len) return {};
        Position tail = body.back();
        static std::array<Position, 64> q;
        static std::array<int, 64> dep;
        int lo = 0, hi = 0, gain = 0;
        q[hi] = tail;
        dep[hi++] = 0;
        space_seen[index(tail)] = true;
        while (lo < hi) {
            Position p = q[lo];
            int k = dep[lo++];
            if (k > 0) {
                auto t = c.get_tile(p);
                // A pearl the head reaches about as soon is the head's (it may be on its way there already).
                if (t && t->has_pearl() && (distance[index(p)] >= INF || distance[index(p)] > k + 2)) ++gain;
            }
            if (k >= 6) continue;
            for (int d = 0; d < 4; ++d) {
                if (s.cells[index(p)].edge[d] != 0) continue;
                Position n = step(p, d);
                auto t = c.get_tile(n);
                if (!t || t->get_dragon() || space_seen[index(n)] || hi >= 64) continue;
                space_seen[index(n)] = true;
                q[hi] = n;
                dep[hi++] = k + 1;
            }
        }
        for (int i = 0; i < hi; ++i) space_seen[index(q[i])] = false;
        int need = s.alpha ? 3 : 2;
        if (gain < need) return {};
        s.harvest_round = round;
        DIAG("tailbfs " << c.get_id() << ' ' << round << " len " << len << " gain " << gain << " alpha " << s.alpha
                        << " tail " << tail.x << ',' << tail.y);
        return Action{{}, 2, Mode::Split, false};
    }

    // v5.8 (F_PATCH_CAMP): a tile we know spawns pearls.
    bool spawner(Position p) const {
        const auto &cell = s.cells[index(p)];
        return cell.seen >= 0 && cell.spawn_at >= 0;
    }

    // The pearl patch `start` lies in: spawners connected through open edges, 4 to PATCH_MAX of them, with at most
    // PATCH_EXITS ways out (open edges to other tiles, unknown ones and portals count). A room like schooltime's, a trophy
    // handle, a small chamber. Its tiles are left in remembered_q[0, n); `due`: rounds until the next pearl in it.
    bool patch_at(Position start, int &n, int &exits, int &due, Position &center) const {
        n = exits = 0;
        due = INF;
        if (!spawner(start)) return false;
        int lo = 0, hi = 0;
        bool big = false;
        remembered_q[hi++] = start;
        remembered_first[index(start)] = 0;
        while (lo < hi && !big) {
            Position p = remembered_q[lo++];
            for (int d = 0; d < 4; ++d) {
                int e = s.cells[index(p)].edge[d];
                if (e == -1) continue;
                if (e != 0) { ++exits; continue; }
                Position q = step(p, d);
                if (!spawner(q)) { ++exits; continue; }
                if (remembered_first[index(q)] >= 0) continue;
                if (hi >= PATCH_MAX) { big = true; break; }
                remembered_first[index(q)] = 0;
                remembered_q[hi++] = q;
            }
        }
        int sx = 0, sy = 0;
        for (int i = 0; i < hi; ++i) {
            Position p = remembered_q[i];
            remembered_first[index(p)] = -1;
            sx += delta(start.x, p.x, w);
            sy += delta(start.y, p.y, h);
            const auto &cell = s.cells[index(p)];
            due = std::min(due, std::max(0, cell.spawn_at - round));
        }
        n = hi;
        center = wrap({start.x + sx / std::max(1, hi), start.y + sy / std::max(1, hi)});
        return !big && hi >= 4 && exits <= PATCH_EXITS;
    }

    bool in_patch_tiles(Position p) const {
        for (int i = 0; i < s.n_patch_tiles; ++i)
            if (s.patch_tiles[i] == p) return true;
        return false;
    }

    bool patch_held(Position p) const {
        return F_PATCH_CAMP && s.cells[index(p)].held_until > round && !s.patch_camp;
    }

    // v5.8 (F_PATCH_CAMP, b5.png): one dragon per patch. Whoever was in it first keeps it (two that came in the same round,
    // or that shared it for 10 rounds without seeing each other come in: the lower ID); the others leave and keep out of
    // it for PATCH_HOLD rounds. The one that keeps it stays while a pearl is due within PATCH_DUE rounds.
    void patch_camp_update() {
        s.patch_camp = false;
        if (!F_PATCH_CAMP || s.alpha || round >= feed_round() || s.camping || s.resident) return;
        int n = 0, exits = 0, due = INF;
        Position center;
        if (!patch_at(here, n, exits, due, center)) return;
        bool fresh = round - s.patch_round > 3 || dist(center, s.patch_center) > 3;
        auto inside = [&](Position p) {
            for (int i = 0; i < n; ++i)
                if (remembered_q[i] == p) return true;
            return false;
        };
        if (fresh) {
            s.patch_enter = round;
            s.patch_crowded_on_entry = false;
            s.patch_mates.fill({-1, -1000});
        }
        s.patch_center = center;
        s.patch_round = round;
        bool yield = false;
        for (const auto &f : friends) {
            if (is_alpha(f.get_id()) || !inside(f.position)) continue;
            if (fresh && round > s.born) s.patch_crowded_on_entry = true;
            std::pair<int, int> *slot = nullptr, *free_slot = nullptr;
            for (auto &m : s.patch_mates) {
                if (m.first == f.get_id()) slot = &m;
                if (m.first < 0 && !free_slot) free_slot = &m;
            }
            if (!slot && free_slot) { *free_slot = {f.get_id(), round}; slot = free_slot; }
            bool together_long = slot && round - slot->second >= 10;
            if ((fresh && round > s.born && f.get_id() < c.get_id()) || (together_long && f.get_id() < c.get_id())) yield = true;
        }
        if (s.patch_crowded_on_entry) yield = true;
        if (yield) {
            for (int i = 0; i < n; ++i) s.cells[index(remembered_q[i])].held_until = round + PATCH_HOLD;
            DIAG("patchyield " << c.get_id() << ' ' << round << ' ' << center.x << ',' << center.y << " n " << n);
            s.n_patch_tiles = 0;
            return;
        }
        if (due > PATCH_DUE) return;
        s.patch_camp = true;
        s.n_patch_tiles = std::min(n, 40);
        for (int i = 0; i < s.n_patch_tiles; ++i) s.patch_tiles[i] = remembered_q[i];
        DIAG("patchcamp " << c.get_id() << ' ' << round << ' ' << center.x << ',' << center.y << " n " << n << " exits " << exits
                          << " due " << due);
    }

    // v5.8 (F_FARM): the region behind entering p moving d, judged on the map alone as sealed_pocket(static_only) does:
    // true if it is a dead end we cannot turn round in. Its tiles are left in farm_q[0, n).
    bool dead_end_region(Position p, int d, int &n, bool unseen_closed = false) const {
        n = 0;
        if (d < 0) return false;
        int back = (d + 2) % 4, lo = 0, hi = 0, edges2 = 0;
        Position pred = step(p, back);
        bool open = false;
        farm_q[hi] = p;
        farm_dep[hi++] = 0;
        space_seen[index(p)] = true;
        while (lo < hi && !open) {
            Position cur = farm_q[lo];
            int dep = farm_dep[lo++];
            if (dep >= FARM_CAP) continue;
            for (int k = 0; k < 4 && !open; ++k) {
                if (cur == p && k == back) continue;
                int e = s.cells[index(cur)].edge[k];
                if (e == -2 || e > 0) { open = true; break; } // unknown edge or a portal: a way on
                if (e != 0) continue;
                Position nx = step(cur, k);
                if (nx == pred) {
                    if (dep >= 2) open = true;
                    continue;
                }
                bool unseen = s.cells[index(nx)].seen < 0;
                if (unseen && !unseen_closed) { open = true; break; }
                ++edges2;
                if (space_seen[index(nx)]) continue;
                if (hi >= FARM_CAP) { open = true; break; }
                if (unseen) { // part of the pocket as far as we know; its own edges are not (unseen_closed: a known farm)
                    space_seen[index(nx)] = true;
                    farm_q[hi] = nx;
                    farm_dep[hi++] = FARM_CAP;
                    continue;
                }
                space_seen[index(nx)] = true;
                farm_q[hi] = nx;
                farm_dep[hi++] = dep + 1;
            }
        }
        for (int i = 0; i < hi; ++i) space_seen[index(farm_q[i])] = false;
        n = hi;
        if (open) return false;
        return !(edges2 / 2 >= hi && hi >= c.get_length() + 2);
    }

    // A tile that spawns every round or so (its countdown was at most 1 on two sightings running).
    bool fast_tile(Position p) const {
        const auto &cell = s.cells[index(p)];
        return cell.fast_obs >= 2 || (F_QUICK_FARM && cell.quick_obs >= 3);
    }

    // Over farm_q[0, n): fast spawners, pearls we expect there now (seen lying there, or a fast spawner we have not seen
    // covered since), and dragon segments in view other than ours.
    void farm_stats(int n, int &fast, int &live, int &busy) const {
        fast = live = busy = 0;
        for (int i = 0; i < n; ++i) {
            Position p = farm_q[i];
            const auto &cell = s.cells[index(p)];
            bool f = cell.fast_obs >= 2;
            fast += f;
            if (auto t = c.get_tile(p)) {
                auto part = t->get_dragon();
                if (part && part->get_id() != c.get_id()) ++busy;
                if (t->has_pearl() && !part) ++live;
            } else if (cell.pearl_round >= 0 && cell.pearl_round <= round && round - cell.seen <= 12) {
                ++live;
            } else if (f && round > cell.seen) {
                ++live; // refills every round, and it is out of view: whatever covered it has moved on
            }
        }
    }

    // A known farm near p (the same dead end seen from another tile), or -1.
    int farm_near(Position p, int radius = 4) const {
        int best = -1, best_d = radius + 1;
        for (int i = 0; i < MAX_FARMS; ++i) {
            int k = dist(s.farms[i].p, p);
            if (s.farms[i].value > 0 && round - s.farms[i].seen <= FARM_TTL && k < best_d) { best_d = k; best = i; }
        }
        return best;
    }

    int note_farm(Position p, int dir, int value, int seen, bool spec, int claim_round = -1000, int claim_id = -1) {
        int k = farm_near(p);
        if (k >= 0) {
            auto &f = s.farms[k];
            if (f.spec && !spec && seen > -1000) { f.p = p; f.dir = dir; f.spec = false; } // confirmed
            f.value = std::max(f.value, value);
            f.seen = std::max(f.seen, seen);
            if (claim_round > f.claim_round) { f.claim_round = claim_round; f.claim_id = claim_id; }
            return -1;
        }
        if (seen <= -1000) return -1; // a claim on a farm we have not heard of
        int slot = 0;
        for (int i = 0; i < MAX_FARMS; ++i) {
            if (i == s.farm_goal) continue;
            if (slot == s.farm_goal || s.farms[i].seen < s.farms[slot].seen) slot = i;
        }
        if (slot == s.farm_goal) return -1;
        s.farms[slot] = {p, dir, value, seen, spec, claim_round, claim_id};
        return slot;
    }

    // Fast spawners in view that lie in a dead end of the map with at least FARM_MIN of them: a farm. Worth it because a
    // dive eats every fast tile (they refill each round) and costs 2 (the head left at the tip when we split L-2 there).
    void find_farms() {
        if (!F_FARM) return;
        // Working a farm (at it or in it): hold the claim, so seekers go to the other one.
        int k = farm_near(here);
        if (k >= 0 && dist(here, s.farms[k].p) <= 4 && round - s.farms[k].claim_round >= 8) {
            s.farms[k].claim_round = round;
            s.farms[k].claim_id = c.get_id() & 4095;
            s.farm_claim_send = round;
            s.farm_claim_slot = k;
        }
        // A guessed mirror farm in view that is not there (its tile spawns slowly or never): forget it.
        for (auto &f : s.farms)
            if (f.spec && f.value > 0)
                if (auto t = c.get_tile(f.p); t && (t->get_pearl_time() > 1 || (t->get_pearl_time() < 0 && !t->has_pearl()))) {
                    DIAG("farmfalse " << c.get_id() << ' ' << round << ' ' << f.p.x << ',' << f.p.y);
                    f.value = 0;
                }
        if ((round + c.get_id()) % 2) return;
        for (const auto &t : c.get_tiles()) {
            Position q = t.get_position();
            if (!fast_tile(q)) continue;
            if (int kk = farm_near(q); kk >= 0 && !s.farms[kk].spec) continue;
            for (int d = 0; d < 4; ++d) {
                Position a = step(q, (d + 2) % 4);
                if (s.cells[index(a)].edge[d] != 0) continue;
                int n = 0;
                if (!dead_end_region(q, d, n)) continue;
                int fast = 0, live = 0, busy = 0;
                farm_stats(n, fast, live, busy);
                if (fast < FARM_MIN) continue;
                int slot = note_farm(q, d, fast, round, false);
                if (slot >= 0) {
                    s.farm_new = slot;
                    s.farm_sent = round;
                    DIAG("farmfound " << c.get_id() << ' ' << round << ' ' << q.x << ',' << q.y << " fast " << fast << " size "
                                      << n << " sym " << s.sym);
                }
                // Its mirror image: under the symmetry we know, or (a guess, checked on arrival) under every candidate
                // not yet ruled out.
                for (int k = 0; k < 4; ++k) {
                    if (s.sym >= 0 ? k != s.sym : (!sym_possible(k) || s.sym_bad[k])) continue;
                    Position m = mirror(q, k);
                    if (chebyshev(m, q) > 4) note_farm(m, mirror_dir(d, k), fast, round, s.sym < 0);
                }
                break;
            }
        }
    }

    std::uint64_t farm_packet(const Farm &f, bool claimed) const {
        int bits = (std::max(0, f.dir) & 3) | (std::clamp(f.value, 0, 15) << 2) | (f.spec ? 64 : 0) | (claimed ? 128 : 0);
        return alpha_packet64(FARM_TAG, f.p, claimed ? f.claim_round : f.seen, bits);
    }

    // Our tail and the tile in front of it (the neck), from the view when the whole body is in it, else from the body we
    // have tracked since birth.
    bool tail_and_neck(Position &tail, Position &neck) const {
        int len = c.get_length();
        auto body = body_tiles();
        if (static_cast<int>(body.size()) == len && len >= 2) {
            tail = body.back();
            neck = body[len - 2];
            return true;
        }
        if (F_CHOKE_LOOP && s.hist_n == len && len >= 2) {
            tail = s.hist[0];
            neck = s.hist[1];
            return dist(tail, neck) == 1;
        }
        return false;
    }

    // v5.8 (F_FARM, b3.png): walking out of a farm, our tail leaves tiles that refill at once. Split a 2-long child off the
    // tail: it is born facing away from us, straight back in, eats them all, and splits L-2 at the tip; its rear comes out
    // and does the same. Wait until the tail is off the spawners (the child then gets every one of them).
    std::optional<Action> farm_harvest() {
        if (!F_FARM || round <= s.born || round - s.farm_harvest_round < 3) return {};
        int len = c.get_length();
        if (len < 4 || !can_split_safe(2)) return {};
        if (s.alpha && round >= feed_round() - 20) return {};
        Position tail, neck;
        if (!tail_and_neck(tail, neck)) return {};
        if (fast_tile(tail)) return {};
        for (const auto &e : enemies)
            if (dist(e.position, tail) <= 3) return {};
        for (int e = 0; e < 4; ++e) {
            if (step(tail, e) == neck || s.cells[index(tail)].edge[e] != 0) continue;
            Position p = step(tail, e);
            if (s.cells[index(p)].seen < 0) continue;
            int n = 0;
            // Part of it never seen (a rear child born at a farm's tip saw only the mouth end of it: our body covered the
            // rest). A farm we know of inside it counts as refilled; otherwise, with two fast spawners seen at its mouth end,
            // the few tiles we never saw are taken to refill as they do.
            bool partial = false;
            if (!dead_end_region(p, e, n)) {
                if (!dead_end_region(p, e, n, true)) continue;
                partial = true;
            }
            int fast = 0, live = 0, busy = 0, unseen = 0, known = -1;
            farm_stats(n, fast, live, busy);
            for (int i = 0; i < n; ++i) {
                unseen += s.cells[index(farm_q[i])].seen < 0;
                if (known < 0) {
                    int k = farm_near(farm_q[i], 0);
                    if (k >= 0) known = k;
                }
            }
            if (partial) {
                if (known >= 0) {
                    live = std::max(live, s.farms[known].value - 1);
                    fast = std::max(fast, s.farms[known].value);
                } else if (fast >= 2 && unseen <= 6) {
                    live += unseen;
                    fast += unseen;
                } else {
                    continue;
                }
            }
            if (busy > 0 || live < FARM_MIN || live < fast - 1) continue;
            s.farm_harvest_round = round;
            DIAG("farmharvest " << c.get_id() << ' ' << round << " len " << len << " live " << live << " fast " << fast << " partial "
                                << partial
                                << " tail " << tail.x << ',' << tail.y << " alpha " << s.alpha);
            return Action{{}, 2, Mode::Split, false};
        }
        return {};
    }

    // v5.8 (F_FARM): the rear child of a split now would be born on our tail with nowhere to go but walls and dead ends.
    bool tail_doomed() const {
        Position tail, neck;
        if (!tail_and_neck(tail, neck)) return false;
        for (int e = 0; e < 4; ++e) {
            if (step(tail, e) == neck) continue;
            int edge = s.cells[index(tail)].edge[e];
            if (edge < 0) continue;
            if (edge > 0) return false;
            int n = 0;
            if (!dead_end_region(step(tail, e), e, n)) return false;
        }
        return true;
    }

    // v5.8 (F_FARM): a dead end with a teammate or an enemy in it (as far as we can see) is not ours to enter: whoever is
    // inside splits at the tip and its rear child walks out the way we would be coming in.
    bool pocket_busy(Position p, int d) const {
        if (!F_FARM) return false;
        int n = 0;
        if (!dead_end_region(p, d, n)) return false;
        for (int i = 0; i < n; ++i)
            if (auto t = c.get_tile(farm_q[i]))
                if (auto part = t->get_dragon(); part && part->get_id() != c.get_id()) return true;
        return false;
    }

    // v5.9b (F_MOUTH_CLEAR, trauma): n lies just outside the mouth of a dead end with a teammate in it. Its rescue split at the
    // tip puts a child on its tail, at the mouth, facing out: a head on n boxes that child in (trauma's x = 11 corridor, five
    // times in one game, each time with a teammate on (12, 8)).
    bool busy_mouth(Position n) const {
        if (!F_MOUTH_CLEAR) return false;
        for (int e = 0; e < 4; ++e) {
            if (s.cells[index(n)].edge[e] != 0) continue;
            Position m = step(n, e);
            if (!c.get_tile(m)) continue;
            int cnt = 0;
            if (!dead_end_region(m, e, cnt)) continue;
            for (int i = 0; i < cnt; ++i)
                if (auto t = c.get_tile(farm_q[i]))
                    if (auto part = t->get_dragon();
                        part && part->get_team() == c.get_team() && part->get_id() != c.get_id())
                        return true;
        }
        return false;
    }

    // v5.8 (F_FARM): the way in and out of a farm a teammate is working (it claimed it within 10 rounds, or it is in view in
    // there): not a place to stand. Its rear child walks out that way, and a head in the way traps it.
    bool farm_lane(Position n) const {
        if (!F_FARM) return false;
        for (const auto &f : s.farms) {
            if (f.value < FARM_MIN || f.spec || round - f.seen > FARM_TTL || f.dir < 0) continue;
            if (round - f.claim_round > 10 || f.claim_id == (c.get_id() & 4095)) continue;
            if (dist(n, farm_approach(f)) <= 2) return true;
        }
        return false;
    }

    // v5.8 (F_FARM_SEEK): nothing to do: walk to a farm nobody else has set off for (ours, or the mirror image on the far
    // side of the map; a guessed mirror only when no real one is free). We aim at the tile in front of its entrance and
    // stop once the entrance is reachable in view (the dead-end rules take over there) or time runs out.
    Position farm_approach(const Farm &f) const {
        return f.dir >= 0 ? step(f.p, (f.dir + 2) % 4) : f.p;
    }

    std::optional<Position> farm_goal_target() {
        if (!F_FARM || !F_FARM_SEEK) return {};
        if (s.farm_goal >= 0) {
            const auto &f = s.farms[s.farm_goal];
            Position a = farm_approach(f);
            bool there = here == a || here == f.p || (distance[index(f.p)] < INF && distance[index(f.p)] <= 4);
            if (round > s.farm_goal_until || f.value == 0 || there) {
                DIAG("farmdone " << c.get_id() << ' ' << round << ' ' << f.p.x << ',' << f.p.y << " k " << dist(here, f.p)
                                 << " there " << there << " v " << f.value);
                s.farm_goal = -1;
                return {};
            }
            return a;
        }
        int best = -1;
        double best_key = 1e18;
        for (int i = 0; i < MAX_FARMS; ++i) {
            const auto &f = s.farms[i];
            if (f.value < FARM_MIN || round - f.seen > FARM_TTL) continue;
            if (round - f.claim_round <= FARM_CLAIM_TTL && f.claim_id != (c.get_id() & 4095)) continue;
            Position a = farm_approach(f);
            int k = dist(here, a);
            if (k < 2 || k > (w + h) / 2) continue;
            if (distance[index(f.p)] < INF && distance[index(f.p)] <= 4) continue; // already here
            bool nearer = false;
            for (const auto &fr : friends) nearer = nearer || dist(fr.position, a) < k;
            if (nearer) continue;
            double key = k - 2.0 * f.value + (f.spec ? 30 : 0);
            if (key < best_key) { best_key = key; best = i; }
        }
        if (best < 0) return {};
        auto &f = s.farms[best];
        f.claim_round = round;
        f.claim_id = c.get_id() & 4095;
        int k = dist(here, farm_approach(f));
        s.farm_goal = best;
        s.farm_goal_until = round + 2 * k + 12;
        s.farm_claim_send = round;
        s.farm_claim_slot = best;
        DIAG("farmgo " << c.get_id() << ' ' << round << ' ' << f.p.x << ',' << f.p.y << " dist " << k << " v " << f.value
                       << " spec " << f.spec);
        return farm_approach(f);
    }

    // v5.6: our head is in a dead end of the map itself (the region ahead is sealed and has no loop we fit round), with
    // `gain` pearls lying in it or due within a few rounds.
    bool in_dead_end(int &gain) const {
        int size = 0, active = 0;
        gain = 0;
        if (!sealed_pocket(here, di(c.get_dir()), gain, size, &active, true)) return false;
        if (enclosure_at(here).small) { gain = 0; return false; } // a portal chamber: the camping rules own it
        return true;
    }

    // The step to take inside a dead end: onto a pearl, else toward the nearest pearl ahead (or one that will have
    // spawned by the time we arrive), else into the most room. `found`: some pearl ahead is reachable at all (in a
    // tree-shaped pocket the branches behind our neck are not).
    int choke_step(bool &found) const {
        int best = -1;
        found = false;
        double best_sc = -1e18;
        for (int d = 0; d < 4; ++d) {
            if (s.cells[index(here)].edge[d] != 0 || !is_step_safe(d)) continue;
            Position n = step(here, d);
            auto tn = c.get_tile(n);
            double sc = 0.01 * std::min(space(n), 40);
            if (tn && tn->has_pearl()) sc += 100;
            int near = INF;
            for (const auto &t : c.get_tiles()) {
                Position p = t.get_position();
                int k = distance[index(p)];
                if (k >= INF || first[index(p)] != d || t.get_dragon()) continue;
                if (t.has_pearl() || (t.get_pearl_time() >= 0 && t.get_pearl_time() <= k)) near = std::min(near, k);
            }
            if (near < INF) {
                sc += 50 - near;
                found = true;
            }
            if (sc > best_sc) { best_sc = sc; best = d; }
        }
        return best;
    }

    // The last `n` distinct positions our head occupied, i.e. the front `n` segments of our body (no sprint since).
    bool on_own_body(Position p, int n) const {
        Position prev{-1, -1};
        for (int i = static_cast<int>(s.recent_path.size()) - 1; i >= 0 && n > 0; --i) {
            if (s.recent_path[i] == prev) continue;
            prev = s.recent_path[i];
            if (prev == p) return true;
            --n;
        }
        return false;
    }

    // First step of the shortest way back into the chamber we were pushed out of: a crossing of its portal (from
    // either side of the partner edge) that lands on a chamber tile our body will have left by then. -1: none in view.
    int reentry_move() const {
        int best = -1, best_cost = INF, opp = (di(c.get_dir()) + 2) % 4;
        for (const auto &t : c.get_tiles())
            for (int d = 0; d < 4; ++d) {
                Position p = t.get_position();
                if (s.cells[index(p)].edge[d] != s.reclaim_portal + 1) continue;
                if (p == here ? d == opp : (distance[index(p)] >= INF || t.get_dragon())) continue;
                auto dest = destination(p, d);
                if (!dest) continue;
                bool inside = s.n_camp_tiles == 0;
                for (int i = 0; i < s.n_camp_tiles && !inside; ++i) inside = s.camp_tiles[i] == *dest;
                int k = p == here ? 0 : distance[index(p)];
                if (!inside || on_own_body(*dest, c.get_length() - k + 1)) continue;
                if (k + 1 < best_cost) {
                    best_cost = k + 1;
                    best = p == here ? d : first[index(p)];
                }
            }
        if (best >= 0 && !is_step_safe(best)) return -1;
        return best;
    }

    // Zero-loss kill: sprint through an enemy's only exit so our neck, not our head, ends on it.
    // The enemy then has nowhere to go and dies on our body; we pay one segment per extra step.
    std::optional<Action> neck_block() const {
        int my_len = c.get_length();
        if (my_len < 3) return {};
        int opp_dir = (di(c.get_dir()) + 2) % 4;
        // Our body tiles, tail last, so we can tell which ones the sprint frees up.
        auto body = body_tiles();
        std::optional<Action> best;
        int best_len = -1;
        for (const auto &e : enemies) {
            auto f = forced_exit(e);
            if (!f || *f == here) continue;
            int k = distance[index(*f)];
            if (k < 1 || k > 2 || k + 1 > my_len - 1) continue;
            auto path = route(*f);
            if (static_cast<int>(path.size()) != k || di(path[0]) == opp_dir) continue;
            if (k == 2 && di(path[1]) == (di(path[0]) + 2) % 4) continue;
            int last = di(path.back());
            for (int d = 0; d < 4; ++d) {
                if (d == (last + 2) % 4 || s.cells[index(*f)].edge[d] != 0) continue;
                Position g = step(*f, d);
                if (g == e.position || !empty(g)) continue;
                if (forward_escape_count(g, d) == 0 || is_dead_end_trap(g, d)) continue;
                // Tail tiles freed by the sprint must not hand the enemy a new exit.
                int steps = k + 1, freed = std::min(static_cast<int>(body.size()), 2 * steps - 1);
                bool leaks = false;
                for (int i = 0; i < freed; ++i) {
                    Position t = body[body.size() - 1 - i];
                    if (dist(t, e.position) == 1 && t != *f) leaks = true;
                }
                if (leaks) continue;
                int elen = enemy_visible_length(e.get_id());
                if (elen > best_len) {
                    auto moves = path;
                    moves.push_back(DIRS[d]);
                    best = Action{moves, 0, Mode::Block, false};
                    best_len = elen;
                }
                break;
            }
        }
        return best;
    }

    // v5.8 (F_EARLY_KILL): a teammate head within EARLY_KILL_FRIEND of `at` (it eats what the collision drops).
    bool friend_near(Position at) const {
        for (const auto &f : friends)
            if ((F_TRUE_MOVES ? moves_of(f, at) : dist(f.position, at)) <= EARLY_KILL_FRIEND) return true;
        return false;
    }

    // `early` (F_EARLY_KILL, any round): only trades on an enemy at least as long as us, with a teammate near the collision.
    std::optional<Action> guaranteed_kill(bool high_value_only = false, bool safe_only = false, bool early = false) const {
        if (s.alpha) return {};
        if (c.get_unit_count() <= 1) return {};
        if (iso && ISO_STRIKES == 0) return {}; // v5.8: alone among enemies, every one of these trades feeds them

        int my_len = c.get_length();
        int opp_dir = (di(c.get_dir()) + 2) % 4;

        auto trade_ok = [&](int elen, int eid) -> bool {
            if (iso && ISO_STRIKES == 1) return elen >= my_len;
            if (eid <= 1 || elen >= 4) return true;
            if (high_value_only) return false;
            if (elen >= my_len) return true;
            if (my_len <= 2) return true;
            return false;
        };
        // Any dragon happily trades itself for an enemy clearly longer than itself.
        auto value_trade = [&](int elen) { return elen >= my_len + 2 && c.get_unit_count() >= 3; };

        // v5.9 (F_EARLY_KILL2): an enemy 1-2 shorter is worth it too, but only with a teammate head right beside the collision
        // (within EARLY_KILL2_FRIEND) and no other enemy head within 3 of it: the teammate eats both drops before anyone else.
        auto strict_friend = [&](Position at, int victim) {
            bool f = false;
            for (const auto &fr : friends)
                f = f || (F_TRUE_MOVES ? moves_of(fr, at) : dist(fr.position, at)) <= EARLY_KILL2_FRIEND;
            if (!f) return false;
            for (const auto &e : enemies)
                if (e.get_id() != victim && (F_TRUE_MOVES ? moves_of(e, at) : dist(e.position, at)) <= 3) return false;
            return true;
        };
        int early_victim = -1;
        auto early_ok = [&](int elen, Position at) {
            if (elen >= my_len && friend_near(at)) return true;
            return F_EARLY_KILL2 && elen >= 2 && elen >= my_len - EARLY_KILL2_GAP && strict_friend(at, early_victim);
        };
        // 1. Immediate 1-step collision (Kamikaze takes all valid trades; others take value trades)
        {
            for (const auto &e : enemies) {
                int elen = enemy_visible_length(e.get_id());
                if (early) {
                    early_victim = e.get_id();
                    if (!early_ok(std::max(elen, body_leaves_view(e.get_id()) ? 4 : 0), e.position)) continue;
                } else {
                    if (safe_only && !value_trade(elen)) continue;
                    if (!trade_ok(elen, e.get_id())) continue;
                }
                if (!strike_protected(e.position, e.get_id(), elen)) continue;
                for (int d = 0; d < 4; ++d) {
                    if (d == opp_dir) continue;
                    auto dest = destination(here, d);
                    if (dest && *dest == e.position && is_step_safe(d, true))
                        return Action{{DIRS[d]}, 0, Mode::Kill, true};
                }
            }
        }

        {
            // 2. Sprint kill (2..5 steps onto a stationary enemy head)
            int max_sprint = std::min(5, std::max(1, my_len - 1));
            for (int k = 2; k <= max_sprint; ++k) {
                for (const auto &e : enemies) {
                    int elen = enemy_visible_length(e.get_id());
                    if (early) {
                        // A sprint costs k - 1 segments: the enemy must be worth it (no relaxation here).
                        if (!(elen >= my_len && friend_near(e.position)) || elen < my_len + k - 1) continue;
                    } else {
                        if (!trade_ok(elen, e.get_id())) continue;
                        if (safe_only && !value_trade(elen)) continue;
                    }
                    if (distance[index(e.position)] != k) continue;
                    if (!strike_protected(e.position, e.get_id(), elen)) continue;
                    auto candidate = route(e.position);
                    if (static_cast<int>(candidate.size()) != k) continue;
                    if (di(candidate[0]) == opp_dir) continue;

                    bool clear_path = true;
                    Position cur = here;
                    int prev_d = di(c.get_dir());
                    for (int i = 0; i < k - 1; ++i) {
                        int step_d = di(candidate[i]);
                        if (step_d == (prev_d + 2) % 4) { clear_path = false; break; }
                        cur = step(cur, step_d);
                        if (!empty(cur)) { clear_path = false; break; }
                        prev_d = step_d;
                    }
                    if (clear_path && di(candidate[k - 1]) != (prev_d + 2) % 4)
                        return Action{candidate, 0, Mode::Kill, true};
                }
            }
        }

        // v5.9b (F_PEARL_SPRINT, task 7 ii): too short to sprint that far, but pearls on the way pay for the extra steps (the
        // trauma opponent's kill on our alpha: 2 long, a pearl in front, then into the head).
        if (F_PEARL_SPRINT)
            for (const auto &e : enemies) {
                int elen = enemy_visible_length(e.get_id());
                int eaten = 0;
                if (early) {
                    if (!(elen >= my_len && friend_near(e.position))) continue;
                } else {
                    if (!trade_ok(elen, e.get_id())) continue;
                    if (safe_only && !value_trade(elen)) continue;
                }
                if (!strike_protected(e.position, e.get_id(), elen)) continue;
                auto path = pearl_sprint(e.position, eaten);
                int k = static_cast<int>(path.size());
                if (k < 2) continue; // one step: part 1 above
                if (early && elen < my_len + k - 1 - eaten) continue;
                DIAG("pearlsprint " << c.get_id() << ' ' << round << " len " << my_len << " steps " << k << " eaten " << eaten
                                    << " elen " << elen);
                return Action{path, 0, Mode::Kill, true};
            }

        // 3. True 1-Exit Corridor trap (1-step works for length >= 2; 2-step sprint works for length >= 3)
        if (early) return {};
        for (const auto &e : enemies) {
            int elen = enemy_visible_length(e.get_id());
            if (high_value_only && e.get_id() > 1 && elen < 4) continue;
            // Our head on the enemy's only exit is a head-on trade, not a free kill.
            if (!trade_ok(elen, e.get_id())) continue;
            int e_opp = (di(e.get_dir()) + 2) % 4;
            Position forced_tile = e.position;
            int enemy_escapes = 0;
            for (int ed = 0; ed < 4; ++ed) {
                if (ed == e_opp) continue;
                int edge = s.cells[index(e.position)].edge[ed];
                if (edge < 0) continue;
                if (edge == 0) {
                    Position ep = step(e.position, ed);
                    if (ep == here || empty(ep)) { ++enemy_escapes; forced_tile = ep; }
                } else ++enemy_escapes;
            }
            if (enemy_escapes == 1 && forced_tile != here) {
                if (distance[index(forced_tile)] == 1) {
                    int d = first[index(forced_tile)];
                    if (d >= 0 && d != opp_dir && is_step_safe(d, false))
                        return Action{{DIRS[d]}, 0, Mode::Kill, false};
                } else if (my_len >= 3 && distance[index(forced_tile)] == 2) {
                    auto candidate = route(forced_tile);
                    if (candidate.size() == 2) {
                        int d1 = di(candidate[0]);
                        int d2 = di(candidate[1]);
                        if (d1 != opp_dir && d2 != (d1 + 2) % 4 &&
                            s.cells[index(here)].edge[d1] == 0 &&
                            empty(step(here, d1)))
                            return Action{candidate, 0, Mode::Kill, false};
                    }
                }
            }
        }

        return {};
    }

    // v5.7: over the unit cap non-alphas stop splitting, so long neutrals pile up. One near our crowd (ASSASSIN_CROWD other
    // teammate heads within 5) rams an enemy head that comes in, on foot or by sprint, when the trade pays once our crowd
    // eats both drops: we lose L, they lose E, we get back about (L + E) / 2, so E >= L / 3 leaves them further behind.
    // v5.9c (F_EXCHANGE): the team's expected gain in length from ramming enemy head e (elen long) after `moves` steps that
    // eat `eaten` pearls. Both bodies drop ceil(L / 2) pearls round the collision; whoever gets a dragon there first eats most
    // of them (by moves: our nearest teammate against their nearest dragon other than the victim). The ladder replays (17
    // games): in collisions the enemy started they lost more length than we did (1839 vs 1406) and still came out ahead,
    // because they ate 1045 of the drops to our 401. Value = their loss - our loss + (our share - their share) of the drop.
    double exchange_value(const DragonPart &e, int moves, int eaten, int elen) const {
        int len = c.get_length();
        int dying = len + eaten - (moves - 1); // our length when we hit it
        int drop = (dying + 1) / 2 + (elen + 1) / 2;
        int us = INF, them = INF;
        for (const auto &f : friends) us = std::min(us, moves_of(f, e.position));
        for (const auto &o : enemies)
            if (o.get_id() != e.get_id()) them = std::min(them, moves_of(o, e.position));
        double share = (us > EXCH_FAR && them > EXCH_FAR) ? 0.5 : us < them ? near_share() : us == them ? 0.5 : 0.15;
        // v5.9c (F_EXCH_OUTNUM): more of their heads than ours within 4 moves of the collision: the one losing class of
        // strike in 2127 logged (net -3 over 27).
        if (F_EXCH_OUTNUM) {
            int nf = 0, ne = 0;
            for (const auto &f : friends) nf += moves_of(f, e.position) <= 4;
            for (const auto &o : enemies)
                if (o.get_id() != e.get_id()) ne += moves_of(o, e.position) <= 4;
            if (ne > nf) return -100.0;
        }
        return elen - len - 0.5 * eaten + (2.0 * share - 1.0) * drop;
    }

    // v5.9c (F_EXCHANGE, PD round 21): any non-alpha strikes an enemy head it can reach this turn (one step, a sprint, or a
    // sprint paid by pearls on the way) when the exchange pays the team (exchange_value >= EXCH_MIN). Our 4-long neutral at
    // (16,8) split instead of sprinting into the 3-long enemy at (15,10) with our 3-long dragon right beside the drop.
    std::optional<Action> exchange_strike() const {
        if (!F_EXCHANGE || s.alpha || c.get_unit_count() <= 2 || (iso && ISO_STRIKES == 0)) return {};
        int opp = (di(c.get_dir()) + 2) % 4;
        std::optional<Action> best;
        double best_v = EXCH_MIN;
        for (const auto &e : enemies) {
            // A body running out of view is at least one longer than what we see (or as long as we last saw it whole).
            int vis = enemy_visible_length(e.get_id()), elen = vis;
            if (body_leaves_view(e.get_id())) {
                elen = vis + 1;
                if (F_ENEMY_LEN)
                    for (const auto &m : s.elen_mem)
                        if (m[0] == e.get_id() && round - m[2] <= 40) elen = std::max(elen, m[1]);
            }
            for (int d = 0; d < 4; ++d) {
                if (d == opp) continue;
                auto dest = destination(here, d);
                if (!dest || *dest != e.position || !is_step_safe(d, true)) continue;
                double v = exchange_value(e, 1, 0, elen);
                if (v >= best_v) { best_v = v; best = Action{{DIRS[d]}, 0, Mode::Kill, true}; }
            }
            if (c.get_length() < 2 || dist(here, e.position) > 6) continue;
            int eaten = 0;
            auto path = pearl_sprint(e.position, eaten);
            int k = static_cast<int>(path.size());
            if (k < 2) continue;
            double v = exchange_value(e, k, eaten, elen);
            if (v >= best_v) { best_v = v; best = Action{path, 0, Mode::Kill, true}; }
        }
        if (best)
            DIAG("exchange " << c.get_id() << ' ' << round << " len " << c.get_length() << " steps " << best->moves.size()
                             << " value " << best_v);
        return best;
    }

    // v5.9c (F_FLANK): face to face on the same row or column the enemy sees us coming and turns away. The tile beside
    // where its head will be after t straight moves (t = 1..FLANK_AHEAD, no wall between the two), reachable in t or t - 1
    // moves: from there we are side by side when it arrives, and the one-step ram takes it from the side, where our own
    // body closes one of its ways out. The nearest such tile, or none.
    std::optional<Position> flank_target(const DragonPart &e) const {
        int ed = di(e.get_dir());
        Position p = e.position;
        std::optional<Position> best;
        int best_k = INF;
        for (int t = 1; t <= FLANK_AHEAD; ++t) {
            if (s.cells[index(p)].edge[ed] != 0) break;
            p = step(p, ed);
            if (!empty(p)) break; // something in its way: it turns here, the straight-line guess ends
            for (int side : {1, 3}) {
                int sd = (ed + side) % 4;
                if (s.cells[index(p)].edge[sd] != 0) continue;
                Position f = step(p, sd);
                int k = distance[index(f)];
                if (k >= INF || !empty(f) || k > t || k < t - 1) continue;
                if (k < best_k) { best_k = k; best = f; }
            }
        }
        return best;
    }

    // v5.9c (F_FLANK): tile p lies on enemy head e's line straight ahead, 1..3 tiles out: face to face.
    bool on_enemy_line(Position p) const {
        for (const auto &e : enemies) {
            int ed = di(e.get_dir());
            Position q = e.position;
            for (int t = 1; t <= 3; ++t) {
                if (s.cells[index(q)].edge[ed] != 0) break;
                q = step(q, ed);
                if (q == p) return true;
            }
        }
        return false;
    }

    // v5.9c (F_SUPPORT, PD round 22): a teammate that an enemy head can ram this turn (its sprint reach, pearls included),
    // where our side would lose the race to the drop: a small hunter nearby goes to stand by it instead of chasing another
    // enemy. If the enemy strikes, we eat the drop; if it doesn't, we are the one beside it. Returns the step, or -1.
    // v5.9c (F_DODGE): the best profit any visible enemy head makes by ramming us on tile n before our next move (its
    // sprint reach, pearls included): our length − its length + (its share − ours) of the drop, by moves to n. 0 if none.
    double strike_risk(Position n) const {
        if (!F_DODGE || enemies.empty()) return 0.0;
        int len = c.get_length();
        double worst = 0.0;
        for (const auto &e : enemies) {
            int vis = enemy_visible_length(e.get_id());
            int est = enemy_len_est(e.get_id(), vis, body_leaves_view(e.get_id()));
            int k = sprint_reach(e, est, n);
            if (k >= INF) continue;
            int hit = std::max(2, est - (k - 1));
            int drop = (len + 1) / 2 + (hit + 1) / 2;
            int us = INF, them = INF;
            for (const auto &f : friends) us = std::min(us, moves_of(f, n));
            for (const auto &o : enemies)
                if (o.get_id() != e.get_id()) them = std::min(them, moves_of(o, n));
            double share = (us > EXCH_FAR && them > EXCH_FAR) ? 0.5 : them < us ? 0.85 : them == us ? 0.5 : 0.15;
            worst = std::max(worst, len - est + (2.0 * share - 1.0) * drop);
        }
        return worst;
    }

    int support_move() const {
        if (!F_SUPPORT || s.alpha || c.get_length() > SUPPORT_MAX_LEN) return -1;
        Position spot;
        double best_sc = 0;
        bool found = false;
        for (const auto &f : friends) {
            int flen = std::max(2, friendly_visible_length(f.get_id()));
            int mine = my_moves(f.position);
            if (mine > SUPPORT_RANGE || mine <= 1) continue; // too far, or already beside it
            for (const auto &e : enemies) {
                int vis = enemy_visible_length(e.get_id());
                int est = enemy_len_est(e.get_id(), vis, body_leaves_view(e.get_id()));
                if (sprint_reach(e, est, f.position) > 3) continue;
                int them = INF, ours = INF;
                for (const auto &o : enemies)
                    if (o.get_id() != e.get_id()) them = std::min(them, moves_of(o, f.position));
                for (const auto &g2 : friends)
                    if (g2.get_id() != f.get_id()) ours = std::min(ours, moves_of(g2, f.position));
                if (ours <= std::min(them, mine)) continue; // another teammate is already the nearest
                double drop = (flen + 1) / 2 + (est + 1) / 2;
                double sc = drop * (them <= mine + 1 ? 2.0 : 1.0) / (mine + 1);
                if (sc > best_sc) { best_sc = sc; spot = f.position; found = true; }
            }
        }
        if (!found) return -1;
        int best = -1, best_k = dist(here, spot);
        for (int d = 0; d < 4; ++d) {
            if (s.cells[index(here)].edge[d] != 0 || !is_step_safe(d)) continue;
            Position n = step(here, d);
            if (n == spot || forward_escape_count(n, d) == 0 || is_dead_end_trap(n, d)) continue;
            int k = dist(n, spot);
            if (k < best_k) { best_k = k; best = d; }
        }
        if (best >= 0)
            DIAG("support " << c.get_id() << ' ' << round << " to " << spot.x << ',' << spot.y << " dir " << best << " score "
                            << best_sc);
        return best;
    }

    std::optional<Action> assassin_strike() const {
        if (!F_ASSASSIN || s.alpha || !over_cap() || c.get_length() < ASSASSIN_MIN_LEN) return {};
        int crowd = 0;
        for (const auto &f : friends)
            if (!is_alpha(f.get_id()) && dist(here, f.position) <= 5) ++crowd;
        if (crowd < ASSASSIN_CROWD) return {};
        int my_len = c.get_length(), opp_dir = (di(c.get_dir()) + 2) % 4;
        std::optional<Action> best;
        int best_len = 0;
        for (const auto &e : enemies) {
            int elen = std::max(enemy_visible_length(e.get_id()), body_leaves_view(e.get_id()) ? 4 : 0);
            if (3 * elen < my_len && e.get_id() > 1) continue;
            int k = distance[index(e.position)];
            if (k < 1 || k > std::min(4, my_len - 1) || elen <= best_len) continue;
            if (!strike_protected(e.position, e.get_id(), elen)) continue;
            auto path = route(e.position);
            if (static_cast<int>(path.size()) != k || di(path[0]) == opp_dir) continue;
            bool clear = true;
            Position cur = here;
            int prev = di(c.get_dir());
            for (int i = 0; i < k && clear; ++i) {
                int d = di(path[i]);
                if (d == (prev + 2) % 4) clear = false;
                cur = step(cur, d);
                if (i < k - 1 && !empty(cur)) clear = false;
                prev = d;
            }
            if (!clear) continue;
            best = Action{path, 0, Mode::Kill, true};
            best_len = elen;
        }
        return best;
    }

    // v5.9b (F_HERD, task 10, autarky M514761 round 13): enemy head E (facing fe) after we step onto m (moving md): how many
    // of its moves are still safe for it? Not into kelp or a body, not into our head, not onto a tile we can ram next turn,
    // not into a dead end.
    int enemy_safe_replies(Position E, int fe, Position m, int md) const {
        int safe = 0;
        for (int ed = 0; ed < 4; ++ed) {
            if (ed == (fe + 2) % 4) continue;
            int edge = s.cells[index(E)].edge[ed];
            if (edge < 0) continue;
            if (edge > 0) { ++safe; continue; }
            Position t = step(E, ed);
            if (t == m) continue;
            auto tt = c.get_tile(t);
            if (!tt) { ++safe; continue; }
            if (tt->get_dragon()) continue;
            bool rammable = false;
            for (int rd = 0; rd < 4; ++rd)
                if (rd != (md + 2) % 4 && s.cells[index(m)].edge[rd] == 0 && step(m, rd) == t) rammable = true;
            if (rammable) continue;
            if (forward_escape_count(t, ed) == 0) continue;
            ++safe;
        }
        return safe;
    }

    // A hunter closing on a worthwhile enemy head (3+ long, or its alpha) 2-HERD_RANGE away: the step that leaves it the fewest
    // safe replies, when that is at most one (at (29,2) the step west to (28,2) left the 11-long enemy one way out, (27,0);
    // our step north left it two). Ties go to the step nearer to it. -1: no such step.
    int herd_move() const {
        const DragonPart *tgt = nullptr;
        int tlen = 0;
        for (const auto &e : enemies) {
            int elen = std::max(enemy_visible_length(e.get_id()), body_leaves_view(e.get_id()) ? 4 : 0);
            if (elen < 3 && e.get_id() > 1) continue;
            int k = dist(here, e.position);
            if (k < 2 || k > HERD_RANGE) continue;
            if (!tgt || elen > tlen) { tgt = &e; tlen = elen; }
        }
        if (!tgt) return -1;
        Position E = tgt->position;
        int fe = di(tgt->get_dir()), opp = (di(c.get_dir()) + 2) % 4;
        int best = -1, best_safe = INF, best_k = INF;
        for (int d = 0; d < 4; ++d) {
            if (d == opp || s.cells[index(here)].edge[d] != 0 || !is_step_safe(d)) continue;
            Position m = step(here, d);
            if (m == E || forward_escape_count(m, d) == 0 || is_dead_end_trap(m, d)) continue;
            int safe = enemy_safe_replies(E, fe, m, d), k = dist(m, E);
            if (safe < best_safe || (safe == best_safe && k < best_k)) { best_safe = safe; best_k = k; best = d; }
        }
        if (best >= 0)
            DIAG("herd " << c.get_id() << ' ' << round << " enemy " << tgt->get_id() << " len " << tlen << " dir " << best
                         << " safe " << best_safe);
        return best_safe <= 1 ? best : -1;
    }

    // v5.7: a long teammate squaring up to an enemy head in view: small dragons close in to eat what the collision drops.
    std::optional<Position> convergence_point() const {
        if (!F_ASSASSIN || s.alpha || c.get_length() > 3 || !over_cap()) return {};
        std::optional<Position> best;
        int best_d = INF;
        for (const auto &f : friends) {
            if (is_alpha(f.get_id()) || friendly_visible_length(f.get_id()) < ASSASSIN_MIN_LEN) continue;
            for (const auto &e : enemies) {
                if (dist(f.position, e.position) > 3) continue;
                int k = distance[index(e.position)];
                if (k < 2 || k > 6 || k >= best_d) continue;
                best_d = k;
                best = e.position;
            }
        }
        return best;
    }

    // v5.9b (F_TRUE_MOVES, autarky round 114): by moves, not straight-line distance. A teammate 3 tiles from a pearl through
    // kelp, facing away, needed 5 moves; ours needed 4 but gave it up.
    bool claimed(Position p) const {
        if (s.alpha && round >= feed_round()) return false;
        int my_dist = F_TRUE_MOVES ? my_moves(p) : dist(here, p);
        for (auto f : friends) {
            int k = F_TRUE_MOVES ? moves_of(f, p) : dist(f.position, p);
            if (!s.alpha && is_alpha(f.get_id()) && k <= 3 && round > threshold()) return true;
            if (k < my_dist || (k == my_dist && f.get_id() < c.get_id())) return true;
        }
        return false;
    }

    std::optional<Position> food_target(bool future) const {
        bool small_map_splitting_alpha = (s.alpha && map_scale() < 20 && !s.growing);
        bool kam = is_kamikaze() || skirmisher();
        std::optional<Position> best;
        double bestscore = -1e20;
        std::vector<Position> pearls;
        for (const auto &t : c.get_tiles())
            if (t.has_pearl()) pearls.push_back(t.get_position());

        for (const auto &t : c.get_tiles()) {
            auto p = t.get_position();
            int n = distance[index(p)];
            if (n < 1 || n > 10 || t.get_dragon()) continue;
            if (future ? !(t.get_pearl_time() >= 1 && t.get_pearl_time() <= 2) : !t.has_pearl()) continue;
            if (claimed(p)) continue;
            if (patch_held(p) && !patch_held(here)) continue; // a teammate camps that patch
            if (EXIT_CLEAR_ON) {
                bool held = false;
                if (exit_lane(p, &held) && held) continue; // the camper or its children come out onto it
            }

            int arr_d = arrival[index(p)];
            int first_d = first[index(p)];
            int exits = escape_count(p);
            int f_exits = forward_escape_count(p, arr_d);
            if (exits == 0 || f_exits == 0) continue;

            // Neutral and Alpha bots will not pursue a pearl if entering it or taking the first step toward it is certain death
            // Kamikazes still refuse dead ends; only the head-on certain-death check is theirs to ignore.
            if (is_dead_end_trap(p, arr_d)) continue;
            if (first_d >= 0) {
                Position step1 = step(here, first_d);
                if (is_dead_end_trap(step1, first_d) || (!kam && is_enemy_certain_death(step1, first_d))) continue;
            }
            if (!kam && n == 1 && is_enemy_certain_death(p, arr_d)) continue;

            int pearl_risk = (s.alpha || iso) ? danger(p) : 0;
            // v5.8 (F_ISOLATED): no pearl an enemy head can reach within 2 steps (3 early on).
            if (iso && !s.alpha && pearl_risk >= (iso_early ? ISO_RISK_EARLY : ISO_RISK)) continue;
            if (s.alpha && !small_map_splitting_alpha) {
                if (pearl_risk >= 360) continue;
                if (pearl_risk >= 320 && (f_exits < 2 || n > 1)) continue;
                if (c.get_length() >= 12 && space(p) < 5) continue;
            }

            double cluster = 0;
            for (const auto &q : pearls) {
                int k = chebyshev(p, q);
                if (k > 0 && k <= 2) cluster += 2.0 / k;
            }

            double risk_weight = (s.alpha && !small_map_splitting_alpha) ? (pearl_risk >= 280 ? 0.06 : 0.02) : 0.0;
            double center_bonus = (!s.alpha || small_map_splitting_alpha) ? 0.35 * ((w + h) / 2 - center_dist(p)) : 0.0;
            double score = (45.0 + 18.0 * cluster) / (n + 0.5) - risk_weight * pearl_risk + center_bonus;

            if (!future && t.has_pearl()) score += 18.0 / (n + 1.0);
            if (future && n < t.get_pearl_time()) score -= 10;

            if (!best || score > bestscore || (score == bestscore && index(p) < index(*best))) {
                best = p;
                bestscore = score;
            }
        }
        return best;
    }

    struct PortalPlan {
        Position approach;
        int direction, id, cost;
    };

    bool long_body() const {
        return F_LONG_PORTAL && c.get_length() >= LONG_PORTAL_LEN;
    }

    std::optional<PortalPlan> portal_target(bool force, bool allow_long = false, int only_id = -1) const {
        std::optional<PortalPlan> best;
        // Long dragons do not route into portals: they come out blind with their whole mass at stake.
        if (long_body() && !allow_long && !force && !s.evacuating && !s.evicting) return best;
        for (const auto &t : c.get_tiles())
            for (int d = 0; d < 4; ++d) {
                auto p = t.get_position();
                int e = s.cells[index(p)].edge[d];
                if (e <= 0 || distance[index(p)] == INF || (p != here && t.get_dragon())) continue;
                if (only_id >= 0 && e - 1 != only_id) continue;
                if (portal_into_dead_cell(p, d)) continue;
                if (!force && !s.evacuating && portal_occupied(e - 1) && !roam_portal(e - 1)) continue;
                if (!force && !s.evacuating && !s.evicting) {
                    const auto &pp = portal_const(e - 1);
                    if (F_PORTAL_EVICT && (pp.barren || round < pp.shun_until)) continue;
                }
                if (p == here && d == (di(c.get_dir()) + 2) % 4) continue;
                if (F_CHAMBER_ONE && !force && !s.evicting && !own_portal(e - 1) && chamber_taken(p, d, e - 1)) continue;
                if (!force && !s.evicting && !own_portal(e - 1) && portal_yield(p, d, e - 1)) continue;
                if (F_ENEMY_CHAMBER && !force && round < portal_const(e - 1).enemy_until) continue;
                // A resident still takes a portal it is forced (or entitled) to use.
                if (s.resident && !((F_REVERSE_SPLIT && force) || own_portal(e - 1))) continue;
                auto exit = destination(p, d);
                if (exit) {
                    auto tile = c.get_tile(*exit);
                    if (tile && tile->get_dragon()) continue;
                }
                int cost = distance[index(p)] + 1;
                // Chained rooms: a portal we just came through is the way back, so prefer any other.
                if (F_CHAIN && only_id < 0 && recently_used(e - 1)) cost += CHAIN_PENALTY;
                if (!best || cost < best->cost || (cost == best->cost && e - 1 < best->id))
                    best = PortalPlan{p, d, e - 1, cost};
            }
        return best;
    }

    struct FeedPlan {
        Position p;
        int id, len;
    };

    // Strict order between alphas, with a 2-segment margin so near-equal alphas agree on who yields.
    bool superior_alpha(int oid, int olen) const {
        int my = c.get_length();
        return olen >= my + 2 || (olen >= my - 1 && oid < (c.get_id() & SONAR_ID_MASK));
    }

    // The alpha to feed: the longest reachable one, weighed against travel distance so
    // mass is not relayed through intermediate alphas (each relay loses half).
    // An alpha only ever considers alphas superior to itself.
    // v5.8 (F_FEED_SCORE): the mass a feeder drops only counts if it ends up in the final apex, and an alpha only gets there
    // if it stays the longest: weigh length against the walk as L - k / 4 (a 19-long alpha 10 away beats a 10-long one 5
    // away: 16.5 against 8.75), among alphas we can reach with time to spare for the apex to eat the drop. The one we are
    // already walking to keeps us unless another beats it by FEED_SWITCH (reports arrive in bursts; flip-flopping between
    // two alphas wasted the walk).
    std::optional<FeedPlan> feed_target() const {
        std::optional<FeedPlan> best;
        double best_score = -1e20, cur_score = -1e20;
        std::optional<FeedPlan> cur;
        int remaining = 500 - round;
        auto consider = [&](int id, Position p, int len) {
            if (id == (c.get_id() & SONAR_ID_MASK)) return;
            if (s.alpha && !superior_alpha(id, len)) return;
            int k = dist(here, p);
            if (k > remaining - 3) return;
            if (F_FEED_SCORE && k + c.get_length() / 2 + 3 > remaining) return;
            double score = 4.0 * len - k;
            if (F_FEED_SCORE && id == s.feed_id && score > cur_score) { cur_score = score; cur = FeedPlan{p, id, len}; }
            if (score > best_score) { best_score = score; best = FeedPlan{p, id, len}; }
        };
        for (const auto &a : s.alphas) {
            if (round - a.seen > SONAR_TTL) continue;
            Position p = a.p;
            for (const auto &f : friends)
                if ((f.get_id() & SONAR_ID_MASK) == a.id) p = f.position;
            consider(a.id, p, a.len);
        }
        for (const auto &f : friends) {
            if (!is_alpha(f.get_id())) continue;
            bool tracked = false;
            for (const auto &a : s.alphas)
                if (a.id == (f.get_id() & SONAR_ID_MASK)) tracked = true;
            int flen = friendly_visible_length(f.get_id());
            if (!tracked) consider(f.get_id() & SONAR_ID_MASK, f.position, F_TRUE_LEN ? flen : std::max(8, flen));
        }
        if (F_FEED_SCORE && cur && best && best->id != cur->id && best_score < cur_score + 4.0 * FEED_SWITCH) return cur;
        return best;
    }

    std::optional<Position> exploration_target() const {
        std::optional<Position> best;
        double best_score = -1e20;
        std::array<Position, 49> bodies;
        int n_bodies = 0;
        if (F_DISPERSE)
            for (const auto &t : c.get_tiles()) {
                auto part = t.get_dragon();
                if (part && part->get_team() == c.get_team() && part->get_id() != c.get_id() && n_bodies < 49)
                    bodies[n_bodies++] = t.get_position();
            }
        for (const auto &t : c.get_tiles()) {
            auto p = t.get_position();
            int path = distance[index(p)];
            if (path < 1 || path >= INF || t.get_dragon()) continue;
            int exits = escape_count(p);
            if (!exits) continue;
            if (is_dead_end_trap(p, arrival[index(p)])) continue;
            int age = std::min(80, round - s.cells[index(p)].visited);
            int unseen = 0;
            for (int d = 0; d < 4; ++d)
                if (s.cells[index(p)].edge[d] == 0 && s.cells[index(step(p, d))].seen < 0) ++unseen;
            double score = 0.7 * age + 16 * unseen + 5 * std::min(exits, 3) - 2 * path;
            score += 3 * (dist(here, s.sector_target) - dist(p, s.sector_target));
            if (s.alpha) score -= 0.10 * danger(p);
            for (const auto &f : friends)
                score -= (F_REPEL && !s.alpha && !is_alpha(f.get_id()) ? 10 : 6) * std::max(0, 4 - dist(p, f.position));
            if (F_PORTAL_CLEAR && portal_adjacent(p)) score -= loiter_penalty(40);
            if (in_barren(p)) score -= BARREN_PENALTY;
            if (farm_lane(p)) score -= 40;
            if (patch_held(p)) score -= 60;
            if (F_PATCH_CAMP && s.patch_camp && !in_patch_tiles(p)) score -= 80;
            for (int i = 0; i < n_bodies; ++i) score -= 2 * std::max(0, 3 - dist(p, bodies[i]));
            if (score > best_score) { best_score = score; best = p; }
        }
        return best;
    }

    std::optional<Position> remembered_pearl_target() const {
        int mscale = map_scale();
        bool small_map_splitting_alpha = (s.alpha && mscale < 20 && !s.growing);
        std::optional<Position> best;
        double best_score = -1e20;
        for (int dy = -8; dy <= 8; ++dy) {
            int rem = 8 - std::abs(dy);
            for (int dx = -rem; dx <= rem; ++dx) {
                if (std::abs(dx) <= 3 && std::abs(dy) <= 3) continue;
                int d = std::abs(dx) + std::abs(dy);
                if (d < 4 || d > 8) continue;
                Position p = wrap({here.x + dx, here.y + dy});
                const auto &cell = s.cells[index(p)];
                if (cell.seen < 0 || cell.seen == round || cell.pearl_round < 0) continue;
                if (round - cell.seen > 24) continue;
                if (cell.pearl_round > round + d) continue;
                int open_edges = 0;
                for (int ed = 0; ed < 4; ++ed)
                    if (cell.edge[ed] >= 0 || cell.edge[ed] == -2) ++open_edges;
                if (open_edges <= 1) continue;
                if (claimed(p)) continue;
                if (s.alpha && !small_map_splitting_alpha && round < feed_round() && danger(p) >= 320) continue;
                // v5.7: only a pearl we know a way to, judged by that way's length (walls, chambers behind portals).
                if (F_ROUTE_MEM) {
                    memory_bfs();
                    int md = mem_depth[index(p)];
                    if (md >= INF || md > d + 3 || cell.pearl_round > round + md) continue;
                    d = md;
                }
                double score = 40.0 / (d + 1.0) - 0.35 * (round - cell.seen);
                if (!best || score > best_score) {
                    best = p;
                    best_score = score;
                }
            }
        }
        return best;
    }

    // v5.7 (ladder bugs a2, a3): the way to a target out of view, over the whole remembered map. Walls are static, so
    // remembered edges are trusted; tiles never seen are assumed open (the target is usually behind them). Visible
    // dragons block. If no route reaches the target, go to the reached tile closest to it. (v5.6 searched only the tiles
    // in view on maps above map_scale 26 and then went by straight-line distance: behind a wall or in a maze it circled.)
    void memory_bfs() const {
        if (mem_round == round) return;
        mem_round = round;
        int n_cells = w * h;
        std::fill_n(mem_depth.begin(), n_cells, INF);
        int lo = 0, hi = 0, back = (di(c.get_dir()) + 2) % 4;
        mem_q[hi++] = here;
        mem_depth[index(here)] = 0;
        mem_first[index(here)] = -1;
        while (lo < hi) {
            Position p = mem_q[lo++];
            int pi = index(p);
            const auto &cp = s.cells[pi];
            // A visible dragon blocks; a target on it is still reached (it ends the route), as in the BFS.
            if (pi != index(here) && cp.seen == round && cp.has_dragon) continue;
            for (int d = 0; d < 4; ++d) {
                if (p == here && d == back) continue;
                int e = cp.seen < 0 ? 0 : cp.edge[d];
                Position n = step(p, d);
                if (e > 0 && F_ROUTE_PORTAL) {
                    // v5.8: through a portal whose both ends we know (trauma's farms are only reachable that way).
                    auto dest = destination(p, d);
                    if (!dest || portal_const(e - 1).barren || portal_into_dead_cell(p, d)) continue;
                    n = *dest;
                } else if (e != 0) {
                    continue;
                }
                int ni = index(n);
                if (mem_depth[ni] < INF) continue;
                if (s.cells[ni].seen >= 0 && chokepoint_blocked(n, d)) continue;
                mem_depth[ni] = mem_depth[pi] + 1;
                mem_first[ni] = p == here ? d : mem_first[pi];
                mem_q[hi++] = n;
            }
        }
    }

    int memory_route(Position target) const {
        memory_bfs();
        int ti = index(target);
        if (mem_depth[ti] < INF) return mem_first[ti];
        // Walled off as far as we know: the reached tile nearest to it (by straight-line distance, then path length).
        int best = -1, best_key = INF;
        for (int i = 0; i < w * h; ++i) {
            if (mem_depth[i] >= INF || mem_depth[i] == 0) continue;
            int key = 4 * dist({i % w, i / w}, target) + mem_depth[i];
            if (key < best_key) { best_key = key; best = mem_first[i]; }
        }
        return best;
    }

    bool mem_reachable(Position p) const {
        if (!F_ROUTE_MEM) return true;
        memory_bfs();
        return mem_depth[index(p)] < INF;
    }

    // v5.7 (a3): the remembered pearl we set off for stays our target until it is seen gone, reached, or its time is up
    // (v5.6 re-chose every turn and could alternate between two pearls on either side of us, circling in place).
    std::optional<Position> committed_pearl_target() {
        if (!F_REM_COMMIT) return remembered_pearl_target();
        if (round <= s.rem_until) {
            const auto &cell = s.cells[index(s.rem_goal)];
            bool gone = cell.pearl_round < 0 || (cell.seen == round && cell.has_dragon);
            // ...and the way there has not grown much longer than planned (walls found where we assumed open ground).
            bool detour = F_ROUTE_MEM && (memory_bfs(), mem_depth[index(s.rem_goal)] > s.rem_until - round + 3);
            if (!gone && s.rem_goal != here && !claimed(s.rem_goal) && mem_reachable(s.rem_goal) && !detour)
                return s.rem_goal;
        }
        s.rem_until = -1000;
        auto p = remembered_pearl_target();
        if (p) {
            s.rem_goal = *p;
            int k = F_ROUTE_MEM ? (memory_bfs(), mem_depth[index(*p)]) : dist(here, *p);
            s.rem_until = round + k + 3;
        }
        return p;
    }

    int remembered_direction(Position target) const {
        if (F_ROUTE_MEM) return memory_route(target);
        bool use_wall_memory = (map_scale() <= 26);
        int max_expand = use_wall_memory ? 200 : MAX_CELLS;
        int lo = 0, hi = 0;
        remembered_q[hi++] = here;
        remembered_first[index(here)] = 4;
        int best_frontier_dir = -1;
        int best_frontier_dist = INF;

        auto cleanup = [&]() {
            for (int i = 0; i < hi; ++i) remembered_first[index(remembered_q[i])] = -1;
        };

        while (lo < hi) {
            auto p = remembered_q[lo++];
            for (int d = 0; d < 4; ++d)
                if (s.cells[index(p)].edge[d] == 0) {
                    if (p == here && d == (di(c.get_dir()) + 2) % 4) continue;
                    auto n = step(p, d);
                    if (chokepoint_blocked(n, d)) continue;
                    if (use_wall_memory) {
                        if (s.cells[index(n)].seen == round && s.cells[index(n)].has_dragon && n != target) continue;
                    } else {
                        if (!empty(n) && n != target) continue;
                    }
                    int first_d = (p == here) ? d : remembered_first[index(p)];
                    if (n == target) {
                        cleanup();
                        return first_d;
                    }
                    if (s.cells[index(n)].seen < 0) {
                        int fd = dist(n, target);
                        if (fd < best_frontier_dist) { best_frontier_dist = fd; best_frontier_dir = first_d; }
                        continue;
                    }
                    if (remembered_first[index(n)] >= 0) continue;
                    if (hi < max_expand) {
                        remembered_first[index(n)] = first_d;
                        remembered_q[hi++] = n;
                    }
                    int fd = dist(n, target);
                    if (fd < best_frontier_dist) { best_frontier_dist = fd; best_frontier_dir = first_d; }
                }
        }
        cleanup();
        return best_frontier_dir;
    }

    // v5.9b (F_PORTAL_PROBE, tasks 2 and 3): a crossing that last turn's probe found blocked is replaced, and a move that
    // lands us beside a portal we are heading for plans a probe of it.
    Action decide() {
        s.probe_plan_dir = -1;
        Action a = decide_core();
        if (F_PORTAL_PROBE) a = probe_filter(a);
        return a;
    }

    Action probe_filter(const Action &a) {
        if (a.child || a.intentional_death || a.moves.size() != 1) return a;
        int d = di(a.moves[0]), e = s.cells[index(here)].edge[d];
        if (e > 0 && s.probe_verdict > 0 && s.probe_bounded && here == s.probe_pos && d == s.probe_dir) {
            // Someone sits right behind this portal: a teammate (the chamber is taken: task 3) or an enemy (task 2).
            auto &pp = portal(e - 1);
            if (s.probe_verdict == 1) occupy(e - 1, round + 20);
            else pp.enemy_until = std::max(pp.enemy_until, round + 30);
            int alt = -1;
            double best = -1e18;
            for (int k = 0; k < 4; ++k) {
                if (k == d || s.cells[index(here)].edge[k] != 0 || !is_step_safe(k)) continue;
                Position n = step(here, k);
                if (forward_escape_count(n, k) == 0 || is_dead_end_trap(n, k)) continue;
                double sc = std::min(space(n), 10) + 4.0 * escape_count(n) - (avoided(n) ? 50 : 0);
                if (sc > best) { best = sc; alt = k; }
            }
            DIAG("probeblock " << c.get_id() << ' ' << round << " pid " << e - 1 << " verdict " << s.probe_verdict << " alt "
                               << alt << " len " << c.get_length());
            if (alt >= 0) return {{DIRS[alt]}, 0, a.mode, false};
            // Forced out through a portal an enemy stands behind: split, so only the 2-long head goes (task 2).
            if (s.probe_verdict == 2 && F_PROBE_SPLIT && c.get_length() >= 4 && can_split_safe(c.get_length() - 2))
                return {{}, c.get_length() - 2, Mode::Rescue, false};
            return a;
        }
        bool heading = a.mode == Mode::Portal || a.mode == Mode::Loop || s.evicting || s.evacuating || s.capture_portal >= 0;
        if (e == 0 && heading) {
            Position n = step(here, d);
            for (int pd = 0; pd < 4; ++pd)
                if (pd != (d + 2) % 4 && s.cells[index(n)].edge[pd] > 0 && !portal_const(s.cells[index(n)].edge[pd] - 1).barren) {
                    s.probe_plan_dir = pd;
                    s.probe_plan_pos = n;
                    break;
                }
        }
        return a;
    }

    Action decide_core() {
        observe();
        paths();
        if (F_ALPHA_MEMORY && F_AMEM_FIX && s.alpha) mark_amem_claims();

        int opp_dir = (di(c.get_dir()) + 2) % 4;
        int feed_start = feed_round();
        // v5.9 (F_EARLY_FEED, portals): no enemy head has been able to reach us for PEACE_WINDOW rounds (portals: enemies sit in
        // the next chamber behind kelp; our deaths there were nearly all our own dragons colliding). Consolidating then costs
        // nothing in safety, and the apex gets the time to eat every drop: start feeding from EARLY_FEED_ROUND.
        if (F_EARLY_FEED) {
            for (const auto &e : enemies)
                if (dist(e.position, here) <= 6 && danger(here) >= 280) s.threat_round = round; // it can get to us in 3 steps
            if (round >= EARLY_FEED_ROUND && round - s.threat_round >= PEACE_WINDOW && feed_start > round) {
                DIAG("earlyfeed " << c.get_id() << ' ' << round << " calm " << round - s.threat_round << " len " << c.get_length());
                feed_start = round;
            }
        }

        // Alpha consolidation: in the endgame every alpha that is not the apex yields its whole body to a
        // superior alpha, as long as it can reach it and the apex has time to eat the dropped trail.
        auto plan = round >= feed_start ? feed_target() : std::optional<FeedPlan>{};
        if (plan && plan->id != s.feed_id) {
            DIAG("feedpick " << c.get_id() << ' ' << round << " to " << plan->id << " len " << plan->len << " k "
                             << dist(here, plan->p) << " was " << s.feed_id);
            s.feed_id = plan->id;
        }
        bool receiver_alpha = s.alpha;
        if (s.alpha && plan && c.get_unit_count() > 1 &&
            dist(here, plan->p) + (F_FEED_SCORE ? std::min(c.get_length(), FEED_EAT_MIN) : c.get_length()) <= (500 - round) - 3)
            receiver_alpha = false;

        auto alpha = (!receiver_alpha && plan) ? std::optional<Position>{plan->p} : std::optional<Position>{};
        bool feed = !receiver_alpha && round >= feed_start && alpha.has_value() &&
                    dist(here, *alpha) <= (500 - round) - 2;
        // v5.8 (F_ISOLATED): alone among enemies. A death here only feeds them: no strikes, no ambushes, no bold moves, and
        // enemy reach counts for every pearl and step (as it does for an alpha), more strictly in the first rounds.
        iso = false;
        if (F_ISOLATED && friends.empty() && round >= 1 && c.get_length() >= (F_ENEMY_LEN ? ISO_MIN_LEN2 : ISO_MIN_LEN)) {
            int near = 0;
            for (const auto &e : enemies) near += dist(e.position, here) <= ISO_RADIUS;
            iso = near >= ISO_MIN_ENEMIES;
        }
        iso_early = iso && round < ISO_EARLY;
        bool kam = is_kamikaze() && !(iso && ISO_STRIKES < 2);
        bool sprint_hunter = is_sprint_hunter() && !(iso && ISO_STRIKES < 2);
        bool bold = (kam || skirmisher()) && !iso; // ignores enemy "certain death" filters
        // v5.9b (F_LONG_GUARD, task 10): a long non-alpha has as much to lose as a small alpha: it weighs enemy reach on its
        // steps and splits away from a head that can ram it (the 2-long rear of the split faces the threat) as alphas do.
        bool guard = F_LONG_GUARD && !s.alpha && !feed && !kam && c.get_length() >= LONG_GUARD_LEN;
        // v5.9c (F_DODGE): foragers dodge strikes that pay the enemy. Hunters (kamikazes, sprint hunters) do not: dodging made
        // them timid on dilemma (16 -> 8 of 16 against v5.7), where the kamikaze regime runs most of the game.
        bool dodger = F_DODGE && !s.alpha && !feed && !kam && !sprint_hunter && (DODGE_SKIRMISH || !skirmisher());

        // v5.9b (F_RETREAT, task 1): outnumbered by enemy heads on the enemy's half of the map (their spawn is the mirror image
        // of ours): walk back toward our spawn for a while. Hunters stay; they are there for the enemies.
        if (F_RETREAT && !feed && !kam && !sprint_hunter && !s.camping && !s.resident && round > s.born) {
            int en = 0, fr = 0;
            // RETREAT_MODE 2: only enemies that can take us on count (at least as long as we are), and only for dragons with
            // something to lose (4+ long). Mode 1 (any enemy heads, any length) cost default 6 of 16 games: 2-long dragons
            // walking home from the enemy half gave up the ground they were fighting for.
            for (const auto &e : enemies)
                en += dist(e.position, here) <= 5 &&
                      (RETREAT_MODE == 1 || std::max(enemy_visible_length(e.get_id()), body_leaves_view(e.get_id()) ? 4 : 0) >=
                                                c.get_length());
            for (const auto &f : friends) fr += dist(f.position, here) <= 5;
            bool worth = RETREAT_MODE == 1 || c.get_length() >= 4;
            if (worth && en >= RETREAT_ENEMIES && en > fr && on_enemy_side(here)) {
                if (round > s.retreat_until)
                    DIAG("retreat " << c.get_id() << ' ' << round << " at " << here.x << ',' << here.y << " enemies " << en
                                    << " friends " << fr << " to " << s.spawn.x << ',' << s.spawn.y);
                s.retreat_until = round + RETREAT_TTL;
            }
        }
        bool retreating = F_RETREAT && round <= s.retreat_until && !feed && !kam && on_enemy_side(here);

        // Round 0: split the rear L-2 off, again and again. Each child acts later this same round, so a spawn
        // body of length L is L/2 two-long dragons before anyone has moved (splitting 2 a turn took L/2 - 1
        // turns, standing still). The front keeps its role: an alpha stays alpha at length 2, as it would anyway.
        // v5.5: "growing" is ignored in round 0. A long spawn child used to be promoted to alpha (length > 7) and, with
        // the cascade's own units already at alpha_split_cap(), counted as growing: it stopped cascading (slithery_fight).
        if (F_SPAWN_CASCADE && round == 0 && (F_CASCADE_FIX || !s.growing) && c.get_length() >= 4 &&
            c.can_split(c.get_length() - 2)) {
            DIAG("cascade " << c.get_id() << ' ' << c.get_length() << ' ' << here.x << ',' << here.y);
            return {{}, c.get_length() - 2, Mode::Cascade, false};
        }

        // Trapped in a sealed pocket we walked into: tell the team where its mouth is, so nobody follows
        // us in and blocks the way out for the child we split off.
        if (F_HAZARD && s.enc_dir >= 0 && round - s.enc_round <= 40 && round - s.hazard_sent >= 3 &&
            dist(here, s.enc_mouth) <= std::max(8, c.get_length() + 2)) {
            int pocket_pearls = 0, pocket_size = 0;
            if (sealed_pocket(here, di(c.get_dir()), pocket_pearls, pocket_size) &&
                (!F_HAZARD_CLOSED || closed_behind(s.enc_mouth, s.enc_dir))) {
                DIAG("hazard " << c.get_id() << ' ' << round << ' ' << s.enc_mouth.x << ',' << s.enc_mouth.y << " dir " << s.enc_dir
                               << " at " << here.x << ',' << here.y << " len " << c.get_length() << " size " << pocket_size
                               << " pearls " << pocket_pearls << " fwd " << forward_escape_count(here, di(c.get_dir()))
                               << " entered " << s.enc_round);
                add_hazard(s.enc_mouth, s.enc_dir, round);
                s.hazard_sent = round;
            }
        }

        // Portal enclosures hold one dragon at most, and only while they pay. Leave one that is barren or
        // has had no pearl for a while; a newcomer that finds a teammate inside leaves at once, or dies
        // (dropping its pearls for the resident) if there is no way out.
        std::optional<PortalPlan> evict_plan;
        s.camping = false;
        int chamber_spawners = 0; // v5.6: spawners of the dense, paying chamber we hold (a hotspot to mirror)
        // A child born in a chamber leaves it to whoever split it off. If no lower ID (the parent, or an older
        // teammate) is inside any more and the chamber pays, the child is the camper instead of walking out too.
        if (F_CAMP2 && F_PORTAL_EVICT && s.evacuating && !s.alpha && round > s.born) {
            auto enc = enclosure_at(here);
            if (enc.small && enc.spawners > 0) {
                bool lower = false;
                for (int fid : enc.friend_ids)
                    if (fid >= 0 && fid < c.get_id()) lower = true;
                bool dense = 2 * enc.spawners >= enc.size;
                if (!lower && ((dense && enc.next_pearl <= CAMP_DENSE_HORIZON) || enc.next_pearl <= CAMP_SOON)) {
                    s.evacuating = false;
                    DIAG("adopt " << c.get_id() << ' ' << round << ' ' << enc.portal_id);
                }
            }
        }
        if (F_PORTAL_EVICT && !s.evacuating && round > s.born) {
            auto enc = enclosure_at(here);
            if (enc.small) {
                auto &home = portal(enc.portal_id);
                home.small = true;
                if (enc.spawners == 0) home.barren = true;
                bool idle = enc.pearls_soon == 0 && round - s.last_food > 10;
                bool newcomer = round - s.portal_arrival <= 6;
                bool crowded = enc.friend_parts > 0 && newcomer;
                bool dense = 2 * enc.spawners >= enc.size;
                if (F_CAMP) {
                    // A chamber that spawns is worth waiting in while it is empty: leave only when no pearl is
                    // due for a long time. (v5.2 left after 10 quiet rounds, even with pearls a few rounds off.)
                    idle = round - s.last_food > 10 && (!dense || enc.next_pearl > CAMP_HORIZON);
                    // Two settled dragons in one chamber: the shorter one (higher ID on a tie) goes.
                    if (enc.friend_parts > 0 && !crowded)
                        crowded = enc.longest_friend > c.get_length() ||
                                  (enc.longest_friend == c.get_length() && enc.longest_friend_id < c.get_id());
                }
                if (F_CAMP2) {
                    bool fresh = s.camp_portal != enc.portal_id || round - s.camp_round > 20;
                    if (fresh) {
                        s.camp_portal = enc.portal_id;
                        s.camp_since = std::max(s.portal_arrival, s.born);
                        s.camp_seen.fill({-1, -1000, -1000});
                    }
                    s.camp_round = round;
                    // One camper per chamber. v5.4 had the shorter dragon leave, but lengths change mid-round (the first
                    // mover eats a pearl), so both could leave at once. Now a teammate we saw come in after us never moves
                    // us; one that was here when we came, or that we have shared the chamber with for 10 rounds, does if
                    // its ID is lower. Every rule yields only to a lower ID, so the lowest ID inside always stays.
                    bool yield = false;
                    for (int k = 0; k < 4; ++k) {
                        int fid = enc.friend_ids[k];
                        if (fid < 0) break;
                        std::array<int, 3> *slot = nullptr, *oldest = &s.camp_seen[0];
                        for (auto &cs : s.camp_seen) {
                            if (cs[0] == fid) { slot = &cs; break; }
                            if (cs[2] < (*oldest)[2]) oldest = &cs;
                        }
                        // Whoever is inside when we first judge the chamber counts as arriving with us (two dragons can come
                        // through in the same round); the lower ID of such a pair stays.
                        if (!slot) { slot = oldest; *slot = {fid, fresh ? s.camp_since : round, round}; }
                        if ((*slot)[2] < round - 3) (*slot)[1] = round; // it left and came back: new again
                        (*slot)[2] = round;
                        if ((*slot)[1] < s.camp_since || ((*slot)[1] == s.camp_since && fid < c.get_id())) yield = true;
                        // Neither saw the other come in (a 6x3 chamber does not fit in view): after 10 rounds together,
                        // the higher ID goes. A leaver's head is out within that time, so this never evicts both.
                        if ((enc.friend_heads >> k & 1) && round - (*slot)[1] >= 10 && fid < c.get_id()) yield = true;
                    }
                    crowded = yield;
                    // Stay while the chamber pays: dense and a pearl due within CAMP_DENSE_HORIZON, or any pearl due soon.
                    bool pays = (dense && enc.next_pearl <= CAMP_DENSE_HORIZON) || enc.next_pearl <= CAMP_SOON;
                    idle = round - s.last_food > 10 && !pays;
                }
                // v5.6: a chamber with no pearl in it and none due within DRY_HORIZON is dry: leave now, not after ten
                // idle rounds (default's rooms spawn every 1-769 rounds; v5.5 waited up to 60 rounds for one pearl).
                bool dry = F_DRY_EVICT && !s.alpha && enc.pearls_now + enc.due_soon == 0;
                bool leave = enc.spawners == 0 || crowded || (idle && !s.alpha) || dry;
                s.dry_portal = dry && !crowded ? enc.portal_id : -1;
                if (F_REENTRY && !leave && enc.size <= SMALL_ENCLOSURE + 4) {
                    s.n_camp_tiles = enc.size;
                    for (int i = 0; i < enc.size; ++i) s.camp_tiles[i] = remembered_q[i];
                }
                if (!leave && 2 * enc.spawners >= enc.size && enc.pearls_now + enc.due_soon >= 2) chamber_spawners = enc.spawners;
                if (F_CAMP2 && !leave) {
                    s.camping = true;
                    if (enc.size > LOOP_CHAMBER) { // the loop chambers keep their own way in and out
                        s.resident = true;
                        s.home_portal = enc.portal_id;
                    }
                }
                DIAG("camp " << c.get_id() << ' ' << round << ' ' << enc.portal_id << " len " << c.get_length() << " since "
                             << s.camp_since << " friends " << enc.friend_ids[0] << ',' << enc.friend_ids[1] << " crowded "
                             << crowded << " idle " << idle << " next " << enc.next_pearl << " leave " << leave << " dry "
                             << dry << " now " << enc.pearls_now << " due " << enc.due_soon << " size " << enc.size);
                if (F_PORTAL_LOOP && !leave && dense && enc.size <= LOOP_CHAMBER) {
                    s.chamber_portal = enc.portal_id;
                    s.chamber_round = round;
                    s.chamber_size = enc.size;
                }
                s.evicting = leave;
                if (leave) {
                    s.resident = false;
                    evict_plan = portal_target(true);
                    if (evict_plan && distance[index(evict_plan->approach)] >= INF) evict_plan.reset();
                    if (!evict_plan && crowded && c.get_length() <= enc.longest_friend)
                        return {{DIRS[opp_dir]}, 0, Mode::Feed, true};
                } else if (round - s.contend_round >= (F_RESERVE_ALL && __builtin_popcount(enc.portal_mask) > 1 ? 5 : 10)) {
                    s.send_reserve = enc.portal_id; // still here: renew the reservation
                    if (F_RESERVE_ALL && enc.portal_mask) { // every portal of the chamber in turn
                        int k = s.resv_rr % 32;
                        for (int j = 0; j < 32; ++j, k = (k + 1) % 32)
                            if (enc.portal_mask >> k & 1) break;
                        s.send_reserve = k;
                        s.resv_rr = k + 1;
                    }
                    s.contend_portal = enc.portal_id;
                    s.contend_round = round;
                }
            } else {
                s.evicting = false;
            }
        }
        // v5.7 (a8): a resident that is not holding a paying chamber and has found nothing for ROAM_IDLE rounds, with no
        // pearl in view and none due within 10 rounds, is free again (v5.6 kept it a resident for good: no portal ever).
        if (F_PORTAL_ROAM && s.resident && !s.camping && !s.alpha && round - s.last_food > ROAM_IDLE) {
            bool soon = false;
            for (const auto &t : c.get_tiles())
                if (t.has_pearl() || (t.get_pearl_time() >= 0 && t.get_pearl_time() <= 10)) soon = true;
            if (!soon) {
                if (s.home_portal >= 0) {
                    auto &hp = portal(s.home_portal);
                    hp.occupied = hp.barren;
                    hp.occupied_until = round;
                }
                DIAG("unreside " << c.get_id() << ' ' << round << ' ' << s.home_portal);
                s.resident = false;
                s.home_portal = -1;
            }
        }
        // The camper holds its chamber to the end; the children it splits off carry the harvest to the apex.
        if (F_CAMP2 && s.camping && !s.alpha && round < 495) feed = false;
        patch_camp_update();
        if (F_MIRROR_SCOUT) find_hotspot(chamber_spawners);

        // Protective Suicide: if our team has many bots and a genuinely long friendly bot (visible length >= 6)
        // is cornered (escape_count <= 1) while this small bot (length <= 3) is blocking its front tile or adjacent
        // with another ally crowding it, immediately sacrifice into our neck to open an exit and drop pearls!
        // v5.7 (a5): in the endgame there is no unit-count gate (units fall as feeders deliver, which switched this off
        // exactly when the apex gets crowded).
        if (!s.alpha && c.get_length() <= 3 &&
            (c.get_unit_count() >= std::max(12, kamikaze_threshold() + 4) || (F_FEED_CLEAR && round >= feed_round()))) {
            for (const auto &f : friends) {
                int flen = friendly_visible_length(f.get_id());
                if (flen < 6) continue;
                int f_exits = escape_count(f.position);
                if (f_exits > 1) continue;
                Position f_front = step(f.position, di(f.get_dir()));
                auto t_front = c.get_tile(f_front);
                bool body_blocks_front = (t_front && t_front->get_dragon() && t_front->get_dragon()->get_id() == c.get_id());
                if (body_blocks_front || here == f_front)
                    return {{DIRS[opp_dir]}, 0, Mode::Feed, true};
                if (dist(here, f.position) == 1) {
                    int crowding_allies = 0;
                    for (const auto &other : friends)
                        if (other.get_id() != f.get_id() && dist(other.position, f.position) <= 2)
                            ++crowding_allies;
                    if (crowding_allies >= 1)
                        return {{DIRS[opp_dir]}, 0, Mode::Feed, true};
                }
            }
        }

        // No move survives this turn. Split so the 2-long head takes the hit and the rear L-2 lives on, born on
        // our tail facing away from the danger (v5.3 split 2 off the tail here, leaving the mass in the trap, or
        // did not split at all with the body across a portal). A feeder beside its apex drops its pearls instead.
        if (F_REVERSE_SPLIT && c.get_length() >= 4 && survival_moves() == 0 && c.can_split(c.get_length() - 2) &&
            !resplit_doomed()) {
            bool delivering = false;
            if (feed)
                for (const auto &f : friends)
                    if ((f.get_id() & SONAR_ID_MASK) == plan->id && chebyshev(here, f.position) <= 3) delivering = true;
            if (!delivering) {
                DIAG("rescue " << c.get_id() << ' ' << round << ' ' << c.get_length() << ' ' << (s.alpha ? 1 : 0) << ' '
                               << (straddling() ? 1 : 0) << " child " << rescue_child());
                return {{}, rescue_child(), Mode::Rescue, false};
            }
        }

        // v5.6: a camper pushed out through its chamber's portal walks round the partner edge and straight back in.
        if (F_REENTRY && s.reclaim_portal >= 0) {
            if (round > s.reclaim_until) {
                DIAG("reclaimfail " << c.get_id() << ' ' << round << ' ' << s.reclaim_portal);
                s.reclaim_portal = -1;
            } else if (int d = reentry_move(); d >= 0) {
                return {{DIRS[d]}, 0, Mode::Loop, false};
            }
        }

        // v5.6: inside a dead end of the map with pearls still ahead, keep eating: no routine split (v5.5 split 2 off at
        // length 4, and the 2-long head then ate one more pearl and died at the tip as a 3). The rescue split above
        // fires at the tip, the last turn we can act: the head (2) stays, the rear L-2 is born on our tail facing out.
        // With nothing left ahead, split now: going on only drags the tail deeper in.
        // v5.8 (F_FARM, b3.png): our tail is just off a farm's refilled spawners: send a 2-long child back in.
        // v5.9c (F_EXCHANGE): a strike that pays the team beats any split this turn (PD round 21: the dragon split instead).
        if (!feed)
            if (auto xs = exchange_strike()) return *xs;
        if (round < 480)
            if (auto fh = farm_harvest()) return *fh;

        // v5.9 (F_FARM_SPLIT): remember farming a dead end: our head is in one, or (born in a split) our body trails back into one.
        if (F_FARM_SPLIT) {
            int gg = 0, n = 0;
            int back = (di(c.get_dir()) + 2) % 4;
            if (in_dead_end(gg)) s.farm_recent = round;
            else if (round == s.born && s.cells[index(here)].edge[back] == 0 && dead_end_region(step(here, back), back, n, true))
                s.farm_recent = round;
        }
        bool choke_hold = false;
        // v5.7 (a7): our head is in (or at the mouth of) a dead end and our tail is boxed in too (every way on from the tail is
        // a wall or another dead end): the child of the split at the tip would be born there and walk into the other
        // pocket, for good (slithery_fight's spawn corridor: 100+ splits a game at one tile). Shed the tail in 2-long pieces
        // now: a 2-long piece eats its way to a tip with its own tail still at the junction, so its child walks out.
        // Checked on our birth turn too: a split child moves the round it is born, and only then is its whole body in view.
        if (F_CHOKE_LOOP && F_CHOKE && c.get_length() >= 4 && round > 0 && can_split_safe(2)) {
            int g0 = 0;
            if (in_dead_end(g0) && tail_trapped()) {
                DIAG("tailtrap " << c.get_id() << ' ' << round << " len " << c.get_length());
                return {{}, 2, Mode::Split, false};
            }
        }
        if (F_CHOKE_GREEDY && F_CHOKE && round > s.born) {
            int gain = 0;
            bool found = false;
            int d = -1;
            if (in_dead_end(gain)) {
                if (gain > 0) d = choke_step(found);
                if (gain > 0 && (found || c.get_length() < 4)) {
                    choke_hold = true;
                    DIAG("chokehold " << c.get_id() << ' ' << round << " len " << c.get_length() << " gain " << gain
                                      << " step " << d << " alpha " << s.alpha);
                    if (d >= 0) return {{DIRS[d]}, 0, Mode::Forage, false};
                } else if (c.get_length() >= 4 && can_split_safe(c.get_length() - 2) && !resplit_doomed()) {
                    DIAG("chokefinal " << c.get_id() << ' ' << round << " len " << c.get_length() << " alpha " << s.alpha);
                    return {{}, rescue_child(), Mode::Rescue, false};
                }
            }
        }

        // v5.9 (F_FARM_SPLIT, autarky (35,0)): just out of a dead end we farmed and longer than FARM_SPLIT_LEN: split 2 now,
        // cooldowns, the unit cap and the alpha's growing phase notwithstanding. Rear children of the tip split came out 5-8
        // long and stayed long (then kept out of the pocket); 2-long pieces go straight back in.
        if (F_FARM_SPLIT && (FARM_SPLIT_ALPHA || !s.alpha) && round > s.farm_recent && round - s.farm_recent <= FARM_SPLIT_WINDOW && !feed &&
            c.get_length() > FARM_SPLIT_LEN && round < feed_round() - 10 && can_split_safe(2) && child_viable() && !choke_hold) {
            DIAG("farmsplit " << c.get_id() << ' ' << round << " len " << c.get_length() << " alpha " << s.alpha << " out "
                              << round - s.farm_recent);
            return {{}, 2, Mode::Split, false};
        }
        if (!s.alpha && !s.growing && !feed && c.get_length() >= 4 && can_split_safe(2) && child_viable() && !choke_hold &&
            !split_blocked())
            return {{}, 2, Mode::Split, false};
#ifdef BOT_DIAG
        if (!s.alpha && !feed && c.get_length() >= 4)
            DIAG("nosplit " << c.get_id() << ' ' << round << " len " << c.get_length() << " growing " << s.growing << " can "
                            << c.can_split(2) << " straddle " << straddling() << " viable " << child_viable() << " choke "
                            << choke_hold << " blocked " << split_blocked());
#endif

        // Children born in a sealed nursery escape through its portal before anything else. (Other newborns
        // no longer race to claim portals first: foraging straight away grows the swarm faster.)
        if (!s.alpha && !s.resident && s.evacuating) {
            auto early = portal_target(false);
            if (early) {
                int d = early->approach == here ? early->direction : first[index(early->approach)];
                if (d >= 0 && is_step_safe(d)) return {{DIRS[d]}, 0, Mode::Portal, false};
            }
        }

        // Committed to a portal: an alpha that handed its role over on the way in, or the 2-long head of a
        // long dragon that split its mass off rather than take it through.
        if (s.capture_portal >= 0 && round <= s.capture_until && !feed) {
            auto cp = portal_target(true, true, s.capture_portal);
            if (cp) {
                int d = cp->approach == here ? cp->direction : first[index(cp->approach)];
                if (d >= 0 && is_step_safe(d)) return {{DIRS[d]}, 0, Mode::Portal, false};
            }
        }
        if (round > s.capture_until) s.capture_portal = -1;

        // Feeder delivery: suicide within the Alpha's 7x7 vision (chebyshev <= 3, Manhattan <= 4)
        if (feed && c.get_unit_count() > 1) {
            for (const auto &f : friends) {
                int gap = dist(here, f.position);
                int c_gap = chebyshev(here, f.position);
                if ((f.get_id() & SONAR_ID_MASK) != plan->id || c_gap > 3) continue;
                Position f_front = step(f.position, di(f.get_dir()));
                // v5.7 (a5): a feeder touching the apex's head, standing on its next tile, or within 2 while the apex has at
                // most 2 ways out, is boxing it in: drop our pearls now. (Round 472 on the ladder: a ring of small feeders
                // held back by the gates below walled the apex in and it died.)
                if (F_FEED_CLEAR && !s.alpha && round - s.mantle_round > MANTLE_GUARD &&
                    (body_leaves_view(f.get_id()) || friendly_visible_length(f.get_id()) > c.get_length())) {
                    int f_exits = escape_count(f.position);
                    if (gap <= 1 || here == f_front || (gap <= 2 && f_exits <= 2)) {
                        DIAG("unblock " << c.get_id() << ' ' << round << " gap " << gap << " exits " << f_exits);
                        return {{DIRS[opp_dir]}, 0, Mode::Feed, true};
                    }
                }
                // v5.5: never die into a dragon we can see whole and that is no longer than us (a 2-long head that kept an
                // old alpha ID), and not at all for MANTLE_GUARD rounds after taking the role in a split. (F_MANTLE_PROMOTE:
                // the longer feeder takes the role itself; off, it cost slithery_fight.)
                if (F_MANTLE) {
                    bool shorter = !body_leaves_view(f.get_id()) && friendly_visible_length(f.get_id()) <= c.get_length();
                    // v5.8: an apex known to be clearly longer than us is fed at once, even just after we took a role in a split
                    // (endgame rescue splits hand the role on every few rounds; each new holder waited 40 rounds).
                    bool clearly = F_FEED_UNGUARD && plan->len >= c.get_length() + 4 && body_leaves_view(f.get_id());
                    if ((round - s.mantle_round <= MANTLE_GUARD && !clearly) || shorter) {
                        DIAG("mantleguard " << c.get_id() << ' ' << round << " len " << c.get_length() << " tgt " << plan->id
                                            << " vis " << friendly_visible_length(f.get_id()) << " alpha " << s.alpha
                                            << " took " << (round - s.mantle_round));
                        if (F_MANTLE_PROMOTE && shorter && !s.alpha && c.get_length() >= 4) {
                            s.alpha = true;
                            s.growing = true;
                            s.mantle_round = round;
                        }
                        break;
                    }
                }
                // Only drop our pearls where the apex will eat them: it must look like a real apex (not a
                // 2-long dragon still carrying an old alpha ID), be able to walk to us, have the time to, and
                // have no enemy head closer to the drop than itself. Otherwise keep walking toward it.
                if (F_FEED_GATE) {
                    bool reach = gap <= 2 || !F_GATE_REACH;
                    int best_k = INF;
                    for (int d = 0; d < 4; ++d) {
                        Position n = step(f.position, d);
                        auto tn = c.get_tile(n);
                        bool ours = tn && tn->get_dragon() && tn->get_dragon()->get_id() == c.get_id();
                        best_k = std::min(best_k, ours ? 0 : distance[index(n)]);
                    }
                    if (best_k <= gap + 2) reach = true;
                    bool contested = false;
                    for (const auto &e : enemies)
                        if (dist(e.position, here) <= gap) contested = true;
                    bool real = friendly_visible_length(f.get_id()) >= 3 || body_leaves_view(f.get_id());
                    bool in_time = 500 - round >= gap + 2;
                    if (!(reach && real && in_time && !contested)) {
                        DIAG("feedgate " << c.get_id() << ' ' << round << ' ' << reach << real << in_time << contested << ' '
                                         << gap << ' ' << best_k << ' ' << friendly_visible_length(f.get_id()) << ' '
                                         << c_gap << ' ' << plan->len);
                        break;
                    }
                }
                // The apex already has a pile of pearls around it: hold off (circling nearby, off its pearls
                // and out of its way) until it has eaten them, unless time is running out. Every feeder still
                // travels as soon as feeding starts (consolidating early is what wins stronghold); length only
                // sets how big a pile makes it wait. Long feeders drop almost at once, short ones and ex-
                // kamikazes wait for the apex to clear its pile first.
                int pile_limit = !F_FEED_ADAPT ? 5 : c.get_length() >= 6 ? 9 : c.get_length() >= 4 ? 5 : 3;
                // v5.9d: F_FEED_BACKOFF alone switches holding (it read F_FEED_BACKOFF || F_FEED_ADAPT, so the v5.2 ablation of
                // F_FEED_BACKOFF left holding on and measured exactly 0.0); F_FEED_ADAPT only sets the pile limit by length.
                if (F_FEED_BACKOFF && 500 - round > c.get_length() + (F_FEED_CLEAR ? LATE_FEED : 10)) {
                    int piled = 0;
                    for (const auto &t : c.get_tiles())
                        if (t.has_pearl() && chebyshev(t.get_position(), f.position) <= 3) ++piled;
                    if (piled >= pile_limit) {
                        // The apex is full for now: a value trade beats circling.
                        if (F_SURPLUS)
                            if (auto k = guaranteed_kill(false, true)) return *k;
                        int hold = -1;
                        double hold_score = -1e9;
                        for (int d = 0; d < 4; ++d) {
                            if (s.cells[index(here)].edge[d] != 0 || !is_step_safe(d)) continue;
                            auto n = step(here, d);
                            auto tile = c.get_tile(n);
                            if (!tile || tile->has_pearl() || n == f_front || danger(n) > 0) continue;
                            if (forward_escape_count(n, d) == 0 || is_dead_end_trap(n, d)) continue;
                            int k = dist(n, f.position);
                            if (k < 2) continue;
                            double sc = -std::abs(k - 4) + 0.5 * escape_count(n);
                            if (sc > hold_score) { hold_score = sc; hold = d; }
                        }
                        DIAG("feedhold " << c.get_id() << ' ' << round << " piled " << piled << " limit " << pile_limit << " len "
                                         << c.get_length() << " gap " << gap << " apex " << plan->id << " hold " << hold);
                        if (hold >= 0) return {{DIRS[hold]}, 0, Mode::Feed, false};
                    }
                }
                if (gap > 2 && round < 490) {
                    for (int d = 0; d < 4; ++d) {
                        if (s.cells[index(here)].edge[d] != 0 || !is_step_safe(d)) continue;
                        auto n = step(here, d);
                        if (n == f_front) continue;
                        auto tile = c.get_tile(n);
                        if (tile && !tile->has_pearl() && danger(n) == 0 && dist(n, f.position) < gap)
                            return {{DIRS[d]}, 0, Mode::Feed, false};
                    }
                }
#ifdef BOT_DIAG
                {
                    int piled = 0;
                    for (const auto &t : c.get_tiles())
                        if (t.has_pearl() && chebyshev(t.get_position(), f.position) <= 3) ++piled;
                    DIAG("feeddrop " << c.get_id() << ' ' << round << " apex " << f.get_id() << " len " << c.get_length()
                                     << " gap " << gap << " piled " << piled << " limit " << pile_limit);
                }
#endif
                return {{DIRS[opp_dir]}, 0, Mode::Feed, true};
            }
        }

        // Zero-loss neck block beats every trade: take it whenever it is on offer.
        if (!feed) {
            if (auto nb = neck_block()) {
                Position land = here;
                for (auto d : nb->moves) land = step(land, di(d));
                if (!s.alpha || danger(land) < 280) return *nb;
            }
        }

        // Split-and-kamikaze: facing a longer enemy head up close, keep the rear L-2 segments safe as a
        // child (it spawns away from the threat and vacuums the pearls afterwards) and turn the 2-segment
        // front into a hunter that goes for the enemy's head.
        if (!feed && round > s.born && c.get_length() >= 5 &&
            can_split_safe(c.get_length() - 2)) {
            for (const auto &e : enemies) {
                int elen = enemy_visible_length(e.get_id());
                if (elen <= c.get_length() || dist(e.position, here) > 3) continue;
                // v5.4 dropped the role here, before execute() could announce it: the team kept feeding our ID.
                if (!F_MANTLE) {
                    s.alpha = false;
                    s.growing = false;
                }
                s.hunter_until = round + 24;
                return {{}, c.get_length() - 2, Mode::Split, false};
            }
        }

        // A 2-long dragon rams any longer enemy head it can reach this turn: one pearl's worth for two or more.
        if (skirmisher() && !(iso && ISO_STRIKES == 0) && (F_KAM_CAP ? over_cap() : c.get_unit_count() >= 3)) {
            for (const auto &e : enemies) {
                if (enemy_visible_length(e.get_id()) < 3 && e.get_id() > 1) continue;
                for (int d = 0; d < 4; ++d) {
                    if (d == opp_dir) continue;
                    auto dest = destination(here, d);
                    if (dest && *dest == e.position && is_step_safe(d, true) &&
                        strike_protected(e.position, e.get_id(), enemy_visible_length(e.get_id()))) {
                        DIAG("ram " << c.get_id() << ' ' << round << ' ' << enemy_visible_length(e.get_id()));
                        return {{DIRS[d]}, 0, Mode::Kill, true};
                    }
                }
            }
        }

        if (!feed)
            if (auto as = assassin_strike()) {
                DIAG("assassin " << c.get_id() << ' ' << round << " len " << c.get_length() << " steps " << as->moves.size());
                return *as;
            }

        // 0. High-value kill — for Kamikaze or length-3 Sprint Hunters above threshold
        if (kam || sprint_hunter) {
            if (auto hv_kill = guaranteed_kill(true, false)) return *hv_kill;
        }

        int mscale = map_scale();
        bool medium_map = (mscale >= 20 && mscale <= 35);
        bool small_map_splitting_alpha = (s.alpha && mscale < 20 && !s.growing);

        // 1. Splitting
        if (!s.growing && !feed && c.get_length() >= 4 && can_split_safe(2) && child_viable() && !choke_hold &&
            !split_blocked()) {
            return {{}, 2, Mode::Split, false};
        }
        if (!feed && !choke_hold)
            if (auto hv = back_harvest()) return *hv;

        auto pearls = food_target(false);

        // v5.7 (a2): two teammates side by side for PAIR_TURNS turns with nothing to eat travel as one and search the same
        // ground. The higher ID turns away from the other.
        if (F_PAIR_SEP && !s.alpha && !feed && round < feed_start) {
            const DragonPart *mate = nullptr;
            for (const auto &f : friends)
                if (!is_alpha(f.get_id()) && chebyshev(here, f.position) <= 2 && (!mate || f.get_id() < mate->get_id()))
                    mate = &f;
            s.pair_turns = mate ? s.pair_turns + 1 : 0;
            if (mate && s.pair_turns >= PAIR_TURNS && !pearls && c.get_id() > mate->get_id()) {
                set_heading_away(-delta(here.x, mate->position.x, w), -delta(here.y, mate->position.y, h));
                s.pair_turns = 0;
                s.goal = -1;      // a scout spot or rendezvous we share with it: let it have it
                s.rdv_goal = -1;
                s.rem_until = -1000;
                DIAG("pairsep " << c.get_id() << ' ' << round << " from " << mate->get_id());
            }
        }

        // 2. Standard kill — Kamikazes take all valid kills; Sprint Hunters take kills when no pearl is adjacent; non-Alphas with >= 3 units take safe 1-exit corridor traps & giant snipes!
        if (!s.alpha && !feed) {
            if (kam || (sprint_hunter && !pearls)) {
                if (auto kill = guaranteed_kill(false, false)) return *kill;
            } else if (c.get_unit_count() >= 3) {
                if (auto safe_kill = guaranteed_kill(false, true)) return *safe_kill;
            }
            // v5.8 (F_EARLY_KILL): from round 1, a sure kill on an enemy at least as long as us with a teammate beside the
            // collision: it eats both drops.
            if (F_EARLY_KILL && c.get_unit_count() >= 2)
                if (auto ek = guaranteed_kill(false, false, true)) {
                    DIAG("earlykill " << c.get_id() << ' ' << round << " len " << c.get_length() << " steps " << ek->moves.size());
                    return *ek;
                }
        } else if (F_SURPLUS && !s.alpha && feed && c.get_unit_count() >= 3) {
            // Feeders on their way still take zero-loss traps and trades against clearly longer enemies.
            if (auto safe_kill = guaranteed_kill(false, true)) return *safe_kill;
        }

        bool allow_enemy_head = kam;
        std::vector<int> legal, dead_end;
        for (int d = 0; d < 4; ++d) {
            if (!is_step_safe(d, allow_enemy_head)) continue;
            if (F_CHOKE && (F_CHOKE2 || c.get_length() > 2) && s.cells[index(here)].edge[d] == 0 &&
                static_dead_end(step(here, d), d))
                dead_end.push_back(d);
            else
                legal.push_back(d);
        }
        if (F_CHOKE && legal.empty() && !dead_end.empty()) {
            // v5.6: forced to the mouth of a dead end with pearls in it: go in and eat them. The rescue split at the tip
            // then keeps L + pearls - 2 outside, where splitting here kept only L - 2.
            if (F_CHOKE_GREEDY) {
                int enter = -1, most = 0;
                for (int d : dead_end) {
                    int pp = 0, psz = 0, pact = 0;
                    sealed_pocket(step(here, d), d, pp, psz, &pact, true);
                    if (pp > most) { most = pp; enter = d; }
                }
                if (enter >= 0) {
                    DIAG("chokeenter " << c.get_id() << ' ' << round << " len " << c.get_length() << " gain " << most);
                    return {{DIRS[enter]}, 0, Mode::Forage, false};
                }
            }
            // Only dead ends ahead: the mass stays out. Split so the rear L-2 is born on our tail facing back out,
            // and only the 2-long head goes on. (A 3-long dragon cannot split: it takes the least bad dead end.)
            if (c.get_length() >= 4 && can_split_safe(c.get_length() - 2) && !resplit_doomed()) {
                DIAG("chokesplit " << c.get_id() << ' ' << round << ' ' << c.get_length() << ' ' << s.alpha);
                return {{}, c.get_length() - 2, Mode::Rescue, false};
            }
            legal = dead_end;
        }

        bool threatened = ((s.alpha && !small_map_splitting_alpha) || guard) &&
                          ((incoming() && danger(here) >= 240) || danger(here) >= 280),
             perpendicular = false;
        // No legal move, but a portal we would rather not use still saves us: take it (below), don't split.
        bool no_way = legal.empty() && !(F_REVERSE_SPLIT && survival_moves() > 0);
        bool has_safe_escape = false;
        for (int d : legal) {
            if (danger(step(here, d)) < 240) has_safe_escape = true;
            if ((d % 2) != (di(c.get_dir()) % 2) && danger(step(here, d)) < 240)
                perpendicular = true;
        }

        if (medium_map) {
            if (s.born < round && can_split_safe(c.get_length() - 2) && !resplit_doomed() &&
                (no_way || ((s.alpha || guard) && (incoming() || danger(here) >= 320) && !has_safe_escape && !perpendicular)))
                return {{}, c.get_length() - 2, Mode::Split, false};
        } else {
            bool perp_safe = false;
            for (int d : legal)
                if ((d % 2) != (di(c.get_dir()) % 2) && danger(step(here, d)) < 240)
                    perp_safe = true;
            if (can_split_safe(c.get_length() - 2) && !resplit_doomed() && (no_way || (threatened && !perp_safe)))
                return {{}, c.get_length() - 2, Mode::Split, false};
        }

        // Portal loop: walked out of a dense tiny chamber (a 2x2 room pushes us out every few steps). Walk round
        // the partner edge and cross it again into the same chamber, as long as our body still fits the loop.
        if (F_PORTAL_LOOP && s.loop_portal >= 0 && round <= s.loop_until && !feed && !threatened &&
            c.get_length() <= s.chamber_size + 3) {
            auto lp = portal_target(true, true, s.loop_portal);
            if (lp && lp->cost <= 6) {
                int d = lp->approach == here ? lp->direction : first[index(lp->approach)];
                if (d >= 0 && is_step_safe(d)) return {{DIRS[d]}, 0, Mode::Loop, false};
            }
        }

        auto future = pearls ? std::optional<Position>{} : food_target(true);
        std::optional<Position> rem_pearl;
        if (!pearls && !future && !feed) {
            // v5.7: a pile of pearls due on a timetable we saw beats a single remembered pearl.
            if (F_RENDEZVOUS && !threatened && !s.camping && !s.evacuating) rem_pearl = rendezvous_target();
            if (!rem_pearl && F_ALPHA_MEMORY && s.alpha && !small_map_splitting_alpha) {
                rem_pearl = alpha_memory_pick();
                if (rem_pearl) DIAG("amem " << c.get_id() << ' ' << round << ' ' << rem_pearl->x << ',' << rem_pearl->y);
            }
            if (!rem_pearl) rem_pearl = committed_pearl_target();
            if (F_PATCH_CAMP && s.patch_camp && rem_pearl && !in_patch_tiles(*rem_pearl)) rem_pearl.reset();
        } else if (F_RENDEZVOUS && s.rdv_goal >= 0 && pearls) {
            s.rdv_goal = -1; // food in view comes first; the slot stays for later
        }
        // A loop breakout ignores remembered and upcoming pearls: they are what kept pulling us round.
        if (F_CYCLE2 && round < s.cycle_break_until && !feed) {
            future.reset();
            rem_pearl.reset();
        }
        auto port = (!s.alpha || small_map_splitting_alpha) ? portal_target(false) : std::optional<PortalPlan>{};
#ifdef BOT_DIAG
        if (!port && !s.alpha && !pearls && !future && !rem_pearl)
            for (const auto &t : c.get_tiles())
                for (int d = 0; d < 4; ++d) {
                    Position p = t.get_position();
                    int e = s.cells[index(p)].edge[d];
                    if (e <= 0) continue;
                    const auto &pp = portal_const(e - 1);
                    DIAG("noport " << c.get_id() << ' ' << round << " pid " << e - 1 << " at " << p.x << ',' << p.y << " dist "
                                   << distance[index(p)] << " occ " << portal_occupied(e - 1) << " barren " << pp.barren
                                   << " shun " << (round < pp.shun_until) << " res " << s.resident << " dead "
                                   << portal_into_dead_cell(p, d) << " long " << long_body() << " exitdragon "
                                   << (destination(p, d) && c.get_tile(*destination(p, d)) &&
                                       c.get_tile(*destination(p, d))->get_dragon()) << " resv "
                                   << (round <= pp.reserved_until) << " occu " << pp.occupied << ' ' << pp.occupied_until
                                   << " idle " << (round - s.last_food) << " camp " << s.camping << " home " << s.home_portal);
                    goto noport_done;
                }
    noport_done:
#endif

        // Early portal capture: before ALPHA_PORTAL_ROUND an alpha that is the closest teammate to a free
        // portal takes it. One step out it hands the role to the longest teammate in view (or releases it
        // if none is in view) and becomes an ordinary dragon; a long alpha sends only a 2-long head in.
        bool alpha_capture = false;
        if (F_ALPHA_PORTAL && s.alpha && round < ALPHA_PORTAL_ROUND && !feed && !threatened &&
            c.get_unit_count() > 1 && !(pearls && distance[index(*pearls)] <= 1)) {
            auto ap = portal_target(false, true);
            if (ap && ap->cost <= 6) {
                int mine = dist(here, ap->approach);
                bool closest = true;
                for (const auto &f : friends) {
                    int k = dist(f.position, ap->approach);
                    if (k < mine || (k == mine && f.get_id() < c.get_id())) closest = false;
                }
                if (closest) {
                    port = ap;
                    alpha_capture = true;
                }
            }
        }
        if (alpha_capture && long_body() && port->approach == here && can_split_safe(c.get_length() - 2)) {
            s.capture_portal = port->id;
            s.capture_until = round + 4;
            return {{}, c.get_length() - 2, Mode::Split, false};
        }
        if (alpha_capture && !long_body() && port->cost <= 2) {
            int best_id = -1, best_len = -1;
            for (const auto &f : friends) {
                int flen = friendly_visible_length(f.get_id());
                if (flen > best_len || (flen == best_len && f.get_id() < best_id)) { best_len = flen; best_id = f.get_id(); }
            }
            if (is_primary_alpha_id(c.get_id())) s.primary_demoted = true;
            s.alpha = false;
            s.growing = false;
            s.handover_to = best_id >= 0 ? (best_id & 4095) : 4095;
            s.handover_old = c.get_id();
            s.handover_round = round;
            s.capture_portal = port->id;
            s.capture_until = round + 6;
        }

        if (feed && pearls && alpha && dist(*pearls, *alpha) <= 5) pearls = std::nullopt;

        bool early_portal = (!s.alpha || small_map_splitting_alpha) && s.evacuating && !s.resident;
        // A free portal a few steps away beats a remembered or upcoming pearl elsewhere: v5.2 walked off
        // after rumours of food with an open portal right beside it.
        bool use_portal = port && ((!pearls && !future && !rem_pearl) || early_portal || alpha_capture ||
                                   (F_PORTAL_FIX && !pearls && !future && port->cost <= 3));

        std::optional<Position> target = pearls ? pearls : (future ? future : rem_pearl);
        Mode mode = s.resident ? Mode::Reside : Mode::Forage;

        if (feed) {
            // v5.9b (F_FEED_EFF, portals): a feeder drops ceil(L / 2) pearls, so a pearl eaten at an even length is one more
            // pearl for the apex and one eaten at an odd length is lost (v5.1-v5.9 grabbed at odd lengths).
            bool grab_adjacent_pearl = pearls && (c.get_length() % 2 == (F_FEED_EFF ? 0 : 1)) && distance[index(*pearls)] == 1 &&
                                       (!alpha || dist(*pearls, *alpha) > 5);
            if (!grab_adjacent_pearl) {
                target = alpha;
                mode = Mode::Feed;
            }
            use_portal = false;
        }
        if (evict_plan) {
            port = evict_plan;
            use_portal = true;
            target = std::nullopt;
        }
        if (use_portal) {
            mode = Mode::Portal;
            DIAG("useportal " << c.get_id() << ' ' << round << " born " << s.born << " pid " << port->id << " barren "
                              << portal_const(port->id).barren << " cost " << port->cost << " len " << c.get_length());
            // Announce the portal we are about to take; a lower-ID teammate reaching for it wins the tie.
            if (F_PORTAL_RESERVE && !s.evicting && port->id < 32 && distance[index(port->approach)] <= 3) {
                s.send_reserve = port->id;
                s.contend_portal = port->id;
                s.contend_round = round;
            }
            if (port->approach == here) {
                if (is_step_safe(port->direction, allow_enemy_head)) {
                    s.pending_portal = port->id;
                    occupy(port->id);
                    return {{DIRS[port->direction]}, 0, mode, false};
                }
            } else target = port->approach;
        }
        if (retreating && !use_portal && !evict_plan &&
            !(pearls && distance[index(*pearls)] <= 2 && danger(*pearls) < 280)) {
            target = s.spawn;
            mode = Mode::Escape;
        }
        if (threatened) mode = Mode::Escape;

        // Apex rendezvous: a yielding alpha carries far more mass than any single pearl, so the apex
        // walks toward it instead of foraging away while it tries to catch up.
        if (receiver_alpha && s.alpha && round >= feed_start && !threatened &&
            !(pearls && distance[index(*pearls)] <= 2)) {
            double best_gain = 0;
            for (const auto &a : s.alphas) {
                if (round - a.seen > 10 || a.len < 5 || superior_alpha(a.id, a.len)) continue;
                int k = dist(here, a.p);
                if (k < 3 || k > 24) continue;
                double gain = a.len - 0.5 * k;
                if (gain > best_gain) { best_gain = gain; target = a.p; }
            }
        }

        // During endgame (round >= 415), Alpha stays near approaching feeders instead of marching away
        if (receiver_alpha && round >= feed_start && !target && !threatened) {
            int best_f = INF;
            for (const auto &f : friends) {
                int df = dist(here, f.position);
                if (df >= 2 && df < best_f) {
                    best_f = df;
                    target = f.position;
                }
            }
        }

        // v5.8: nothing to eat in view and no errand: a farm nobody works (ours, or its mirror image) comes first.
        if (F_FARM_SEEK && !target && !feed && !threatened && !s.alpha && !s.resident && !s.camping && !s.evacuating &&
            !s.patch_camp &&
            round < feed_start)
            if (auto g = farm_goal_target()) {
                target = *g;
                mode = Mode::Disperse;
            }
        // v5.6: nothing to eat in view and no errand: walk to a mirrored hotspot a teammate reported.
        if (F_MIRROR_SCOUT && !target && !feed && !threatened && !s.alpha && !s.resident && !s.camping && !s.patch_camp &&
            !s.evacuating && c.get_length() <= 3 && round < feed_start && !(uniform_map() && !F_POCKET_SEEK)) {
            if (auto g = scout_goal()) {
                target = *g;
                mode = Mode::Disperse;
            }
        }
        // v5.7 (a1): a crowd (CROWD_HEADS or more other non-alpha heads within 4) shares whatever is here; one member at a
        // time heads for a known rich spot at least 6 away, even with food in view (trophy: seven dragons sat in one handle
        // all game while the cup and the far handle filled up).
        if (F_HOTSPOT2 && F_MIRROR_SCOUT && !feed && !retreating && !threatened && !s.alpha && !s.resident && !s.camping && s.farm_goal < 0 &&
            !s.patch_camp &&
            !s.evacuating && c.get_length() <= 4 && round < feed_start && !(uniform_map() && !F_POCKET_SEEK) &&
            !(pearls && distance[index(*pearls)] <= 2)) {
            // One leaver per crowd and turn, the highest ID: members leaving together all picked the same nearest spot and
            // then travelled side by side (the leaver's claim reaches the rest by sonar, so the next one picks another).
            int crowd = 0;
            bool highest = true;
            for (const auto &f : friends)
                if (!is_alpha(f.get_id()) && dist(here, f.position) <= 4) {
                    ++crowd;
                    if (f.get_id() > c.get_id()) highest = false;
                }
            // ...and only when what is here does not go round: fewer pearls in view than dragons in the crowd (with us).
            // A crowd in the richest spot on the map (trophy's cup) stays.
            int here_pearls = 0;
            for (const auto &t : c.get_tiles()) here_pearls += t.has_pearl() && !t.get_dragon();
            bool poor = here_pearls <= crowd;
            if ((crowd >= CROWD_HEADS && highest && poor) || s.goal >= 0)
                if (auto g = scout_goal(6, here_pearls)) {
                    target = *g;
                    mode = Mode::Disperse;
                    DIAG("crowdgo " << c.get_id() << ' ' << round << " crowd " << crowd << " to " << g->x << ',' << g->y);
                }
        }

        // v5.7: a long teammate is about to trade with an intruder: be there to eat the drop.
        if (!target && !feed && !threatened)
            if (auto cp = convergence_point()) {
                target = *cp;
                DIAG("converge " << c.get_id() << ' ' << round << ' ' << cp->x << ',' << cp->y);
            }

        bool patrol_only = false;
        if (!target && !feed && !threatened && !(receiver_alpha && round >= feed_start && !friends.empty()) &&
            dist(here, s.sector_target) > 2) {
            target = exploration_target();
            if (!target) target = s.sector_target;
            mode = s.dispersing ? Mode::Disperse : Mode::Forage;
            patrol_only = true;
        }

        // Ambush — Kamikaze bots above the map-scaled or adaptive threshold engage
        // v5.9c (F_COLLECT): a hunter with a fresh death drop within reach eats it first (nobody nearer claims it).
        bool collecting = false;
        if (F_COLLECT && (kam || sprint_hunter) && !feed && !use_portal)
            if (auto dp = fresh_drop()) {
                target = *dp;
                mode = Mode::Forage;
                patrol_only = false;
                collecting = true;
            }
        if (!collecting && (kam || sprint_hunter) && !feed && !use_portal && !(pearls && distance[index(*pearls)] <= 1))
            if (int sd = support_move(); sd >= 0) return {{DIRS[sd]}, 0, Mode::Ambush, false};
        if (!collecting && kam && !feed && !use_portal) {
            if (F_HERD && !(pearls && distance[index(*pearls)] <= 1) && c.get_unit_count() >= 3)
                if (int hd = herd_move(); hd >= 0) return {{DIRS[hd]}, 0, Mode::Ambush, false};
            for (auto e : enemies) {
                bool is_enemy_alpha = (e.get_id() <= 1 || enemy_visible_length(e.get_id()) >= 4);
                if (pearls && !is_enemy_alpha) continue;

                int ed = di(e.get_dir());
                if (dist(step(e.position, ed), here) >= dist(e.position, here)) continue;
                if (e.get_dir() == c.get_dir()) continue;
                if (s.cells[index(e.position)].edge[ed] < 0) continue;

                // v5.9c (F_FLANK): come down the lane beside its path and turn into it from the side, not face to face.
                if (F_FLANK)
                    if (auto fl = flank_target(e)) {
                        target = *fl;
                        mode = Mode::Ambush;
                        patrol_only = false;
                        break;
                    }
                auto aim = step(e.position, ed);
                if (empty(aim) && distance[index(aim)] < INF) { target = aim; mode = Mode::Ambush; patrol_only = false; break; }
                auto lateral = step(aim, (ed + ((c.get_id() % 2) ? 1 : 3)) % 4);
                if (empty(lateral) && distance[index(lateral)] < INF) { target = lateral; mode = Mode::Ambush; patrol_only = false; break; }
            }
            if (mode != Mode::Ambush && !pearls && round - s.enemy_alpha_seen <= 8 &&
                dist(here, s.enemy_alpha_pos) <= 9) {
                target = s.enemy_alpha_pos;
                mode = Mode::Ambush;
                patrol_only = false;
            }
        }

#ifdef BOT_DIAG
        {
            const char *kind = !target ? "none" : feed && mode == Mode::Feed ? "feed" : use_portal ? "portal"
                             : mode == Mode::Ambush ? "ambush" : pearls && target == pearls ? "pearl"
                             : future && target == future ? "future" : rem_pearl && target == rem_pearl
                             ? (s.rdv_goal >= 0 ? "rdv" : "rem") : patrol_only ? "explore" : "other";
            DIAG("tgt " << c.get_id() << ' ' << round << ' ' << kind << ' ' << (target ? target->x : -1) << ','
                        << (target ? target->y : -1) << " tdir " << (target ? route_first(*target) : -1) << " res "
                        << s.resident << " camp " << s.camping);
        }
#endif
        int target_direction = target ? route_first(*target) : -1;
        bool direct_feed = feed && target && target_direction >= 0;
        if (target && target_direction < 0) target_direction = remembered_direction(*target);

        // Direct BFS path for pearl/portal/visible-feed targets (avoiding pearls reserved for Alpha & friendly sole-exit traps)
        bool alpha_safe_fastpath = s.alpha && (pearls || future) && target_direction >= 0 &&
                                   danger(step(here, target_direction)) < 280;
        // v5.9d: F_CYCLE2's breakout (set by either detector) steers here too; with F_CYCLE off this used to switch off the
        // breakout steering of F_CYCLE2 as well, so the F_CYCLE ablation measured both modules.
        bool breaking_cycle = (F_CYCLE || F_CYCLE2) && round < s.cycle_break_until;
        if ((!s.alpha || feed || small_map_splitting_alpha || alpha_safe_fastpath) && target &&
            (pearls || future || rem_pearl || use_portal || direct_feed) &&
            !(breaking_cycle && !pearls && !use_portal && !direct_feed) &&
            (use_portal || direct_feed || std::count(s.recent_path.begin(), s.recent_path.end(), here) < 3) &&
            target_direction >= 0 && is_step_safe(target_direction, allow_enemy_head)) {
            Position next_td = step(here, target_direction);
            auto tile_td = c.get_tile(next_td);
            bool steals_alpha_pearl = (feed && alpha && tile_td && tile_td->has_pearl() && dist(next_td, *alpha) <= 5);
            bool blocks_alpha_head = false;
            for (const auto &f : friends) {
                if (!s.alpha && (is_alpha(f.get_id()) || friendly_visible_length(f.get_id()) >= 6) &&
                    next_td == step(f.position, di(f.get_dir()))) {
                    blocks_alpha_head = true;
                    break;
                }
                if (dist(next_td, f.position) == 1 && escape_count(f.position) <= 1) {
                    blocks_alpha_head = true;
                    break;
                }
            }
            int edge_td = s.cells[index(here)].edge[target_direction];
            bool certain_death_step = edge_td == 0 && mode != Mode::Ambush &&
                                      (is_dead_end_trap(next_td, target_direction) ||
                                       (!bold && is_enemy_certain_death(next_td, target_direction)) ||
                                       chokepoint_blocked(next_td, target_direction));
            // Tiles with a portal edge are where dragons pop out blind; only step on one to use the
            // portal or to take a pearl sitting on it.
            bool portal_loiter = F_PORTAL_CLEAR && !use_portal && edge_td == 0 && portal_adjacent(next_td) &&
                                 !(tile_td && tile_td->has_pearl()) && next_td != *target;
            // v5.8 (F_ISOLATED): alone, a step an enemy head can reach goes to the scorer, which weighs the risk.
            bool iso_risky = iso && !s.alpha && edge_td == 0 && danger(next_td) >= (iso_early ? ISO_RISK_EARLY : ISO_RISK);
            // v5.9 (F_EXIT_CLEAR): a chamber's exit tile is only for crossing its portal; anything else goes to the scorer.
            if (EXIT_CLEAR_ON && !use_portal && edge_td == 0 && exit_lane(next_td)) iso_risky = true;
            // v5.9c (F_DODGE): a step an enemy would profit from ramming goes to the scorer.
            if (F_DODGE && dodger && edge_td == 0 && strike_risk(next_td) >= DODGE_DIRECT) iso_risky = true;
            // v5.9c (F_FLANK): an ambushing kamikaze does not walk down an enemy's line toward it.
            if (F_FLANK && kam && mode == Mode::Ambush && edge_td == 0 && on_enemy_line(next_td)) iso_risky = true;
            // v5.9b: a corridor a teammate is coming along (F_LANE), a tile a teammate comes out of a portal onto
            // (F_PORTAL_PROBE), the mouth of a dead end a teammate is in (F_MOUTH_CLEAR): the scorer weighs them.
            if (edge_td == 0 && (lane_occupied(next_td, target_direction) || avoided(next_td) ||
                                 (busy_mouth(next_td) && !(tile_td && tile_td->has_pearl()))))
                iso_risky = true;
            if (!steals_alpha_pearl && !blocks_alpha_head && !certain_death_step && !portal_loiter && !iso_risky &&
                (edge_td > 0 || (escape_count(next_td) >= 1 && forward_escape_count(next_td, target_direction) >= 1)))
                return {{DIRS[target_direction]}, 0, mode, false};
            DIAG("directfail " << c.get_id() << ' ' << round << " steal " << steals_alpha_pearl << " block "
                               << blocks_alpha_head << " death " << certain_death_step << " loiter " << portal_loiter);
        }

        double best = -1e30;
        int chosen = -1;
        int cur_dir = di(c.get_dir());

        // Friendly bodies in view, and whether we are queueing behind one in single file.
        std::array<Position, 49> fb;
        int n_fb = 0;
        bool following = false;
        if (F_DISPERSE && !s.alpha && !feed) {
            for (const auto &t : c.get_tiles()) {
                auto part = t.get_dragon();
                if (part && part->get_team() == c.get_team() && part->get_id() != c.get_id() && !part->is_head() &&
                    n_fb < 49)
                    fb[n_fb++] = t.get_position();
            }
            Position a1 = step(here, cur_dir), a2 = step(a1, cur_dir);
            for (int i = 0; i < n_fb; ++i)
                if (fb[i] == a1 || fb[i] == a2) following = true;
        }

        // Non-alpha teammate heads nearby: the repulsion field below keeps the swarm spread out.
        std::array<Position, 16> rh;
        int n_rh = 0;
        if (F_REPEL && !s.alpha && !feed && round < feed_start)
            for (const auto &f : friends)
                if (!is_alpha(f.get_id()) && dist(here, f.position) <= 5 && n_rh < 16) rh[n_rh++] = f.position;

        for (int d : legal) {
            auto dest_opt = destination(here, d);
            bool blind_portal = false;
            if (!dest_opt) {
                // A portal whose far end we have never seen: still a way out, scored as open ground.
                if (!F_PORTAL_FIX || s.cells[index(here)].edge[d] <= 0) continue;
                blind_portal = true;
            }
            auto n = blind_portal ? step(here, d) : *dest_opt;
            auto tile = blind_portal ? nullptr : c.get_tile(n);
            int exits = blind_portal ? 2 : escape_count(n), f_exits = blind_portal ? 2 : forward_escape_count(n, d),
                area = blind_portal ? 10 : space(n);
            int risk = (s.alpha || iso || guard) ? danger(n) : 0;
            bool trap_step = s.cells[index(here)].edge[d] == 0 && mode != Mode::Ambush &&
                             (is_dead_end_trap(n, d) || (!bold && is_enemy_certain_death(n, d)));

            bool endgame_safe = (round >= feed_start && risk == 0 && f_exits >= 1);
            bool has_food = !trap_step && !(early_portal && use_portal) && tile && tile->has_pearl() &&
                            (!feed || !alpha || dist(n, *alpha) > 5) &&
                            ((!s.alpha && !iso && !guard) || small_map_splitting_alpha || endgame_safe ||
                             (f_exits >= 1 && (risk < 280 || (risk < 320 && f_exits >= 2)) && (c.get_length() < 12 || area >= 5)));

            double risk_penalty = 0.0;
            if (iso && !s.alpha) {
                if (risk >= 360)      risk_penalty = iso_early ? 700.0 : 520.0;
                else if (risk >= 320) risk_penalty = iso_early ? 320.0 : 240.0;
                else if (risk >= 280) risk_penalty = iso_early ? 160.0 : 100.0;
                else                  risk_penalty = 0.25 * risk;
            } else if ((s.alpha && !small_map_splitting_alpha) || guard) {
                if (risk >= 360)      risk_penalty = 520.0;
                else if (risk >= 320) risk_penalty = 240.0;
                else if (risk >= 280) risk_penalty = 100.0;
                else                  risk_penalty = 0.25 * risk;
            }
            double score = 2.0 * std::min(area, 10) + 4.0 * exits - risk_penalty;
            if (F_TRAP_AVOID && (c.get_length() >= TRAP_MIN_LEN || F_TRAP_SMALL) && !blind_portal &&
                s.cells[index(here)].edge[d] == 0) {
                int need = c.get_length() >= TRAP_MIN_LEN ? std::min(c.get_length(), TRAP_NEED) : std::max(c.get_length() + 2, 4);
                int room = escape_room(n, need);
                if (room < need) score -= TRAP_W * (need - room);
            }

            if (trap_step) score -= 950.0;
            if (long_body() && s.cells[index(here)].edge[d] > 0 && !s.evicting && !s.evacuating) score -= 700.0;
            if (s.cells[index(here)].edge[d] == 0 && chokepoint_blocked(n, d)) score -= 600.0;
            if (F_PORTAL_CLEAR && !use_portal && s.cells[index(here)].edge[d] == 0 && portal_adjacent(n) &&
                !(tile && tile->has_pearl()) && !(target && n == *target))
                score -= loiter_penalty(45);
            if (exits == 0 || f_exits == 0) score -= 800;
            else if (exits == 1) {
                if (s.alpha && risk >= 280) score -= 80;
                else if (!has_food) score -= 15;
            }

            if (threatened && perpendicular && (d % 2) == (cur_dir % 2)) score -= 400;

            if (target) {
                double dir_bonus = patrol_only ? 28.0 : 80.0;
                double step_bonus = patrol_only ? 8.0 : 15.0;
                if (target_direction == d) score += dir_bonus;
                else score += step_bonus * (dist(here, *target) - dist(n, *target));
            }

            int cd_here = center_dist(here);
            int cd_next = center_dist(n);
            int pull_radius = std::max(3, (w + h) / 4);
            double pull_weight = 2.0;
            if (cd_next > pull_radius) score += pull_weight * (cd_here - cd_next);
            if ((n.x <= 2 || n.x >= w - 3) && (n.y <= 2 || n.y >= h - 3)) score -= 20.0;

            if (F_BARREN && patrol_only && !has_food) {
                bool in_n = in_barren(n), in_here = in_barren(here);
                if (in_n && !in_here) score -= 25.0;
                else if (!in_n && in_here) score += 10.0;
            }

            if (has_food) score += 120;
            else if (feed && alpha && tile && tile->has_pearl() && dist(n, *alpha) <= 5) score -= 350.0;
            if (!has_food && s.cells[index(here)].edge[d] == 0 && farm_lane(n) && !(target && dist(n, *target) <= 2))
                score -= 60.0;
            if (EXIT_CLEAR_ON && s.cells[index(here)].edge[d] == 0 && !(use_portal && port && target && n == *target) &&
                exit_lane(n))
                score -= has_food ? EXIT_LANE_PENALTY / 3.0 : EXIT_LANE_PENALTY;
            // v5.9b: a corridor a teammate is coming along (F_LANE); a tile a teammate is about to come out of a portal onto
            // (F_PORTAL_PROBE); the mouth of a dead end a teammate works (F_MOUTH_CLEAR).
            if (s.cells[index(here)].edge[d] == 0 && lane_occupied(n, d)) score -= 500.0;
            if (avoided(n)) score -= 250.0;
            if (F_DODGE && dodger && !blind_portal)
                score -= (mode == Mode::Forage && target ? DODGE_FORAGE_W : DODGE_W) * strike_risk(n);
            else if (F_DODGE && DODGE_AMBUSH_W > 0 && kam && mode == Mode::Ambush && !blind_portal)
                score -= DODGE_AMBUSH_W * strike_risk(n);
            if (F_FLANK && kam && mode == Mode::Ambush && s.cells[index(here)].edge[d] == 0 && on_enemy_line(n))
                score -= FLANK_LINE_PENALTY;
            if (!has_food && s.cells[index(here)].edge[d] == 0 && busy_mouth(n)) score -= 120.0;
            // v5.9b (F_PORTAL_PROBE): what last turn's probe found behind this portal edge.
            if (s.cells[index(here)].edge[d] > 0 && s.probe_verdict > 0 && here == s.probe_pos && d == s.probe_dir) {
                if (s.probe_verdict == 1) score -= s.probe_bounded ? 300.0 : 120.0;
                else score -= s.probe_bounded ? 600.0 : 80.0;
            }
            if (F_PATCH_CAMP && !has_food) {
                if (patch_held(n) && !patch_held(here)) score -= 80.0;       // keep out of a teammate's patch
                else if (patch_held(here) && !patch_held(n)) score += 30.0;  // on our way out of it
                if (s.patch_camp && !in_patch_tiles(n) && s.cells[index(here)].edge[d] == 0) score -= 150.0; // ours: stay
            }

            if (tile && !tile->has_pearl() && tile->get_pearl_time() == 1) {
                if (distance[index(n)] <= 2 && !trap_step) score += 25;
                else score -= 15;
            }

            // Productive moves override revisit penalties; exploration and non-visible feeding use anti-cycling.
            bool bypass_anti_cycle = has_food || direct_feed || use_portal || s.resident ||
                                      (s.alpha && pearls.has_value() && target_direction == d);
            int v_time = s.cells[index(n)].visited;
            if (v_time < 0) score += 55.0;
            else if (!bypass_anti_cycle) {
                int v_age = round - v_time;
                if (v_age <= 2)        score -= 280.0;
                else if (v_age <= 6)   score -= 120.0;
                else if (v_age <= 20)  score -= 45.0;
                else if (v_age <= 55)  score -= 12.0;
                else                   score += std::min(v_age, 200) * 0.22;
            }

            if (!bypass_anti_cycle) {
                for (int i = static_cast<int>(s.recent_path.size()) - 1; i >= 0; --i) {
                    if (s.recent_path[i] == n) {
                        int age = static_cast<int>(s.recent_path.size() - i);
                        if (age <= 6) score -= 220.0;
                        else if (age <= 14) score -= 70.0;
                        break;
                    }
                }
            }

            Position ahead = n;
            for (int step_k = 1; step_k <= 3; ++step_k) {
                if (s.cells[index(ahead)].edge[d] != 0) break;
                ahead = step(ahead, d);
                if (s.cells[index(ahead)].seen < 0) score += 12.0;
                else if (s.cells[index(ahead)].visited < 0) score += 6.0;
            }

            if (d == cur_dir) score += 12.0;
            int turn_diff = (d - cur_dir + 4) % 4;
            if (turn_diff == 1 || turn_diff == 3) score -= 8.0;

            // Soft repulsion from teammates' bodies spreads the swarm over parallel lanes.
            if (F_DISPERSE && !s.alpha && !feed && mode != Mode::Ambush && !use_portal && !has_food) {
                int adj = 0;
                for (int i = 0; i < n_fb; ++i)
                    if (dist(n, fb[i]) == 1) ++adj;
                score -= 7.0 * std::min(adj, 3);
                if (following && d == cur_dir) score -= 20.0;
            }

            if (breaking_cycle) {
                std::uint32_t r = mix(static_cast<std::uint32_t>(c.get_id()) * 131U +
                                      static_cast<std::uint32_t>(round) * 17U + static_cast<std::uint32_t>(d));
                score += static_cast<double>(r % 31) - 15.0;
                int sz = static_cast<int>(s.recent_path.size());
                for (int i = std::max(0, sz - (F_CYCLE2 ? 16 : 8)); i < sz; ++i)
                    if (s.recent_path[i] == n) { score -= 60.0; break; }
                // Head away from the loop: reward closing on the breakout point.
                if (F_CYCLE2) score += 10.0 * (dist(here, s.sector_target) - dist(n, s.sector_target));
            }

            // Repulsion field: quadratic in closeness to every non-alpha teammate head within 4 tiles.
            if (F_REPEL && !has_food && mode != Mode::Ambush && !use_portal)
                for (int i = 0; i < n_rh; ++i) {
                    int k = dist(n, rh[i]);
                    if (k <= 4) score -= REPEL_W * (5 - k) * (5 - k);
                }

            if (!s.alpha) {
                for (auto f : friends) {
                    int d_team = dist(n, f.position);
                    if (d_team == 1 && escape_count(f.position) <= 1) score -= 650.0;
                    if (is_alpha(f.get_id()) || friendly_visible_length(f.get_id()) >= 6) {
                        if (n == step(f.position, di(f.get_dir()))) score -= 350.0;
                        else if (!feed && d_team <= 2) score -= 65.0;
                    } else if (!feed && d_team <= 4) {
                        score -= 6.0 * (5 - d_team);
                    }
                }
            } else if (round < feed_start) {
                for (auto f : friends) {
                    int d_team = dist(n, f.position);
                    if (d_team == 1 && escape_count(f.position) <= 1) score -= 650.0;
                    if (d_team <= 4) score -= 6.0 * (5 - d_team);
                }
            }

            if (score > best) { best = score; chosen = d; }
        }

        // A long dragon whose only way on is a portal splits instead: the 2-long head goes through, the
        // rear L-2 is born on the tail facing back into the open map and keeps the mass (and the role).
        auto sacrifice = [&](int edge) -> std::optional<Action> {
            if (!long_body() || s.evicting || s.evacuating || !can_split_safe(c.get_length() - 2)) return {};
            s.capture_portal = edge - 1;
            s.capture_until = round + 4;
            return Action{{}, c.get_length() - 2, Mode::Split, false};
        };
        if (chosen >= 0) {
            int edge = s.cells[index(here)].edge[chosen];
            if (edge > 0)
                if (auto sp = sacrifice(edge)) return *sp;
            if (edge > 0 && legal.size() == 1) note_transit(edge - 1);
            return {{DIRS[chosen]}, 0, mode, false};
        }

        for (int d = 0; d < 4; ++d)
            if (is_step_safe(d, false)) {
                int edge = s.cells[index(here)].edge[d];
                if (edge > 0) {
                    if (auto sp = sacrifice(edge)) return *sp;
                    occupy(edge - 1);
                    note_transit(edge - 1);
                    return {{DIRS[d]}, 0, Mode::Portal, false};
                }
            }

        auto forced = portal_target(true);
        if (forced && forced->approach == here && forced->direction != opp_dir) {
            if (auto sp = sacrifice(forced->id + 1)) return *sp;
            occupy(forced->id);
            note_transit(forced->id);
            return {{DIRS[forced->direction]}, 0, Mode::Portal, false};
        }

        if (can_split_safe(c.get_length() - 2) && !resplit_doomed()) return {{}, c.get_length() - 2, Mode::Split, false};

        for (int d = 0; d < 4; ++d)
            if (is_step_safe(d, true)) return {{DIRS[d]}, 0, Mode::Trapped, true};

        for (int d = 0; d < 4; ++d) {
            if (d == opp_dir || s.cells[index(here)].edge[d] < 0) continue;
            auto opt_dest = destination(here, d);
            if (opt_dest) {
                auto t = c.get_tile(*opt_dest);
                if (t && t->get_dragon() && t->get_dragon()->get_team() == c.get_team() &&
                    t->get_dragon()->is_head()) continue;
            }
            return {{DIRS[d]}, 0, Mode::Trapped, false};
        }

        for (int d = 0; d < 4; ++d) {
            auto opt_dest = destination(here, d);
            if (opt_dest) {
                auto t = c.get_tile(*opt_dest);
                if (t && t->get_dragon() && t->get_dragon()->get_team() == c.get_team() &&
                    t->get_dragon()->is_head()) continue;
            }
            return {{DIRS[d]}, 0, Mode::Trapped, true};
        }

        return {{c.get_dir()}, 0, Mode::Trapped, true};
    }

    // Which of this turn's beams end on the child we are splitting off (bit d for direction d). The body is the one
    // seen this turn, before the split: segments [len - child, len) become the child. A beam fired into our own neck
    // refracts out of our new tail, straight on in the direction that tail points away from the body.
    int beams_into_child(int child) const {
        auto body = body_tiles();
        int len = c.get_length(), keep = len - child;
        if (keep < 1 || static_cast<int>(body.size()) <= keep) return 0;
        auto first_hit = [&](Position p, int d) -> int { // 1 child, 0 anything else or nothing within view
            for (int k = 0; k < 8; ++k) {
                if (s.cells[index(p)].edge[d] != 0) return 0; // kelp, portal or unknown: give up
                p = step(p, d);
                auto t = c.get_tile(p);
                if (!t) return 0;
                auto part = t->get_dragon();
                if (!part) continue;
                if (part->get_id() != c.get_id()) return 0;
                for (int i = 0; i < static_cast<int>(body.size()); ++i)
                    if (body[i] == p) return i >= keep ? 1 : 0;
                return 0;
            }
            return 0;
        };
        int mask = 0, back = (di(c.get_dir()) + 2) % 4;
        for (int d = 0; d < 4; ++d) {
            if (d == back) {
                Position tail = body[keep - 1];
                int away = -1;
                if (keep == 1) away = back;
                else
                    for (int k = 0; k < 4; ++k)
                        if (step(tail, k) == body[keep - 2]) away = (k + 2) % 4;
                if (away >= 0 && first_hit(tail, away)) mask |= 1 << d;
            } else if (first_hit(here, d)) {
                mask |= 1 << d;
            }
        }
        return mask;
    }

    void execute(const Action &a) {
        Position post = here;
        s.lane_dir = -1;
        s.last_mode = a.mode;
        if (a.child) {
            c.do_split(a.child);
            // v5.5: an alpha whose rear child is the larger piece (every L-2 split: cascade, rescue, kamikaze split, portal
            // sacrifice) hands it the role. The 2-long head we keep is expendable: a skirmisher.
            int rem = c.get_length() - a.child;
            if (F_MANTLE && s.alpha && (F_MANTLE_CASCADE || a.mode != Mode::Cascade) &&
                (a.child > rem || (a.child == rem && a.mode != Mode::Split))) {
                // The head never keeps the role. A child too short to be worth it gets none either (the packet then only
                // retires our ID): the endgame apex election fills the gap.
                int min_child = a.mode == Mode::Cascade ? 3 : MANTLE_MIN_CHILD;
                if (is_primary_alpha_id(c.get_id())) s.primary_demoted = true;
                note_demoted(c.get_id(), round);
                s.mantle_send = round;
                s.mantle_child_len = a.child >= min_child ? a.child : 0;
                s.alpha = false;
                s.growing = false;
                if (a.mode != Mode::Cascade) s.hunter_until = round + 24;
                DIAG("mantle " << c.get_id() << ' ' << round << ' ' << name(a.mode) << " child " << a.child << " keep " << rem);
            }
            // The rear half keeps the mass; a short front half left behind turns on the threat. With
            // F_HANDOVER this holds for alphas too: the rear child (4+ long) takes the role at birth.
            bool rear_takes_role = F_HANDOVER && a.child >= 4;
            if (a.mode != Mode::Cascade && (a.child >= 5 || rear_takes_role) && c.get_length() - a.child <= 3 &&
                (!s.alpha || s.growing || rear_takes_role)) {
                if (s.alpha) {
                    // Tell the team the old alpha is gone (no successor named: the child claims it itself).
                    if (is_primary_alpha_id(c.get_id())) s.primary_demoted = true;
                    s.handover_to = 4095;
                    s.handover_old = c.get_id();
                    s.handover_round = round;
                }
                s.alpha = false;
                s.growing = false;
                s.hunter_until = round + 24;
            }
        } else {
            s.moved_len = c.get_length();
            s.moved_steps = std::max<int>(1, static_cast<int>(a.moves.size()));
            s.moved_cross = a.moves.empty() && s.cells[index(here)].edge[di(c.get_dir())] > 0 ? 0 : -1;
            for (std::size_t i = 0; i < a.moves.size(); ++i) {
                int dd = di(a.moves[i]);
                int e = s.cells[index(post)].edge[dd];
                auto n = destination(post, dd);
                if (e > 0) { s.pending_portal = e - 1; occupy(e - 1); s.moved_cross = static_cast<int>(i); }
                if (!n) break;
                auto t = c.get_tile(*n);
                if (t && t->has_pearl()) s.last_food = round;
                post = *n;
            }

            if (a.moves.size() > 1) s.sprint_round = round;
            s.lane_dir = -1;
            if (F_LANE && !a.intentional_death && !a.moves.empty() && s.pending_portal < 0) {
                int hd = di(a.moves.back()), steps = 0;
                Position end;
                bool straight = false;
                if (corridor_ahead(post, hd, end, steps, straight) && straight) {
                    s.lane_dir = hd;
                    s.lane_end = end;
                    s.lane_steps = steps;
                }
            }
            if (a.moves.empty()) c.make_move(c.get_dir());
            else if (a.moves.size() == 1) c.make_move(a.moves.front());
            else c.make_moves(a.moves);
        }

        if (!a.intentional_death && (s.pending_portal < 0 ||
            (!a.moves.empty() && destination(here, di(a.moves.front())).has_value()))) {
            std::array<std::uint64_t, 4> beam{};
            std::array<bool, 4> used{};
            if (s.alpha) {
                std::uint64_t pkt = alpha_packet64(c.get_id(), post, round, c.get_length());
                beam.fill(pkt);
                used.fill(true);
            } else {
                std::vector<AlphaTrack> active;
                for (const auto &ai : s.alphas)
                    if (round - ai.seen <= SONAR_TTL) active.push_back(ai);
                bool by_len = F_FEED_SCORE && round >= feed_round() - 20;
                std::sort(active.begin(), active.end(), [&](const AlphaTrack &x, const AlphaTrack &y) {
                    // v5.8: in the endgame the longest alphas go out first, so feeders hear of the apex.
                    if (by_len && x.len != y.len) return x.len > y.len;
                    int dx = dist(post, x.p), dy = dist(post, y.p);
                    return dx != dy ? dx < dy : x.seen > y.seen;
                });
                if (!active.empty()) {
                    for (int d = 0; d < 4; ++d) {
                        const auto &ai = active[d % active.size()];
                        beam[d] = alpha_packet64(ai.id, ai.p, ai.seen, ai.len);
                        used[d] = true;
                    }
                } else if (round - s.enemy_alpha_seen <= 12 || is_kamikaze_regime()) {
                    beam.fill(alpha_packet64(SONAR_ID_MASK, {0, 0}, round, 0));
                    used.fill(true);
                }
            }

            // v5.2 tagged packets take over beams, rotating so every direction carries them in turn.
            // A dragon that just found itself trapped spends all four beams on its hazard.
            std::array<std::uint64_t, 8> extra{};
            int n_extra = 0;
            bool critical = true; // a broadcast every beam must carry this turn (no probe then)
            if (F_MANTLE && round - s.mantle_send <= 2) {
                // We just gave the alpha role to our split child: every beam says so for three rounds. Only the split
                // turn's beam into our own body names the child's length (it refracts out of our tail into the child).
                // (A rescue split out of a dead cell still spends its beams on barring that portal.)
                beam.fill(F_PORTAL_TRAP && s.dead_alarm >= 0 ? barred_packet() : mantle_packet(c.get_id(), 0, s.mantle_send));
                used.fill(true);
            } else if (s.handover_old == c.get_id() && round - s.handover_round <= 3) {
                // We gave up the alpha role: for a few turns every beam names the successor (a beam that
                // wraps round the map into our own body is wasted, so one turn is often not enough).
                beam.fill(handover_packet(s.handover_to, c.get_id(), round));
                used.fill(true);
            } else if (F_PORTAL_TRAP && s.dead_alarm >= 0) {
                // Dying in a dead cell: every beam bars its portal. The one fired back into our body refracts out
                // of the tail straight into the child we just split off, a fresh process standing beside that portal.
                beam.fill(barred_packet());
                used.fill(true);
            } else if (F_HAZARD && s.hazard_sent == round) {
                for (const auto &hz : s.hazards)
                    if (hz.origin == round) {
                        beam.fill(hazard_packet(hz));
                        used.fill(true);
                        break;
                    }
            } else {
                critical = false;
                // Now and then, pass on a barren enclosure we know of, so newborns never walk into it.
                // (v5.4: the whole barred set every 2 rounds; v5.3 only ever repeated the first one it knew.)
                if (F_PORTAL_TRAP && (round + c.get_id()) % 2 == 0 && barred_mask()) extra[n_extra++] = barred_packet();
                if (s.send_clear < 0 && !F_PORTAL_TRAP && F_PORTAL_EVICT && (round + c.get_id()) % 8 == 0)
                    for (const auto &pp : s.portals)
                        if (pp.barren && pp.id >= 0 && pp.id < 32) { s.send_clear = pp.id; break; }
                if (s.send_clear >= 0 && s.send_clear < 32) extra[n_extra++] = portal_packet(s.send_clear, true);
                else if (F_PORTAL_RESERVE && s.send_reserve >= 0 && s.send_reserve < 32)
                    extra[n_extra++] = portal_packet(s.send_reserve, false);
                if (F_HAZARD && F_HAZARD_RR) {
                    // Every live hazard gets its turn; with two or more live, two beams carry them.
                    std::array<const Hazard *, MAX_HAZARDS> live{};
                    int n_live = 0;
                    for (const auto &hz : s.hazards)
                        if (round - hz.origin <= HAZARD_TTL - 2) live[n_live++] = &hz;
                    for (int k = 0; k < std::min(n_live, 2); ++k)
                        extra[n_extra++] = hazard_packet(*live[(s.hazard_rr + k) % n_live]);
                    if (n_live > 0) s.hazard_rr = (s.hazard_rr + std::min(n_live, 2)) % MAX_HAZARDS;
                } else if (F_HAZARD) {
                    const Hazard *fresh = nullptr;
                    for (const auto &hz : s.hazards)
                        if (round - hz.origin <= HAZARD_TTL - 2 && (!fresh || hz.origin > fresh->origin)) fresh = &hz;
                    if (fresh) extra[n_extra++] = hazard_packet(*fresh);
                }
                // A portal we just learnt is barred goes out on two opposite beams for two rounds, so the
                // news crosses the team in a few hops instead of trickling out every 8 rounds.
                if (F_PORTAL_TRAP && s.news_portal >= 0 && round <= s.news_until && n_extra <= 1) {
                    extra[n_extra++] = barred_packet();
                    extra[n_extra++] = barred_packet();
                }
                // v5.6: a mirrored hotspot we just found goes out on two beams for two rounds; a spot we set off for is sent
                // as claimed; otherwise every third round one beam relays the freshest spot we know. Now and then one beam
                // carries our resolved symmetry alone (value 0), so newborns and late dragons learn it.
                if (F_MIRROR_SCOUT && n_extra <= 2 && !(uniform_map() && !F_POCKET_SEEK)) {
                    if (s.scout_new >= 0 && round - s.scout_sent <= 1) {
                        const auto &sc = s.scouts[s.scout_new];
                        extra[n_extra++] = scout_packet(sc.p, sc.origin, sc.value, sc.claimed);
                        extra[n_extra++] = scout_packet(sc.p, sc.origin, sc.value, sc.claimed);
                    } else if (s.goal >= 0 && round - s.claim_send <= 2) {
                        const auto &sc = s.scouts[s.goal];
                        extra[n_extra++] = scout_packet(sc.p, sc.origin, sc.value, true);
                    } else if ((round + c.get_id()) % 3 == 0) {
                        const Scout *fresh = nullptr;
                        for (const auto &sc : s.scouts)
                            if (sc.value > 0 && round - sc.origin <= SCOUT_TTL / 2 && (!fresh || sc.origin > fresh->origin))
                                fresh = &sc;
                        if (fresh) extra[n_extra++] = scout_packet(fresh->p, fresh->origin, fresh->value, fresh->claimed);
                    }
                }
                // v5.7: a barren zone we just found goes out on one beam for two rounds; every fourth round one beam
                // relays a live zone we know (in rotation).
                if (F_BARREN && F_BARREN_SONAR && n_extra <= 2) {
                    if (s.barren_new >= 0 && round - s.barren_sent <= 1 && round < s.barren[s.barren_new].until) {
                        extra[n_extra++] = barren_packet(s.barren[s.barren_new]);
                    } else if ((round + c.get_id()) % 4 == 1) {
                        for (int k = 0; k < MAX_BARREN; ++k) {
                            const auto &z = s.barren[(s.barren_rr + k) % MAX_BARREN];
                            if (z.until - round < 8) continue;
                            extra[n_extra++] = barren_packet(z);
                            s.barren_rr = (s.barren_rr + k + 1) % MAX_BARREN;
                            break;
                        }
                    }
                }
                // v5.8: a farm we just found goes out on two beams for two rounds; a claim on one; every fourth round one
                // beam relays a known farm (in rotation).
                if (F_FARM && n_extra <= 2) {
                    if (s.farm_new >= 0 && round - s.farm_sent <= 1) {
                        extra[n_extra++] = farm_packet(s.farms[s.farm_new], false);
                        if (n_extra <= 2) extra[n_extra++] = farm_packet(s.farms[s.farm_new], false);
                    } else if (s.farm_claim_slot >= 0 && round - s.farm_claim_send <= 1) {
                        extra[n_extra++] = farm_packet(s.farms[s.farm_claim_slot], true);
                    } else if ((round + c.get_id()) % 4 == 2) {
                        for (int k = 0; k < MAX_FARMS; ++k) {
                            const auto &f = s.farms[(s.farm_rr + k) % MAX_FARMS];
                            if (f.value < FARM_MIN || round - f.seen > FARM_TTL) continue;
                            extra[n_extra++] = farm_packet(f, false);
                            s.farm_rr = (s.farm_rr + k + 1) % MAX_FARMS;
                            break;
                        }
                    }
                }
                if (F_SYMMETRY && s.sym >= 0 && n_extra <= 1 && (round + c.get_id()) % 6 == 0 && !uniform_map())
                    extra[n_extra++] = scout_packet({0, 0}, round, 0, false);
                // v5.5: pass on the latest alpha that gave its role away, so nobody keeps feeding its old ID.
                if (F_MANTLE && n_extra < 3) {
                    const std::pair<int, int> *last = nullptr;
                    for (const auto &d : s.demoted)
                        if (d.first >= 0 && round - d.second <= 8 && d.first != c.get_id() && (!last || d.second > last->second))
                            last = &d;
                    if (last && (round + c.get_id()) % 2 == 1) extra[n_extra++] = mantle_packet(last->first, 0, last->second);
                }
                // Pass on a recent alpha handover so it reaches the successor wherever it is.
                if (s.handover_to >= 0 && round - s.handover_round <= 8 && s.handover_old != c.get_id())
                    extra[n_extra++] = handover_packet(s.handover_to, s.handover_old, s.handover_round);
            }
            for (int i = 0; i < std::min(n_extra, 4); ++i) {
                int d = (round + i) % 4;
                beam[d] = extra[i];
                used[d] = true;
            }
            // v5.9d (F_RESERVE_AIM): a camper's reservation goes out through its own portal, where newcomers approach.
            if (F_PORTAL_RESERVE && F_RESERVE_AIM && !critical && (s.camping || s.resident) && !s.evicting &&
                round - s.resv_aim_round >= 3) {
                auto enc = enclosure_at(post);
                std::uint32_t mask = F_RESERVE_ALL ? enc.portal_mask
                                   : enc.portal_id >= 0 && enc.portal_id < 32 ? 1U << enc.portal_id : 0U;
                for (int pid = 0; enc.small && pid < 32; ++pid) {
                    if (!(mask >> pid & 1) || portal_const(pid).barren) continue;
                    int d = portal_beam_dir(post, pid);
                    if (d < 0) continue;
                    beam[d] = portal_packet(pid, false);
                    used[d] = true;
                    s.resv_aim_round = round;
                    DIAG("resvaim " << c.get_id() << ' ' << round << ' ' << pid << " dir " << d);
                    break;
                }
            }
            // A split child is a fresh process that knows nothing. The beam we fire back into our own body
            // refracts out of our tail straight into it: hand it a barred portal on its first turn.
            if (F_PORTAL_TRAP && a.child && s.dead_alarm < 0 && barred_mask()) {
                int back = (di(c.get_dir()) + 2) % 4;
                beam[back] = barred_packet();
                used[back] = true;
            }
            if (F_MANTLE && a.child && s.mantle_send == round && s.mantle_child_len > 0 && s.dead_alarm < 0) {
                int hits = beams_into_child(a.child);
                for (int d = 0; d < 4; ++d)
                    if (hits & (1 << d)) {
                        beam[d] = mantle_packet(c.get_id(), s.mantle_child_len, round);
                        used[d] = true;
                    }
                DIAG("mantlebeam " << c.get_id() << ' ' << round << " mask " << hits);
            } else if (F_FARM && a.child && a.mode == Mode::Rescue && s.dead_alarm < 0) {
                // v5.8: our rear child is born in a farm it has only half seen: tell it (the beam into our body refracts
                // out of our tail into it).
                int k = farm_near(here, 8);
                if (k >= 0) {
                    int hits = beams_into_child(a.child);
                    for (int d = 0; d < 4; ++d)
                        if (hits & (1 << d)) {
                            Farm f = s.farms[k];
                            f.seen = round;
                            beam[d] = farm_packet(f, false);
                            used[d] = true;
                        }
                    DIAG("farmbeam " << c.get_id() << ' ' << round << " mask " << hits);
                }
            }
            // v5.9b (F_RETREAT): now and then one beam carries our spawn point, so dragons born later know which half is ours.
            if (F_RETREAT && !critical && s.spawn_known && (round + c.get_id()) % 5 == 3) {
                int d = (round + 3) % 4;
                beam[d] = alpha_packet64(SPAWN_TAG, s.spawn, round, 0);
                used[d] = true;
            }
            // v5.9b (F_LANE, task 11): in a straight one-way corridor, the beam ahead tells whoever waits at its far end.
            if (F_LANE && s.lane_dir >= 0 && !critical) {
                beam[s.lane_dir] = alpha_packet64(LANE_TAG, s.lane_end, round, (s.lane_dir & 3) | (std::min(s.lane_steps, 31) << 2));
                used[s.lane_dir] = true;
                DIAG("lanesend " << c.get_id() << ' ' << round << " end " << s.lane_end.x << ',' << s.lane_end.y << " dir "
                                 << s.lane_dir << " steps " << s.lane_steps);
            }
            // v5.9b (F_PORTAL_PROBE, tasks 2 and 3): the probe goes alone, so next turn's echo counts are its own.
            if (F_PORTAL_PROBE && s.probe_plan_dir >= 0 && post == s.probe_plan_pos && !critical && !a.child) {
                auto dest = destination(post, s.probe_plan_dir);
                used.fill(false);
                beam[s.probe_plan_dir] = alpha_packet64(PROBE_TAG, dest ? *dest : post, round, 0);
                used[s.probe_plan_dir] = true;
                s.probe_round = round;
                s.probe_pos = post;
                s.probe_dir = s.probe_plan_dir;
                DIAG("probesend " << c.get_id() << ' ' << round << " at " << post.x << ',' << post.y << " dir " << s.probe_dir);
            }
            for (int d = 0; d < 4; ++d)
                if (used[d]) c.send_sonar(DIRS[d], beam[d]);
        }
        s.send_clear = s.send_reserve = s.dead_alarm = -1;

        // Per-turn trace for offline metrics (split chains, portal-tile loitering, dead-end entries).
        DIAG("turn " << c.get_id() << ' ' << round << ' ' << c.get_length() << ' ' << name(a.mode) << ' ' << post.x << ','
                     << post.y << ' ' << (portal_adjacent(post) ? 1 : 0) << ' ' << ((s.camping || s.resident) ? 1 : 0)
                     << ' ' << a.child << ' ' << s.born << ' ' << s.born_len << ' ' << c.get_unit_count());
        const char *role_prefix = s.alpha ? "Alpha:" : (is_kamikaze() ? "Kamikaze:" : "Neutral:");
        c.set_indicator_string(std::string(role_prefix) + name(a.mode));
    }
};

} // namespace bot

int main() {
    try {
        auto [ct, game] = unswbc::init();
        auto brain = std::make_unique<bot::Brain>(ct, game);

        while (unswbc::update(ct, game)) {
            try {
                auto action = brain->decide();
                brain->execute(action);
            } catch (...) {
                ct.make_move(ct.get_dir());
            }
            unswbc::end_turn();
        }
    } catch (...) { return 0; }
    return 0;
}