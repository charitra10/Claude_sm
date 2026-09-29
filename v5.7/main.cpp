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
constexpr int PORTAL_RESIDENCY = 2;

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
// type (2 bits) | spot value (5 bits, 0: symmetry only, no spot) | claimed (bit 7).
constexpr int SCOUT_TAG = 8185;
// v5.7: a barren zone. Position field = its centre, origin = round seen, alen = symmetry (2 bits, valid with bit 2: the
// receiver adds the mirror image too) | rounds it stays barren / 4 (5 bits, from bit 3).
constexpr int BARREN_TAG = 8184;
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
    int sym_t = -2;     // v5.6: round of the next spawn attempt as last seen, -1 never spawns, -2 unknown
    int sym_done = 0;   // v5.6: bit k: this tile already counted as evidence for symmetry candidate k
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
};

// The mouth of a sealed pocket: stepping onto `p` while moving in `dir` walks into it.
struct Hazard {
    Position p;
    int dir = -1, origin = -1000;
};

struct PearlMemory {
    Position p;
    int seen = -1000;
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
    // v5.7 state
    std::array<Rendezvous, MAX_RDV> rdv{};
    int rdv_goal = -1;                                  // rendezvous slot we are walking to
    std::array<BarrenZone, MAX_BARREN> barren{};
    int barren_new = -1, barren_sent = -1000, barren_rr = 0; // zone to announce, when we last found one, relay rotor
    int assassin_round = -1000;
    int born_len = 0;                                   // our length on our first turn
    Position rem_goal;                                  // remembered pearl we are walking to (F_REM_COMMIT)
    int rem_until = -1000;
    int pair_turns = 0;                                 // turns a teammate head has been beside ours (F_PAIR_SEP)
    int deadend_until = -1000;                          // no dead-end entry until then (F_CHOKE_LOOP)
    std::array<Position, 64> hist{};                    // our body, tail first (F_CHOKE_LOOP); hist_n < length: unknown
    int hist_n = 0;
    int explore_timer_h = 0;                            // rounds since the last hybrid retarget
    int heading = -1;                                   // exploration octant (DIR8), -1 none yet
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

    std::array<int, MAX_CELLS> distance{}, first{}, predecessor{}, arrival{};
    std::array<int, MAX_CELLS> wcost{}, wfirst{}; // v5.7: portal-averse route cost and first step (tiles BFS reached)
    // v5.7 (a2, a3): shortest ways over the remembered map from our head (never-seen tiles assumed open), once per turn.
    mutable std::array<int, MAX_CELLS> mem_depth{}, mem_first{};
    mutable int mem_round = -1;
    std::array<int, 64> reached{};
    int n_reached = 0;
    mutable std::array<int, MAX_CELLS> risk_cache{};
    mutable std::array<int, MAX_CELLS> danger_depth{};
    mutable std::array<bool, MAX_CELLS> space_seen{};
    mutable std::array<int, MAX_CELLS> remembered_first{};
    mutable std::array<Position, MAX_CELLS> remembered_q{};

    std::vector<std::pair<int, int>> friend_len_cache, enemy_len_cache;
    std::vector<int> friend_alpha_cache;

  public:
    DragonState s;

