#include "helper.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bot {
using namespace unswbc;

constexpr int MAX_CELLS = 4096, INF = 100000;
constexpr int BFS_Q = 512;
constexpr int SONAR_TTL = 14, SONAR_ID_MASK = 8191;
const auto DIRS = Direction::get_direction_list();

inline int di(Direction d) {
    for (int i = 0; i < 4; ++i)
        if (DIRS[i] == d)
            return i;
    return 0;
}

enum class Mode { Disperse, Forage, Ambush, Escape, Portal, Reside, Return, Feed, Split, Kill, Trapped };

inline const char *name(Mode m) {
    static const char *names[] = {"disperse", "forage", "ambush", "escape", "portal", "reside",
                                  "return",   "feed",   "split",  "kill",   "trapped"};
    return names[static_cast<int>(m)];
}

struct Action {
    std::vector<Direction> moves;
    int child = 0;
    Mode mode = Mode::Forage;
    bool intentional_death = false;
};

struct Cell {
    int seen = -1, visited = -10000;
    std::array<int, 4> edge{{-2, -2, -2, -2}}; // unknown=-2, kelp=-1, open=0, portal=id+1
};

struct Portal {
    int id;
    std::vector<std::pair<Position, int>> ends;
    int occupied_until = -1;
    bool occupied = false;
};

struct AlphaTrack {
    int id, seen;
    Position p;
};

struct DragonState {
    bool initialized = false, alpha = false, growing = false, resident = false, dispersing = false, evacuating = false;
    int born = 0, last_food = 0, sector = 0, home_portal = -1, pending_portal = -1, explore_timer = 0;
    Position sector_target;
    Position explore_target;
    Position return_tile;
    Position enemy_alpha_pos{0, 0};
    int enemy_alpha_seen = -1000;
    // Rolling memory of our own recent trajectory — used to escape tight oscillation loops
    std::vector<Position> recent_path;
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

    std::array<int, MAX_CELLS> distance{}, first{}, predecessor{}, arrival{};
    mutable std::array<int, MAX_CELLS> risk_cache{};
    mutable std::array<int, MAX_CELLS> danger_depth{};
    mutable std::array<bool, MAX_CELLS> space_seen{};
    mutable std::array<int, MAX_CELLS> remembered_first{};
    mutable std::array<Position, MAX_CELLS> remembered_q{};

  public:
    DragonState s;

    Brain(Controller &controller, Game &game_state) : c(controller), g(game_state) {
        std::tie(w, h) = g.get_map_size();
        danger_depth.fill(-1);
        space_seen.fill(false);
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
        auto t = c.get_tile(p);
        return t && !t->get_dragon();
    }

    void remember_alpha(int id, Position p, int origin_round) {
        for (auto &a : s.alphas)
            if (a.id == id) {
                if (origin_round > a.seen) a = {id, origin_round, p};
                return;
            }
        s.alphas.push_back({id, origin_round, p});
    }

    int friendly_visible_length(int friend_id) const {
        int len = 0;
        for (const auto &t : c.get_tiles())
            if (t.get_dragon() && t.get_dragon()->get_id() == friend_id) ++len;
        return len;
    }

    int team_zero_id_val() const {
        return (c.get_team() == Team::A) ? 0 : 1;
    }

    bool is_primary_alpha_id(int id) const {
        return id == team_zero_id_val();
    }

    bool is_alpha(int id) const {
        if (id == c.get_id()) return s.alpha;
        if (is_primary_alpha_id(id)) return true;
        if (friendly_visible_length(id) > 7) return true;
        for (const auto &a : s.alphas)
            if (a.id == (id & SONAR_ID_MASK) && round - a.seen <= SONAR_TTL) return true;
        return false;
    }

    int kamikaze_threshold() const {
        int n = std::max(w, h);
        return n <= 12 ? 4 : n < 20 ? 8 : n <= 26 ? 18 : n <= 35 ? 28 : n <= 50 ? 38 : 46;
    }

