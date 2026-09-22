#include "ActionSearchOptimized.h"
#include "BattleEmulator.h"
#include "BattleResult.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <numeric>
#include <tuple>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoPath = UINT32_MAX;
constexpr int SelectedVariant = 30;
constexpr std::array<int32_t, 8> Actions = {
    BattleEmulator::ATTACK_ALLY, BattleEmulator::DRAGON_SLASH,
    BattleEmulator::CRACK_ALLY, BattleEmulator::HEAL,
    BattleEmulator::MEDICINAL_HERBS, BattleEmulator::DEFENCE,
    BattleEmulator::ACROBATIC_STAR, BattleEmulator::FLEE_ALLY
};

// All Player fields participate in semantic replay equality. No game field is
// discarded, even if it looks inactive in this particular branch.
auto playerTuple(const Player &p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis, p.paralysisLevel,
        p.paralysisTurns, p.SpecialMedicineCount, p.defence, p.sleeping,
        p.sleepingTurn, p.BuffLevel, p.BuffTurns, p.hasMagicMirror,
        p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn, p.TensionLevel, p.rage,
        p.SageElixirCount, p.ElfinElixirCount, p.MagicWaterCount, p.speedTurn,
        p.speedLevel, p.PoisonTurn, p.PoisonEnable, p.SpecialAntidoteCount,
        p.acrobaticStar, p.acrobaticStarTurn, p.rageTurns,
        p.medicinal_herbs_count, p.inactive);
}

uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

uint64_t fingerprint(const ActionSearchState &s) {
    // Reading object representation is legal; padding differences may only miss
    // a duplicate, never merge unequal states. Equality is checked on collision.
    uint64_t a = 0x9e3779b97f4a7c15ULL, b = 0x243f6a8885a308d3ULL;
    const auto *bytes = reinterpret_cast<const unsigned char *>(s.players);
    size_t i = 0;
    for (; i + 16 <= sizeof(s.players); i += 16) {
        uint64_t x, y;
        std::memcpy(&x, bytes + i, 8); std::memcpy(&y, bytes + i + 8, 8);
        a = (a ^ x) * 0xd6e8feb86659fd93ULL;
        b = (b ^ y) * 0xa0761d6478bd642fULL;
    }
    for (; i < sizeof(s.players); ++i) a = (a ^ bytes[i]) * 1099511628211ULL;
    return mix(a ^ b ^ mix(s.nowState) ^ mix(static_cast<uint32_t>(s.position)));
}

struct Config {
    const char *name;
    int charge, star, hp, danger, status, phaseQuota;
    int firstWidth;
    int widthMultiplier;
    bool duration;
    int positionWeight;
    int scoreMode = 0;
    int blockSize = 1;
};