    Brain(Controller &controller, Game &game_state) : c(controller), g(game_state) {
        std::tie(w, h) = g.get_map_size();
        danger_depth.fill(-1);
        space_seen.fill(false);
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
        for (const auto &f : friends) mine = std::min(mine, dist(f.position, at));
        for (const auto &e : enemies)
            if (e.get_id() != victim) theirs = std::min(theirs, dist(e.position, at));
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
        return std::min(415, 500 - (3 * n) / 2 - 25) - 60;
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
        int bits = (std::max(0, s.sym) & 3) | (std::clamp(value, 0, 31) << 2) | (claimed ? 128 : 0);
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
            (!F_HOTSPOT2 && s.pearl_ema >= SCOUT_SATURATION) || uniform_map())
            return;
        Position spot = here;
        int value = 0;
        if (chamber >= SCOUT_MIN_SPAWNERS) {
            value = chamber;
        } else {
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

    void add_hazard(Position p, int dir, int origin) {
        int slot = 0;
        for (int i = 0; i < MAX_HAZARDS; ++i) {
            auto &hz = s.hazards[i];
            if (hz.p == p && hz.dir == dir) {
                hz.origin = std::max(hz.origin, origin);
                return;
            }
            if (hz.origin < s.hazards[slot].origin) slot = i;
        }
        s.hazards[slot] = {p, dir, origin};
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
        if (tag == SCOUT_TAG) {
            if (!F_SYMMETRY || age > SCOUT_TTL) return;
            int k = bits & 3;
            if (s.sym < 0 && sym_possible(k) && !s.sym_bad[k]) {
                s.sym = k;
                DIAG("symheard " << c.get_id() << ' ' << round << ' ' << k);
            }
            int value = (bits >> 2) & 31;
            if (!F_MIRROR_SCOUT || value == 0 || px >= w || py >= h) return;
            DIAG("scoutrecv " << c.get_id() << ' ' << round << ' ' << px << ',' << py << " v " << value << " claimed "
                              << ((bits >> 7) & 1) << " age " << age);
            note_scout({px, py}, origin, value, (bits & 128) != 0);
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
            add_hazard({px, py}, bits & 3, origin);
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
                for (int d = 0; d < 4; ++d) s.n_kelp += !t.get_edge(DIRS[d]).is_passable();
            }
            cell.seen = round;
            auto part = t.get_dragon();
            cell.has_dragon = (part != nullptr);
            cell.spawn_at = t.has_pearl() ? round : t.get_pearl_time() >= 0 ? round + t.get_pearl_time() : -1;
            cell.fast_obs = (t.get_pearl_time() >= 0 && t.get_pearl_time() <= 1) ? cell.fast_obs + 1 : 0;
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
                alpha_id == SCOUT_TAG || alpha_id == BARREN_TAG) {
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
                if (distinct <= 7) break_loop(16);
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
                         : PORTAL_RESIDENCY == 2 ? spawners_in_view() == 0 : false;
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
            s.pearl_mem[slot] = {p, round};
        }
    }

