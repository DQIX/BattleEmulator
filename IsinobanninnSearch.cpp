#include "IsinobanninnSearch.h"
#include "BattleEmulator.h"
#include "lcg.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <queue>
#include <tuple>
#include <vector>

namespace IsinobanninnSearch {
namespace {
using Clock = std::chrono::steady_clock;
using BE = BattleEmulator;
thread_local Statistics statistics;

// Explicit fields avoid depending on Player padding or on the action history.
auto playerFields(const Player& p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis, p.paralysisLevel,
        p.paralysisTurns, p.SpecialMedicineCount, p.defence, p.sleeping,
        p.sleepingTurn, p.BuffLevel, p.BuffTurns, p.hasMagicMirror,
        p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn, p.TensionLevel, p.rage,
        p.SageElixirCount, p.ElfinElixirCount, p.MagicWaterCount, p.speedTurn,
        p.speedLevel, p.PoisonTurn, p.PoisonEnable, p.SpecialAntidoteCount,
        p.acrobaticStar, p.acrobaticStarTurn, p.BarrierLevel, p.BarrierTurns);
}

struct Node {
    Player players[2];
    uint64_t state = 0;
    uint64_t hash = 0;
    int cursor = 1;             // LCG cursor, never the objective.
    int depth = 0;
    int records = 0;            // Sum of actual BattleResult.position values.
    int changes = 0;            // Actual ACTION_EQUIPMENT_CHANGED records.
    int path = -1;
    double priority = 0;
};
struct Link { int parent; int action; };

bool sameWorld(const Node& a, const Node& b) {
    return a.cursor == b.cursor && a.state == b.state && a.depth == b.depth
        && a.records == b.records
        && playerFields(a.players[0]) == playerFields(b.players[0])
        && playerFields(a.players[1]) == playerFields(b.players[1]);
}

uint64_t hashWorld(const Node& n) {
    uint64_t h = 0xcbf29ce484222325ULL;
    auto mix = [&](auto value) {
        uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(value));
        h ^= bits + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };
    std::apply([&](auto... values) { (mix(values), ...); }, playerFields(n.players[0]));
    std::apply([&](auto... values) { (mix(values), ...); }, playerFields(n.players[1]));
    mix(n.cursor); mix(n.state); mix(n.depth); mix(n.records);
    return h;
}

int equipmentChanges(const BattleResult& log) {
    int count = 0;
    for (int i = 0; i < log.position; ++i)
        if (!log.isEnemy[i] && (log.actions[i] & BE::ACTION_EQUIPMENT_CHANGED)) ++count;
    return count;
}

class Search {
    const Player* initial;
    uint64_t seed;
    Options options;
    Clock::time_point started;
    Clock::time_point deadline;
    int prefixLength = 0;
    std::array<int, 350> prefix{};
    std::array<int, 350> scratch{};
    std::vector<Link> paths;
    std::vector<std::array<Node, 24>> dfsScratch;
    BattleResult stepLog;
    Node root;
    Node best;
    Genome answer{};
    bool won = false;
    double damageUpper[41][5][5] = {};

    bool expired(Clock::time_point limit) const { return Clock::now() >= limit; }

    // Only real simulator transitions are used. One-turn logs also avoid the
    // fixed 400-record capacity imposing a prefix-length-specific boundary.
    void step(Node& n, int action, const int* gene) {
        (void)action;
        stepLog.clear();
        BE::Main(&n.cursor, 1, gene, n.players, &stepLog, seed,
                 nullptr, nullptr, -1, &n.state);
        ++n.depth;
        n.records += stepLog.position;
        n.changes += equipmentChanges(stepLog);
        ++statistics.transitions;
    }

