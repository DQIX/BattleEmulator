#include "ErusionnSearch.h"
#include "BattleEmulator.h"
#include "lcg.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <queue>
#include <tuple>
#include <vector>

namespace {
using B = BattleEmulator;
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoPath = UINT32_MAX;
// This actor's existing menu, plus the expressly allowed terminal attack.
constexpr int Actions[] = { B::PSYCHE_UP_ALLY, B::DOUBLE_UP, B::MULTITHRUST,
    B::SPECIAL_MEDICINE, B::MIDHEAL, B::BUFF, B::DEFENCE, B::FLEE_ALLY,
    B::ATTACK_ALLY };
constexpr const char *Names[] = {"portfolio", "balanced-beam", "burst-beam",
    "survival-beam", "global-beam", "best-first", "no-equipment-beam",
    "zero-equipment-portfolio", "damage-bound-dfs", "beam-then-bound-dfs", "prebound-dfs", "transposition-dfs", "large-transposition-dfs", "small-transposition-dfs"};

// Semantic equality: no padding, omitted duration or equipment-blind hash merge.
auto fields(const Player &p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis, p.paralysisLevel,
        p.paralysisTurns, p.SpecialMedicineCount, p.defence, p.sleeping,
        p.sleepingTurn, p.BuffLevel, p.BuffTurns, p.hasMagicMirror,
        p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn, p.TensionLevel,
        p.rage, p.SageElixirCount, p.ElfinElixirCount, p.MagicWaterCount,
        p.speedTurn, p.speedLevel, p.PoisonTurn, p.PoisonEnable,
        p.SpecialAntidoteCount, p.acrobaticStar, p.acrobaticStarTurn);
}
struct State {
    Player players[2]{};
    uint64_t now = 0;
    int rng = 1;
    int records = 0;
    int changes = 0;
};
int turnOf(const State &s) { return int((s.now >> 12) & 0xfffff); }
bool sameFuture(const State &a, const State &b) {
    return a.now == b.now && a.rng == b.rng && a.records == b.records &&
        fields(a.players[0]) == fields(b.players[0]) &&
        fields(a.players[1]) == fields(b.players[1]);
}
uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
uint64_t fingerprint(const State &s) {
    const auto &p = s.players[0];
    uint64_t h = mix(s.now) ^ mix(uint32_t(s.rng));
    h ^= mix(uint64_t(uint32_t(p.hp)) << 32 | uint32_t(s.players[1].hp));
    h ^= mix(uint64_t(uint32_t(p.defaultATK)) << 32 | uint32_t(p.atk));
    h ^= mix(uint64_t(uint32_t(p.mp)) << 32 | uint32_t(p.def));
    h ^= mix(uint64_t(uint32_t(p.TensionLevel)) << 32 | uint32_t(p.BuffTurns));
    h ^= mix(uint64_t(uint32_t(p.AtkBuffTurn)) << 32 | uint32_t(p.specialChargeTurn));
    return mix(h ^ uint32_t(s.records));
}
int equipmentBit(const Player &p) {
    return p.defaultATK == B::ERUSIONN_BARE_HANDS_ATK ? B::ACTION_BARE_HANDS : 0;
}
int countChanges(const BattleResult &r) {
    int count = 0;
    for (int i = 0; i < r.position; ++i)
        if (!r.isEnemy[i] && (r.actions[i] & B::ACTION_EQUIPMENT_CHANGED)) ++count;
    return count;
}
// Candidate conditions belong here, never in BattleEmulator's hot path.
bool legal(const Player &p, int encoded) {
    if (encoded <= 0 || (encoded & ~(B::ACTION_ID_MASK | B::ACTION_BARE_HANDS))) return false;
    const int id = encoded & B::ACTION_ID_MASK;
    const bool rest = p.sleeping || p.paralysis;
    if (rest && (encoded & B::ACTION_BARE_HANDS) != equipmentBit(p)) return false;
    switch (id) {
        case B::PSYCHE_UP_ALLY: return p.TensionLevel < 4;
        case B::DOUBLE_UP: return p.AtkBuffLevel < 2;
        case B::MULTITHRUST:
            return !(encoded & B::ACTION_BARE_HANDS) && p.mp >= 4;
        case B::SPECIAL_MEDICINE: return p.SpecialMedicineCount > 0;
        case B::MIDHEAL: return p.mp >= 4;
        case B::BUFF: return p.mp >= 3 && p.BuffLevel < 2;
        case B::DEFENCE: return true;
        case B::FLEE_ALLY: return !rest;
        case B::ATTACK_ALLY: return true; // Admitted only on enemy HP == 0 below.
        default: return false;
    }
}