    std::optional<Position> alpha_memory_target() const {
        std::optional<Position> best;
        double best_score = -1e20;
        for (const auto &m : s.pearl_mem) {
            if (m.seen <= -1000 || m.seen == round) continue; // still in view: food_target already judged it
            int d = dist(here, m.p);
            if (d < 1 || d > 24) continue;
            double score = 40.0 / (d + 1.0) - 0.15 * (round - m.seen);
            if (score > best_score) { best_score = score; best = m.p; }
        }
        return best;
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
                int ni = index(n);
                auto t = c.get_tile(n);
                if (!t || distance[ni] != INF) continue;
                auto part = t->get_dragon();
                if (part && !(part->get_team() != c.get_team() && part->is_head())) continue;
                distance[ni] = distance[pi] + 1;
                first[ni] = pi == index(here) ? d : first[pi];
                predecessor[ni] = pi;
                arrival[ni] = d;
                if (n_reached < 64) reached[n_reached++] = ni;
                if (!part && hi < BFS_Q) q[hi++] = n;
            }
        }
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
            else if (edge > 0 && !portal_occupied(edge - 1)) ++n;
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
                return !choke_pays(sact) || entry_dooms_child(p, arr_d, ssz, sact);
        }
        bool small = c.get_length() <= 3 && (!s.alpha || !s.growing);
        if (!small || size < 5) return true;
        return pearls == 0 && size < 12;
    }

    // v5.5: `static_only` judges the map alone (dragon bodies do not count as walls): a dead-end corridor, not a gap
    // between bodies that will have moved on in a few rounds.
    // v5.5: a dead-end corridor of the map itself that this dragon may not enter (any length > 2; a 2-long dragon
    // when no pearl lies in it now).
    bool static_dead_end(Position p, int arr_d) const {
        int sp = 0, ssz = 0, sact = 0;
        return sealed_pocket(p, arr_d, sp, ssz, &sact, true) &&
               (!choke_pays(sact) || entry_dooms_child(p, arr_d, ssz, sact));
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
        if (F_CHOKE_LOOP && !s.alpha && len >= 5 && tail_in_corridor()) return 2;
        return len - 2;
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
        if (F_CHOKE_LOOP && round < s.deadend_until) return false;
        if (F_CHOKE2) return live >= CHOKE_MIN_PEARLS;
        return c.get_length() == 2 && live > 0;
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
            int budget = std::min(6, std::max(1, (partial ? std::max(visible_length, 4) : visible_length) - 1));

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
    bool can_split_safe(int child) const {
        return c.can_split(child) && !straddling();
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

    std::optional<Action> guaranteed_kill(bool high_value_only = false, bool safe_only = false) const {
        if (s.alpha) return {};
        if (c.get_unit_count() <= 1) return {};

        int my_len = c.get_length();
        int opp_dir = (di(c.get_dir()) + 2) % 4;

        auto trade_ok = [&](int elen, int eid) -> bool {
            if (eid <= 1 || elen >= 4) return true;
            if (high_value_only) return false;
            if (elen >= my_len) return true;
            if (my_len <= 2) return true;
            return false;
        };
        // Any dragon happily trades itself for an enemy clearly longer than itself.
        auto value_trade = [&](int elen) { return elen >= my_len + 2 && c.get_unit_count() >= 3; };

        // 1. Immediate 1-step collision (Kamikaze takes all valid trades; others take value trades)
        {
            for (const auto &e : enemies) {
                int elen = enemy_visible_length(e.get_id());
                if (safe_only && !value_trade(elen)) continue;
                if (!trade_ok(elen, e.get_id())) continue;
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
                    if (!trade_ok(elen, e.get_id())) continue;
                    if (safe_only && !value_trade(elen)) continue;
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

        // 3. True 1-Exit Corridor trap (1-step works for length >= 2; 2-step sprint works for length >= 3)
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

    bool claimed(Position p) const {
        if (s.alpha && round >= feed_round()) return false;
        int my_dist = dist(here, p);
        for (auto f : friends) {
            int k = dist(f.position, p);
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

            int pearl_risk = s.alpha ? danger(p) : 0;
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
    std::optional<FeedPlan> feed_target() const {
        std::optional<FeedPlan> best;
        double best_score = -1e20;
        int remaining = 500 - round;
        auto consider = [&](int id, Position p, int len) {
            if (id == (c.get_id() & SONAR_ID_MASK)) return;
            if (s.alpha && !superior_alpha(id, len)) return;
            int k = dist(here, p);
            if (k > remaining - 3) return;
            double score = 4.0 * len - k;
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
        remembered_q[hi++] = here;
        mem_depth[index(here)] = 0;
        mem_first[index(here)] = -1;
        while (lo < hi) {
            Position p = remembered_q[lo++];
            int pi = index(p);
            const auto &cp = s.cells[pi];
            // A visible dragon blocks; a target on it is still reached (it ends the route), as in the BFS.
            if (pi != index(here) && cp.seen == round && cp.has_dragon) continue;
            for (int d = 0; d < 4; ++d) {
                if (p == here && d == back) continue;
                int e = cp.seen < 0 ? 0 : cp.edge[d];
                if (e != 0) continue;
                Position n = step(p, d);
                int ni = index(n);
                if (mem_depth[ni] < INF) continue;
                if (s.cells[ni].seen >= 0 && chokepoint_blocked(n, d)) continue;
                mem_depth[ni] = mem_depth[pi] + 1;
                mem_first[ni] = p == here ? d : mem_first[pi];
                remembered_q[hi++] = n;
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

    Action decide() {
        observe();
        paths();

        int opp_dir = (di(c.get_dir()) + 2) % 4;
        int feed_start = feed_round();

        // Alpha consolidation: in the endgame every alpha that is not the apex yields its whole body to a
        // superior alpha, as long as it can reach it and the apex has time to eat the dropped trail.
        auto plan = round >= feed_start ? feed_target() : std::optional<FeedPlan>{};
        bool receiver_alpha = s.alpha;
        if (s.alpha && plan && c.get_unit_count() > 1 &&
            dist(here, plan->p) + c.get_length() <= (500 - round) - 3)
            receiver_alpha = false;

        auto alpha = (!receiver_alpha && plan) ? std::optional<Position>{plan->p} : std::optional<Position>{};
        bool feed = !receiver_alpha && round >= feed_start && alpha.has_value() &&
                    dist(here, *alpha) <= (500 - round) - 2;
        bool kam = is_kamikaze();
        bool sprint_hunter = is_sprint_hunter();
        bool bold = kam || skirmisher(); // ignores enemy "certain death" filters

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
            if (sealed_pocket(here, di(c.get_dir()), pocket_pearls, pocket_size)) {
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
                } else if (round - s.contend_round >= 10) {
                    s.send_reserve = enc.portal_id; // still here: renew the reservation
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
        if (F_REVERSE_SPLIT && c.get_length() >= 4 && survival_moves() == 0 && c.can_split(c.get_length() - 2)) {
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
                } else if (c.get_length() >= 4 && can_split_safe(c.get_length() - 2)) {
                    DIAG("chokefinal " << c.get_id() << ' ' << round << " len " << c.get_length() << " alpha " << s.alpha);
                    return {{}, rescue_child(), Mode::Rescue, false};
                }
            }
        }

        if (!s.alpha && !s.growing && !feed && c.get_length() >= 4 && can_split_safe(2) && child_viable() && !choke_hold &&
            !split_blocked())
            return {{}, 2, Mode::Split, false};

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
                    if (round - s.mantle_round <= MANTLE_GUARD || shorter) {
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
                if ((F_FEED_BACKOFF || F_FEED_ADAPT) && 500 - round > c.get_length() + (F_FEED_CLEAR ? LATE_FEED : 10)) {
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
        if (skirmisher() && (F_KAM_CAP ? over_cap() : c.get_unit_count() >= 3)) {
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
            if (c.get_length() >= 4 && can_split_safe(c.get_length() - 2)) {
                DIAG("chokesplit " << c.get_id() << ' ' << round << ' ' << c.get_length() << ' ' << s.alpha);
                return {{}, c.get_length() - 2, Mode::Rescue, false};
            }
            legal = dead_end;
        }

        bool threatened = (s.alpha && !small_map_splitting_alpha) &&
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
            if (s.born < round && can_split_safe(c.get_length() - 2) &&
                (no_way || (s.alpha && (incoming() || danger(here) >= 320) && !has_safe_escape && !perpendicular)))
                return {{}, c.get_length() - 2, Mode::Split, false};
        } else {
            bool perp_safe = false;
            for (int d : legal)
                if ((d % 2) != (di(c.get_dir()) % 2) && danger(step(here, d)) < 240)
                    perp_safe = true;
            if (can_split_safe(c.get_length() - 2) && (no_way || (threatened && !perp_safe)))
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
            if (!rem_pearl && F_ALPHA_MEMORY && s.alpha && !small_map_splitting_alpha) rem_pearl = alpha_memory_target();
            if (!rem_pearl) rem_pearl = committed_pearl_target();
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
            bool grab_adjacent_pearl = pearls && (c.get_length() % 2 == 1) && distance[index(*pearls)] == 1 &&
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

        // v5.6: nothing to eat in view and no errand: walk to a mirrored hotspot a teammate reported.
        if (F_MIRROR_SCOUT && !target && !feed && !threatened && !s.alpha && !s.resident && !s.camping &&
            !s.evacuating && c.get_length() <= 3 && round < feed_start && !uniform_map()) {
            if (auto g = scout_goal()) {
                target = *g;
                mode = Mode::Disperse;
            }
        }
        // v5.7 (a1): a crowd (CROWD_HEADS or more other non-alpha heads within 4) shares whatever is here; one member at a
        // time heads for a known rich spot at least 6 away, even with food in view (trophy: seven dragons sat in one handle
        // all game while the cup and the far handle filled up).
        if (F_HOTSPOT2 && F_MIRROR_SCOUT && !feed && !threatened && !s.alpha && !s.resident && !s.camping &&
            !s.evacuating && c.get_length() <= 4 && round < feed_start && !uniform_map() &&
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
        if (kam && !feed && !use_portal) {
            for (auto e : enemies) {
                bool is_enemy_alpha = (e.get_id() <= 1 || enemy_visible_length(e.get_id()) >= 4);
                if (pearls && !is_enemy_alpha) continue;

                int ed = di(e.get_dir());
                if (dist(step(e.position, ed), here) >= dist(e.position, here)) continue;
                if (e.get_dir() == c.get_dir()) continue;
                if (s.cells[index(e.position)].edge[ed] < 0) continue;

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
        bool breaking_cycle = F_CYCLE && round < s.cycle_break_until;
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
            if (!steals_alpha_pearl && !blocks_alpha_head && !certain_death_step && !portal_loiter &&
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
            int risk = s.alpha ? danger(n) : 0;
            bool trap_step = s.cells[index(here)].edge[d] == 0 && mode != Mode::Ambush &&
                             (is_dead_end_trap(n, d) || (!bold && is_enemy_certain_death(n, d)));

            bool endgame_safe = (round >= feed_start && risk == 0 && f_exits >= 1);
            bool has_food = !trap_step && !(early_portal && use_portal) && tile && tile->has_pearl() &&
                            (!feed || !alpha || dist(n, *alpha) > 5) &&
                            (!s.alpha || small_map_splitting_alpha || endgame_safe ||
                             (f_exits >= 1 && (risk < 280 || (risk < 320 && f_exits >= 2)) && (c.get_length() < 12 || area >= 5)));

            double risk_penalty = 0.0;
            if (s.alpha && !small_map_splitting_alpha) {
                if (risk >= 360)      risk_penalty = 520.0;
                else if (risk >= 320) risk_penalty = 240.0;
                else if (risk >= 280) risk_penalty = 100.0;
                else                  risk_penalty = 0.25 * risk;
            }
            double score = 2.0 * std::min(area, 10) + 4.0 * exits - risk_penalty;

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

        if (can_split_safe(c.get_length() - 2)) return {{}, c.get_length() - 2, Mode::Split, false};

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
                std::sort(active.begin(), active.end(), [&](const AlphaTrack &x, const AlphaTrack &y) {
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
                if (F_MIRROR_SCOUT && n_extra <= 2 && !uniform_map()) {
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