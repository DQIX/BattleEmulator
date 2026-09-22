#include "ZuoSearch.h"
#include "lcg.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <memory>
#include <queue>
#include <tuple>
#include <vector>

namespace {
using B = BattleEmulator;
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoPath = UINT32_MAX;
// Existing ally menu, plus ordinary attack and the magic water in Player's
// inventory. Enemy skills / unrelated weapon skills are not candidate actions.
constexpr int Actions[] = {B::PSYCHE_UP_ALLY, B::DOUBLE_UP, B::MULTITHRUST,
    B::BUFF, B::ATTACK_ALLY, B::SPECIAL_MEDICINE, B::MIDHEAL,
    B::SPECIAL_ANTIDOTE, B::DEFENCE, B::FLEE_ALLY, B::MAGIC_WATER};
// The unchanged RNG cache has 5000 entries. Leave room for a complete turn;
// never wrap/modify the RNG or change the world to extend the search horizon.
constexpr int RngLimit = 4750;

int turnOf(const ZuoState &s) { return int((s.state >> 12) & 0xfffff); }
int equipment(const Player &p) {
    return p.defaultATK == B::ZUO_BARE_HANDS_ATK ? B::ACTION_BARE_HANDS : 0;
}
bool carriedRest(const Player &p) { return p.inactive || p.sleeping || p.paralysis; }
int countChanges(const BattleResult &log) {
    int n = 0;
    for (int i = 0; i < log.position; ++i)
        if (!log.isEnemy[i] && (log.actions[i] & B::ACTION_EQUIPMENT_CHANGED)) ++n;
    return n;
}
// ALL semantic fields participate in equality. No struct-padding hashes and
// no HP/buff-only state merging; timers, equipment and camera state matter.
auto fields(const Player &p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis,
        p.paralysisLevel, p.paralysisTurns, p.SpecialMedicineCount, p.defence,
        p.sleeping, p.sleepingTurn, p.BuffLevel, p.BuffTurns,
        p.hasMagicMirror, p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn,
        p.TensionLevel, p.rage, p.SageElixirCount, p.ElfinElixirCount,
        p.MagicWaterCount, p.speedTurn, p.speedLevel, p.PoisonTurn,
        p.PoisonEnable, p.SpecialAntidoteCount, p.acrobaticStar,
        p.acrobaticStarTurn, p.EerieTurn, p.EerieLevel, p.inactive, p.rageTurns);
}
bool sameFuture(const ZuoState &a, const ZuoState &b) {
    return a.rng == b.rng && a.state == b.state &&
        fields(a.players[0]) == fields(b.players[0]) &&
        fields(a.players[1]) == fields(b.players[1]);
}
uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
uint64_t fingerprint(const ZuoState &s) {
    const auto &p = s.players[0];
    auto pair = [](int a, int b) { return (uint64_t(uint32_t(a)) << 32) | uint32_t(b); };
    return mix(mix(s.state) ^ mix(s.rng) ^ mix(pair(p.hp, s.players[1].hp)) ^
        mix(pair(p.defaultATK, p.atk)) ^ mix(pair(p.mp, p.def)) ^
        mix(pair(p.TensionLevel, p.BuffTurns)) ^ mix(pair(p.AtkBuffTurn, p.EerieTurn)) ^
        mix(pair(p.inactive, p.PoisonTurn)));
}
bool betterCost(const ZuoState &a, const ZuoState &b) {
    return std::tie(a.position, a.changes) < std::tie(b.position, b.changes);
}