    int alpha_split_cap() const {
        int n = std::max(w, h);
        return n <= 12 ? 42 : n < 20 ? 36 : n <= 26 ? 24 : n <= 35 ? 18 : n <= 50 ? 12 : 8;
    }

    bool is_kamikaze() const {
        return !s.alpha && c.get_unit_count() > kamikaze_threshold() && c.get_length() == 2;
    }

    void occupy(int id) {
        auto &p = portal(id);
        p.occupied = true;
        p.occupied_until = INF;
    }

    int center_dist(Position p) const {
        return std::abs(p.x - w / 2) + std::abs(p.y - h / 2);
    }

    Position sector_waypoint(int sec) const {
        int cx = w / 2, cy = h / 2;
        bool large_map = (std::max(w, h) > 35);
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

    int threshold() const {
        int n = std::max(w, h);
        return std::clamp(480 - 6 * n, 120, 410);
    }

    std::uint32_t alpha_packet(int id, Position p, int origin_round) const {
        unsigned signature = c.get_team() == Team::A ? 0u : 1u;
        return (signature << 30) | ((static_cast<unsigned>(id) & SONAR_ID_MASK) << 17) |
               ((static_cast<unsigned>(origin_round) & 31u) << 12) |
               (static_cast<unsigned>(p.x) << 6) | static_cast<unsigned>(p.y);
    }

    int enemy_visible_length(int enemy_id) const {
        int len = 0;
        for (const auto &t : c.get_tiles())
            if (t.get_dragon() && t.get_dragon()->get_id() == enemy_id) ++len;
        return len;
    }

    bool enclosed_nursery() const {
        remembered_first.fill(-1);
        int lo = 0, hi = 0;
        bool has_portal = false;
        remembered_q[hi++] = here;
        remembered_first[index(here)] = 0;
        while (lo < hi) {
            Position p = remembered_q[lo++];
            if (hi > std::min(49, w * h / 4)) return false;
            for (int d = 0; d < 4; ++d) {
                int e = s.cells[index(p)].edge[d];
                if (e == -2) return false;
                if (e > 0) has_portal = true;
                if (e != 0) continue;
                auto n = step(p, d);
                if (remembered_first[index(n)] >= 0) continue;
                remembered_first[index(n)] = 0;
                remembered_q[hi++] = n;
            }
        }
        return has_portal;
    }

    void initialize() {
        s.initialized = true;
        s.born = round;
        s.last_food = round;

        s.alpha = is_primary_alpha_id(c.get_id());
        s.evacuating = !s.alpha && c.get_id() > 1 && enclosed_nursery();

        s.sector = (c.get_id() / 2) % 8;
        s.sector_target = sector_waypoint(s.sector);
        s.explore_timer = 0;
        s.dispersing = true;
    }

    void observe() {
        round = g.get_round_num();
        here = c.get_position();
        friends.clear();
        enemies.clear();
        risk_cache.fill(-1);

        auto cur_tile = c.get_tile(here);
        if (cur_tile && cur_tile->has_pearl()) s.last_food = round;

        for (const auto &t : c.get_tiles()) {
            auto p = t.get_position();
            auto &cell = s.cells[index(p)];
            cell.seen = round;
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
            auto part = t.get_dragon();
            if (part && part->is_head() && part->get_id() != c.get_id())
                (part->get_team() == c.get_team() ? friends : enemies).push_back(*part);
        }

        auto by_id = [](const DragonPart &a, const DragonPart &b) { return a.get_id() < b.get_id(); };
        std::sort(friends.begin(), friends.end(), by_id);
        std::sort(enemies.begin(), enemies.end(), by_id);

        if (!s.initialized) initialize();

        // Promote long survivors (>7) to Alpha and originate their own sonar.
        if (!s.alpha && c.get_length() > 7) {
            s.alpha = true;
            s.growing = true;
            s.alphas.clear();
        }

        ++s.explore_timer;
        if (dist(here, s.sector_target) <= 3 || s.explore_timer > std::clamp((w + h) / 2, 16, 64)) {
            s.dispersing = false;
            s.sector = (s.sector + 3) % 8;
            s.sector_target = sector_waypoint(s.sector);
            s.explore_timer = 0;
        }

        int repeats = static_cast<int>(std::count(s.recent_path.begin(), s.recent_path.end(), here));
        if (repeats >= 2 && !s.resident) {
            s.sector = (s.sector + 3) % 8;
            s.sector_target = sector_waypoint(s.sector);
            s.explore_timer = 0;
        }
        s.cells[index(here)].visited = round;

        // Record rolling recent path to escape tight oscillation loops
        s.recent_path.push_back(here);
        if (s.recent_path.size() > 24) s.recent_path.erase(s.recent_path.begin());

        if (s.pending_portal >= 0) {
            occupy(s.pending_portal);
            s.resident = !s.evacuating;
            s.home_portal = s.resident ? s.pending_portal : -1;
            s.return_tile = here;
            s.evacuating = false;
            s.last_food = round;
            s.pending_portal = -1;
        }

        for (auto f : friends) {
            if (!s.alpha && is_primary_alpha_id(f.get_id())) remember_alpha(f.get_id(), f.position, round);
            else if (!s.alpha && is_alpha(f.get_id())) remember_alpha( (f.get_id() & SONAR_ID_MASK), f.position, round);
            int e = s.cells[index(f.position)].edge[(di(f.get_dir()) + 2) % 4];
            if (e > 0 && round > s.born) {
                bool changed = true;
                for (auto prev : s.previous_heads)
                    if (prev.get_id() == f.get_id() && prev.position == f.position) changed = false;
                if (changed)
                    occupy(e - 1);
            }
        }

        // Enemy Alpha tracking
        for (const auto &e : enemies) {
            if (e.get_id() <= 1 || enemy_visible_length(e.get_id()) >= 6) {
                s.enemy_alpha_pos = e.position;
                s.enemy_alpha_seen = round;
            }
        }

        // 2-bit team signature, 13-bit global dragon ID, 5-bit source round,
        // 6-bit x and y. A 14-round lifetime is shorter than half the round
        // modulus, so hop-by-hop relays cannot rejuvenate expired packets.
        unsigned team_sig = c.get_team() == Team::A ? 0u : 1u;
        if (!s.alpha) for (auto msg : c.get_sonar_messages()) {
            if ((msg >> 30) != team_sig) continue;
            Position p{static_cast<int>((msg >> 6) & 63u), static_cast<int>(msg & 63u)};
            if (p.x >= w || p.y >= h) continue;
            int age = (round - static_cast<int>((msg >> 12) & 31u)) & 31;
            if (age > SONAR_TTL || age > round) continue;
            int alpha_id = static_cast<int>((msg >> 17) & SONAR_ID_MASK);
            remember_alpha(alpha_id, p, round - age);
        }

        s.previous_heads = friends;
        s.alphas.erase(std::remove_if(s.alphas.begin(), s.alphas.end(),
                                      [&](const AlphaTrack &a) { return round - a.seen > SONAR_TTL; }),
                       s.alphas.end());

        s.growing = s.alpha && (round > threshold() || c.get_unit_count() >= alpha_split_cap());
    }

    bool portal_occupied(int id) const {
        for (const auto &p : s.portals)
            if (p.id == id) return p.occupied;
        return false;
    }

    void paths() {
        int n_cells = w * h;
        std::fill_n(distance.begin(), n_cells, INF);
        std::fill_n(first.begin(), n_cells, -1);
        std::fill_n(predecessor.begin(), n_cells, -1);
        std::fill_n(arrival.begin(), n_cells, -1);
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
                int ni = index(n);
                auto t = c.get_tile(n);
                if (!t || distance[ni] != INF) continue;
                auto part = t->get_dragon();
                if (part && !(part->get_team() != c.get_team() && part->is_head())) continue;
                distance[ni] = distance[pi] + 1;
                first[ni] = pi == index(here) ? d : first[pi];
                predecessor[ni] = pi;
                arrival[ni] = d;
                if (!part && hi < BFS_Q) q[hi++] = n;
            }
        }
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

