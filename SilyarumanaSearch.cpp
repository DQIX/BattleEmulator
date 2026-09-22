#include "SilyarumanaSearch.h"
#include "BattleEmulator.h"
#include "lcg.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <numeric>
#include <tuple>
#include <vector>

namespace SilyarumanaSearch {
namespace {
using BE = BattleEmulator;
using Clock = std::chrono::steady_clock;
thread_local unsigned long long lastNodes = 0;
constexpr uint32_t NoPath = UINT32_MAX;
constexpr int ActionCapacity = 350;

// Enumerate semantic fields, not struct padding. In particular all status timers,
// item counts, actual equipment and special-charge flags belong to the state.
auto fields(const Player& p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis, p.paralysisLevel,
        p.paralysisTurns, p.SpecialMedicineCount, p.defence, p.sleeping,
        p.sleepingTurn, p.BuffLevel, p.BuffTurns, p.hasMagicMirror, p.MagicMirrorTurn,
        p.AtkBuffLevel, p.AtkBuffTurn, p.TensionLevel, p.rage, p.SageElixirCount,
        p.ElfinElixirCount, p.MagicWaterCount, p.speedTurn, p.speedLevel,
        p.PoisonTurn, p.PoisonEnable, p.SpecialAntidoteCount, p.acrobaticStar,
        p.acrobaticStarTurn, p.DazzleTurns, p.DazzleLevel, p.BarrierLevel,
        p.BarrierTurns, p.isStunned, p.rageTurns);
}
uint64_t bits(double v) { uint64_t b; std::memcpy(&b, &v, sizeof(b)); return b; }
template<class T> uint64_t bits(T v) { return static_cast<uint64_t>(v); }
void mix(uint64_t& h, uint64_t x) { h = (h ^ x) * 0x100000001b3ULL; }
void hashPlayer(uint64_t& h, const Player& p) {
    std::apply([&](const auto&... x) { (mix(h, bits(x)), ...); }, fields(p));
}
struct Node {
    Player p[2];
    uint64_t state = 0;
    uint64_t hash = 0;
    int cursor = 1;            // RNG cursor, NOT the objective.
    int records = 0;           // Actual accumulated BattleResult.position.
    int changes = 0;
    int depth = 0;             // Full prefix + suffix turns.
    uint32_t path = NoPath;
    int action = 0;
    float score = 0;
};
struct Link { uint32_t parent; int action; };
uint64_t hashNode(const Node& n) {
    uint64_t h = 0xcbf29ce484222325ULL;
    mix(h, n.state); mix(h, n.cursor);
    hashPlayer(h, n.p[0]); hashPlayer(h, n.p[1]);
    return h;
}
bool sameState(const Node& a, const Node& b) {
    return a.cursor == b.cursor && a.state == b.state &&
           fields(a.p[0]) == fields(b.p[0]) && fields(a.p[1]) == fields(b.p[1]);
}
bool bare(const Player& p) { return p.defaultATK == BE::SILYARUMANA_BARE_HANDS_ATK; }
bool carriedRest(const Player& p) { return p.isStunned || p.sleeping || p.paralysis; }
bool chargeAvailable(const Player& p) {
    return p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn >= 1;
}
int changeCount(const BattleResult& r) {
    int c = 0;
    for (int i = 0; i < r.position; ++i)
        if (!r.isEnemy[i] && (r.actions[i] & BE::ACTION_EQUIPMENT_CHANGED)) ++c;
    return c;
}

// Candidate conditions live here, not in BattleEmulator's hot path.
int candidates(const Node& n, std::array<int, 24>& out) {
    const Player& p = n.p[0];
    const int currentEquipment = bare(p) ? BE::ACTION_BARE_HANDS : 0;
    if (carriedRest(p)) {
        out[0] = BE::ATTACK_ALLY | currentEquipment;
        return 1;
    }
    int count = 0;
    auto add = [&](int action, bool weaponOnly = false) {
        if (!weaponOnly || !bare(p)) out[count++] = action | currentEquipment;
        if (!weaponOnly || bare(p)) out[count++] = action | (currentEquipment ^ BE::ACTION_BARE_HANDS);
    };
    // All original useful actions remain available, but real MP costs are used.
    // MP 1..3 is not permission to exploit the emulator's permissive <=0 check.
    if (p.mp >= 4) add(BE::MULTITHRUST, true);
    if (p.TensionLevel < 4) add(BE::PSYCHE_UP_ALLY);
    if (p.AtkBuffLevel < 2) add(BE::DOUBLE_UP);
    if (chargeAvailable(p) && (p.DazzleLevel != 0 || p.hp < p.maxHp || p.BuffLevel < 0))
        add(BE::GOSPEL_SONG);
    if (p.MagicWaterCount > 0 && p.mp < p.maxMp) add(BE::MAGIC_WATER);
    if (p.SpecialMedicineCount > 0 && p.hp < p.maxHp) add(BE::SPECIAL_MEDICINE);
    if (p.mp >= 4 && p.hp < p.maxHp) add(BE::MIDHEAL);
    if (p.mp >= 3 && p.BuffLevel < 2) add(BE::BUFF);
    add(BE::DEFENCE);
    add(BE::FLEE_ALLY);
    return count;
}

// Ranking only: none of these estimates change damage, RNG, victory or the
// lexicographic objective. Natural dazzle expiry and Gospel are both searched.
float rank(const Node& n, int variant) {
    const auto& p = n.p[0];
    const auto& e = n.p[1];
    const double hp = static_cast<double>(p.hp) / std::max(1, p.maxHp);
    double score;
    if (variant == 1) {
        score = 8.0 * e.hp / std::max(1, e.maxHp) - 1.10 * p.TensionLevel -
                0.62 * p.AtkBuffLevel;
    } else {
        constexpr double tm[5] = {1, 1.5, 2.5, 4, 6};
        constexpr double am[5] = {0.5, 0.75, 1, 1.25, 1.5};
        score = 1000;
        const int level = std::clamp(p.TensionLevel, 0, 4);
        for (int buff = 0; buff < 2; ++buff) {
            if (buff && p.AtkBuffLevel >= 2) continue;
            const int al = buff ? 2 : std::clamp(p.AtkBuffLevel, -2, 2);
            const double attack = BE::SILYARUMANA_EQUIPPED_ATK * am[al + 2];
            const double hit = std::max(1.0, (2 * attack - e.def) * 0.125);
            const double plainVolley = 3.6 * hit;
            for (int tension = level; tension <= 4; ++tension) {
                const double damage = 3.6 * (hit * tm[tension] + tension * 3);
                const double remaining = std::max(0.0, e.hp - damage);
                double future = remaining / std::max(1.0, plainVolley);
                // Future charge cycles can beat repeated zero-tension volleys.
                for (int t = 1; t <= 4; ++t)
                    future = std::min(future, remaining * (t + 1) /
                        (3.6 * (hit * tm[t] + t * 3)));
                const double estimate = buff + (tension - level) +
                    std::min(1.0, e.hp / std::max(1.0, damage)) + future;
                score = std::min(score, estimate);
            }
        }
    }
    score += (1 - hp) * (variant == 3 ? 1.1 : 0.7);
    if (p.hp < p.maxHp * 0.30) score += 0.65;
    score -= p.BuffLevel * 0.16;
    if (carriedRest(p)) score += 1.2;
    if (p.DazzleLevel) {
        score += chargeAvailable(p) ? 0.5 :
            0.6 + 0.32 * std::clamp(p.DazzleTurns, 0, 5);
    }
    if (p.mp < 4) score += p.MagicWaterCount > 0 ? 1.0 : 3.0;
    else if (p.mp < 8) score += 0.15;
    if (chargeAvailable(p)) score -= 0.12;
    score += n.changes * 0.012;
    return static_cast<float>(score);
}
int bucket(const Node& n) {
    const auto& p = n.p[0];
    return (((std::clamp(p.TensionLevel, 0, 4) * 3 +
        (p.AtkBuffLevel >= 2 ? 2 : p.AtkBuffLevel > 0 ? 1 : 0)) * 2 +
        (p.DazzleLevel != 0)) * 2 + (p.isStunned ? 1 : 0)) * 2 + bare(p);
}

class Search {
public:
    const Player* initial;
    uint64_t seed;
    Result result;
    Node root;
    std::array<int, ActionCapacity> prefix{};
    std::array<int, ActionCapacity> work{};
    Clock::time_point start, deadline;
    BattleResult step;
    bool timedOut = false;

