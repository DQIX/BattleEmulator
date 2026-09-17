#include "ZilyadamaSearch.h"
#include "lcg.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <limits>
#include <numeric>
#include <tuple>
#include <vector>

// Compact parent links, exact collision checks and preview ordering were
// informed by the supplied reokonn-GPT6-Pro/bilyouma-gpt6-pro sources.
// All transitions, including previews, use THIS branch's unmodified emulator.
namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoPath = UINT32_MAX;
constexpr int MaxActions = 349; // Leave the original 350-entry terminator.
constexpr int Actions[] = {
    BattleEmulator::ATTACK_ALLY, BattleEmulator::DRAGON_SLASH,
    BattleEmulator::DEFENCE, BattleEmulator::FLEE_ALLY,
    BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::HEAL,
    BattleEmulator::CRACK_ALLY, BattleEmulator::WOOSH_ALLY,
    BattleEmulator::CRACKLE, BattleEmulator::ACROBATIC_STAR
};

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

uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

uint64_t fingerprint(const ZilyadamaState &s) {
    uint64_t h = mix(s.nowState) ^ mix(uint32_t(s.rngPosition));
    // No padding bytes and no lossy state-identity decision. These inexpensive
    // fields locate a bucket; every Player field is compared on a hash match.
    for (const auto &p : s.players) {
        for (int v : {p.hp, p.mp, p.def, p.BuffLevel, p.BuffTurns,
                      p.specialChargeTurn, p.acrobaticStarTurn,
                      p.SpecialMedicineCount, int(p.rage), int(p.specialCharge),
                      int(p.acrobaticStar), int(p.dirtySpecialCharge)})
            h = (h ^ uint32_t(v)) * 0xd6e8feb86659fd93ULL;
    }
    return mix(h ^ uint32_t(s.resultPosition));
}

struct Config {
    const char *name;
    int hp, danger, mp, medicine, star, charge, preview, blend;
    int block = 1;
};
// These affect ordering only, never damage, RNG, action legality or victory.
constexpr Config Configs[] = {
    {"damage-beam",           8, 1,  8, 25,  150,  50,  0, 0},
    {"critical-preview",      8, 1,  8, 25,  150,  50, 75, 0},
    {"balanced-preview",     24, 4, 15, 50,  300, 100, 75, 0},
    {"acrobat-preview",      16, 2, 10, 30, 2500, 800, 75, 0},
    {"dual-critical-preview", 8, 1,  8, 25,  150,  50, 75, 8},
    {"strong-preview",        8, 1,  8, 25,  150,  50,125, 0},
    {"two-turn-beam",         8, 1,  8, 25,  150,  50,  0, 0, 2},
    {"two-turn-preview",      8, 1,  8, 25,  150,  50, 75, 0, 2}
};

int64_t value(const ZilyadamaState &s, const Config &cfg) {
    const auto &p = s.players[0];
    const int danger = std::max(0, p.maxHp / 3 - p.hp);
    int64_t score = -int64_t(s.players[1].hp) * 100;
    score += int64_t(p.hp) * cfg.hp + int64_t(std::max(0, p.mp)) * cfg.mp;
    score += int64_t(p.SpecialMedicineCount) * cfg.medicine;
    score -= int64_t(danger) * danger * cfg.danger;
    if (p.acrobaticStar) score += cfg.star;
    if (p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn > 0)
        score += cfg.charge;
    if (p.sleeping || p.paralysis) score -= 1500;
    if (p.PoisonEnable) score -= 400;
    return score;
}

struct Link { uint32_t parent; int32_t action; };
struct Node {
    ZilyadamaState state{};
    uint64_t hash = 0;
    int64_t score = 0;
    uint32_t path = NoPath;
    int32_t action = 0;
};

struct Context {
    const Player *initial;
    uint64_t seed;
    Clock::time_point start, deadline;
    ZilyadamaSearchResult result{};
    BattleResult transitionLog{}, verificationLog{};
    std::array<int32_t, 350> scratch{};
    int bestPosition = std::numeric_limits<int>::max();