struct Config {
    double tension, attackBuff, hp, danger, defence, resource;
    int quota;
};
constexpr Config Configs[] = {
    {.94, 85, .70, .055, 45, .4, 3}, // balanced
    {1.18, 65, .28, .020, 15, .2, 3}, // burst
    {.82, 75, 1.35, .090, 75, .8, 3}, // survival
    {.94, 85, .70, .055, 45, .4, 0}, // global (no tactical diversity quotas)
};
double value(const State &s, const Config &c, bool equipment) {
    const auto &p = s.players[0];
    const auto &e = s.players[1];
    constexpr double tension[] = {1, 1.5, 2.5, 4, 6};
    constexpr double atkBuff[] = {.5, .75, 1, 1.25, 1.5};
    double attack = p.atk;
    if (equipment) attack = std::max(attack, std::floor(B::ERUSIONN_EQUIPPED_ATK *
        atkBuff[std::clamp(p.AtkBuffLevel + 2, 0, 4)]));
    const bool spear = equipment || !equipmentBit(p);
    const double hits = spear ? 3.5 : 1.;
    const double base = std::max(0., (attack - e.def * .5) * (spear ? .25 : .5));
    const int level = std::clamp(p.TensionLevel, 0, 4);
    // Only an ordering estimate; all damage/state updates use Main.
    const double burst = (base * tension[level] + level * 3) * hits * .98;
    double credit = std::max(0., std::min(double(e.hp), burst) -
                                 std::min(double(e.hp), base * hits));
    if (spear && p.mp < 4) credit *= .2;
    double v = -double(e.hp) + credit * c.tension;
    v += p.hp * c.hp;
    const double danger = std::max(0., p.maxHp * .36 - p.hp);
    v -= danger * danger * c.danger;
    v += std::max(0, p.mp) * c.resource + p.SpecialMedicineCount * 8.;
    v += std::max(0, p.AtkBuffLevel) * c.attackBuff *
         std::clamp(p.AtkBuffTurn + 1, 1, 4) / 4.;
    v += p.BuffLevel * c.defence * std::clamp(p.BuffTurns + 1, 1, 4) / 4.;
    if (level == 4) v += 85; // SHT also reduces incoming damage in this world.
    if (p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn > 0) v += 8;
    if (p.sleeping || p.paralysis) v -= 100;
    return v;
}
struct Link { uint32_t parent; int action; };
struct Node {
    State s{};
    uint64_t hash = 0;
    double score = 0;
    uint32_t path = NoPath;
    int action = 0;
};
struct Context {
    const Player *initial;
    uint64_t seed;
    int maxTurns;
    Clock::time_point start, deadline, passDeadline;
    ErusionnSearchResult result{};
    State root{};
    std::array<int, 350> prefix{}, scratch{}, bestActions{};
    BattleResult stepLog{}, verifyLog{};
    int bestPosition = INT32_MAX, bestChanges = INT32_MAX;