    bool replay(const std::array<int, 350>& gene, int length, Node& out) {
        out = Node{};
        out.players[0] = initial[0]; out.players[1] = initial[1];
        lcg::init(seed, true);
        while (out.depth < length) {
            if (out.players[0].hp == 0 || out.players[1].hp == 0) break;
            // Fresh bulk Main replay, independently of the one-turn frontier.
            // A larger prefix is batched only to respect BattleResult's capacity.
            const int before = out.depth;
            stepLog.clear();
            BE::Main(&out.cursor, std::min(133, length - out.depth), gene.data(),
                     out.players, &stepLog, seed, nullptr, nullptr, -1, &out.state);
            out.depth = static_cast<int>((out.state >> 12) & 0xfffff);
            out.records += stepLog.position;
            out.changes += equipmentChanges(stepLog);
            statistics.transitions += out.depth - before;
        }
        return out.depth == length;
    }

    std::array<int, 350> sequence(const Node& n) const {
        auto gene = prefix;
        int link = n.path;
        for (int i = n.depth - 1; i >= prefixLength; --i) {
            gene[i] = paths[link].action;
            link = paths[link].parent;
        }
        gene[n.depth] = -1;
        return gene;
    }

    void consider(const Node& candidate) {
        if (candidate.players[1].hp != 0) return;
        if (won && (candidate.records > best.records
            || (candidate.records == best.records && candidate.changes >= best.changes))) return;
        const auto gene = sequence(candidate);
        Node exact;
        ++statistics.replays;
        if (!replay(gene, candidate.depth, exact) || !sameWorld(exact, candidate)
            || exact.changes != candidate.changes || exact.players[1].hp != 0) {
            ++statistics.replayMismatches;
            return;
        }
        if (won && (exact.records > best.records
            || (exact.records == best.records && exact.changes >= best.changes))) return;
        won = true;
        best = exact;
        answer.AllyPlayer = exact.players[0]; answer.EnemyPlayer = exact.players[1];
        answer.position = exact.cursor; answer.state = exact.state;
        answer.turn = exact.depth + 1; answer.processed = exact.depth;
        answer.Initialized = true;
        std::copy(gene.begin(), gene.end(), answer.actions);
    }

    bool competitive(const Node& n) const {
        if (n.players[0].hp == 0 || n.depth >= options.maxTotalTurns) return false;
        // Every additional turn contributes at least one actual log record.
        return !won || n.records + 1 < best.records
            || (n.records + 1 == best.records && n.changes < best.changes);
    }

    int actions(const Node& n, std::array<int, 24>& result) const {
        const Player& p = n.players[0];
        const bool bare = p.defaultATK == BE::ISINOBANNNINN_BARE_HANDS_ATK;
        const bool resting = p.sleeping || p.paralysis;
        int count = 0;
        auto add = [&](int action, bool weapon = false) {
            // FLEE's pre-action skip is unsafe when rest carries into the turn.
            if (resting && action == BE::FLEE_ALLY) return;
            for (int mode = 0; mode < (resting ? 1 : 2); ++mode) {
                bool requestedBare = mode == 0 ? bare : !bare;
                if (weapon && requestedBare) continue;
                result[count++] = action | (requestedBare ? BE::ACTION_BARE_HANDS : 0);
            }
        };
        // Do not require Double Up or ten MP to use a legal four-MP spear skill.
        if (p.mp >= 4) add(BE::MULTITHRUST, true);
        if (p.TensionLevel < 4) add(BE::PSYCHE_UP_ALLY);
        if (p.AtkBuffLevel < 2) add(BE::DOUBLE_UP);
        add(BE::ATTACK_ALLY);
        if (p.SpecialMedicineCount > 0) add(BE::SPECIAL_MEDICINE);
        if (p.mp >= 4 && p.hp < p.maxHp) add(BE::MIDHEAL);
        if (p.mp >= 3 && p.BuffLevel < 2) add(BE::BUFF);
        add(BE::DEFENCE);
        add(BE::FLEE_ALLY);
        if (p.mp < 8 && p.MagicWaterCount > 0) add(BE::MAGIC_WATER);
        return count;
    }