// Search-only ordering parameters. They neither alter nor approximate a
// transition. Every edge is executed by the actual BattleEmulator::Main.
struct Config {
    const char *name;
    double tension, attack, health, defence, danger, inactive, poison;
    int quota;
};
constexpr Config Configs[] = {
    {"balanced", .95, 105, .65, 38, .060, 110, 25, 3},
    {"burst",    1.20, 110, .25, 20, .020, 140, 18, 3},
    {"survival",  .80,  80, 1.4, 60, .100, 95, 45, 3},
    {"damage",   .60,  65, .45, 28, .040, 80, 20, 0},
    {"tactical", 1.05,  95, .75, 32, .040, 160, 25, 2}
};
constexpr const char *VariantNames[] = {
    "hybrid-portfolio", "balanced-beam", "burst-beam", "survival-beam",
    "damage-beam", "tactical-beam", "best-first", "zero-change-beam",
    "wide-balanced", "wide-burst", "bounded-dfs"
};

double value(const ZuoState &s, const Config &c, bool changeEquipment) {
    const auto &p = s.players[0];
    const auto &e = s.players[1];
    constexpr double tension[] = {1., 1.5, 2.5, 4., 6.};
    constexpr double atkBuff[] = {.5, .75, 1., 1.25, 1.5};
    const int tl = std::clamp(p.TensionLevel, 0, 4);
    // An equipped future finisher is an estimate only, not a fabricated state.
    const double attack = changeEquipment ? std::max(double(p.atk),
        B::ZUO_EQUIPPED_ATK * atkBuff[std::clamp(p.AtkBuffLevel + 2, 0, 4)]) : p.atk;
    const bool multi = p.mp >= 4 && (changeEquipment || !equipment(p));
    const double hits = multi ? 3.5 : 1.;
    const double base = std::max(0., attack * .5 - e.def * .25) * (multi ? .5 : 1.);
    const double normal = hits * base;
    const double burst = hits * (base * tension[tl] + tl * 3.);
    const double credit = std::max(0., std::min(double(e.hp), burst) - std::min(double(e.hp), normal));
    double v = -e.hp + c.tension * credit;
    v += c.attack * std::max(0, p.AtkBuffLevel) * std::clamp(p.AtkBuffTurn + 1, 1, 4) / 4.;
    v += c.defence * p.BuffLevel * std::clamp(p.BuffTurns + 1, 1, 4) / 4.;
    v += p.hp * c.health;
    const double danger = std::max(0., p.maxHp * .40 - p.hp);
    v -= c.danger * danger * danger;
    v += std::min(20, p.mp) * .6 + p.SpecialMedicineCount * 3. + p.SpecialAntidoteCount * 2.;
    if (tl == 4) v += 95; // Existing SHT resistance/damage reduction.
    if (p.inactive) v -= c.inactive;
    if (p.sleeping || p.paralysis) v -= 160;
    if (p.PoisonEnable) v -= c.poison;
    return v;
}

