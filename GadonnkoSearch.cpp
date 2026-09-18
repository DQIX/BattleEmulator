#include "GadonnkoSearch.h"
#include "lcg.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <tuple>
#include <vector>

// Reused techniques, not donor worlds: Reokonn arena paths/replay admission;
// Bilyouma anytime widening/repair; Erugiosu equipment lanes/verified flat TT.
namespace {
using B = BattleEmulator;
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoPath = UINT32_MAX;
// All eight actions from this branch's ACTION_TABLE, plus the basic attack
// already available through InputBuilder/Main. No donor skill ownership.
constexpr int Actions[] = {B::PSYCHE_UP_ALLY, B::MULTITHRUST, B::DOUBLE_UP,
    B::ATTACK_ALLY, B::MIDHEAL, B::SPECIAL_MEDICINE, B::BUFF,
    B::DEFENCE, B::FLEE_ALLY};
// Existing lcg.cpp has 5000 entries, BattleResult has 400 events. Leave more
// than the maximum single-turn consumption before entering the unchanged Main.
constexpr int LastSafeRngStart = 5000 - 256;
constexpr int EventCapacity = 400;

auto fields(const Player &p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis, p.paralysisLevel,
        p.paralysisTurns, p.SpecialMedicineCount, p.defence, p.sleeping,
        p.sleepingTurn, p.BuffLevel, p.BuffTurns, p.hasMagicMirror,
        p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn, p.TensionLevel,
        p.rage, p.SageElixirCount, p.ElfinElixirCount, p.MagicWaterCount,
        p.speedTurn, p.speedLevel, p.PoisonTurn, p.PoisonEnable,
        p.SpecialAntidoteCount, p.acrobaticStar, p.acrobaticStarTurn,
        p.rageTurns, p.isStunned);
}

uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
uint64_t fingerprint(const GadonnkoState &s) {
    const auto &p = s.players[0];
    uint64_t h = mix(s.nowState) ^ mix(uint32_t(s.rngPosition));
    h ^= mix(uint64_t(uint32_t(p.hp)) << 32 | uint32_t(s.players[1].hp));
    h ^= mix(uint64_t(uint32_t(p.defaultATK)) << 32 | uint32_t(p.atk));
    h ^= mix(uint64_t(uint32_t(p.mp)) << 32 | uint32_t(p.def));
    h ^= mix(uint64_t(uint32_t(p.TensionLevel)) << 32 | uint32_t(p.BuffTurns));
    h ^= mix(uint64_t(uint32_t(p.AtkBuffTurn)) << 32 | uint32_t(p.paralysisTurns));
    return mix(h ^ uint32_t(s.resultPosition));
}
int turnOf(const GadonnkoState &s) { return int((s.nowState >> 12) & 0xfffff); }
int equipmentBit(const Player &p) {
    return p.defaultATK == B::BARE_HANDS_ATK ? B::ACTION_BARE_HANDS : 0;
}
bool disabled(const Player &p) { return p.paralysis || p.sleeping || p.isStunned; }
bool encoding(int a) {
    return a > 0 && (a & ~(B::ACTION_ID_MASK | B::ACTION_BARE_HANDS)) == 0;
}
bool safeTransition(const GadonnkoState &before, const GadonnkoState &after,
                    int action, const BattleResult &log) {
    if ((action & B::ACTION_ID_MASK) != B::FLEE_ALLY) return true;
    if (disabled(before.players[0])) return false;
    // Main's pre-action FLEE skip must not bypass an enemy-first paralysis or
    // Heart Breaker stun inflicted in this very turn. Do not modify the world.
    for (int i = 0; i < log.position; ++i)
        if (!log.isEnemy[i] && (log.actions[i] & B::ACTION_ID_MASK) == B::FLEE_ALLY)
            return log.initiative[i] || !disabled(after.players[0]);
    return true;
}

// Pure ordering parameters. Damage, HP, MP, RNG and statuses are never altered.
struct Config {
    double tension, attackBuff, hp, danger, defence, resource;
    int quota;
    double preview;
};
constexpr Config Configs[] = {
    {.95, .42, .28, .012, .13, .35, 4, 0}, // burst
    {.80, .34, .65, .035, .24, .60, 4, 0}, // survival
    {1.08,.30, .12, .005, .06, .15, 4, 0}, // offensive
    {.72, .30, .25, .012, .12, .30, 4, .65}, // exact attack preview
    {.95, .42, .28, .012, .13, .35, 0, 0}  // no phase quotas (benchmark)
};
constexpr const char *VariantNames[] = {
    "equipment-portfolio-repair", "zero-change-portfolio",
    "burst-equipment", "survival-equipment", "offensive-equipment",
    "exact-preview-equipment", "no-quota-equipment"
};