    double estimate(const Node& n, int style) const {
        const Player& p = n.players[0]; const Player& e = n.players[1];
        constexpr double tension[] = {1, 1.5, 2.5, 4, 6};
        constexpr double attack[] = {.5, .75, 1, 1.25, 1.5};
        const double defence = std::min<double>(e.def, e.defaultDEF * 1.4);
        double remaining = 1000;
        // Relaxed damage estimates order the frontier; they never decide victory
        // or replace an emulator transition. No seed/prefix-specific policy.
        for (int buff = 0; buff != 2; ++buff) {
            if (buff && p.AtkBuffLevel >= 2) continue;
            const double atk = BE::ISINOBANNNINN_EQUIPPED_ATK
                * (buff ? 1.5 : attack[std::clamp(p.AtkBuffLevel + 2, 0, 4)]);
            const double hit = std::max(1.0, (atk * .5 - defence * .25) * .5);
            const double cycle = std::max(1.0, hit * 3.8 * 4 / 4);
            for (int level = p.TensionLevel; level <= 4; ++level) {
                double damage = 3.8 * (hit * tension[level] + level * 3);
                double h = buff + level - p.TensionLevel;
                h += std::min(1.0, e.hp / damage);
                h += std::max(0.0, e.hp - damage) / cycle;
                remaining = std::min(remaining, h);
            }
        }
        const double hpCost = 1.0 - p.hp / std::max(1.0, p.maxHp);
        const double danger = std::max(0, e.TensionLevel - 1) * hpCost;
        if (style == 1)
            return remaining + hpCost * .8 + danger * .2 - p.BuffLevel * .07;
        if (style == 2)
            return remaining + hpCost * .25 + danger * .06;
        return remaining + hpCost * 1.3 + danger * .4 - p.BuffLevel * .1;
    }

    Node child(const Node& parent, int action) {
        Node n = parent;
        scratch[parent.depth] = action;
        step(n, action, scratch.data());
        paths.push_back({parent.path, action});
        n.path = static_cast<int>(paths.size()) - 1;
        return n;
    }

    static bool better(const Node& a, const Node& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        if (a.changes != b.changes) return a.changes < b.changes;
        if (a.players[1].hp != b.players[1].hp) return a.players[1].hp < b.players[1].hp;
        return a.path < b.path;
    }

    void beam(int width, int style, Clock::time_point limit) {
        paths.clear();
        std::vector<Node> frontier{root}, next;
        next.reserve(width * 18);
        std::array<int, 24> choices{};
        while (!frontier.empty() && !expired(limit)) {
            next.clear();
            for (const Node& parent : frontier) {
                if (!competitive(parent)) continue;
                const int count = actions(parent, choices);
                for (int a = 0; a < count; ++a) {
                    if ((statistics.transitions & 127) == 0 && expired(limit)) return;
                    Node n = child(parent, choices[a]);
                    if (n.players[1].hp == 0) { consider(n); continue; }
                    if (!competitive(n)) continue;
                    n.priority = estimate(n, style) + n.changes * .00001;
                    next.push_back(n);
                }
            }
            // Exact-state deduplication. Hash collisions are checked fieldwise.
            std::vector<int> table;
            size_t capacity = 1;
            while (capacity < next.size() * 2) capacity *= 2;
            table.assign(capacity, -1);
            size_t kept = 0;
            for (size_t i = 0; i < next.size(); ++i) {
                Node n = next[i]; n.hash = hashWorld(n);
                size_t slot = n.hash & (capacity - 1);
                while (table[slot] >= 0) {
                    Node& old = next[table[slot]];
                    if (old.hash == n.hash && sameWorld(old, n)) {
                        if (n.changes < old.changes) old = n;
                        break;
                    }
                    slot = (slot + 1) & (capacity - 1);
                }
                if (table[slot] < 0) {
                    table[slot] = static_cast<int>(kept);
                    next[kept++] = n;
                }
            }
            next.resize(kept);
            if (next.size() > static_cast<size_t>(width)) {
                std::nth_element(next.begin(), next.begin() + width, next.end(), better);
                next.resize(width);
            }
            std::sort(next.begin(), next.end(), better);
            frontier.swap(next);
        }
    }