// Values are generic ordering estimates in hundredths of enemy HP. They never
// modify actual state, RNG, legal actions or reported victory scores.
constexpr Config Configs[] = {
    {"tactical-duration",       4200, 7200, 32, 12, 1800, 4,  512, 4, true,  0},
    {"position-tie-only",          5,    7,  1,  1,   18, 0, 2048, 4, false, 0},
    {"charge-20-star-40",       2000, 4000, 20, 10, 1200, 0,  512, 4, false, 0},
    {"charge-40-star-70",       4000, 7000, 30, 12, 1800, 0,  512, 4, false, 0},
    {"charge-60-star-100",      6000,10000, 30, 12, 1800, 0,  512, 4, false, 0},
    {"charge-80-star-120",      8000,12000, 30, 12, 1800, 0,  512, 4, false, 0},
    {"duration-credit",        4200, 7200, 32, 12, 1800, 0,  512, 4, true,  0},
    {"phase-quota-quarter",    4200, 7200, 32, 12, 1800, 4,  512, 4, true,  0},
    {"phase-quota-half",       4200, 7200, 32, 12, 1800, 2,  512, 4, true,  0},
    {"phase-quota-eighth",     4200, 7200, 32, 12, 1800, 8,  512, 4, true,  0},
    {"microbeam-128",          4200, 7200, 32, 12, 1800, 4,  128, 4, true,  0},
    {"microbeam-256",          4200, 7200, 32, 12, 1800, 4,  256, 4, true,  0},
    {"wide-start-1024",        4200, 7200, 32, 12, 1800, 4, 1024, 4, true,  0},
    {"wide-start-2048",        4200, 7200, 32, 12, 1800, 4, 2048, 4, true,  0},
    {"gentle-widening",        4200, 7200, 32, 12, 1800, 4,  512, 2, true,  0},
    {"damage-greedy",          4200, 7200,  8,  2,  800, 4,  512, 4, true,  0},
    {"survival-heavy",         4200, 7200, 70, 25, 2500, 4,  512, 4, true,  0},
    {"charge-heavy-duration",  7000, 7200, 32, 12, 1800, 4,  512, 4, true,  0},
    {"counter-heavy-duration", 4200,10500, 32, 12, 1800, 4,  512, 4, true,  0},
    {"status-aware-heavy",     4200, 7200, 32, 12, 4000, 4,  512, 4, true,  0},
    {"status-aware-light",     4200, 7200, 32, 12,  600, 4,  512, 4, true,  0},
    {"rng-progress-tie",       4200, 7200, 32, 12, 1800, 4,  512, 4, true,  2},
    {"duration-low-credit",    2400, 4500, 32, 12, 1800, 4,  512, 4, true,  0},
    {"duration-high-credit",   7000,11000, 32, 12, 1800, 4,  512, 4, true,  0},
    {"narrow-survival",        4200, 7200, 55, 18, 2200, 4,  256, 4, true,  0},
    {"wide-counter",           4200,10500, 25,  8, 1400, 4, 1024, 4, true,  0},
    {"damage-tactical-portfolio",4200,7200,32, 12, 1800, 4,  512, 4, true,  0},
    {"two-ply-delayed-pruning",4200, 7200, 32, 12, 1800, 4,  128, 4, true,  0, 0, 2},
    {"three-ply-delayed-pruning",4200,7200,32, 12, 1800, 4,   32, 4, true,  0, 0, 3},
    {"incumbent-tail-repair",  4200, 7200, 32, 12, 1800, 4,  512, 4, true,  0},
    {"portfolio-tail-repair",  4200, 7200, 32, 12, 1800, 4,  512, 4, true,  0},
    {"baseline-score-fast",       0,    0,  0,  0,    0, 0, 2048, 4, false, 0, 1}
};

bool legal(const Player &p, int a) {
    if (a == BattleEmulator::ACROBATIC_STAR) return p.specialCharge && p.specialChargeTurn != 0;
    if (a == BattleEmulator::HEAL) return p.mp >= 2;
    if (a == BattleEmulator::CRACK_ALLY) return p.mp >= 3;
    if (a == BattleEmulator::MEDICINAL_HERBS) return p.medicinal_herbs_count > 0;
    return true;
}

int64_t score(const ActionSearchState &s, const Config &cfg) {
    const auto &a = s.players[0];
    if (cfg.scoreMode == 1) {
        // Original nonterminal score, verbatim weights. New selection/metric
        // handling is tested independently from tactical heuristic changes.
        int64_t v = int64_t(std::max(0, s.players[1].maxHp - s.players[1].hp)) * 10000000LL;
        v += int64_t(a.hp) * 110000 + int64_t(std::max(0, a.mp)) * 22000;
        v += int64_t(a.medicinal_herbs_count) * 18000 + int64_t(a.SpecialMedicineCount) * 3000;
        v += int64_t(a.AtkBuffLevel) * 90000 + int64_t(a.BuffLevel) * 35000 + int64_t(a.speedLevel) * 18000;
        v += a.specialCharge ? 500000 : 0; v += a.acrobaticStar ? 650000 : 0;
        v -= a.paralysis ? 1800000 : 0; v -= a.inactive ? 1100000 : 0; v -= a.sleeping ? 1100000 : 0;
        const int danger = std::max(0, 28 - a.hp);
        return v - int64_t(danger) * danger * 55000;
    }
    int64_t v = -int64_t(s.players[1].hp) * 100;
    v += a.hp * cfg.hp + std::max(0, a.mp) * 18 + a.medicinal_herbs_count * 24;
    v += a.AtkBuffLevel * 30 + a.BuffLevel * 12 + a.speedLevel * 6;
    if (a.specialCharge && a.specialChargeTurn > 0)
        v += cfg.charge * (cfg.duration ? std::min(3, a.specialChargeTurn) : 3) / 3;
    if (a.acrobaticStar)
        v += cfg.star * (cfg.duration ? std::clamp(a.acrobaticStarTurn, 0, 5) : 5) / 5;
    v -= a.paralysis ? cfg.status + std::max(0, a.paralysisTurns) * cfg.status / 5 : 0;
    v -= a.inactive ? cfg.status / 2 : 0;
    v -= a.sleeping ? cfg.status : 0;
    const int danger = std::max(0, 25 - a.hp);
    v -= danger * danger * cfg.danger;
    v -= int64_t(cfg.positionWeight) * s.position;
    return v;
}

