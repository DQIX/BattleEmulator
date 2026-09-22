#include "BilyoumaSearch.h"
#include "lcg.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <numeric>
#include <tuple>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoPath = UINT32_MAX;
constexpr int Actions[] = {50, 55, 53, 73, 26, 75, 85, 25, 27, 56};

auto fields(const Player &p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis, p.paralysisLevel,
        p.paralysisTurns, p.SpecialMedicineCount, p.defence, p.sleeping,
        p.sleepingTurn, p.BuffLevel, p.BuffTurns, p.hasMagicMirror,
        p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn, p.TensionLevel, p.rage,
        p.SageElixirCount, p.ElfinElixirCount, p.MagicWaterCount, p.speedTurn,
        p.speedLevel, p.PoisonTurn, p.PoisonEnable, p.SpecialAntidoteCount,
        p.acrobaticStar, p.acrobaticStarTurn);
}

uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

uint64_t hashState(const BilyoumaState &s) {
    // Padding can miss a duplicate, but can NEVER merge unequal states.
    // Every collision is resolved using all semantic Player fields below.
    const auto *bytes = reinterpret_cast<const unsigned char *>(s.players);
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    size_t i = 0;
    for (; i + 8 <= sizeof(s.players); i += 8) {
        uint64_t v;
        std::memcpy(&v, bytes + i, 8);
        h = (h ^ v) * 0xd6e8feb86659fd93ULL;
    }
    for (; i < sizeof(s.players); ++i) h = (h ^ bytes[i]) * 1099511628211ULL;
    return mix(h ^ mix(s.nowState) ^ mix(uint32_t(s.rngPosition))
                 ^ mix(uint32_t(s.resultPosition)));
}

// Ordering estimates are in hundredths of enemy HP. They are NOT game rules.
// All parameter sets are retained for reproducible development comparisons.
struct Config {
    const char *name;
    int charge, star, hp, danger, sleep, poison, items;
    int quota, firstWidth;
    bool duration, portfolio, repair;
};
constexpr Config Configs[] = {
    {"star-portfolio-repair", 7000, 12000, 22, 12, 2600, 1000, 160, 4, 256, true, true, true},
    {"damage-only",              0,     0,  8,  4, 1000,  200,  50, 0, 256, false,false,false},
    {"star-balanced",        7000, 12000, 22, 12, 2600, 1000, 160, 4, 256, true, false,false},
    {"star-low-credit",      3500,  6500, 22, 12, 2600, 1000, 160, 4, 256, true, false,false},
    {"star-high-credit",    11000, 18000, 22, 12, 2600, 1000, 160, 4, 256, true, false,false},
    {"survival",             7000, 12000, 50, 25, 4000, 1800, 280, 4, 256, true, false,false},
    {"burst-greedy",          6000, 14000,  8,  3, 1600,  500,  60, 4, 256, true, false,false},
    {"full-duration-credit", 7000, 12000, 22, 12, 2600, 1000, 160, 4, 256, false,false,false},
    {"no-phase-quota",       7000, 12000, 22, 12, 2600, 1000, 160, 0, 256, true, false,false},
    {"star-portfolio",       7000, 12000, 22, 12, 2600, 1000, 160, 4, 256, true, true, false},
    {"star-repair",          7000, 12000, 22, 12, 2600, 1000, 160, 4, 256, true, false,true},
    {"star-wide-start",      7000, 12000, 22, 12, 2600, 1000, 160, 4,1024, true, true, true},
    {"star-micro-start",     7000, 12000, 22, 12, 2600, 1000, 160, 4,  64, true, true, true},
    {"burst-micro-portfolio-repair",6000,14000,8,3,1600,500,60,4,64,true,true,true},
    {"burst-repair",          6000, 14000,  8,  3, 1600,  500,  60, 4, 256, true, false,true},
    {"burst-micro",           6000, 14000,  8,  3, 1600,  500,  60, 4,  64, true, false,false},
    {"burst-micro-repair",    6000, 14000,  8,  3, 1600,  500,  60, 4,  64, true, false,true},
    {"burst-quota-half",      6000, 14000,  8,  3, 1600,  500,  60, 2,  64, true, true, true},
    {"burst-quota-eighth",    6000, 14000,  8,  3, 1600,  500,  60, 8,  64, true, true, true},
    {"burst-nano-start",      6000, 14000,  8,  3, 1600,  500,  60, 4,  32, true, true, true}
};