struct Link { uint32_t parent; int32_t action; };
struct Node {
    ZuoState s{};
    uint64_t hash = 0;
    double score = 0;
    uint32_t path = NoPath;
    int32_t action = 0;
};
struct Context {
    const Player *initial;
    uint64_t seed;
    Clock::time_point start, deadline, passDeadline;
    ZuoSearchResult result{};
    std::array<int32_t, 350> scratch{};
    BattleResult one{}, verification{};
    int bestPosition = 401, bestChanges = INT32_MAX;
    double upperDamage[134][5][5]{};
    void makeBound() {
        constexpr double multipliers[] = {.5, .75, 1., 1.25, 1.5};
        constexpr double tension[] = {1., 1.5, 2.5, 4., 6.};
        const auto &p = initial[0];
        const double baseAttack = std::max({double(B::ZUO_EQUIPPED_ATK),
            double(B::ZUO_BARE_HANDS_ATK), double(p.defaultATK),
            std::ceil(p.atk / multipliers[std::clamp(p.AtkBuffLevel + 2, 0, 4)])});
        const double enemyDefence = initial[1].def;
        double attackDamage[5][5], multiDamage[5][5];
        for (int t = 0; t < 5; ++t) for (int b = 0; b < 5; ++b) {
            // Deliberately optimistic: ignore misses, MP, enemy damage, rest,
            // buff expiry, change restrictions and critical-hit probability.
            // A negative attack buff may expire, so grant unbuffed ATK too.
            const double attack = std::ceil(baseAttack * std::max(1., multipliers[b]));
            const double centre = (attack - enemyDefence * .5) * .5;
            const double upperBase = std::max({0., centre * 17. / 16. + 1., attack / 16.});
            const double normal = upperBase * tension[t] + t * 3.;
            attackDamage[t][b] = std::ceil(std::max(normal * 1.2, baseAttack * 1.05));
            const double half = std::ceil(upperBase * .5);
            multiDamage[t][b] = 4. * std::ceil(std::max((half * tension[t] + t * 3.) * 1.2, half * 2.));
        }
        for (int k = 1; k < 134; ++k) for (int t = 0; t < 5; ++t) for (int b = 0; b < 5; ++b) {
            upperDamage[k][t][b] = std::max({
                std::max(attackDamage[t][b], multiDamage[t][b]) + upperDamage[k-1][0][b],
                upperDamage[k-1][std::min(4, t+1)][b],
                upperDamage[k-1][t][std::min(4, b+2)], upperDamage[k-1][t][b]});
        }
    }
    double elapsed() const { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
    bool expired() const { return Clock::now() >= passDeadline; }
    bool step(ZuoState &s, int action) {
        const int turn = turnOf(s);
        if (turn >= 349 || s.rng >= RngLimit || s.position >= 400) return false;
        scratch[turn] = action;
        one.clear();
        B::Main(&s.rng, 1, scratch.data(), s.players, &one, seed, nullptr, nullptr, -1, &s.state);
        s.position += one.position;
        s.changes += countChanges(one);
        ++result.expanded;
        return s.position <= 400;
    }
    bool prunable(const ZuoState &s) const {
        // In this actual world every nonterminal turn records two enemy
        // actions and one ally action (including FLEE); a kill needs >=1 row.
        // If changes already tie/exceed the incumbent, position must improve.
        const int limit = bestPosition - int(s.changes >= bestChanges);
        const int turns = std::min(133, (limit - s.position + 2) / 3);
        if (limit <= s.position || turns <= 0) return true;
        const auto &p = s.players[0];
        return upperDamage[turns][std::clamp(p.TensionLevel, 0, 4)]
                           [std::clamp(p.AtkBuffLevel + 2, 0, 4)] < s.players[1].hp;
    }
    void consider(const Node &n, const std::vector<Link> &links) {
        if (n.s.players[1].hp != 0 || expired()) return;
        if (n.s.position > bestPosition ||
            (n.s.position == bestPosition && n.s.changes >= bestChanges)) return;
        std::vector<int32_t> suffix{n.action};
        for (auto p = n.path; p != NoPath; p = links[p].parent) suffix.push_back(links[p].action);
        std::reverse(suffix.begin(), suffix.end());
        const int length = result.prefixLength + int(suffix.size());
        if (length >= 350) return;
        auto actions = result.actions;
        std::copy(suffix.begin(), suffix.end(), actions.begin() + result.prefixLength);
        std::fill(actions.begin() + length, actions.end(), -1);
        ZuoState exact;
        if (!ZuoSearch::Replay(initial, seed, actions.data(), length, result.prefixLength, exact, verification) ||
            !ZuoSearch::SameState(exact, n.s) || exact.players[1].hp != 0) {
            ++result.rejectedReplay;
            return;
        }
        if (Clock::now() >= deadline) return;
        if (!result.victory) result.firstVictoryMs = elapsed();
        result.victory = result.replayVerified = true;
        result.actions = actions;
        result.length = length;
        result.finalState = exact;
        result.replay = verification;
        bestPosition = exact.position;
        bestChanges = exact.changes;
        result.bestVictoryMs = elapsed();
        ++result.updates;
    }
};

struct StateSet {
    std::vector<uint32_t> slots;
    size_t mask = 0;
    void reset(size_t expected) {
        size_t capacity = 8;
        while (capacity < expected * 2) capacity *= 2;
        slots.assign(capacity, NoPath);
        mask = capacity - 1;
    }
    bool insert(const Node &n, std::vector<Node> &nodes, ZuoSearchResult &stats) {
        size_t i = n.hash & mask;
        while (slots[i] != NoPath) {
            auto &o = nodes[slots[i]];
            if (n.hash == o.hash && sameFuture(n.s, o.s)) {
                ++stats.duplicates;
                if (betterCost(n.s, o.s)) o = n;
                return false;
            }
            i = (i + 1) & mask;
        }
        slots[i] = uint32_t(nodes.size());
        return true;
    }
};

// Only turn-boundary flags determine carried rest. An INACTIVE_ALLY row in the
// preceding log is NOT a reason to collapse a currently selectable menu.
template<class Visit>
void successors(Context &ctx, const Node &parent, bool changeEquipment, Visit visit) {
    const auto &p = parent.s.players[0];
    const int keepBit = equipment(p);
    if (carriedRest(p)) {
        Node n = parent;
        n.action = B::ATTACK_ALLY | keepBit;
        if (ctx.step(n.s, n.action)) visit(n);
        return;
    }
    for (int action : Actions) {
        for (int choice = 0; choice < (changeEquipment ? 2 : 1); ++choice) {
            if ((ctx.result.expanded & 63) == 0 && ctx.expired()) return;
            const int weapon = choice ? keepBit ^ B::ACTION_BARE_HANDS : keepBit;
            const int encoded = action | weapon;
            if (!ZuoSearch::Legal(p, encoded)) continue;
            Node n = parent;
            n.action = encoded;
            if (ctx.step(n.s, encoded)) visit(n);
        }
    }
}

int phase(const Node &n) {
    const auto &p = n.s.players[0];
    if (carriedRest(p)) return 10;
    return std::clamp(p.TensionLevel, 0, 4) * 2 + int(p.AtkBuffLevel > 0);
}

bool beam(Context &ctx, const Config &cfg, size_t width, bool changeEquipment,
          int anchorLength = 0) {
    std::vector<Node> current, next, candidates;
    std::vector<Link> links;
    std::vector<uint32_t> order, chosen, group;
    std::vector<uint8_t> selected;
    StateSet seen;
    constexpr size_t Branches = std::size(Actions) * 2;
    current.reserve(width); next.reserve(width); candidates.reserve(width * Branches);
    links.reserve(width * 32);
    Node root; root.s = ctx.result.root;
    // Repair reuses only a suffix discovered in THIS invocation; fixed prefix
    // is not touched and no seed/prefix/answer is stored between invocations.
    for (int i = 0; i < anchorLength; ++i) {
        const int a = ctx.result.actions[ctx.result.prefixLength + i];
        if (ctx.expired() || !ZuoSearch::Legal(root.s.players[0], a) || !ctx.step(root.s, a)) return false;
        links.push_back({root.path, a}); root.path = uint32_t(links.size() - 1);
    }
    current.push_back(root);
    bool truncated = false;
    while (!current.empty()) {
        if (ctx.expired()) return false;
        candidates.clear(); seen.reset(current.size() * Branches);
        for (const auto &parent : current) {
            if (ctx.prunable(parent.s)) continue;
            successors(ctx, parent, changeEquipment, [&](Node &n) {
                if (n.s.players[1].hp == 0) { ctx.consider(n, links); return; }
                if (n.s.players[0].hp <= 0 || ctx.prunable(n.s)) return;
                n.hash = fingerprint(n.s);
                n.score = value(n.s, cfg, changeEquipment);
                if (seen.insert(n, candidates, ctx.result)) candidates.push_back(n);
            });
            if (ctx.expired()) return false;
        }
        const size_t keep = std::min(width, candidates.size());
        if (!keep) break;
        truncated |= keep < candidates.size();
        if (ctx.expired()) return false;
        order.resize(candidates.size()); std::iota(order.begin(), order.end(), 0);
        const auto better = [&](uint32_t a, uint32_t b) {
            const auto &x = candidates[a]; const auto &y = candidates[b];
            if (x.score != y.score) return x.score > y.score;
            if (x.s.position != y.s.position) return x.s.position < y.s.position;
            if (x.s.changes != y.s.changes) return x.s.changes < y.s.changes;
            return x.hash < y.hash;
        };
        chosen.clear(); selected.assign(candidates.size(), 0);
        if (cfg.quota && keep < candidates.size()) {
            const size_t quota = std::max<size_t>(1, keep / (11 * cfg.quota));
            for (int ph = 0; ph < 11; ++ph) {
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
    return !truncated;
}

// Depth-first branch-and-bound is deliberately a different search structure.
// The optimistic damage envelope only rejects impossible remaining horizons;
// actual damage, victory and the two objective values still come from Main.
bool depthFirst(Context &ctx, const Config &cfg, bool changeEquipment) {
    std::vector<Link> links;
    links.reserve(134);
    auto visit = [&](auto &&self, const Node &parent) -> void {
        if (ctx.expired() || ctx.prunable(parent.s)) return;
        std::vector<Node> children;
        children.reserve(std::size(Actions) * 2);
        successors(ctx, parent, changeEquipment, [&](Node &n) {
            if (n.s.players[1].hp == 0) { ctx.consider(n, links); return; }
            if (n.s.players[0].hp <= 0 || ctx.prunable(n.s)) return;
            n.score = value(n.s, cfg, changeEquipment);
            children.push_back(n);
        });
        std::sort(children.begin(), children.end(), [](const Node &a, const Node &b) {
            if (a.score != b.score) return a.score > b.score;
            return betterCost(a.s, b.s);
        });
        for (auto &n : children) {
            if (ctx.expired()) return;
            links.push_back({n.path, n.action}); n.path = uint32_t(links.size()-1);
            self(self, n);
            links.pop_back();
        }
    };
    Node root; root.s = ctx.result.root;
    visit(visit, root);
    return !ctx.expired();
}

// A distinct, global best-first alternative, compared against layered beams.
// Heuristic ordering is not a proof/lower bound and is never used for victory.
void bestFirst(Context &ctx, const Config &cfg, double weight) {
    struct Entry {
        double priority;
        uint32_t id;
        bool operator<(const Entry &b) const {
            if (priority != b.priority) return priority > b.priority;
            return id > b.id;
        }
    };
    std::vector<Node> nodes;
    std::vector<Link> links;
    constexpr size_t MaxNodes = 240000;
    nodes.reserve(MaxNodes); links.reserve(MaxNodes);
    StateSet seen; seen.reset(MaxNodes);
    std::priority_queue<Entry> open;
    Node root; root.s = ctx.result.root; root.hash = fingerprint(root.s);
    seen.insert(root, nodes, ctx.result); nodes.push_back(root); open.push({0., 0});
    while (!open.empty() && nodes.size() + 32 < MaxNodes && !ctx.expired()) {
        const Node parent = nodes[open.top().id]; open.pop();
        if (ctx.prunable(parent.s)) continue;
        successors(ctx, parent, true, [&](Node &n) {
            if (n.s.players[1].hp == 0) { ctx.consider(n, links); return; }
            if (n.s.players[0].hp <= 0 || ctx.prunable(n.s)) return;
            n.hash = fingerprint(n.s);
            n.score = n.s.position * weight - value(n.s, cfg, true) / 100. + n.s.changes * .015;
            size_t slot = n.hash & seen.mask;
            while (seen.slots[slot] != NoPath) {
                const auto &old = nodes[seen.slots[slot]];
                if (n.hash == old.hash && sameFuture(n.s, old.s)) {
                    ++ctx.result.duplicates;
                    if (!betterCost(n.s, old.s)) return;
                    break; // Reopen a cheaper identical state with a NEW path.
                }
                slot = (slot + 1) & seen.mask;
            }
            seen.slots[slot] = uint32_t(nodes.size());
            links.push_back({n.path, n.action}); n.path = uint32_t(links.size() - 1);
            open.push({n.score, uint32_t(nodes.size())}); nodes.push_back(n);
        });
    }
}
} // namespace

Genome ZuoSearchResult::toGenome() const {
    Genome g{};
    g.AllyPlayer = finalState.players[0]; g.EnemyPlayer = finalState.players[1];
    g.state = finalState.state; g.position = finalState.rng;
    g.turn = victory ? turnOf(finalState) + 1 : 100;
    g.processed = turnOf(finalState); g.Initialized = victory;
    std::copy(actions.begin(), actions.end(), g.actions);
    return g;
}

bool ZuoSearch::Legal(const Player &p, int action) {
    if (action <= 0 || (action & ~(B::ACTION_ID_MASK | B::ACTION_BARE_HANDS))) return false;
    const int id = action & B::ACTION_ID_MASK;
    if (carriedRest(p))
        return id == B::ATTACK_ALLY && (action & B::ACTION_BARE_HANDS) == equipment(p);
    switch (id) {
        case B::ATTACK_ALLY: case B::DEFENCE: case B::DOUBLE_UP:
        case B::PSYCHE_UP_ALLY: case B::FLEE_ALLY: return true;
        case B::BUFF: return p.mp >= 3;
        case B::MIDHEAL: return p.mp >= 4;
        case B::MULTITHRUST: return p.mp >= 4 && !(action & B::ACTION_BARE_HANDS);
        case B::SPECIAL_MEDICINE: return p.SpecialMedicineCount > 0;
        case B::SPECIAL_ANTIDOTE: return p.SpecialAntidoteCount > 0;
        case B::MAGIC_WATER: return p.MagicWaterCount > 0;
        default: return false;
    }
}
bool ZuoSearch::SameState(const ZuoState &a, const ZuoState &b) {
    return sameFuture(a, b) && a.position == b.position && a.changes == b.changes;
}

bool ZuoSearch::Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, int prefixLength, ZuoState &state, BattleResult &log) {
    if (!initial || !actions || length < 0 || length >= 350 || prefixLength < 0 || prefixLength > length) return false;
    ZuoState sequential;
    sequential.players[0] = initial[0]; sequential.players[1] = initial[1];
    BattleResult one;
    lcg::init(seed, true);
    for (int i = 0; i < length && sequential.players[0].hp > 0 && sequential.players[1].hp > 0; ++i) {
        if (actions[i] == -1 || sequential.rng >= RngLimit || sequential.position >= 400) return false;
        if (i >= prefixLength && !Legal(sequential.players[0], actions[i])) return false;
        one.clear();
        B::Main(&sequential.rng, 1, actions, sequential.players, &one, seed, nullptr, nullptr, -1, &sequential.state);
        sequential.position += one.position; sequential.changes += countChanges(one);
        if (sequential.position > 400) return false;
    }
    state = ZuoState{}; state.players[0] = initial[0]; state.players[1] = initial[1];
    log.clear(); lcg::init(seed, true);
    if (length && state.players[0].hp > 0 && state.players[1].hp > 0)
        B::Main(&state.rng, length, actions, state.players, &log, seed, nullptr, nullptr, -1, &state.state);
    state.position = log.position; state.changes = countChanges(log);
    return SameState(state, sequential);
}
int ZuoSearch::VariantCount() { return int(std::size(VariantNames)); }
const char *ZuoSearch::VariantName(int v) {
    return VariantNames[v >= 0 && v < VariantCount() ? v : DefaultVariant];
}

ZuoSearchResult ZuoSearch::Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength, int budgetMs, int variant) {
    const auto start = Clock::now();
    const auto budget = std::chrono::milliseconds(std::max(0, budgetMs));
    const auto reserve = std::min(std::chrono::milliseconds(20), budget / 20);
    // Large replay buffers live on the heap, including in a browser worker.
    std::unique_ptr<Context> context(new Context{initial, seed, start,
        start + budget - reserve, start + budget - reserve});
    Context &ctx = *context;
    ctx.result.actions.fill(-1); ctx.scratch.fill(-1);
    if (!initial || !prefix || prefixLength < 0 || prefixLength >= 350) {
        ctx.result.error = "Invalid initial state or fixed prefix length";
    } else {
        ctx.result.prefixLength = ctx.result.length = prefixLength;
        std::copy_n(prefix, prefixLength, ctx.result.actions.begin());
        if (!Replay(initial, seed, ctx.result.actions.data(), prefixLength, prefixLength, ctx.result.root, ctx.result.replay)) {
            ctx.result.error = "Fixed prefix cannot be replayed within the unchanged emulator buffers";
        } else {
            ctx.result.inputValid = true; ctx.result.finalState = ctx.result.root;
            ctx.makeBound();
            if (ctx.result.root.players[1].hp == 0) {
                ctx.result.victory = ctx.result.replayVerified = true;
                ctx.result.firstVictoryMs = ctx.result.bestVictoryMs = ctx.elapsed();
            } else if (ctx.result.root.players[0].hp > 0 && budgetMs > 0) {
                variant = variant >= 0 && variant < VariantCount() ? variant : DefaultVariant;
                auto pass = [&](int cfg, size_t width, bool equip, int anchor = 0, int ms = 220) {
                    const auto now = Clock::now();
                    if (now >= ctx.deadline) return;
                    ctx.passDeadline = std::min(ctx.deadline, now + std::chrono::milliseconds(ms));
                    beam(ctx, Configs[cfg], width, equip, anchor);
                };
                // Small passes provide an incumbent early; remaining time widens
                // and changes the ranking rather than returning the first win.
                const bool zeroOnly = variant == 7;
                const int cfg = variant >= 1 && variant <= 5 ? variant - 1 : (variant == 9 ? 1 : 0);
                pass(cfg, 48, false);
                if (!zeroOnly) pass(variant == 0 ? 1 : cfg, 64, true);
                bool complete = false;
                if (variant == 0) {
                    pass(1, 256, true);
                    ctx.passDeadline = std::min(ctx.deadline, Clock::now() + std::chrono::milliseconds(250));
                    complete = depthFirst(ctx, Configs[0], true);
                }
                for (int round = 0; !complete && Clock::now() < ctx.deadline; ++round) {
                    const size_t width = std::min<size_t>(4096, size_t(128) << std::min(round / 2, 5));
                    if (variant == 10) {
                        ctx.passDeadline = ctx.deadline;
                        if (depthFirst(ctx, Configs[round % 3], true)) break;
                    } else if (variant == 6) {
                        ctx.passDeadline = ctx.deadline;
                        bestFirst(ctx, Configs[round % 3], .5 + .15 * (round % 3));
                    } else if (variant == 0) {
                        pass(round % 3, width, round % 4 != 2);
                        if (ctx.result.victory && round % 2 == 1) {
                            const int tail = 3 + round % 5;
                            const int anchor = std::max(0, ctx.result.length - ctx.result.prefixLength - tail);
                            pass((round + 1) % 3, std::min<size_t>(width, 1024), true, anchor, 140);
                            ctx.passDeadline = std::min(ctx.deadline, Clock::now() + std::chrono::milliseconds(250));
                            complete = depthFirst(ctx, Configs[round % 3], true);
                        }
                    } else {
                        pass(cfg, variant >= 8 ? std::min<size_t>(8192, width * 4) : width, !zeroOnly, 0, 400);
                    }
                }
            }
        }
    }
    ctx.result.elapsedMs = ctx.elapsed();
    return ctx.result;
}