    void bestFirst(Clock::time_point limit) {
        paths.clear();
        struct Compare { bool operator()(const Node& a, const Node& b) const { return better(b, a); } };
        std::priority_queue<Node, std::vector<Node>, Compare> open;
        open.push(root);
        std::array<int, 24> choices{};
        while (!open.empty() && !expired(limit)) {
            Node parent = open.top(); open.pop();
            if (!competitive(parent)) continue;
            const int count = actions(parent, choices);
            for (int a = 0; a < count; ++a) {
                if ((statistics.transitions & 127) == 0 && expired(limit)) return;
                Node n = child(parent, choices[a]);
                if (n.players[1].hp == 0) { consider(n); continue; }
                if (!competitive(n)) continue;
                n.priority = n.records / 3.0 + 1.5 * estimate(n, 3) + n.changes * .00001;
                open.push(n);
            }
            // Bounded frontier, rather than letting an unpromising request use
            // unbounded memory. The incumbent is always retained separately.
            if (open.size() > 100000) return;
        }
    }

    void prepareDamageUpper() {
        constexpr double tension[] = {1, 1.5, 2.5, 4, 6};
        constexpr double attack[] = {.5, .75, 1, 1.25, 1.5};
        const double baseAtk = std::max(BE::ISINOBANNNINN_EQUIPPED_ATK,
                                        root.players[0].defaultATK);
        const double minDef = std::max(0, std::min(root.players[1].def,
                                                  root.players[1].defaultDEF));
        // Relaxation: free weapon changes, no MP/survival/buff-expiry costs,
        // four connected critical hits and successful tension raises. Enemy
        // buffs can expire but our candidate actions cannot lower its defence.
        for (int left = 1; left <= 40; ++left) {
            for (int buff = 0; buff < 5; ++buff) {
                double atk = std::floor(baseAtk * attack[buff]);
                double base = std::ceil(std::max((2 * atk - minDef) / 4.0 * 1.0625 + 1,
                                                  atk / 16.0));
                for (int level = 0; level <= 4; ++level) {
                    double hit = std::floor(base * .5);
                    double multi = 4 * std::max((hit * tension[level] + level * 3) * 1.2,
                                                 hit * 2);
                    double ordinary = std::max((base * tension[level] + level * 3) * 1.2,
                                               baseAtk * 1.05);
                    double damage = std::max(multi, ordinary);
                    damageUpper[left][buff][level] = std::max({
                        damage + damageUpper[left - 1][buff][0],
                        damageUpper[left - 1][std::min(4, buff + 2)][level],
                        damageUpper[left - 1][buff][std::min(4, level + 1)]});
                }
            }
        }
    }

    bool canDamage(const Node& n, int left) const {
        if (left <= 0) return false;
        if (left > 40) return true;
        return damageUpper[left][std::clamp(n.players[0].AtkBuffLevel + 2, 0, 4)]
                                [std::clamp(n.players[0].TensionLevel, 0, 4)]
            >= n.players[1].hp;
    }

    void depthFirst(const Node& parent, int maxDepth, Clock::time_point limit) {
        if (!competitive(parent) || !canDamage(parent, maxDepth - parent.depth)
            || expired(limit)) return;
        std::array<int, 24> choices{};
        auto& children = dfsScratch[parent.depth - root.depth];
        int kept = 0;
        const int count = actions(parent, choices);
        for (int i = 0; i < count; ++i) {
            Node n = child(parent, choices[i]);
            if (n.players[1].hp == 0) { consider(n); continue; }
            if (!competitive(n) || !canDamage(n, maxDepth - n.depth)) continue;
            n.priority = estimate(n, 2) + n.changes * .00001;
            children[kept++] = n;
        }
        std::sort(children.begin(), children.begin() + kept, better);
        for (int i = 0; i < kept; ++i) depthFirst(children[i], maxDepth, limit);
    }