struct Link { uint32_t parent; uint8_t action; };
struct Node {
    ActionSearchState state{};
    int64_t value = 0;
    uint64_t hash = 0;
    uint32_t path = NoPath;
    int events = 0;
    uint8_t action = 0;
};

std::vector<int32_t> path(const std::vector<Link> &links, const Node &n) {
    std::vector<int32_t> result{int32_t(n.action)};
    for (uint32_t p = n.path; p != NoPath; p = links[p].parent) result.push_back(links[p].action);
    std::reverse(result.begin(), result.end());
    return result;
}

struct Context {
    const ActionSearchState &root;
    uint64_t seed;
    int maxTurns;
    Clock::time_point began, deadline;
    ActionSearchMetrics stats{};
    ActionSearchResult result{};
    int bestEvents = INT32_MAX;
    int bestDepth = INT32_MAX;
    int64_t bestPartial = INT64_MIN;
    std::vector<int32_t> best;
    BattleResult log; // Reused: do not clear the 1000-entry backing arrays per node.

    double elapsed() const { return std::chrono::duration<double, std::milli>(Clock::now() - began).count(); }

    void consider(const Node &n, const std::vector<Link> &links, int depth) {
        if (n.state.players[1].hp == 0) {
            const double when = elapsed();
            if (Clock::now() >= deadline) return; // Never adopt an after-deadline win.
            if (stats.firstVictoryMs < 0) stats.firstVictoryMs = when;
            if (n.events < bestEvents || (n.events == bestEvents &&
                (depth < bestDepth || (depth == bestDepth && n.state.players[0].hp > stats.finalState.players[0].hp)))) {
                bestEvents = n.events; bestDepth = depth;
                result.victory = true;
                best = path(links, n);
                stats.bestVictoryMs = when; ++stats.updates;
                stats.finalState = n.state; stats.suffixResultPosition = n.events;
                result.score = -n.events;
            }
        } else if (!result.victory && score(n.state, Configs[0]) > bestPartial) {
            bestPartial = score(n.state, Configs[0]); best = path(links, n);
            stats.finalState = n.state; stats.suffixResultPosition = n.events;
            result.score = bestPartial;
        }
    }
};

// Collision-resolving state table. No fingerprint-only equality, no lossy
// HP bucket dominance, and no mutation of the emulator's state representation.
struct StateSet {
    std::vector<uint32_t> slots;
    size_t mask = 0;
    void clear(size_t expected) {
        size_t cap = 8; while (cap < expected * 2) cap *= 2;
        slots.assign(cap, NoPath); mask = cap - 1;
    }
    bool insert(const Node &n, const std::vector<Node> &nodes, ActionSearchMetrics &stats) {
        size_t i = n.hash & mask;
        while (slots[i] != NoPath) {
            const Node &other = nodes[slots[i]];
            if (other.hash == n.hash) {
                if (other.events == n.events && other.state.nowState == n.state.nowState &&
                    other.state.position == n.state.position &&
                    std::memcmp(other.state.players, n.state.players, sizeof(n.state.players)) == 0) {
                    ++stats.duplicates; return false;
                }
                ++stats.hashCollisions;
            }
            i = (i + 1) & mask;
        }
        slots[i] = static_cast<uint32_t>(nodes.size());
        return true;
    }
};

int phase(const Node &n) {
    const auto &p = n.state.players[0];
    return (p.acrobaticStar ? 2 : (p.specialCharge && p.specialChargeTurn > 0 ? 1 : 0))
         + (p.paralysis || p.inactive || p.sleeping ? 3 : 0);
}