    double elapsed() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    bool expired() {
        if (Clock::now() < deadline) return false;
        result.deadlineReached = true;
        return true;
    }
    void step(ZilyadamaState &s, int action) {
        // This emulator indexes Gene by the absolute turn stored in nowState.
        // Passing a one-element suffix (as in another branch) is NOT correct.
        const int turn = int((s.nowState >> 12) & 0xfffff);
        scratch[turn] = action;
        transitionLog.clear();
        BattleEmulator::Main(&s.rngPosition, 1, scratch.data(), s.players,
            &transitionLog, seed, nullptr, nullptr, -1, &s.nowState);
        s.resultPosition += transitionLog.position;
        ++result.transitions;
    }
    void consider(const Node &n, const std::vector<Link> &links,
                  const ZilyadamaState *preview = nullptr) {
        const auto &state = preview ? *preview : n.state;
        if (state.players[1].hp != 0 || state.resultPosition >= bestPosition) return;
        std::vector<int32_t> suffix{n.action};
        for (auto i = n.path; i != NoPath; i = links[i].parent)
            suffix.push_back(links[i].action);
        std::reverse(suffix.begin(), suffix.end());
        if (preview) suffix.push_back(BattleEmulator::ATTACK_ALLY);
        const int length = result.prefixLength + int(suffix.size());
        if (length > MaxActions) return;
        auto actions = result.actions;
        std::copy(suffix.begin(), suffix.end(), actions.begin() + result.prefixLength);
        std::fill(actions.begin() + length, actions.end(), -1);
        ZilyadamaState exact;
        if (!ZilyadamaSearch::Replay(initial, seed, actions.data(), length,
                                    exact, verificationLog) ||
            exact.players[1].hp != 0 || !ZilyadamaSearch::SameState(exact, state)) {
            ++result.rejectedReplay;
            return;
        }
        if (expired()) return;
        if (!result.victory) result.firstVictoryMs = elapsed();
        bestPosition = exact.resultPosition;
        result.victory = result.replayVerified = true;
        result.actions = actions;
        result.length = length;
        result.finalState = exact;
        result.replay = verificationLog;
        ++result.improvements;
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
    bool insert(const Node &node, const std::vector<Node> &nodes, uint64_t &duplicates) {
        size_t bucket = node.hash & mask;
        while (slots[bucket] != NoPath) {
            const auto &other = nodes[slots[bucket]];
            if (node.hash == other.hash && ZilyadamaSearch::SameState(node.state, other.state)) {
                ++duplicates;
                return false;
            }
            bucket = (bucket + 1) & mask;
        }
        slots[bucket] = uint32_t(nodes.size());
        return true;
    }
};

bool beam(Context &ctx, const Config &cfg, size_t width) {
    std::vector<Node> current, next, candidates;
    std::vector<Link> links;
    std::vector<uint32_t> order, chosen, safeOrder;
    std::vector<uint8_t> chosenFlag;
    StateSet seen;
    current.reserve(width); next.reserve(width); candidates.reserve(width * std::size(Actions));
    links.reserve(width * 40);
    Node root; root.state = ctx.result.root;
    current.push_back(root);
    bool pruned = false;
    for (int turn = ctx.result.prefixLength; turn < MaxActions && !current.empty(); ++turn) {
        if (ctx.expired()) return false;
        candidates.clear();
        seen.reset(current.size() * std::size(Actions));
        for (const auto &parent : current) {
            // BattleResult.position is monotone; RNG position is not the cost.
            if (parent.state.resultPosition + 1 >= ctx.bestPosition) continue;
            for (int action : Actions) {
                if (!ZilyadamaSearch::Legal(parent.state.players[0], action)) continue;
                if (!ZilyadamaSearch::Safe(parent.state.players[0], action)) {
                    ++ctx.result.unsafeFleeSkipped; continue;
                }
                if ((ctx.result.transitions & 127) == 0 && ctx.expired()) return false;
                Node n; n.state = parent.state; n.path = parent.path; n.action = action;
                ctx.step(n.state, action);
                if (n.state.players[1].hp == 0) { ctx.consider(n, links); continue; }
                if (n.state.players[0].hp == 0 || n.state.resultPosition + 1 >= ctx.bestPosition) continue;
                n.hash = fingerprint(n.state);
                if (!seen.insert(n, candidates, ctx.result.duplicates)) continue;
                n.score = value(n.state, cfg);
                if (cfg.preview && turn + 1 < MaxActions) {
                    auto forecast = n.state;
                    ctx.step(forecast, BattleEmulator::ATTACK_ALLY);
                    if (forecast.players[1].hp == 0) ctx.consider(n, links, &forecast);
                    // An observed future critical is rewarded, not fabricated.
                    n.score += int64_t(n.state.players[1].hp - forecast.players[1].hp) * cfg.preview;
                    if (forecast.players[0].hp == 0) n.score -= cfg.hp * 12;
                }
                candidates.push_back(n);
            }
        }
        // On the first half of a two-turn block keep every legal child. This
        // lets a healing/defence/RNG setup reach its payoff before ranking it.
        const bool openBlock = cfg.block == 2 && (turn - ctx.result.prefixLength) % 2 == 0;
        const size_t keep = std::min(openBlock ? width * std::size(Actions) : width, candidates.size());
        if (!keep) break;
        pruned |= keep < candidates.size();
        order.resize(candidates.size());
        std::iota(order.begin(), order.end(), 0);
        const auto better = [&](uint32_t a, uint32_t b) {
            const auto &x = candidates[a]; const auto &y = candidates[b];
            if (x.score != y.score) return x.score > y.score;
            if (x.state.resultPosition != y.state.resultPosition)
                return x.state.resultPosition < y.state.resultPosition;
            if (x.hash != y.hash) return x.hash < y.hash;
            return a < b; // Total order, including genuine hash collisions.
        };
        chosen.clear(); chosenFlag.assign(candidates.size(), 0);
        if (cfg.blend && keep < candidates.size()) {
            safeOrder = order;
            const size_t safeCount = keep / cfg.blend;
            const auto safer = [&](uint32_t a, uint32_t b) {
                const auto va = value(candidates[a].state, Configs[2]);
                const auto vb = value(candidates[b].state, Configs[2]);
                return va != vb ? va > vb : better(a, b);
            };
            std::partial_sort(safeOrder.begin(), safeOrder.begin() + safeCount, safeOrder.end(), safer);
            for (size_t i = 0; i < safeCount; ++i) {
                chosen.push_back(safeOrder[i]); chosenFlag[safeOrder[i]] = 1;
            }
        }
        if (keep < order.size()) {
            std::nth_element(order.begin(), order.begin() + keep, order.end(), better);
            order.resize(keep);
        }
        std::sort(order.begin(), order.end(), better);
        for (auto i : order) if (!chosenFlag[i] && chosen.size() < keep) chosen.push_back(i);
        if (ctx.expired()) return false;
        next.clear();
        for (auto i : chosen) {
            Node n = candidates[i];
            links.push_back({n.path, n.action});
            n.path = uint32_t(links.size() - 1);
            next.push_back(n);
        }
        current.swap(next);
    }
    ctx.result.completedWidth = int(width);
    return !pruned;
}
}