    void boundedDfs(Clock::time_point limit) {
        prepareDamageUpper();
        for (int depth = root.depth + 1; depth <= options.maxTotalTurns && !expired(limit); ++depth) {
            // In this world a nonterminal turn records the hero and two enemy
            // actions. A lethal final turn has one or three records. This is
            // only a horizon bound; candidate ranking still uses fresh logs.
            if (won && root.records + 3 * (depth - root.depth - 1) + 1 > best.records) break;
            paths.clear();
            // Keep per-depth frontiers off the native/Wasm call stack. Growth
            // happens between traversals, so recursive references stay valid.
            dfsScratch.resize(depth - root.depth + 1);
            depthFirst(root, depth, limit);
        }
    }

public:
    Search(const Player* players, uint64_t value, const Options& opt)
        : initial(players), seed(value), options(opt), started(Clock::now()),
          deadline(started + std::chrono::milliseconds(std::max(1, opt.budgetMs - 10))) {}

    Genome run(const int input[350]) {
        statistics = Statistics{};
        statistics.variant = options.variant;
        answer.turn = std::numeric_limits<int>::max();
        answer.AllyPlayer = initial[0]; answer.EnemyPlayer = initial[1];
        // -1, not a hardcoded turn count, determines the complete fixed prefix.
        while (prefixLength < 350 && input[prefixLength] != -1) ++prefixLength;
        if (prefixLength == 350) {
            statistics.inputValid = false;
            return finish();
        }
        statistics.prefixLength = prefixLength;
        std::copy_n(input, prefixLength, prefix.begin());
        prefix[prefixLength] = -1;
        std::copy(prefix.begin(), prefix.end(), answer.actions);
        options.maxTotalTurns = std::clamp(options.maxTotalTurns, prefixLength, 349);
        // Existing emulator input capacity and RNG cache remain unchanged.
        // No status/equipment validators are added to the battle hot path.
        if (!replay(prefix, prefixLength, root)) return finish();
        scratch = prefix;
        if (root.players[1].hp == 0) { consider(root); return finish(); }
        if (!competitive(root)) return finish();
        if (options.variant == 1) beam(2048, 1, deadline);
        else if (options.variant == 2) beam(8192, 2, deadline);
        else if (options.variant == 3) bestFirst(deadline);
        else if (options.variant == 4) {
            beam(512, 1, std::min(deadline, started + std::chrono::milliseconds(options.budgetMs / 10)));
            if (!expired(deadline)) boundedDfs(deadline);
        }
        else {
            // One shared deadline includes all passes, prefix and fresh replays.
            beam(512, 1, std::min(deadline, started + std::chrono::milliseconds(options.budgetMs / 5)));
            if (!expired(deadline)) beam(4096, 2, deadline);
            if (!expired(deadline)) bestFirst(deadline);
        }
        return finish();
    }

    Genome finish() {
        statistics.victory = won;
        if (won) {
            statistics.battlePosition = best.records;
            statistics.equipmentChanges = best.changes;
            statistics.turns = best.depth;
            statistics.rngPosition = best.cursor;
        }
        statistics.elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
        return answer;
    }
};
}

Genome Run(const Player initial[2], uint64_t seed, const int actions[350], const Options& options) {
    // The replay log and DP workspace should not consume the caller's stack.
    auto search = std::make_unique<Search>(initial, seed, options);
    return search->run(actions);
}
const Statistics& GetStatistics() { return statistics; }
const char* VariantName(int variant) {
    switch (variant) {
        case 1: return "balanced-beam-2048";
        case 2: return "offensive-beam-8192";
        case 3: return "weighted-best-first";
        case 4: return "beam-and-bounded-dfs";
        default: return "beam-portfolio";
    }
}
}