double value(const GadonnkoState &s, const Config &cfg, bool equipment) {
    const auto &p = s.players[0];
    const auto &e = s.players[1];
    constexpr double tension[] = {1, 1.5, 2.5, 4, 6};
    constexpr double attackBuff[] = {.5, .75, 1, 1.25, 1.5};
    double attack = p.atk;
    if (equipment) attack = std::max(attack, B::EQUIPPED_ATK *
        attackBuff[std::clamp(p.AtkBuffLevel + 2, 0, 4)]);
    const int level = std::clamp(p.TensionLevel, 0, 4);
    const bool thrust = p.mp >= 4 && (equipment || !equipmentBit(p));
    // Approximate damage credit is used only to order exact emulator children.
    const double hit = std::max(1., (attack - e.def * .5) * .5);
    const double ordinary = hit * (thrust ? 1.75 : 1.) * 1.1 * .92;
    const double burst = ordinary * tension[level];
    const double credit = std::max(0., std::min(double(e.hp), burst) -
                                           std::min(double(e.hp), ordinary));
    double v = -double(e.hp) + cfg.tension * credit + p.hp * cfg.hp;
    const double danger = std::max(0., std::min(p.maxHp * .45, e.atk * .60) - p.hp);
    v -= danger * danger * cfg.danger;
    v += std::max(0, p.mp) * cfg.resource + p.SpecialMedicineCount * 8;
    v += std::max(0, p.AtkBuffLevel) * B::EQUIPPED_ATK * cfg.attackBuff *
        std::clamp(p.AtkBuffTurn + 1, 0, 4) / 4.;
    v += p.BuffLevel * p.defaultDEF * cfg.defence *
        std::clamp(p.BuffTurns + 1, 0, 4) / 4.;
    if (level == 4) v += p.maxHp * .16;
    if (p.paralysis) v -= p.maxHp * (.40 + .07 * std::max(0, p.paralysisTurns));
    if (p.sleeping) v -= p.maxHp * (.35 + .06 * std::max(0, p.sleepingTurn));
    if (p.isStunned) v -= p.maxHp * .30;
    return v;
}

struct Link { uint32_t parent; int32_t action; }; // Preserve equipment bit 16.
struct Node {
    GadonnkoState state{};
    uint64_t hash = 0;
    double score = 0;
    uint32_t path = NoPath;
    int32_t action = 0;
};
std::vector<int32_t> materialize(const Node &n, const std::vector<Link> &links) {
    std::vector<int32_t> out{n.action};
    for (auto p = n.path; p != NoPath; p = links[p].parent) out.push_back(links[p].action);
    std::reverse(out.begin(), out.end());
    return out;
}

struct Context {
    const Player *initial;
    uint64_t seed;
    int maxTurns;
    Clock::time_point start, deadline, passDeadline;
    GadonnkoResult result{};
    std::vector<int32_t> bestSuffix;
    std::array<int32_t, 350> scratch{};
    BattleResult transitionLog{}, verificationLog{};
    int bestPosition = INT32_MAX;
    int bestChanges = INT32_MAX;