    double elapsed() const { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
    bool expired() const { return Clock::now() >= passDeadline; }
    // At most 3 records/turn in this target; respect the existing fixed buffers.
    bool canStep(const State &s) const {
        return turnOf(s) < maxTurns && s.records <= 396 && s.rng < 4700 &&
            s.players[0].hp > 0 && s.players[1].hp > 0;
    }
    bool step(State &s, int action) {
        if (!canStep(s)) return false;
        scratch[turnOf(s)] = action;
        stepLog.clear();
        B::Main(&s.rng, 1, scratch.data(), s.players, &stepLog, seed,
                nullptr, nullptr, -1, &s.now);
        s.records += stepLog.position;
        s.changes += countChanges(stepLog);
        ++result.expanded;
        // A regular attack is terminal-only, including a miss or a skipped action.
        return (action & B::ACTION_ID_MASK) != B::ATTACK_ALLY || s.players[1].hp == 0;
    }
    void consider(const Node &n, const std::vector<Link> &links) {
        if (n.s.players[1].hp != 0 || Clock::now() >= deadline) return;
        if (std::tie(n.s.records, n.s.changes) >= std::tie(bestPosition, bestChanges)) return;
        auto actions = prefix;
        std::vector<int> suffix{n.action};
        for (auto p = n.path; p != NoPath; p = links[p].parent) suffix.push_back(links[p].action);
        std::reverse(suffix.begin(), suffix.end());
        const int length = result.prefixLength + int(suffix.size());
        std::copy(suffix.begin(), suffix.end(), actions.begin() + result.prefixLength);
        actions[length] = -1;

        State fresh; fresh.players[0] = initial[0]; fresh.players[1] = initial[1];
        lcg::init(seed, true);
        verifyLog.clear();
        B::Main(&fresh.rng, length, actions.data(), fresh.players, &verifyLog,
                seed, nullptr, nullptr, -1, &fresh.now);
        fresh.records = verifyLog.position;
        fresh.changes = countChanges(verifyLog);
        if (fresh.players[1].hp != 0 || !sameFuture(fresh, n.s) || fresh.changes != n.s.changes) {
            ++result.rejectedReplay;
            return;
        }
        if (std::tie(fresh.records, fresh.changes) >= std::tie(bestPosition, bestChanges)) return;
        bestPosition = fresh.records; bestChanges = fresh.changes;
        bestActions = actions;
        result.replay = verifyLog;
        result.victory = result.replayVerified = true;
        result.length = length;
        result.equipmentChanges = fresh.changes;
        result.bestVictoryMs = elapsed();
        if (result.firstVictoryMs < 0) result.firstVictoryMs = result.bestVictoryMs;
        ++result.updates;
        auto &g = result.genome;
        g.AllyPlayer = fresh.players[0]; g.EnemyPlayer = fresh.players[1];
        g.position = fresh.rng; g.state = fresh.now;
        g.turn = length + 1; g.processed = length; g.Initialized = true;
        std::copy(actions.begin(), actions.end(), g.actions);
    }
};
struct StateSet {
    std::vector<uint32_t> slots;
    size_t mask = 0;
    void reset(size_t expected) {
        size_t cap = 8;
        while (cap < expected * 2) cap *= 2;
        slots.assign(cap, NoPath); mask = cap - 1;
    }
    bool insert(const Node &n, std::vector<Node> &nodes, Context &ctx) {
        size_t i = n.hash & mask;
        while (slots[i] != NoPath) {
            auto &old = nodes[slots[i]];
            if (n.hash == old.hash && sameFuture(n.s, old.s)) {
                ++ctx.result.duplicates;
                if (n.s.changes < old.s.changes) old = n;
                return false;
            }
            i = (i + 1) & mask;
        }
        slots[i] = uint32_t(nodes.size());
        return true;
    }
};
int phase(const Node &n) {
    return std::clamp(n.s.players[0].TensionLevel, 0, 4) * 2 +
           int(n.s.players[0].AtkBuffLevel >= 2);
}

// Layered exact transitions. An arena retains paths without copying 350 genes
// into every candidate. Width is the only approximate part of this search.
bool beam(Context &ctx, const Config &cfg, size_t width, bool equipment) {
    std::vector<Node> current, next, candidates;
    std::vector<Link> links;
    std::vector<uint32_t> order, group, chosen;
    std::vector<uint8_t> selected;
    StateSet seen;
    const size_t branching = std::size(Actions) * (equipment ? 2 : 1);
    current.reserve(width); next.reserve(width); candidates.reserve(width * branching);
    links.reserve(width * size_t(std::max(1, ctx.maxTurns - ctx.result.prefixLength)));
    Node root; root.s = ctx.root; current.push_back(root);
    bool pruned = false;
    for (int turn = ctx.result.prefixLength; turn < ctx.maxTurns && !current.empty(); ++turn) {
        if (ctx.expired()) return false;
        candidates.clear(); seen.reset(current.size() * branching);
        for (const auto &parent : current) {
            if (parent.s.records + 1 > ctx.bestPosition) continue;
            for (int action : Actions) {
                const int keep = equipmentBit(parent.s.players[0]);
                for (int eq = 0; eq < (equipment ? 2 : 1); ++eq) {
                    const int encoded = action | (eq ? (keep ^ B::ACTION_BARE_HANDS) : keep);
                    if (!legal(parent.s.players[0], encoded)) continue;
                    if ((ctx.result.expanded & 127) == 0 && ctx.expired()) return false;
                    Node n; n.s = parent.s; n.path = parent.path; n.action = encoded;
                    if (!ctx.step(n.s, encoded) || n.s.records > ctx.bestPosition) continue;
                    if (n.s.players[1].hp == 0) { ctx.consider(n, links); continue; }
                    if (!ctx.canStep(n.s) || n.s.records + 1 > ctx.bestPosition) continue;
                    n.hash = fingerprint(n.s); n.score = value(n.s, cfg, equipment);
                    if (seen.insert(n, candidates, ctx)) candidates.push_back(n);
                }
            }
        }
        const size_t keep = std::min(width, candidates.size());
        if (!keep) break;
        if (ctx.expired()) return false;
        pruned |= keep < candidates.size();
        order.resize(candidates.size()); std::iota(order.begin(), order.end(), 0);
        const auto better = [&](uint32_t a, uint32_t b) {
            const auto &x = candidates[a]; const auto &y = candidates[b];
            if (x.score != y.score) return x.score > y.score;
            if (x.s.changes != y.s.changes) return x.s.changes < y.s.changes;
            return x.hash < y.hash;
        };
        chosen.clear(); selected.assign(candidates.size(), 0);
        if (cfg.quota && keep < candidates.size()) {
            const size_t quota = std::max<size_t>(1, keep / (10 * cfg.quota));
            for (int ph = 0; ph < 10; ++ph) {
                group.clear();
                for (uint32_t i = 0; i < candidates.size(); ++i)
                    if (phase(candidates[i]) == ph) group.push_back(i);
                const size_t k = std::min(quota, group.size());
                if (k < group.size()) std::nth_element(group.begin(), group.begin() + k, group.end(), better);
                for (size_t j = 0; j < k && chosen.size() < keep; ++j) {
                    chosen.push_back(group[j]); selected[group[j]] = 1;
                }
            }
        }
        if (keep < order.size()) {
            std::nth_element(order.begin(), order.begin() + keep, order.end(), better);
            order.resize(keep);
        }
        std::sort(order.begin(), order.end(), better);
        for (auto i : order) if (!selected[i] && chosen.size() < keep) chosen.push_back(i);
        if (ctx.expired()) return false;
        next.clear();
        for (auto i : chosen) {
            Node n = candidates[i];
            links.push_back({n.path, n.action}); n.path = uint32_t(links.size() - 1);
            next.push_back(n);
        }
        current.swap(next);
    }
    return !pruned;
}

// An independently ordered comparison variant (not a separate world).
void bestFirst(Context &ctx, const Config &cfg) {
    struct Entry { double priority; uint32_t index; };
    const auto worse = [](const Entry &a, const Entry &b) { return a.priority > b.priority; };
    std::priority_queue<Entry, std::vector<Entry>, decltype(worse)> open(worse);
    std::vector<Node> nodes;
    std::vector<Link> links;
    nodes.reserve(200000); links.reserve(200000);
    Node root; root.s = ctx.root;
    nodes.push_back(root); open.push({0, 0});
    while (!open.empty() && !ctx.expired() && nodes.size() < 500000) {
        const Node parent = nodes[open.top().index]; open.pop();
        if (parent.s.records + 1 > ctx.bestPosition) continue;
        for (int action : Actions) for (int eq = 0; eq < 2; ++eq) {
            const int keep = equipmentBit(parent.s.players[0]);
            const int encoded = action | (eq ? (keep ^ B::ACTION_BARE_HANDS) : keep);
            if (!legal(parent.s.players[0], encoded)) continue;
            if ((ctx.result.expanded & 127) == 0 && ctx.expired()) return;
            Node n; n.s = parent.s; n.path = parent.path; n.action = encoded;
            if (!ctx.step(n.s, encoded) || n.s.records > ctx.bestPosition) continue;
            if (n.s.players[1].hp == 0) { ctx.consider(n, links); continue; }
            if (!ctx.canStep(n.s) || n.s.records + 1 > ctx.bestPosition) continue;
            n.score = value(n.s, cfg, true);
            links.push_back({n.path, encoded}); n.path = uint32_t(links.size() - 1);
            const uint32_t index = uint32_t(nodes.size()); nodes.push_back(n);
            open.push({n.s.records - n.score / 155. + n.s.changes * .025, index});
        }
    }
}
// Optimistic damage envelope for this compiled world's formulas. All hits land,
// all critical rolls succeed, tension always increases and buffs never expire.
// This is an upper bound used ONLY to discard too-short subtrees. In particular
// no predicted damage, cursor or player value is written to an emulator state.
struct DamageEnvelope {
    double damage[134][5][5]{};
    double multi[5][5]{}, attack[5][5]{};
    explicit DamageEnvelope(const Context &ctx) {
        constexpr double tension[] = {1, 1.5, 2.5, 4, 6};
        constexpr double buffs[] = {.5, .75, 1, 1.25, 1.5};
        const double weapon = std::max({B::ERUSIONN_EQUIPPED_ATK,
            B::ERUSIONN_BARE_HANDS_ATK, ctx.initial[0].defaultATK, ctx.initial[0].atk});
        const double defence = std::max(0, ctx.root.players[1].def);
        for (int t = 1; t <= 133; ++t) for (int b = 0; b < 5; ++b) for (int l = 0; l < 5; ++l) {
            const double atk = std::floor(weapon * buffs[b]);
            const double normal = std::max(0., (2 * atk - defence) * .25);
            // Slightly loose ceilings avoid relying on floating rounding details.
            const double high = std::ceil(std::max(normal * 1.0625 + 1., atk / 16.));
            const double half = std::floor(high * .5);
            multi[b][l] = 4 * std::ceil(std::max((half * tension[l] + l * 3) * 1.2, half * 2.));
            attack[b][l] = std::ceil(std::max((high * tension[l] + l * 3) * 1.2, weapon * 1.05));
            damage[t][b][l] = std::max({attack[b][l], multi[b][l] + damage[t-1][b][0],
                damage[t-1][b][std::min(l+1, 4)], damage[t-1][4][l], damage[t-1][b][l]});
        }
    }
    bool possible(const State &s, int left) const {
        return left > 0 && s.players[1].hp <= damage[std::min(left,133)]
            [std::clamp(s.players[0].AtkBuffLevel+2,0,4)][std::clamp(s.players[0].TensionLevel,0,4)];
    }
    bool afterPossible(const State &s, int action, int left) const {
        if (left <= 0) return false;
        const int b = std::clamp(s.players[0].AtkBuffLevel+2,0,4);
        const int l = std::clamp(s.players[0].TensionLevel,0,4);
        const int t = std::min(left-1,133);
        double upper = 0;
        switch (action) {
            case B::MULTITHRUST: upper = multi[b][l] + damage[t][b][0]; break;
            case B::ATTACK_ALLY: upper = attack[b][l]; break;
            case B::DOUBLE_UP: upper = damage[t][4][l]; break;
            case B::PSYCHE_UP_ALLY: upper = damage[t][b][std::min(l+1,4)]; break;
            default: upper = damage[t][b][l]; break;
        }
        return s.players[1].hp <= upper;
    }

};
struct Dfs {
    Context &ctx;
    const DamageEnvelope envelope;
    std::vector<Link> links;
    struct Slot { State s{}; uint64_t hash = 0; int left = -1; };
    std::vector<Slot> table;
    uint64_t visits = 0;
    bool prebound;
    std::vector<std::array<Node, std::size(Actions) * 2>> levels;
    explicit Dfs(Context &c, bool pre = false, size_t tableSize = 0)
        : ctx(c), envelope(c), prebound(pre) {
        links.reserve(134);
        levels.resize(size_t(std::max(1, c.maxTurns - c.result.prefixLength + 1)));
        if (tableSize) table.resize(tableSize);
    }
    bool visit(const Node &parent, int left) {
        if ((++visits & 255) == 0 && ctx.expired()) return false;
        if (!ctx.canStep(parent.s) || parent.s.records + 1 > ctx.bestPosition ||
            !envelope.possible(parent.s, left)) return true;
        if (!table.empty()) {
            const uint64_t hash = fingerprint(parent.s);
            auto &slot = table[hash & (table.size()-1)];
            if (slot.left >= left && slot.hash == hash && sameFuture(slot.s, parent.s) &&
                slot.s.changes <= parent.s.changes) {
                ++ctx.result.duplicates; return true;
            }
            slot.s = parent.s; slot.hash = hash; slot.left = left;
        }
        // All children are real one-turn simulations, ordered by their own value.
        auto &children = levels[size_t(turnOf(parent.s) - ctx.result.prefixLength)];
        int count = 0;
        for (int action : Actions) for (int eq = 0; eq < 2; ++eq) {
            const int keep = equipmentBit(parent.s.players[0]);
            const int encoded = action | (eq ? (keep ^ B::ACTION_BARE_HANDS) : keep);
            if (!legal(parent.s.players[0], encoded)) continue;
            if (prebound && !envelope.afterPossible(parent.s, action, left)) continue;
            if (left == 1 && action != B::MULTITHRUST && action != B::ATTACK_ALLY) continue;
            Node n; n.s = parent.s; n.path = parent.path; n.action = encoded;
            if (!ctx.step(n.s, encoded) || n.s.records > ctx.bestPosition) continue;
            if (n.s.players[1].hp == 0) { ctx.consider(n, links); continue; }
            if (!ctx.canStep(n.s) || n.s.records + 1 > ctx.bestPosition ||
                !envelope.possible(n.s, left - 1)) continue;
            if (n.s.records + 1 == ctx.bestPosition && n.s.changes >= ctx.bestChanges) continue;
            n.score = value(n.s, Configs[0], true);
            // Only semantically identical siblings are dominated by fewer changes.
            bool duplicate = false;
            for (int k = 0; k < count; ++k) if (sameFuture(n.s, children[k].s)) {
                if (n.s.changes < children[k].s.changes) children[k] = n;
                duplicate = true; ++ctx.result.duplicates; break;
            }
            if (!duplicate) children[count++] = n;
        }
        std::sort(children.begin(), children.begin()+count, [](const Node &a, const Node &b) {
            if (a.score != b.score) return a.score > b.score;
            return a.s.changes < b.s.changes;
        });
        for (int k = 0; k < count; ++k) {
            Node n = children[k];
            links.push_back({n.path, n.action}); n.path = uint32_t(links.size()-1);
            if (!visit(n, left-1)) return false;
            links.pop_back();
        }
        return !ctx.expired();
    }
    void run() {
        Node root; root.s = ctx.root;
        for (int left = 1; left <= ctx.maxTurns - ctx.result.prefixLength; ++left) {
            if (ctx.expired()) break;
            if (!envelope.possible(root.s, left)) continue;
            if (!visit(root, left)) break;
            // Shorter horizons and this complete horizon have already been searched.
            if (ctx.result.victory && ctx.result.length <= ctx.result.prefixLength + left) break;
        }
    }
};

} // namespace