bool beam(Context &ctx, const Config &cfg, size_t width,
          const std::vector<int32_t> &anchor = {}) {
    std::vector<Node> current, next, candidates;
    std::vector<Link> links;
    std::vector<uint32_t> order, chosen;
    std::vector<uint8_t> flags;
    StateSet seen;
    current.reserve(width); next.reserve(width); candidates.reserve(width * 8);
    links.reserve(width * std::min(ctx.maxTurns, 40));
    Node root; root.state = ctx.root; root.value = score(root.state, cfg);
    // This is a prefix of a solution discovered during THIS Run, not an external
    // solution or an initial-world restart. Replaying from the authoritative
    // root keeps the user's fixed prefix strictly outside the search domain.
    for (int32_t action : anchor) {
        if (Clock::now() >= ctx.deadline) return false;
        const int32_t gene[2] = {action, -1};
        ctx.log.clear();
        BattleEmulator::Main(&root.state.position, 1, gene, root.state.players,
            &ctx.log, ctx.seed, nullptr, nullptr, -1, &root.state.nowState, true);
        ++ctx.result.expanded; root.events += ctx.log.position;
        links.push_back({root.path, uint8_t(action)}); root.path = uint32_t(links.size() - 1);
    }
    current.push_back(root);
    bool pruned = false;
    for (int depth = int(anchor.size()) + 1; depth <= ctx.maxTurns && !current.empty(); ++depth) {
        if (Clock::now() >= ctx.deadline) return false;
        candidates.clear(); seen.clear(current.size() * 8);
        for (const Node &parent : current) {
            if (parent.events + 1 > ctx.bestEvents) continue;
            for (int action : Actions) {
                if (!legal(parent.state.players[0], action)) continue;
                // Deadline is tested regardless of death, duplicates or wins.
                if ((ctx.result.expanded & 255) == 0 && Clock::now() >= ctx.deadline) return false;
                Node n;
                n.state = parent.state; n.path = parent.path; n.action = uint8_t(action);
                const int32_t gene[2] = {action, -1};
                ctx.log.clear();
                BattleEmulator::Main(&n.state.position, 1, gene, n.state.players,
                    &ctx.log, ctx.seed, nullptr, nullptr, -1, &n.state.nowState, true);
                ++ctx.result.expanded;
                n.events = parent.events + ctx.log.position;
                if (n.state.players[0].hp == 0 || n.events > ctx.bestEvents) continue;
                n.value = score(n.state, cfg);
                ctx.consider(n, links, depth);
                if (n.state.players[1].hp == 0 || n.events + 1 > ctx.bestEvents) continue;
                n.hash = fingerprint(n.state);
                if (!seen.insert(n, candidates, ctx.stats)) continue;
                candidates.push_back(n);
            }
        }
        size_t layerWidth = width;
        for (int k = 0; k < depth % cfg.blockSize; ++k) layerWidth *= 8;
        const size_t keep = std::min(layerWidth, candidates.size());
        if (keep == 0) break;
        pruned |= keep < candidates.size();
        order.resize(candidates.size()); std::iota(order.begin(), order.end(), 0);
        const auto better = [&](uint32_t a, uint32_t b) {
            return candidates[a].value != candidates[b].value ? candidates[a].value > candidates[b].value
                 : candidates[a].hash < candidates[b].hash;
        };
        chosen.clear(); chosen.reserve(keep);
        flags.assign(candidates.size(), 0);
        if (cfg.phaseQuota && keep < candidates.size()) {
            // Reserve a small independent beam per tactical phase from ALL
            // candidates, not merely from the already-greedy top score pool.
            const size_t quota = std::max<size_t>(1, keep / (6 * cfg.phaseQuota));
            for (int p = 0; p < 6; ++p) {
                std::vector<uint32_t> group;
                for (uint32_t i = 0; i < candidates.size(); ++i) if (phase(candidates[i]) == p) group.push_back(i);
                const size_t k = std::min(quota, group.size());
                if (k < group.size()) std::nth_element(group.begin(), group.begin() + k, group.end(), better);
                for (size_t j = 0; j < k; ++j) { chosen.push_back(group[j]); flags[group[j]] = 1; }
            }
        }
        // The top keep global scores contain enough remaining candidates even
        // after phase reservations; no 4*width full sort is necessary.
        if (keep < order.size()) { std::nth_element(order.begin(), order.begin() + keep, order.end(), better); order.resize(keep); }
        std::sort(order.begin(), order.end(), better);
        for (uint32_t i : order) if (!flags[i] && chosen.size() < keep) chosen.push_back(i);
        next.clear();
        for (uint32_t i : chosen) {
            Node n = candidates[i];
            links.push_back({n.path, n.action}); n.path = static_cast<uint32_t>(links.size() - 1);
            next.push_back(n);
        }
        current.swap(next);
    }
    ctx.result.completedBeamWidth = static_cast<int>(width);
    return !pruned;
}