int64_t value(const BilyoumaState &s, const Config &cfg) {
    const auto &p = s.players[0];
    // Scale tactical credit to the actual actor rather than an Ishudalu build.
    const int attack = std::max(1, p.defaultATK);
    int64_t v = -int64_t(s.players[1].hp) * 100;
    v += int64_t(p.hp) * cfg.hp + std::max(0, p.mp) * 18;
    v += int64_t(p.SpecialMedicineCount) * cfg.items;
    v += p.SpecialAntidoteCount * cfg.items / 2;
    v += p.BuffLevel * 220 + p.speedLevel * 90 + p.AtkBuffLevel * 250;
    if (p.specialCharge && p.specialChargeTurn != 0 && !p.dirtySpecialCharge)
        v += int64_t(cfg.charge) * attack / 82
           * (cfg.duration ? std::clamp(p.specialChargeTurn, 0, 3) : 3) / 3;
    if (p.acrobaticStar)
        v += int64_t(cfg.star) * attack / 82
           * (cfg.duration ? std::clamp(p.acrobaticStarTurn, 0, 5) : 5) / 5;
    if (p.sleeping) v -= cfg.sleep + std::max(0, p.sleepingTurn) * 150;
    if (p.paralysis) v -= cfg.sleep;
    if (p.PoisonEnable) v -= cfg.poison;
    const int danger = std::max(0, std::min(p.maxHp / 2, 38) - p.hp);
    v -= int64_t(danger) * danger * cfg.danger;
    return v;
}

struct Link { uint32_t parent; uint8_t action; };
struct Node {
    BilyoumaState state{};
    uint64_t hash = 0;
    int64_t score = 0;
    uint32_t path = NoPath;
    uint8_t action = 0;
};

std::vector<int32_t> materialize(const Node &n, const std::vector<Link> &links) {
    std::vector<int32_t> out{int32_t(n.action)};
    for (auto id = n.path; id != NoPath; id = links[id].parent)
        out.push_back(links[id].action);
    std::reverse(out.begin(), out.end());
    return out;
}

struct Context {
    const Player *initial;
    uint64_t seed;
    int maxTotalTurns;
    Clock::time_point start, deadline;
    BilyoumaResult result{};
    std::vector<int32_t> bestSuffix;
    BattleResult transitionLog{}; // Only clear counters, not 1000-entry arrays.
    BattleResult verificationLog{};
    int bestPosition = INT32_MAX;

    double elapsed() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    void consider(const Node &n, const std::vector<Link> &links) {
        if (n.state.players[1].hp != 0 || Clock::now() >= deadline) return;
        if (n.state.resultPosition > bestPosition) return;
        if (n.state.resultPosition == bestPosition &&
            n.state.players[0].hp <= result.finalState.players[0].hp) return;
        const auto suffix = materialize(n, links);
        std::array<int32_t, 350> actions = result.actions;
        std::copy(suffix.begin(), suffix.end(), actions.begin() + result.prefixLength);
        const int length = result.prefixLength + int(suffix.size());
        actions[length] = -1;
        BilyoumaState exact;
        // Same seed/cache, original world, original prefix; no generated answer input.
        if (!BilyoumaSearch::Replay(initial, seed, actions.data(), length, exact, verificationLog)
            || !BilyoumaSearch::SameState(exact, n.state)) {
            ++result.rejectedReplay;
            return;
        }
        if (Clock::now() >= deadline) return;
        const double when = elapsed();
        if (!result.victory) result.firstVictoryMs = when;
        bestPosition = exact.resultPosition;
        bestSuffix = suffix;
        result.victory = result.replayVerified = true;
        result.actions = actions;
        result.length = length;
        result.finalState = exact;
        result.replay = verificationLog;
        result.bestVictoryMs = when;
        ++result.updates;
    }
};