bool ZilyadamaSearch::SamePlayer(const Player &a, const Player &b) { return fields(a) == fields(b); }
bool ZilyadamaSearch::SameState(const ZilyadamaState &a, const ZilyadamaState &b) {
    return a.nowState == b.nowState && a.rngPosition == b.rngPosition &&
        a.resultPosition == b.resultPosition && SamePlayer(a.players[0], b.players[0]) &&
        SamePlayer(a.players[1], b.players[1]);
}

bool ZilyadamaSearch::Legal(const Player &p, int action) {
    // Exact action set and runtime conditions from ActionOptimizer::RunAlgorithm.
    switch (action) {
        case BattleEmulator::ATTACK_ALLY:
        case BattleEmulator::DRAGON_SLASH:
        case BattleEmulator::DEFENCE:
        case BattleEmulator::FLEE_ALLY: return true;
        case BattleEmulator::SPECIAL_MEDICINE: return p.SpecialMedicineCount >= 1;
        case BattleEmulator::HEAL: return p.mp >= 2;
        case BattleEmulator::CRACK_ALLY:
        case BattleEmulator::WOOSH_ALLY: return p.mp >= 3;
        case BattleEmulator::CRACKLE: return p.mp >= 8;
        case BattleEmulator::ACROBATIC_STAR: return p.specialCharge && p.specialChargeTurn != 0;
        default: return false;
    }
}
bool ZilyadamaSearch::Safe(const Player &p, int action) {
    // Do not exploit the known FLEE pre-action skip; FLEE remains in the legal set.
    return action != BattleEmulator::FLEE_ALLY || (!p.paralysis && !p.sleeping);
}