void repair(Context &ctx, const Config &cfg, size_t width) {
    if (!ctx.result.victory) return;
    for (int tail : {3, 5, 7, 9}) {
        if (Clock::now() >= ctx.deadline) break;
        const size_t cut = ctx.best.size() > size_t(tail) ? ctx.best.size() - tail : 0;
        const std::vector<int32_t> anchor(ctx.best.begin(), ctx.best.begin() + cut);
        beam(ctx, cfg, width, anchor);
    }
}
} // namespace

bool ActionSearchOptimized::SameState(const ActionSearchState &a, const ActionSearchState &b) {
    return a.position == b.position && a.nowState == b.nowState &&
           playerTuple(a.players[0]) == playerTuple(b.players[0]) &&
           playerTuple(a.players[1]) == playerTuple(b.players[1]);
}

const char *ActionSearchOptimized::VariantName(int variant) {
    if (variant == 0) variant = SelectedVariant;
    return Configs[variant >= 0 && variant < int(std::size(Configs)) ? variant : 0].name;
}

ActionSearchResult ActionSearchOptimized::Run(const ActionSearchState &start, uint64_t seed,
        int maxTurns, int timeBudgetMs, ActionSearchMetrics *metrics, int variant) {
    const auto began = Clock::now();
    if (variant == 0) variant = SelectedVariant;
    const auto budget = std::chrono::microseconds(int64_t(std::max(0, timeBudgetMs)) * 1000);
    // Reserve bounded cleanup/selection time within the caller's budget; no
    // candidate discovered after the internal deadline is accepted.
    const auto cleanupReserve = std::min(std::chrono::microseconds(50000), budget / 20);
    Context ctx{start, seed, std::clamp(maxTurns, 0, 349), began,
                began + budget - cleanupReserve};
    ctx.stats.finalState = start;
    ctx.result.actions.fill(-1);
    if (start.players[1].hp == 0 && start.players[0].hp > 0 && maxTurns > 0 && timeBudgetMs > 0) {
        ctx.result.victory = true;
        ctx.stats.firstVictoryMs = ctx.stats.bestVictoryMs = 0;
    }
    if (ctx.maxTurns > 0 && timeBudgetMs > 0 && start.players[0].hp > 0 && !ctx.result.victory) {
        const auto &cfg = Configs[variant >= 0 && variant < int(std::size(Configs)) ? variant : 0];
        ctx.bestPartial = score(start, Configs[0]); ctx.result.score = ctx.bestPartial;
        if (variant == 26 || variant == 30) {
            for (size_t width : {size_t(512), size_t(2048), size_t(8192), size_t(32768), size_t(65536)}) {
                if (Clock::now() >= ctx.deadline) break;
                // A complete unpruned traversal has already covered every legal
                // state within the remaining objective bound. Repeating it at a
                // larger width cannot discover anything new, including no-win roots.
                if (beam(ctx, Configs[31], width)) break;
                if (variant == 30) repair(ctx, cfg, std::min<size_t>(width, 2048));
                if (Clock::now() >= ctx.deadline) break;
                if (beam(ctx, cfg, width)) break;
            }
        } else {
            for (size_t width = cfg.firstWidth; width <= 65536 && Clock::now() < ctx.deadline; width *= cfg.widthMultiplier) {
                if (beam(ctx, cfg, width)) break;
                if (variant == 29) repair(ctx, cfg, std::min<size_t>(width, 4096));
            }
        }
    }
    ctx.result.length = std::min<int>(349, ctx.best.size());
    std::copy_n(ctx.best.begin(), ctx.result.length, ctx.result.actions.begin());
    ctx.stats.elapsedMs = ctx.elapsed();
    if (metrics) *metrics = ctx.stats;
    return ctx.result;
}