    double elapsed() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    bool expired() const { return Clock::now() >= passDeadline; }
    bool step(GadonnkoState &s, int action) {
        const int turn = turnOf(s);
        if (turn >= 349 || s.rngPosition > LastSafeRngStart ||
            s.resultPosition + 2 > EventCapacity) return false;
        const auto before = s;
        scratch[turn] = action;
        transitionLog.clear();
        B::Main(&s.rngPosition, 1, scratch.data(), s.players, &transitionLog,
                seed, nullptr, nullptr, -1, &s.nowState);
        s.resultPosition += transitionLog.position;
        const bool changed = before.players[0].defaultATK != s.players[0].defaultATK;
        s.equipmentChanges += changed;
        ++result.expanded;
        ++result.actionExpansions[action & B::ACTION_ID_MASK];
        result.equipmentBranches += changed;
        if (!safeTransition(before, s, action, transitionLog)) {
            ++result.unsafeFleeSkipped;
            return false;
        }
        return true;
    }
    void consider(const Node &n, const std::vector<Link> &links) {
        if (n.state.players[1].hp != 0 || expired()) return;
        const bool better = n.state.resultPosition < bestPosition ||
            (n.state.resultPosition == bestPosition && n.state.equipmentChanges < bestChanges);
        const bool zero = n.state.equipmentChanges == result.root.equipmentChanges;
        const bool betterZero = zero && (result.zeroChangePosition < 0 ||
                                        n.state.resultPosition < result.zeroChangePosition);
        if (!better && !betterZero) return;
        const auto suffix = materialize(n, links);
        const int length = result.prefixLength + int(suffix.size());
        auto actions = result.actions;
        std::copy(suffix.begin(), suffix.end(), actions.begin() + result.prefixLength);
        std::fill(actions.begin() + length, actions.end(), -1);
        GadonnkoState exact;
        if (!GadonnkoSearch::Replay(initial, seed, actions.data(), length, exact,
                verificationLog, result.prefixLength) || !GadonnkoSearch::SameState(exact, n.state)) {
            ++result.rejectedReplay;
            return;
        }
        if (Clock::now() >= deadline) return;
        if (betterZero) {
            result.zeroChangePosition = exact.resultPosition;
            result.zeroChangeLength = length;
        }
        if (!better) return;
        if (!result.victory) result.firstVictoryMs = elapsed();
        bestPosition = exact.resultPosition;
        bestChanges = exact.equipmentChanges;
        bestSuffix = suffix;
        result.victory = result.replayVerified = true;
        result.actions = actions;
        result.length = length;
        result.finalState = exact;
        result.replay = verificationLog;
        result.bestVictoryMs = elapsed();
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
    bool insert(const Node &n, std::vector<Node> &nodes, GadonnkoResult &stats) {
        size_t i = n.hash & mask;
        while (slots[i] != NoPath) {
            auto &old = nodes[slots[i]];
            if (n.hash == old.hash) {
                // Hash equality alone NEVER merges states; all Player fields,
                // the complete NowState, RNG cursor and exact event count agree.
                if (GadonnkoSearch::SameFuture(n.state, old.state)) {
                    ++stats.duplicates;
                    if (n.state.equipmentChanges < old.state.equipmentChanges) old = n;
                    return false;
                }
                ++stats.hashCollisions;
            }
            i = (i + 1) & mask;
        }
        slots[i] = uint32_t(nodes.size());
        return true;
    }
};
int phase(const Node &n) {
    const auto &p = n.state.players[0];
    if (disabled(p)) return 10;
    return std::clamp(p.TensionLevel, 0, 4) * 2 + int(p.AtkBuffLevel > 0);
}

bool beam(Context &ctx, const Config &cfg, size_t width, bool equipment,
          const std::vector<int32_t> &anchor = {}) {
    std::vector<Node> current, next, candidates;
    std::vector<Link> links;
    std::vector<uint32_t> order, chosen, group;
    std::vector<uint8_t> selected;
    StateSet seen;
    const size_t branching = std::size(Actions) * (equipment ? 2 : 1);
    current.reserve(width); next.reserve(width); candidates.reserve(width * branching);
    links.reserve(width * size_t(std::max(1, ctx.maxTurns - ctx.result.prefixLength)));
    Node root; root.state = ctx.result.root;
    for (int action : anchor) {
        if (ctx.expired() || !GadonnkoSearch::Legal(root.state.players[0], action) ||
            (!equipment && (action & B::ACTION_BARE_HANDS) != equipmentBit(root.state.players[0])) ||
            !ctx.step(root.state, action)) return false;
        links.push_back({root.path, action}); root.path = uint32_t(links.size() - 1);
    }
    current.push_back(root);
    bool pruned = false;
    for (int turn = turnOf(root.state); turn < ctx.maxTurns && !current.empty(); ++turn) {
        if (ctx.expired()) return false;
        candidates.clear(); seen.reset(current.size() * branching);
        for (const auto &parent : current) {
            if (parent.state.resultPosition + 1 > ctx.bestPosition) continue;
            const auto &p = parent.state.players[0];
            const int keepBit = equipmentBit(p);
            const int choices = equipment && !disabled(p) ? 2 : 1;
            for (int action : Actions) {
                for (int choice = 0; choice < choices; ++choice) {
                    const int encoded = action | (keepBit ^ (choice ? B::ACTION_BARE_HANDS : 0));
                    if (!GadonnkoSearch::Legal(p, encoded)) continue;
                    if ((ctx.result.expanded & 127) == 0 && ctx.expired()) return false;
                    Node n; n.state = parent.state; n.path = parent.path; n.action = encoded;
                    if (!ctx.step(n.state, encoded)) continue;
                    if (n.state.resultPosition > ctx.bestPosition) continue;
                    if (n.state.players[1].hp == 0) { ctx.consider(n, links); continue; }
                    if (n.state.players[0].hp <= 0 || n.state.resultPosition + 1 > ctx.bestPosition) continue;
                    n.hash = fingerprint(n.state);
                    n.score = value(n.state, cfg, equipment);
                    if (cfg.preview && turn + 1 < ctx.maxTurns && !ctx.expired()) {
                        auto forecast = n.state;
                        int preview = n.state.players[0].mp >= 4 ? B::MULTITHRUST : B::ATTACK_ALLY;
                        preview |= equipment ? 0 : equipmentBit(n.state.players[0]);
                        if (GadonnkoSearch::Legal(n.state.players[0], preview) && ctx.step(forecast, preview)) {
                            n.score += cfg.preview * (n.state.players[1].hp - forecast.players[1].hp);
                            if (forecast.players[0].hp == 0) n.score -= n.state.players[0].maxHp * .5;
                            if (forecast.players[1].hp == 0) {
                                links.push_back({n.path, n.action});
                                Node win; win.state = forecast; win.action = preview;
                                win.path = uint32_t(links.size() - 1);
                                ctx.consider(win, links);
                            }
                        }
                    }
                    if (seen.insert(n, candidates, ctx.result)) candidates.push_back(n);
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
            if (x.state.equipmentChanges != y.state.equipmentChanges)
                return x.state.equipmentChanges < y.state.equipmentChanges;
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
    ctx.result.completedWidth = int(width);
    return !pruned;
}
void repair(Context &ctx, const Config &cfg, size_t width, bool equipment, int tail) {
    if (!ctx.result.victory || ctx.bestSuffix.empty()) return;
    const size_t cut = ctx.bestSuffix.size() > size_t(tail) ? ctx.bestSuffix.size() - tail : 0;
    const std::vector<int32_t> anchor(ctx.bestSuffix.begin(), ctx.bestSuffix.begin() + cut);
    beam(ctx, cfg, width, equipment, anchor);
}
} // namespace

bool GadonnkoSearch::SamePlayer(const Player &a, const Player &b) { return fields(a) == fields(b); }
bool GadonnkoSearch::SameFuture(const GadonnkoState &a, const GadonnkoState &b) {
    return a.rngPosition == b.rngPosition && a.nowState == b.nowState &&
        a.resultPosition == b.resultPosition && SamePlayer(a.players[0], b.players[0]) &&
        SamePlayer(a.players[1], b.players[1]);
}
bool GadonnkoSearch::SameState(const GadonnkoState &a, const GadonnkoState &b) {
    return SameFuture(a, b) && a.equipmentChanges == b.equipmentChanges;
}
bool GadonnkoSearch::Legal(const Player &p, int action) {
    if (!encoding(action)) return false;
    const int id = action & B::ACTION_ID_MASK;
    if (disabled(p))
        return id == B::ATTACK_ALLY && (action & B::ACTION_BARE_HANDS) == equipmentBit(p);
    switch (id) {
        case B::ATTACK_ALLY: case B::DEFENCE: case B::DOUBLE_UP:
        case B::PSYCHE_UP_ALLY: case B::FLEE_ALLY: return true;
        case B::BUFF: return p.mp >= 3;
        case B::MIDHEAL: return p.mp >= 4;
        // Spear skill cannot be selected after unequipping the spear.
        case B::MULTITHRUST: return p.mp >= 4 && !(action & B::ACTION_BARE_HANDS);
        case B::SPECIAL_MEDICINE: return p.SpecialMedicineCount > 0;
        default: return false;
    }
}
bool GadonnkoSearch::Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, GadonnkoState &state, BattleResult &log,
        int validateFrom) {
    if (!initial || !actions || !seed || length < 0 || length >= 350 ||
        validateFrom < 0 || validateFrom > length) return false;
    GadonnkoState sequential{};
    sequential.players[0] = initial[0]; sequential.players[1] = initial[1];
    lcg::init(seed, true);
    BattleResult one;
    for (int turn = 0; turn < length; ++turn) {
        if (sequential.players[0].hp <= 0 || sequential.players[1].hp <= 0 ||
            sequential.rngPosition > LastSafeRngStart ||
            sequential.resultPosition + 2 > EventCapacity || !encoding(actions[turn])) return false;
        if (turn >= validateFrom && !Legal(sequential.players[0], actions[turn])) return false;
        const auto before = sequential;
        one.clear();
        B::Main(&sequential.rngPosition, 1, actions, sequential.players, &one,
                seed, nullptr, nullptr, -1, &sequential.nowState);
        sequential.resultPosition += one.position;
        sequential.equipmentChanges += before.players[0].defaultATK != sequential.players[0].defaultATK;
        if (turn >= validateFrom && !safeTransition(before, sequential, actions[turn], one)) return false;
    }
    state = GadonnkoState{};
    state.players[0] = initial[0]; state.players[1] = initial[1];
    log.clear(); lcg::init(seed, true);
    if (length) B::Main(&state.rngPosition, length, actions, state.players, &log,
                      seed, nullptr, nullptr, -1, &state.nowState);
    state.resultPosition = log.position;
    state.equipmentChanges = sequential.equipmentChanges;
    return SameState(state, sequential);
}
int GadonnkoSearch::VariantCount() { return int(std::size(VariantNames)); }
const char *GadonnkoSearch::VariantName(int v) {
    return VariantNames[v >= 0 && v < VariantCount() ? v : DefaultVariant];
}
GadonnkoResult GadonnkoSearch::Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength, int budgetMs, int variant) {
    const auto start = Clock::now();
    const auto budget = std::chrono::microseconds(int64_t(std::max(0, budgetMs)) * 1000);
    const auto reserve = std::min(std::chrono::microseconds(20000), budget / 20);
    Context ctx{initial, seed, std::min(349, std::max(0, prefixLength) + 40), start,
                start + budget - reserve, start + budget - reserve};
    ctx.result.actions.fill(-1); ctx.scratch.fill(-1);
    if (!initial || !prefix || !seed || prefixLength < 0 || prefixLength >= 350) {
        ctx.result.error = "Invalid initial world, seed, or prefix length";
    } else {
        ctx.result.prefixLength = ctx.result.length = prefixLength;
        std::copy_n(prefix, prefixLength, ctx.result.actions.begin());
        if (!Replay(initial, seed, ctx.result.actions.data(), prefixLength,
                    ctx.result.root, ctx.result.replay, prefixLength)) {
            ctx.result.error = "Prefix cannot be replayed within the supplied emulator limits";
        } else {
            ctx.result.inputValid = true;
            ctx.result.finalState = ctx.result.root;
            if (ctx.result.root.players[1].hp == 0) {
                ctx.result.victory = ctx.result.replayVerified = true;
                ctx.result.firstVictoryMs = ctx.result.bestVictoryMs = ctx.elapsed();
                ctx.result.zeroChangePosition = ctx.result.root.resultPosition;
                ctx.result.zeroChangeLength = prefixLength;
            } else if (ctx.result.root.players[0].hp > 0 && budgetMs > 0) {
                if (variant < 0 || variant >= VariantCount()) variant = DefaultVariant;
                const bool zeroOnly = variant == 1;
                const bool portfolio = variant <= 1;
                auto pass = [&](int config, size_t width, bool equipment, int tail = 0) {
                    const auto now = Clock::now();
                    if (now >= ctx.deadline) return;
                    ctx.passDeadline = std::min(ctx.deadline, now + std::chrono::milliseconds(180));
                    if (tail) repair(ctx, Configs[config], width, equipment, tail);
                    else beam(ctx, Configs[config], width, equipment);
                };
                pass(0, 48, false);
                if (!zeroOnly) pass(0, 48, true);
                for (int round = 0; Clock::now() < ctx.deadline; ++round) {
                    const size_t width = std::min<size_t>(8192, size_t(128) << std::min(round / 2, 6));
                    int cfg = portfolio ? round % 3 : variant - 2;
                    const bool equip = !zeroOnly && (round % 4 != 2);
                    pass(cfg, width, equip);
                    if (portfolio && ctx.result.victory && round % 2 == 1)
                        pass(cfg, std::min<size_t>(width, 2048), !zeroOnly, 3 + round % 5);
                }
            }
        }
    }
    ctx.result.elapsedMs = ctx.elapsed();
    return ctx.result;
}