bool ZilyadamaSearch::Replay(const Player initial[2], uint64_t seed,
    const int32_t actions[350], int length, ZilyadamaState &state, BattleResult &log) {
    if (length < 0 || length > MaxActions) return false;
    state = ZilyadamaState{};
    state.players[0] = initial[0]; state.players[1] = initial[1];
    log.clear();
    lcg::init(seed, true);
    if (length && state.players[0].hp != 0 && state.players[1].hp != 0)
        BattleEmulator::Main(&state.rngPosition, length, actions, state.players,
            &log, seed, nullptr, nullptr, -1, &state.nowState);
    state.resultPosition = log.position;
    return true;
}

int ZilyadamaSearch::VariantCount() { return int(std::size(Configs)); }
const char *ZilyadamaSearch::VariantName(int v) {
    return v >= 0 && v < VariantCount() ? Configs[v].name : "invalid";
}

ZilyadamaSearchResult ZilyadamaSearch::Run(const Player initial[2], uint64_t seed,
    const int32_t prefix[350], const ZilyadamaSearchOptions &options) {
    const auto start = Clock::now();
    // Reserve time for unwinding buffers, output construction and final replay.
    const auto budget = std::chrono::milliseconds(std::max(0, options.budgetMs));
    const auto reserve = std::min(budget / 10, std::chrono::milliseconds(75));
    Context ctx{initial, seed, start, start + budget - reserve};
    ctx.result.actions.fill(-1);
    ctx.result.variant = options.variant;
    int length = 0;
    while (length < 350 && prefix[length] != 0 && prefix[length] != -1) ++length;
    if (length > MaxActions || options.variant < 0 || options.variant >= VariantCount()) {
        ctx.result.elapsedMs = ctx.elapsed(); return ctx.result;
    }
    ctx.result.inputValid = true;
    ctx.result.prefixLength = ctx.result.length = length;
    std::copy(prefix, prefix + length, ctx.result.actions.begin());
    Replay(initial, seed, ctx.result.actions.data(), length, ctx.result.root, ctx.result.replay);
    ctx.result.finalState = ctx.result.root;
    if (ctx.result.root.players[1].hp == 0) {
        ctx.result.victory = ctx.result.replayVerified = true;
        ctx.result.firstVictoryMs = ctx.elapsed();
    } else if (ctx.result.root.players[0].hp > 0 && length < MaxActions && options.budgetMs > 0) {
        const auto &cfg = Configs[options.variant];
        const size_t limit = size_t(std::max(1, options.maxWidth));
        for (size_t width = std::min<size_t>(64, limit); !ctx.expired();) {
            if (beam(ctx, cfg, width) || width == limit) break;
            width = std::min(width * 4, limit);
        }
    }
    ctx.result.elapsedMs = ctx.elapsed();
    return ctx.result;
}