    Search(const Player* p, uint64_t s, int budget, int variant)
        : initial(p), seed(s), start(Clock::now()),
          deadline(start + std::chrono::milliseconds(std::max(1, budget - 20))) {
        result.variant = variant;
        root.p[0] = p[0]; root.p[1] = p[1];
        prefix.fill(-1); work.fill(-1);
    }
    bool expired() {
        if (Clock::now() >= deadline) timedOut = true;
        return timedOut;
    }
    bool room(const Node& n) const {
        // Respect the existing fixed logging/RNG buffers; do not extend world
        // storage or run implicit ATTACK turns past the supplied action array.
        return n.depth < ActionCapacity - 1 && n.records <= 396 && n.cursor < 4700;
    }
    bool prepare(const int actions[350]) {
        int length = 0;
        while (length < ActionCapacity && actions[length] != -1) ++length;
        if (length == ActionCapacity) { result.inputValid = false; return false; }
        result.prefixLength = length;
        std::copy_n(actions, length, prefix.begin());
        work = prefix;
        lcg::init(seed, true);
        // General length, exact prefix order. No prefix re-optimization.
        for (int t = 0; t < length; ++t) {
            if (root.p[1].hp == 0 || root.p[0].hp == 0) break;
            if (!room(root)) { result.inputValid = false; return false; }
            step.clear();
            BE::Main(&root.cursor, 1, work.data(), root.p, &step, seed,
                     nullptr, nullptr, -1, &root.state);
            root.records += step.position;
            root.changes += changeCount(step);
            root.depth = t + 1;
        }
        result.prefixHP = root.p[0].hp;
        result.prefixMP = root.p[0].mp;
        result.prefixDazzle = root.p[0].DazzleLevel;
        result.prefixStunned = root.p[0].isStunned;
        std::copy(prefix.begin(), prefix.end(), result.genome.actions);
        result.genome.AllyPlayer = root.p[0]; result.genome.EnemyPlayer = root.p[1];
        result.genome.position = root.cursor; result.genome.state = root.state;
        result.genome.turn = root.depth + 1; result.genome.processed = root.depth;
        if (root.p[1].hp == 0) {
            std::vector<Link> empty;
            accept(root, empty, false);
        }
        return root.p[0].hp > 0 && root.p[1].hp > 0 && root.depth == length;
    }
    void accept(const Node& n, const std::vector<Link>& links, bool hasLast = true) {
        if (result.victory && (n.records > result.replay.position ||
            (n.records == result.replay.position && n.changes >= result.equipmentChanges))) return;
        std::array<int, ActionCapacity> full = prefix;
        int index = n.depth;
        if (hasLast) {
            full[--index] = n.action;
            uint32_t path = n.path;
            while (path != NoPath) {
                full[--index] = links[path].action;
                path = links[path].parent;
            }
            if (index != result.prefixLength) { ++result.rejectedReplays; return; }
        }
        // Fresh initial world + unchanged full prefix + suffix, no old result.
        Player p[2] = {initial[0], initial[1]};
        int cursor = 1; uint64_t state = 0;
        // Keep full logs off the small WebAssembly call stack.
        auto freshLog = std::make_unique<BattleResult>();
        BattleResult& replay = *freshLog;
        lcg::init(seed, true);
        BE::Main(&cursor, n.depth, full.data(), p, &replay, seed,
                 nullptr, nullptr, -1, &state);
        const int changes = changeCount(replay);
        if (p[1].hp != 0 || replay.position != n.records || changes != n.changes ||
            cursor != n.cursor || state != n.state || fields(p[0]) != fields(n.p[0]) ||
            fields(p[1]) != fields(n.p[1])) { ++result.rejectedReplays; return; }
        if (result.victory && (replay.position > result.replay.position ||
            (replay.position == result.replay.position && changes >= result.equipmentChanges))) return;
        result.victory = true;
        result.replay = replay;
        result.equipmentChanges = changes;
        result.genome.AllyPlayer = p[0]; result.genome.EnemyPlayer = p[1];
        result.genome.state = state; result.genome.position = cursor;
        result.genome.turn = n.depth + 1; result.genome.processed = n.depth;
        result.genome.Initialized = true;
        std::copy(full.begin(), full.end(), result.genome.actions);
    }
    bool expand(const Node& parent, int action, Node& child) {
        child = parent;
        child.action = action;
        work[parent.depth] = action;
        step.clear();
        BE::Main(&child.cursor, 1, work.data(), child.p, &step, seed,
                 nullptr, nullptr, -1, &child.state);
        ++result.nodes;
        child.depth++;
        child.records += step.position;
        child.changes += changeCount(step);
        return child.p[0].hp > 0 || child.p[1].hp == 0;
    }
    // Layered beam with exact-state transpositions. Only surviving nodes acquire
    // parent links: action histories are not copied into every generated node.
    bool beam(size_t width, int variant) {
        bool clipped = false;
        std::vector<Node> current(1, root), next;
        std::vector<Link> links;
        std::vector<size_t> order;
        std::vector<uint32_t> seen;
        next.reserve(width * 18);
        size_t tableSize = 1;
        while (tableSize < width * 48) tableSize <<= 1;
        seen.resize(tableSize, NoPath);
        links.reserve(width * 24);
        while (!current.empty() && !expired()) {
            next.clear(); std::fill(seen.begin(), seen.end(), NoPath);
            for (const Node& parent : current) {
                if (!room(parent)) continue;
                if (result.victory && parent.records >= result.replay.position) continue;
                if ((result.nodes & 255) == 0 && expired()) return false;
                std::array<int, 24> options;
                const int count = candidates(parent, options);
                for (int i = 0; i < count; ++i) {
                    Node n;
                    if (!expand(parent, options[i], n)) continue;
                    if (n.p[1].hp == 0) { accept(n, links); continue; }
                    if (result.victory && n.records >= result.replay.position) continue;
                    n.hash = hashNode(n);
                    size_t slot = (n.hash ^ (n.hash >> 32)) & (seen.size() - 1);
                    bool duplicate = false;
                    while (seen[slot] != NoPath) {
                        Node& previous = next[seen[slot]];
                        if (n.hash == previous.hash && sameState(n, previous)) {
                            if (n.records < previous.records ||
                                (n.records == previous.records && n.changes < previous.changes)) previous = n;
                            duplicate = true; break;
                        }
                        slot = (slot + 1) & (seen.size() - 1);
                    }
                    if (!duplicate) {
                        seen[slot] = static_cast<uint32_t>(next.size());
                        next.push_back(n);
                    }
                    if ((result.nodes & 255) == 0 && expired()) return false;
                }
            }
            if (next.empty()) return !clipped;
            clipped = clipped || next.size() > width;
            order.resize(next.size());
            std::iota(order.begin(), order.end(), 0);
            for (size_t i = 0; i < next.size(); ++i) {
                next[i].score = rank(next[i], variant);
                if ((i & 511) == 0 && expired()) return false;
            }
            auto less = [&](size_t a, size_t b) {
                const Node& x = next[a]; const Node& y = next[b];
                if (x.score != y.score) return x.score < y.score;
                if (x.changes != y.changes) return x.changes < y.changes;
                return x.hash < y.hash;
            };
            if (variant != 3 && order.size() > width) {
                std::nth_element(order.begin(), order.begin() + width, order.end(), less);
                order.resize(width);
            }
            if (expired()) return false;
            std::sort(order.begin(), order.end(), less);
            if (expired()) return false;
            current.clear();
            if (variant == 3 && order.size() > width) {
                // Reserve half of the beam for distinct tactical situations;
                // fill unused quota globally. Never merge different statuses.
                std::array<int, 120> used{};
                const int quota = std::max(1, static_cast<int>(width / 120));
                std::vector<size_t> chosen;
                chosen.reserve(width);
                for (size_t i : order) {
                    const int b = bucket(next[i]);
                    if (used[b]++ < quota && chosen.size() < width / 2) chosen.push_back(i);
                }
                std::vector<unsigned char> selected(next.size(), 0);
                for (size_t i : chosen) selected[i] = 1;
                for (size_t i : order) {
                    if (chosen.size() == width) break;
                    if (!selected[i]) chosen.push_back(i);
                }
                order = std::move(chosen);
            }
            current.reserve(order.size());
            for (size_t i : order) {
                Node n = next[i];
                links.push_back({n.path, n.action});
                n.path = static_cast<uint32_t>(links.size() - 1);
                current.push_back(n);
            }
        }
        return current.empty() && !clipped;
    }
    Result run(const int actions[350], int variant) {
        if (prepare(actions)) {
            // Progressive widening reaches viable deep continuations before a
            // wider pass spends the remaining shared budget improving position.
            constexpr size_t widths[] = {256, 2048, 16384, 65536};
            const int ranking = variant == 0 ? 1 : variant;
            for (size_t width : widths) {
                if (expired()) break;
                // A completed pass that never clipped the frontier needs no
                // identical wider replay. Otherwise keep improving the incumbent.
                if (beam(width, ranking)) break;
            }
        }
        result.elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        lastNodes = result.nodes;
        return std::move(result);
    }
};
}
Result Run(const Player initial[2], uint64_t seed, const int actions[350], int budgetMs, int variant) {
    // Search owns the incumbent and the per-turn log; do not put both full
    // BattleResult buffers on the WebAssembly call stack.
    auto search = std::make_unique<Search>(initial, seed, budgetMs, variant);
    return search->run(actions, variant);
}
unsigned long long NodesUsed() { return lastNodes; }
}