    bool is_step_safe(int d, bool allow_enemy_head = false) const {
        int opp_dir = (di(c.get_dir()) + 2) % 4;
        if (d == opp_dir) return false;

        int edge = s.cells[index(here)].edge[d];
        if (edge < 0) return false;

        if (edge > 0) {
            if (s.resident || (portal_occupied(edge - 1) && !s.evacuating)) return false;
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
        auto dragon = t->get_dragon();
        if (dragon) {
            if (dragon->get_id() == c.get_id()) return false;
            if (dragon->get_team() == c.get_team()) return false;
            if (!dragon->is_head()) return false;
            if (!allow_enemy_head) return false;
        }
        return true;
    }

    std::optional<Action> guaranteed_kill(bool high_value_only = false) const {
        if (s.alpha) return {};
        if (c.get_unit_count() <= 1) return {};

        int my_len = c.get_length();
        int opp_dir = (di(c.get_dir()) + 2) % 4;

        auto trade_ok = [&](int elen) -> bool {
            if (high_value_only && elen < 4 && c.get_unit_count() <= 1) return false;
            if (elen >= my_len) return true;
            if (my_len <= 3) return true;
            if (elen + 2 >= my_len && c.get_unit_count() > 1) return true;
            return false;
        };

        // 1. Immediate 1-step collision
        for (const auto &e : enemies) {
            int elen = enemy_visible_length(e.get_id());
            if (high_value_only && e.get_id() > 1 && elen < 4) continue;
            if (!trade_ok(elen)) continue;
            for (int d = 0; d < 4; ++d) {
                if (d == opp_dir) continue;
                auto dest = destination(here, d);
                if (dest && *dest == e.position && is_step_safe(d, true))
                    return Action{{DIRS[d]}, 0, Mode::Kill, true};
            }
        }

        // 2. Sprint kill (2..5 steps onto a stationary enemy head)
        int max_sprint = std::min(5, std::max(1, my_len - 1));
        for (int k = 2; k <= max_sprint; ++k) {
            for (const auto &e : enemies) {
                int elen = enemy_visible_length(e.get_id());
                if (high_value_only && e.get_id() > 1 && elen < 4) continue;
                if (distance[index(e.position)] != k) continue;
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
                    auto t_inter = c.get_tile(cur);
                    if (!t_inter || t_inter->get_dragon()) { clear_path = false; break; }
                    prev_d = step_d;
                }
                if (clear_path && di(candidate[k - 1]) != (prev_d + 2) % 4)
                    return Action{candidate, 0, Mode::Kill, true};
            }
        }

        // 3. Predict higher-ID enemies (they haven't acted yet this turn)
        for (const auto &e : enemies) {
            if (e.get_id() < c.get_id()) continue;
            int ed = di(e.get_dir());
            if (s.cells[index(e.position)].edge[ed] < 0) continue;
            Position predicted = step(e.position, ed);
            if (!empty(predicted)) continue;
            if (distance[index(predicted)] != 1) continue;
            int d = first[index(predicted)];
            if (d >= 0 && d != opp_dir && is_step_safe(d, true))
                return Action{{DIRS[d]}, 0, Mode::Kill, false};
        }

        // 4. Corridor trap
        if (my_len >= 3) {
            for (const auto &e : enemies) {
                int elen = enemy_visible_length(e.get_id());
                if (high_value_only && e.get_id() > 1 && elen < 4) continue;
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
                    } else if (distance[index(forced_tile)] == 2) {
                        auto candidate = route(forced_tile);
                        if (candidate.size() == 2) {
                            int d1 = di(candidate[0]);
                            int d2 = di(candidate[1]);
                            if (d1 != opp_dir && d2 != (d1 + 2) % 4 &&
                                s.cells[index(here)].edge[d1] == 0) {
                                Position mid = step(here, d1);
                                auto t_mid = c.get_tile(mid);
                                if (t_mid && !t_mid->get_dragon())
                                    return Action{candidate, 0, Mode::Kill, false};
                            }
                        }
                    }
                }
            }
        }

        return {};
    }

    bool claimed(Position p) const {
        int my_dist = dist(here, p);
        for (auto f : friends) {
            int k = dist(f.position, p);
            if (!s.alpha && is_alpha(f.get_id()) && k <= 3 && round > threshold()) return true;
            if (k < my_dist || (k == my_dist && f.get_id() < c.get_id())) return true;
        }
        return false;
    }

    std::optional<Position> food_target(bool future) const {
        std::optional<Position> best;
        double bestscore = -1e20;
        for (const auto &t : c.get_tiles()) {
            auto p = t.get_position();
            int n = distance[index(p)];
            if (n < 1 || n > 10 || t.get_dragon()) continue;
            if (future ? !(t.get_pearl_time() >= 1 && t.get_pearl_time() <= 2) : !t.has_pearl()) continue;
            if (claimed(p)) continue;

            int exits = escape_count(p);
            if (exits == 0) continue;

            int pearl_risk = s.alpha ? danger(p) : 0;
            bool endgame_safe_feed = (round >= 415 && pearl_risk == 0 && exits >= 1);
            if (s.alpha && !endgame_safe_feed && (pearl_risk >= 100 || exits < 2 || (c.get_length() >= 12 && space(p) < 6)))
                continue;

            double cluster = 0;
            for (const auto &other : c.get_tiles())
                if (other.has_pearl()) {
                    int k = chebyshev(p, other.get_position());
                    if (k <= 2) cluster += (k == 0 ? 0 : 2.0 / k);
                }

            double risk_weight = s.alpha ? 0.08 : 0.0;
            double center_bonus = (!s.alpha) ? 0.35 * ((w + h) / 2 - center_dist(p)) : 0.0;
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

    std::optional<PortalPlan> portal_target(bool force) const {
        std::optional<PortalPlan> best;
        for (const auto &t : c.get_tiles())
            for (int d = 0; d < 4; ++d) {
                auto p = t.get_position();
                int e = s.cells[index(p)].edge[d];
                if (e <= 0 || distance[index(p)] == INF || (p != here && t.get_dragon())) continue;
                if (!force && !s.evacuating && portal_occupied(e - 1)) continue;
                if (p == here && d == (di(c.get_dir()) + 2) % 4) continue;
                if (s.resident) continue;
                auto exit = destination(p, d);
                if (exit) {
                    auto tile = c.get_tile(*exit);
                    if (tile && tile->get_dragon()) continue;
                }
                int cost = distance[index(p)] + 1;
                if (!best || cost < best->cost || (cost == best->cost && e - 1 < best->id))
                    best = PortalPlan{p, d, e - 1, cost};
            }
        return best;
    }

    std::optional<Position> feed_target() const {
        std::optional<Position> target;
        int best_dist = INF;
        for (const auto &a : s.alphas) {
            if (round - a.seen > SONAR_TTL) continue;
            int k = dist(here, a.p);
            if (k < best_dist) { best_dist = k; target = a.p; }
        }
        for (const auto &f : friends) {
            if (!is_alpha(f.get_id())) continue;
            int k = dist(here, f.position);
            if (k <= best_dist) { best_dist = k; target = f.position; }
        }
        return target;
    }

    std::optional<Position> exploration_target() const {
        std::optional<Position> best;
        double best_score = -1e20;
        for (const auto &t : c.get_tiles()) {
            auto p = t.get_position();
            int path = distance[index(p)];
            if (path < 1 || path >= INF || t.get_dragon()) continue;
            int exits = escape_count(p);
            if (!exits) continue;
            int age = std::min(80, round - s.cells[index(p)].visited);
            int unseen = 0;
            for (int d = 0; d < 4; ++d)
                if (s.cells[index(p)].edge[d] == 0 && s.cells[index(step(p,d))].seen < 0) ++unseen;
            double score = 0.7 * age + 16 * unseen + 5 * std::min(exits, 3) - 2 * path;
            score += 3 * (dist(here, s.sector_target) - dist(p, s.sector_target));
            if (s.alpha) score -= 0.15 * danger(p);
            for (const auto &f : friends) score -= 6 * std::max(0, 4 - dist(p, f.position));
            if (score > best_score) { best_score = score; best = p; }
        }
        return best;
    }

    int remembered_direction(Position target) const {
        remembered_first.fill(-1);
        int lo = 0, hi = 0;
        remembered_q[hi++] = here;
        remembered_first[index(here)] = 4;
        int best_frontier_dir = -1;
        int best_frontier_dist = INF;

        while (lo < hi) {
            auto p = remembered_q[lo++];
            for (int d = 0; d < 4; ++d)
                if (s.cells[index(p)].edge[d] == 0) {
                    if (p == here && d == (di(c.get_dir()) + 2) % 4) continue;
                    auto n = step(p, d);
                    auto t = c.get_tile(n);
                    if (t && t->get_dragon()) continue;
                    int first_d = (p == here) ? d : remembered_first[index(p)];
                    if (n == target) return first_d;
                    if (s.cells[index(n)].seen < 0) {
                        int fd = dist(n, target);
                        if (fd < best_frontier_dist) { best_frontier_dist = fd; best_frontier_dir = first_d; }
                        continue;
                    }
                    if (remembered_first[index(n)] >= 0) continue;
                    remembered_first[index(n)] = first_d;
                    if (hi < MAX_CELLS) remembered_q[hi++] = n;
                    int fd = dist(n, target);
                    if (fd < best_frontier_dist) { best_frontier_dist = fd; best_frontier_dir = first_d; }
                }
        }
        return best_frontier_dir;
    }

    Action decide() {
        observe();
        paths();

        int opp_dir = (di(c.get_dir()) + 2) % 4;
        int feed_round = 415;
        bool receiver_alpha = s.alpha;
        auto alpha = receiver_alpha ? std::optional<Position>{} : feed_target();
        bool feed = !receiver_alpha && round >= feed_round && alpha.has_value();
        bool kam = is_kamikaze();

        if (!s.alpha && c.get_length() >= 4 && c.can_split(2))
            return {{}, 2, Mode::Split, false};

        // New children claim nearby portals before foraging or combat. A child
        // born in a fully observed chamber may cross its occupied exit.
        if (!s.alpha && !s.resident && (s.evacuating || round - s.born < 8)) {
            auto early = portal_target(false);
            if (early) {
                int d = early->approach == here ? early->direction : first[index(early->approach)];
                if (d >= 0 && is_step_safe(d)) return {{DIRS[d]}, 0, Mode::Portal, false};
            }
        }

        // Deliver only to a live, visible Alpha. Never chase a single elected receiver.
        if (feed && c.get_unit_count() > 2) {
            for (const auto &f : friends) {
                int gap = dist(here, f.position);
                if (!is_alpha(f.get_id()) || gap > 3) continue;
                if (gap > 1 && round < 490) {
                    for (int d = 0; d < 4; ++d) {
                        if (s.cells[index(here)].edge[d] != 0 || !is_step_safe(d)) continue;
                        auto n = step(here, d);
                        auto tile = c.get_tile(n);
                        if (tile && !tile->has_pearl() && danger(n) == 0 && dist(n, f.position) < gap)
                            return {{DIRS[d]}, 0, Mode::Feed, false};
                    }
                }
                return {{DIRS[opp_dir]}, 0, Mode::Feed, true};
            }
        }

        // 0. High-value kill — strictly only for length-2 Kamikaze bots above the map-scaled threshold
        if (kam) {
            if (auto hv_kill = guaranteed_kill(true)) return *hv_kill;
        }

        bool medium_map = (std::max(w, h) >= 20 && std::max(w, h) <= 35);

        // 1. Splitting
        if (!s.growing && c.get_length() >= 4 && c.can_split(2)) {
            return {{}, 2, Mode::Split, false};
        }

        // 2. Standard kill — strictly only for length-2 Kamikaze bots above the map-scaled threshold
        if (kam && !feed) {
            if (auto kill = guaranteed_kill(false)) return *kill;
        }

        bool allow_enemy_head = kam;
        std::vector<int> legal;
        for (int d = 0; d < 4; ++d)
            if (is_step_safe(d, allow_enemy_head)) legal.push_back(d);

        bool threatened = s.alpha && (incoming() || danger(here) >= 100), perpendicular = false;
        bool has_safe_escape = false;
        for (int d : legal) {
            if (danger(step(here, d)) < 160) has_safe_escape = true;
            if ((d % 2) != (di(c.get_dir()) % 2) && danger(step(here, d)) < 160)
                perpendicular = true;
        }

        if (medium_map) {
            if (s.born < round && c.can_split(c.get_length() - 2) &&
                (legal.empty() || (s.alpha && (incoming() || danger(here) >= 280) && !has_safe_escape && !perpendicular)))
                return {{}, c.get_length() - 2, Mode::Split, false};
        } else {
            bool perp100 = false;
            for (int d : legal)
                if ((d % 2) != (di(c.get_dir()) % 2) && danger(step(here, d)) < 100)
                    perp100 = true;
            if (c.can_split(c.get_length() - 2) && (legal.empty() || (threatened && !perp100)))
                return {{}, c.get_length() - 2, Mode::Split, false};
        }

        auto pearls = food_target(false);
        auto future = pearls ? std::optional<Position>{} : food_target(true);
        auto port = (!s.alpha) ? portal_target(false) : std::optional<PortalPlan>{};

        if (feed && pearls && alpha && dist(*pearls, *alpha) <= 5) pearls = std::nullopt;

        bool early_portal = !s.alpha && (s.evacuating || round - s.born < 8) && !s.resident;
        bool use_portal = port && ((!pearls && !future) || early_portal);

        std::optional<Position> target = pearls ? pearls : future;
        Mode mode = s.resident ? Mode::Reside : Mode::Forage;

        if (feed) {
            target = alpha;
            mode = Mode::Feed;
            use_portal = false;
        }
        if (use_portal) {
            mode = Mode::Portal;
            if (port->approach == here) {
                if (is_step_safe(port->direction, allow_enemy_head)) {
                    s.pending_portal = port->id;
                    occupy(port->id);
                    return {{DIRS[port->direction]}, 0, mode, false};
                }
            } else target = port->approach;
        }
        if (threatened) mode = Mode::Escape;

        // During endgame (round >= 415), Alpha stays near approaching feeders instead of marching away
        if (receiver_alpha && round >= feed_round && !target && !threatened) {
            int best_f = INF;
            for (const auto &f : friends) {
                int df = dist(here, f.position);
                if (df >= 2 && df < best_f) {
                    best_f = df;
                    target = f.position;
                }
            }
        }

        bool patrol_only = false;
        if (!target && !feed && !threatened && !(receiver_alpha && round >= feed_round && !friends.empty()) &&
            dist(here, s.sector_target) > 2) {
            target = exploration_target();
            if (!target) target = s.sector_target;
            mode = s.dispersing ? Mode::Disperse : Mode::Forage;
            patrol_only = true;
        }

        // Ambush — only Kamikaze bots above the map-scaled threshold engage
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
            if (mode != Mode::Ambush && !pearls && round - s.enemy_alpha_seen <= 4 &&
                dist(here, s.enemy_alpha_pos) <= 7) {
                target = s.enemy_alpha_pos;
                mode = Mode::Ambush;
                patrol_only = false;
            }
        }

        int target_direction = target ? first[index(*target)] : -1;
        if (target && target_direction < 0) target_direction = remembered_direction(*target);

        // Direct BFS path for pearl/portal/feed targets (avoiding pearls reserved for Alpha)
        if ((!s.alpha || feed) && target && (pearls || future || use_portal || feed) &&
            (use_portal || feed || std::count(s.recent_path.begin(), s.recent_path.end(), here) < 3) && target_direction >= 0 &&
            is_step_safe(target_direction, allow_enemy_head)) {
            Position next_td = step(here, target_direction);
            auto tile_td = c.get_tile(next_td);
            bool steals_alpha_pearl = (feed && alpha && tile_td && tile_td->has_pearl() && dist(next_td, *alpha) <= 5);
            int edge_td = s.cells[index(here)].edge[target_direction];
            if (!steals_alpha_pearl && (edge_td > 0 || escape_count(next_td) >= 1))
                return {{DIRS[target_direction]}, 0, mode, false};
        }

        double best = -1e30;
        int chosen = -1;
        int cur_dir = di(c.get_dir());

        for (int d : legal) {
            auto dest_opt = destination(here, d);
            if (!dest_opt) continue;
            auto n = *dest_opt;
            auto tile = c.get_tile(n);
            int exits = escape_count(n), area = space(n), risk = s.alpha ? danger(n) : 0;

            bool endgame_safe = (round >= 415 && risk == 0 && exits >= 1);
            bool has_food = !(early_portal && use_portal) && tile && tile->has_pearl() && (!feed || !alpha || dist(n, *alpha) > 5) &&
                            (!s.alpha || endgame_safe || (risk < 100 && exits >= 2 && (c.get_length() < 12 || area >= 6)));

            double risk_factor = s.alpha ? 2.5 : 0.0;
            double score = 2.0 * std::min(area, 10) + 4.0 * exits - risk * risk_factor;

            if (exits == 0) score -= 800;
            else if (exits == 1) score -= (s.alpha && !(round >= 415 && risk == 0)) ? 80 : 15;

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

            if (has_food) score += 120;
            else if (feed && alpha && tile && tile->has_pearl() && dist(n, *alpha) <= 5) score -= 350.0;

            if (tile && !tile->has_pearl() && tile->get_pearl_time() == 1) {
                if (distance[index(n)] <= 2) score += 25;
                else score -= 15;
            }

            // Productive moves override revisit penalties; exploration uses the latest visit.
            bool bypass_anti_cycle = has_food || feed || use_portal || s.resident ||
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

            if (!feed && !(s.alpha && round >= 415)) {
                for (auto f : friends) {
                    int d_team = dist(n, f.position);
                    if (d_team <= 4) score -= 6.0 * (5 - d_team);
                }
            }

            if (score > best) { best = score; chosen = d; }
        }

        if (chosen >= 0) return {{DIRS[chosen]}, 0, mode, false};

        for (int d = 0; d < 4; ++d)
            if (is_step_safe(d, false)) {
                int edge = s.cells[index(here)].edge[d];
                if (edge > 0) {
                    occupy(edge - 1);
                    return {{DIRS[d]}, 0, Mode::Portal, false};
                }
            }

        auto forced = portal_target(true);
        if (forced && forced->approach == here && forced->direction != opp_dir) {
            occupy(forced->id);
            return {{DIRS[forced->direction]}, 0, Mode::Portal, false};
        }

        if (c.can_split(c.get_length() - 2)) return {{}, c.get_length() - 2, Mode::Split, false};

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

    void execute(const Action &a) {
        Position post = here;
        if (a.child) {
            c.do_split(a.child);
        } else {
            for (auto d : a.moves) {
                int e = s.cells[index(post)].edge[di(d)];
                auto n = destination(post, di(d));
                if (e > 0) { s.pending_portal = e - 1; occupy(e - 1); }
                if (!n) break;
                auto t = c.get_tile(*n);
                if (t && t->has_pearl()) s.last_food = round;
                post = *n;
            }

            if (a.moves.empty()) c.make_move(c.get_dir());
            else if (a.moves.size() == 1) c.make_move(a.moves.front());
            else c.make_moves(a.moves);
        }

        if (!a.intentional_death && (s.pending_portal < 0 ||
            (!a.moves.empty() && destination(here, di(a.moves.front())).has_value()))) {
            if (s.alpha) {
                c.send_sonar(alpha_packet(c.get_id(), post, round));
            } else {
                const AlphaTrack *best_a = nullptr;
                for (const auto &ai : s.alphas) {
                    if (round - ai.seen <= SONAR_TTL &&
                        (!best_a || dist(post, ai.p) < dist(post, best_a->p) ||
                         (dist(post, ai.p) == dist(post, best_a->p) && ai.seen > best_a->seen)))
                        best_a = &ai;
                }
                if (best_a) c.send_sonar(alpha_packet(best_a->id, best_a->p, best_a->seen));
            }
        }

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