struct StateSet {
    std::vector<uint32_t> slots;
    size_t mask = 0;
    void reset(size_t expected) {
        size_t cap = 8;
        while (cap < expected * 2) cap *= 2;
        slots.assign(cap, NoPath);
        mask = cap - 1;
    }
    bool insert(const Node &n, const std::vector<Node> &nodes, BilyoumaResult &stats) {
        size_t i = n.hash & mask;
        while (slots[i] != NoPath) {
            const auto &o = nodes[slots[i]];
            if (n.hash == o.hash) {
                if (BilyoumaSearch::SameState(n.state, o.state)) {
                    ++stats.duplicates;
                    return false;
                }
                ++stats.collisions;
            }
            i = (i + 1) & mask;
        }
        slots[i] = uint32_t(nodes.size());
        return true;
    }
};

int phase(const Node &n) {
    const auto &p = n.state.players[0];
    return (p.acrobaticStar ? 2 : (p.specialCharge && p.specialChargeTurn != 0 ? 1 : 0))
        + ((p.sleeping || p.paralysis) ? 3 : 0);
}

// All successors use the real Main, including enemies, poison and free camera.
// True means an unpruned traversal completed; repeating it cannot improve it.
bool beam(Context &ctx, const Config &cfg, size_t width,
          const std::vector<int32_t> &anchor = {}) {
    std::vector<Node> current, next, candidates;
    std::vector<Link> links;
    std::vector<uint32_t> order, chosen, group;
    std::vector<uint8_t> flags;
    StateSet seen;
    current.reserve(width); next.reserve(width); candidates.reserve(width * 10);
    links.reserve(width * size_t(std::max(1, ctx.maxTotalTurns - ctx.result.prefixLength)));
    Node root;
    root.state = ctx.result.root;
    for (int a : anchor) {
        if (Clock::now() >= ctx.deadline) return false;
        if (!BilyoumaSearch::Legal(root.state.players[0], a) ||
            !BilyoumaSearch::Safe(root.state.players[0], a)) return false;
        const int32_t one[2] = {a, -1};
        ctx.transitionLog.clear();
        BattleEmulator::Main(&root.state.rngPosition, 1, one, root.state.players,
            &ctx.transitionLog, ctx.seed, nullptr, nullptr, -1, &root.state.nowState, true);
        root.state.resultPosition += ctx.transitionLog.position;
        ++ctx.result.expanded;
        links.push_back({root.path, uint8_t(a)});
        root.path = uint32_t(links.size() - 1);
    }
    current.push_back(root);
    bool pruned = false;
    for (int turn = ctx.result.prefixLength + int(anchor.size()) + 1;
         turn <= ctx.maxTotalTurns && !current.empty(); ++turn) {
        if (Clock::now() >= ctx.deadline) return false;
        candidates.clear();
        seen.reset(current.size() * 10);
        for (const Node &parent : current) {
            if (parent.state.resultPosition + 1 > ctx.bestPosition) continue;
            for (int action : Actions) {
                if (!BilyoumaSearch::Legal(parent.state.players[0], action)) continue;
                if (!BilyoumaSearch::Safe(parent.state.players[0], action)) {
                    ++ctx.result.unsafeFleeSkipped;
                    continue;
                }
                if ((ctx.result.expanded & 127) == 0 && Clock::now() >= ctx.deadline) return false;
                Node n;
                n.state = parent.state; n.path = parent.path; n.action = uint8_t(action);
                const int32_t one[2] = {action, -1};
                ctx.transitionLog.clear();
                BattleEmulator::Main(&n.state.rngPosition, 1, one, n.state.players,
                    &ctx.transitionLog, ctx.seed, nullptr, nullptr, -1, &n.state.nowState, true);
                ++ctx.result.expanded;
                n.state.resultPosition += ctx.transitionLog.position;
                if (n.state.resultPosition > ctx.bestPosition) continue;
                // The explicit objective is all enemy HP == 0. Do not substitute
                // a heuristic or reject an otherwise valid simultaneous kill.
                if (n.state.players[1].hp == 0) {
                    ctx.consider(n, links);
                    continue;
                }
                if (n.state.players[0].hp == 0 || n.state.resultPosition + 1 > ctx.bestPosition) continue;
                n.score = value(n.state, cfg);
                n.hash = hashState(n.state);
                if (seen.insert(n, candidates, ctx.result)) candidates.push_back(n);
            }
        }
        const size_t keep = std::min(width, candidates.size());
        if (keep == 0) break;
        pruned |= keep < candidates.size();
        order.resize(candidates.size());
        std::iota(order.begin(), order.end(), 0);
        const auto better = [&](uint32_t a, uint32_t b) {
            if (candidates[a].score != candidates[b].score)
                return candidates[a].score > candidates[b].score;
            if (candidates[a].state.resultPosition != candidates[b].state.resultPosition)
                return candidates[a].state.resultPosition < candidates[b].state.resultPosition;
            return candidates[a].hash < candidates[b].hash;
        };
        chosen.clear(); flags.assign(candidates.size(), 0);
        if (cfg.quota && keep < candidates.size()) {
            const size_t quota = std::max<size_t>(1, keep / (6 * cfg.quota));
            for (int p = 0; p < 6; ++p) {
                group.clear();
                for (uint32_t i = 0; i < candidates.size(); ++i)
                    if (phase(candidates[i]) == p) group.push_back(i);
                const size_t k = std::min(quota, group.size());
                if (k < group.size()) std::nth_element(group.begin(), group.begin() + k, group.end(), better);
                for (size_t i = 0; i < k; ++i) {
                    chosen.push_back(group[i]); flags[group[i]] = 1;
                }
            }
        }
        if (keep < order.size()) {
            std::nth_element(order.begin(), order.begin() + keep, order.end(), better);
            order.resize(keep);
        }
        std::sort(order.begin(), order.end(), better);
        for (auto i : order) if (!flags[i] && chosen.size() < keep) chosen.push_back(i);
        next.clear();
        for (auto i : chosen) {
            Node n = candidates[i];
            links.push_back({n.path, n.action}); n.path = uint32_t(links.size() - 1);
            next.push_back(n);
        }
        current.swap(next);
    }
    ctx.result.completedWidth = int(width);
    return !pruned;
}