int ErusionnSearch::VariantCount() { return int(std::size(Names)); }
const char *ErusionnSearch::VariantName(int v) {
    return v >= 0 && v < VariantCount() ? Names[v] : "invalid";
}

ErusionnSearchResult ErusionnSearch::Run(const Player initial[2], uint64_t seed,
    const int input[350], const ErusionnSearchOptions &options) {
    // Keep large result buffers off the small WebAssembly stack.
    auto storage = std::make_unique<Context>();
    auto &ctx = *storage;
    ctx.initial = initial; ctx.seed = seed;
    ctx.start = Clock::now();
    ctx.deadline = ctx.start + std::chrono::milliseconds(std::max(1, options.budgetMs));
    // Keep a small return/release margin inside the total request budget.
    ctx.passDeadline = ctx.deadline - std::chrono::milliseconds(std::min(8, std::max(0, options.budgetMs / 20)));
    const auto searchEnd = ctx.passDeadline;
    ctx.maxTurns = std::clamp(options.maxTotalTurns, 1, 133);
    ctx.result.variant = options.variant;
    ctx.result.genome.turn = INT32_MAX;
    ctx.prefix.fill(-1); ctx.bestActions.fill(-1); ctx.scratch.fill(-1);
    auto finish = [&]() {
        ctx.result.elapsedMs = ctx.elapsed();
        return ctx.result;
    };
    if (!initial || !input || seed == 0 || options.variant < 0 || options.variant >= VariantCount()) {
        ctx.result.error = "Invalid search input or variant."; return finish();
    }
    int length = 0;
    while (length < 350 && input[length] != -1) ++length;
    if (length == 350) {
        ctx.result.error = "Fixed prefix has no -1 terminator."; return finish();
    }
    ctx.result.prefixLength = length;
    std::copy_n(input, length, ctx.prefix.begin());
    std::copy(ctx.prefix.begin(), ctx.prefix.end(), ctx.result.genome.actions);
    ctx.root.players[0] = initial[0]; ctx.root.players[1] = initial[1];
    lcg::init(seed, true);
    // Replay all of the prefix, without searching, normalizing or deleting it.
    for (int i = 0; i < length; ++i) {
        if (ctx.expired() || !ctx.canStep(ctx.root)) {
            ctx.result.error = "Prefix cannot be fully replayed within the world buffers/budget.";
            return finish();
        }
        if (input[i] == 0) {
            ctx.result.error = "Zero inside fixed prefix would invoke implicit default actions.";
            return finish();
        }
        ctx.stepLog.clear();
        B::Main(&ctx.root.rng, 1, ctx.prefix.data(), ctx.root.players, &ctx.stepLog,
                seed, nullptr, nullptr, -1, &ctx.root.now);
        ctx.root.records += ctx.stepLog.position;
        ctx.root.changes += countChanges(ctx.stepLog);
    }
    ctx.result.inputValid = true;
    if (ctx.root.players[1].hp == 0) {
        State fresh; fresh.players[0] = initial[0]; fresh.players[1] = initial[1];
        lcg::init(seed, true);
        if (length) B::Main(&fresh.rng, length, ctx.prefix.data(), fresh.players,
            &ctx.result.replay, seed, nullptr, nullptr, -1, &fresh.now);
        fresh.records = ctx.result.replay.position; fresh.changes = countChanges(ctx.result.replay);
        if (sameFuture(fresh, ctx.root) && fresh.changes == ctx.root.changes) {
            ctx.result.victory = ctx.result.replayVerified = true;
            ctx.result.length = length; ctx.result.equipmentChanges = fresh.changes;
            auto &g = ctx.result.genome;
            g.AllyPlayer = fresh.players[0]; g.EnemyPlayer = fresh.players[1];
            g.position = fresh.rng; g.state = fresh.now; g.turn = length + 1;
            g.processed = length; g.Initialized = true;
        }
        return finish();
    }
    if (ctx.root.players[0].hp <= 0) {
        ctx.result.error = "Hero is dead after the immutable prefix."; return finish();
    }
    const int v = options.variant;
    if (v >= 8) {
        if (v != 8) {
            ctx.passDeadline = std::min(searchEnd, Clock::now() + std::chrono::milliseconds(std::max(1, options.budgetMs / 8)));
            beam(ctx, Configs[0], 512, true);
        }
        ctx.passDeadline = searchEnd;
        const size_t tableSize = v == 11 ? 65536 : v == 12 ? 262144 : v == 13 ? 16384 : 0;
        auto dfs = std::make_unique<Dfs>(ctx, v >= 10, tableSize);
        dfs->run();
    } else if (v == 5) {
        bestFirst(ctx, Configs[0]);
    } else {
        const size_t widths[] = {128, 512, 2048, 8192, 16384, 32768};
        for (size_t width : widths) {
            if (Clock::now() >= searchEnd) break;
            if (v == 0 || v == 7) {
                // One shared deadline, not one full budget per lane.
                for (int pass = 0; pass < 4; ++pass) {
                    const bool equipment = !(v == 7 && pass == 0);
                    const int cfg = pass == 0 ? 0 : (pass - 1);
                    const auto now = Clock::now();
                    if (now >= searchEnd) break;
                    const auto slice = std::chrono::milliseconds(std::max(15, options.budgetMs / 5));
                    ctx.passDeadline = std::min(searchEnd, now + slice);
                    const bool exhausted = beam(ctx, Configs[cfg], width, equipment);
                    if (exhausted && equipment) { ctx.passDeadline = searchEnd; return finish(); }
                }
            } else {
                ctx.passDeadline = searchEnd;
                const int cfg = v == 6 ? 0 : std::clamp(v - 1, 0, 3);
                if (beam(ctx, Configs[cfg], width, v != 6)) break;
            }
        }
    }
    if (!ctx.result.victory) ctx.result.error = "No replay-verified victory within the search budget.";
    return finish();
}