void repair(Context &ctx, const Config &cfg, size_t width) {
    if (!ctx.result.victory) return;
    for (int tail : {3, 5, 8, 11}) {
        if (Clock::now() >= ctx.deadline) return;
        const size_t cut = ctx.bestSuffix.size() > size_t(tail) ? ctx.bestSuffix.size() - tail : 0;
        const std::vector<int32_t> anchor(ctx.bestSuffix.begin(), ctx.bestSuffix.begin() + cut);
        beam(ctx, cfg, std::min<size_t>(width, 2048), anchor);
    }
}
} // namespace

bool BilyoumaSearch::SamePlayer(const Player &a, const Player &b) { return fields(a) == fields(b); }
bool BilyoumaSearch::SameState(const BilyoumaState &a, const BilyoumaState &b) {
    return a.rngPosition == b.rngPosition && a.nowState == b.nowState
        && a.resultPosition == b.resultPosition
        && SamePlayer(a.players[0], b.players[0]) && SamePlayer(a.players[1], b.players[1]);
}

bool BilyoumaSearch::Legal(const Player &p, int a) {
    // Verbatim ACTION_TABLE predicates, including its existing antidote test.
    switch (a) {
        case BattleEmulator::SPECIAL_MEDICINE: return p.SpecialMedicineCount > 0;
        case BattleEmulator::SPECIAL_ANTIDOTE: return p.SpecialMedicineCount >= 1 && p.PoisonEnable;
        case BattleEmulator::FLEE_ALLY:
        case BattleEmulator::DRAGON_SLASH:
        case BattleEmulator::ATTACK_ALLY:
        case BattleEmulator::DEFENCE: return true;
        case BattleEmulator::HEAL: return p.mp >= 2;
        case BattleEmulator::CRACK_ALLY:
        case BattleEmulator::WOOSH_ALLY: return p.mp >= 3;
        case BattleEmulator::ACROBATIC_STAR: return p.specialCharge && p.specialChargeTurn != 0;
        default: return false;
    }
}

bool BilyoumaSearch::Safe(const Player &p, int a) {
    // FLEE remains in the menu. Never exploit its known pre-action skip bug.
    // Sleeping is automatically normalized by Main before FLEE handling;
    // excluding it too is conservative and matches baseline's forced attack.
    return a != BattleEmulator::FLEE_ALLY || (!p.paralysis && !p.sleeping);
}

bool BilyoumaSearch::Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, BilyoumaState &state, BattleResult &log) {
    if (length < 0 || length >= 350) return false;
    state = BilyoumaState{};
    state.players[0] = initial[0]; state.players[1] = initial[1];
    log.clear();
    lcg::init(seed, true);
    if (length && initial[0].hp > 0 && initial[1].hp > 0)
        BattleEmulator::Main(&state.rngPosition, length, actions, state.players,
            &log, seed, nullptr, nullptr, -1, &state.nowState);
    state.resultPosition = log.position;
    return true;
}

const char *BilyoumaSearch::VariantName(int v) {
    return Configs[v >= 0 && v < VariantCount() ? v : 0].name;
}
int BilyoumaSearch::VariantCount() { return int(std::size(Configs)); }

BilyoumaResult BilyoumaSearch::Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength, int budgetMs, int variant, int maxTotalTurns) {
    const auto start = Clock::now();
    const auto budget = std::chrono::microseconds(int64_t(std::max(0, budgetMs)) * 1000);
    const auto reserve = std::min(std::chrono::microseconds(30000), budget / 10);
    Context ctx{initial, seed, std::min(60, std::max(0, maxTotalTurns)), start, start + budget - reserve};
    ctx.result.actions.fill(-1);
    if (!initial || !prefix || prefixLength < 0 || prefixLength >= 350 || seed == 0) return ctx.result;
    ctx.result.prefixLength = ctx.result.length = prefixLength;
    std::copy_n(prefix, prefixLength, ctx.result.actions.begin());
    Replay(initial, seed, ctx.result.actions.data(), prefixLength, ctx.result.root, ctx.result.replay);
    ctx.result.finalState = ctx.result.root;
    if (ctx.result.root.players[1].hp == 0) {
        ctx.result.victory = ctx.result.replayVerified = true;
        ctx.result.firstVictoryMs = ctx.result.bestVictoryMs = 0;
    } else if (ctx.result.root.players[0].hp > 0 && prefixLength < ctx.maxTotalTurns && budgetMs > 0) {
        const auto &cfg = Configs[variant >= 0 && variant < VariantCount() ? variant : 0];
        for (size_t width = cfg.firstWidth; width <= 16384 && Clock::now() < ctx.deadline; width *= 4) {
            if (beam(ctx, cfg, width)) break;
            if (cfg.repair) repair(ctx, cfg, width);
            if (cfg.portfolio && Clock::now() < ctx.deadline)
                if (beam(ctx, Configs[1], width)) break;
        }
    }
    ctx.result.elapsedMs = ctx.elapsed();
    return ctx.result;
}
