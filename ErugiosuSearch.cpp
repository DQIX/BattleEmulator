#include "ErugiosuSearch.h"
#include "lcg.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <tuple>
#include <vector>

// Engine techniques: ReokonnSearch (arena paths, full replay admission),
// BilyoumaSearch (anytime widening and repair), ActionSearchOptimized/Enumerated
// (flat verified transpositions and quotas before global partial selection).
// Only techniques are adapted; no donor actions, seeds, answers or world data.
namespace {
using B = BattleEmulator;
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoPath = UINT32_MAX;
constexpr int Actions[] = {
    B::PSYCHE_UP_ALLY, B::MULTITHRUST, B::MAGIC_MIRROR, B::DOUBLE_UP,
    B::BUFF, B::ATTACK_ALLY, B::MORE_HEAL, B::SPECIAL_MEDICINE,
    B::MIDHEAL, B::FULLHEAL, B::DEFENCE, B::DEFENDING_CHAMPION,
    B::INSULATE, B::GOSPEL_SONG, B::HEAL, B::MEDICINAL_HERBS,
    B::SAGE_ELIXIR, B::MAGIC_WATER, B::ELFIN_ELIXIR, B::FLEE_ALLY
};

auto fields(const Player &p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis,
        p.paralysisLevel, p.paralysisTurns, p.SpecialMedicineCount, p.defence,
        p.sleeping, p.sleepingTurn, p.BuffLevel, p.BuffTurns,
        p.hasMagicMirror, p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn,
        p.TensionLevel, p.rage, p.SageElixirCount, p.ElfinElixirCount,
        p.MagicWaterCount, p.InsulateLevel, p.InsulateTurns);
}

uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
uint64_t fingerprint(const ErugiosuState &s) {
    const auto &p = s.players[0];
    // A deliberately cheap fingerprint, followed by ALL semantic field equality.
    // Equipment and derived attack explicitly participate; padding never does.
    uint64_t h = mix(s.nowState) ^ mix(uint32_t(s.rngPosition));
    h ^= mix(uint64_t(uint32_t(p.hp)) << 32 | uint32_t(s.players[1].hp));
    h ^= mix(uint64_t(uint32_t(p.defaultATK)) << 32 | uint32_t(p.atk));
    h ^= mix(uint64_t(uint32_t(p.mp)) << 32 | uint32_t(p.def));
    h ^= mix(uint64_t(uint32_t(p.TensionLevel)) << 32 | uint32_t(p.BuffTurns));
    h ^= mix(uint64_t(uint32_t(p.AtkBuffTurn)) << 32 | uint32_t(p.MagicMirrorTurn));
    return mix(h ^ uint32_t(s.resultPosition));
}

int turnOf(const ErugiosuState &s) { return int((s.nowState >> 12) & 0xfffff); }
int equipmentBit(const Player &p) { return p.defaultATK == 179 ? B::ACTION_BARE_HANDS : 0; }

bool encoding(int a) {
    return a > 0 && (a & ~(B::ACTION_ID_MASK | B::ACTION_BARE_HANDS)) == 0;
}
bool historicalAction(int a) {
    if (!encoding(a)) return false;
    const int id = a & B::ACTION_ID_MASK;
    for (int legal : Actions) if (id == legal) return true;
    // Existing logged status actions are normalized by Main, never generated.
    return id == B::PARALYSIS || id == B::CURE_PARALYSIS || id == B::SLEEPING ||
           id == B::CURE_SLEEPING || id == B::TURN_SKIPPED;
}

bool safeTransition(const ErugiosuState &before, const ErugiosuState &after,
                    int action, const BattleResult &log) {
    if ((action & B::ACTION_ID_MASK) != B::FLEE_ALLY) return true;
    if (before.players[0].paralysis || before.players[0].sleeping) return false;
    // Enemy-first Burning Breath can inflict paralysis in THIS turn. Main's
    // FLEE skip would bypass it even though the turn-start actor was healthy.
    for (int i = 0; i < log.position; ++i)
        if (!log.isEnemy[i] && (log.actions[i] & B::ACTION_ID_MASK) == B::FLEE_ALLY)
            return log.initiative[i] || !after.players[0].paralysis;
    return true;
}

// Value parameters are heuristic HP credits, not changes to the emulator.
struct Config {
    const char *name;
    double tension, attackBuff, mirror, hp, danger, defence, resource;
    int quota;
    int preview;
};
constexpr Config Configs[] = {
    {"reflection-tension", .86, 110, 220, .50, .035, 70, .65, 4, 0},
    {"burst",             .99, 100, 100, .22, .015, 35, .35, 4, 0},
    {"defensive-reflect",  .78,  90, 320, .85, .055,110, .90, 4, 0},
    {"donor-damage-value", .58,  70,  95, .35, .020, 40, .40, 0, 0},
    {"multi-value",       .90, 115, 250, .45, .030, 65, .60, 2, 0},
    {"attack-preview",    .75, 100, 180, .40, .025, 60, .50, 4, 1}
};
constexpr const char *VariantNames[] = {
    "hybrid-zero-plus-equipment",        // Default: lanes within ONE budget.
    "A-zero-change-portfolio",
    "B-equipment-balanced",
    "C-reflection-tension-zero-change",
    "D-reflection-tension-equipment",
    "E-adapted-donor-value",
    "F-full-action-multi-value-portfolio",
    "burst-equipment",
    "defensive-reflection-equipment",
    "exact-attack-preview-equipment"
};

double value(const ErugiosuState &s, const Config &c, bool equipment) {
    const auto &p = s.players[0];
    const auto &e = s.players[1];
    constexpr double tension[] = {1, 1.5, 2.5, 4, 6};
    constexpr double atkBuff[] = {.5, .75, 1, 1.25, 1.5};
    // A future equipped finisher is available in the equipment lane. This is
    // only an estimate: no stat or RNG cursor is ever modified to realize it.
    double attack = p.atk;
    if (equipment) attack = std::max(attack, 324 * atkBuff[std::clamp(p.AtkBuffLevel + 2, 0, 4)]);
    const double base = std::max(0.0, (attack - e.def * .5) * .25);
    const int level = std::clamp(p.TensionLevel, 0, 4);
    const double ordinary = base * 3.5 * 1.25 * .98;
    const double burst = (base * tension[level] + level * 6) * 3.5 * 1.25 * .98;
    double credit = std::max(0.0, std::min(double(e.hp), burst) - std::min(double(e.hp), ordinary));
    if (p.mp < 4) credit *= .45;
    double v = -double(e.hp) + credit * c.tension;
    v += p.hp * c.hp;
    const double danger = std::max(0.0, std::min(110.0, p.maxHp * .36) - p.hp);
    v -= danger * danger * c.danger;
    v += std::max(0, p.mp) * c.resource;
    v += p.SpecialMedicineCount * 12 + p.ElfinElixirCount * 12 + p.SageElixirCount * 7;
    v += std::max(0, p.AtkBuffLevel) * c.attackBuff *
         std::clamp(p.AtkBuffTurn + 1, 1, 4) / 4.;
    v += p.BuffLevel * c.defence * std::clamp(p.BuffTurns + 1, 1, 4) / 4.;
    v += p.InsulateLevel * c.defence * .45 * std::clamp(p.InsulateTurns + 1, 1, 4) / 4.;
    if (p.hasMagicMirror) {
        const double rotation = (s.nowState & 15) == B::TYPE_2C ? 1.35 : .65;
        v += c.mirror * rotation * std::clamp(p.MagicMirrorTurn + 1, 1, 4) / 4.;
        // Mirror pays while tension is being built, not only on attacking turns.
        v += std::min(3, level) * 20 * rotation;
    }
    if (p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn > 0) v += 45;
    if (level == 4) v += 90; // Actual high-tension defensive benefit.
    if (p.paralysis) v -= 160 + std::max(0, p.paralysisTurns) * 28;
    if (p.sleeping) v -= 120 + std::max(0, p.sleepingTurn) * 22;
    return v;
}

struct Link { uint32_t parent; int32_t action; }; // Must retain bit 16.
struct Node {
    ErugiosuState state{};
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
    ErugiosuResult result{};
    std::vector<int32_t> bestSuffix;
    std::array<int32_t, 350> scratch{};
    BattleResult transitionLog{}, verificationLog{};
    int bestPosition = INT32_MAX;
    int bestChanges = INT32_MAX;

    double elapsed() const { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
    bool expired() const { return Clock::now() >= passDeadline; }
    bool step(ErugiosuState &s, int action) {
        const int turn = turnOf(s);
        // The supplied RNG cache has 7000 entries. Refuse an unsupported
        // horizon, rather than changing that cache or wrapping its cursor.
        if (turn >= 349 || s.rngPosition >= 6500) return false;
        const auto before = s;
        scratch[turn] = action;
        transitionLog.clear();
        B::Main(&s.rngPosition, 1, scratch.data(), s.players, &transitionLog,
                seed, nullptr, nullptr, -1, &s.nowState);
        s.resultPosition += transitionLog.position;
        s.equipmentChanges += before.players[0].defaultATK != s.players[0].defaultATK;
        ++result.expanded;
        ++result.actionExpansions[action & B::ACTION_ID_MASK];
        if (before.players[0].defaultATK != s.players[0].defaultATK) ++result.equipmentBranches;
        if (!safeTransition(before, s, action, transitionLog)) {
            ++result.unsafeFleeSkipped;
            return false;
        }
        return true;
    }

    void consider(const Node &n, const std::vector<Link> &links, bool equipment) {
        if (n.state.players[1].hp != 0 || n.state.players[0].hp <= 0 || expired()) return;
        const int changes = n.state.equipmentChanges - result.root.equipmentChanges;
        const bool better = n.state.resultPosition < bestPosition ||
            (n.state.resultPosition == bestPosition && changes < bestChanges);
        auto &lane = equipment ? result.equipmentAware : result.zeroChange;
        const bool laneBetter = !lane.victory || n.state.resultPosition < lane.position ||
            (n.state.resultPosition == lane.position && changes < lane.equipmentChanges);
        if (!better && !laneBetter) return;
        const auto suffix = materialize(n, links);
        const int length = result.prefixLength + int(suffix.size());
        auto actions = result.actions;
        std::copy(suffix.begin(), suffix.end(), actions.begin() + result.prefixLength);
        std::fill(actions.begin() + length, actions.end(), -1);
        ErugiosuState exact;
        if (!ErugiosuSearch::Replay(initial, seed, actions.data(), length, exact,
                verificationLog, result.prefixLength) || !ErugiosuSearch::SameState(exact, n.state)) {
            ++result.rejectedReplay; return;
        }
        if (Clock::now() >= deadline) return;
        const double when = elapsed();
        auto updateLane = [&](ErugiosuLaneResult &l) {
            if (l.victory && (exact.resultPosition > l.position ||
                    (exact.resultPosition == l.position && changes >= l.equipmentChanges))) return;
            if (!l.victory) l.firstVictoryMs = when;
            l.victory = true; l.position = exact.resultPosition;
            l.equipmentChanges = changes; l.length = length; l.bestVictoryMs = when;
        };
        updateLane(lane);
        if (changes == 0) updateLane(result.zeroChange);
        if (!better) return;
        if (!result.victory) result.firstVictoryMs = when;
        bestPosition = exact.resultPosition; bestChanges = changes;
        bestSuffix = suffix;
        result.victory = result.replayVerified = true;
        result.actions = actions; result.length = length;
        result.finalState = exact; result.replay = verificationLog;
        result.bestVictoryMs = when; ++result.updates;
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
    bool insert(const Node &n, std::vector<Node> &nodes, ErugiosuResult &stats) {
        size_t i = n.hash & mask;
        while (slots[i] != NoPath) {
            auto &o = nodes[slots[i]];
            if (n.hash == o.hash) {
                if (ErugiosuSearch::SameFuture(n.state, o.state)) {
                    ++stats.duplicates;
                    // Same future AND same event cost: fewer actual switches
                    // dominates. Different future states are NEVER merged.
                    if (n.state.equipmentChanges < o.state.equipmentChanges) o = n;
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
    if (p.paralysis || p.sleeping) return 10;
    return std::clamp(p.TensionLevel, 0, 4) * 2 + int(p.hasMagicMirror);
}

// Every legal action is expanded. Quotas reserve different tactical phases
// BEFORE global score truncation; no fixed solution or scripted opening.
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
        if (ctx.expired() || !ErugiosuSearch::Legal(root.state.players[0], action) ||
            (!equipment && (action & B::ACTION_BARE_HANDS) != equipmentBit(root.state.players[0])) ||
            !ctx.step(root.state, action)) return false;
        links.push_back({root.path, action}); root.path = uint32_t(links.size() - 1);
    }
    current.push_back(root);
    bool pruned = false;
    for (int turn = ctx.result.prefixLength + int(anchor.size());
            turn < ctx.maxTurns && !current.empty(); ++turn) {
        if (ctx.expired()) return false;
        candidates.clear(); seen.reset(current.size() * branching);
        for (const auto &parent : current) {
            if (parent.state.resultPosition + 1 > ctx.bestPosition) continue;
            const auto &p = parent.state.players[0];
            const bool disabled = p.paralysis || p.sleeping;
            for (int action : Actions) {
                if (disabled && action != B::ATTACK_ALLY) continue;
                const int keepBit = equipmentBit(p);
                const int choices = equipment && !disabled ? 2 : 1;
                for (int choice = 0; choice < choices; ++choice) {
                    const int encoded = action | (keepBit ^ (choice ? B::ACTION_BARE_HANDS : 0));
                    if (!ErugiosuSearch::Legal(p, encoded)) continue;
                    if ((ctx.result.expanded & 127) == 0 && ctx.expired()) return false;
                    Node n; n.state = parent.state; n.path = parent.path; n.action = encoded;
                    if (!ctx.step(n.state, encoded)) continue;
                    if (n.state.resultPosition > ctx.bestPosition) continue;
                    if (n.state.players[1].hp == 0) { ctx.consider(n, links, equipment); continue; }
                    if (n.state.players[0].hp <= 0 || n.state.resultPosition + 1 > ctx.bestPosition) continue;
                    n.hash = fingerprint(n.state);
                    n.score = value(n.state, cfg, equipment);
                    if (cfg.preview && turn + 1 < ctx.maxTurns && !ctx.expired()) {
                        auto forecast = n.state;
                        int preview = n.state.players[0].mp >= 4 ? B::MULTITHRUST : B::ATTACK_ALLY;
                        preview |= equipment ? 0 : equipmentBit(n.state.players[0]);
                        if (ErugiosuSearch::Legal(n.state.players[0], preview) && ctx.step(forecast, preview)) {
                            n.score += .35 * (n.state.players[1].hp - forecast.players[1].hp);
                            if (forecast.players[0].hp == 0) n.score -= 90;
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
            if (x.state.resultPosition != y.state.resultPosition)
                return x.state.resultPosition < y.state.resultPosition;
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

bool ErugiosuSearch::SamePlayer(const Player &a, const Player &b) { return fields(a) == fields(b); }
bool ErugiosuSearch::SameFuture(const ErugiosuState &a, const ErugiosuState &b) {
    return a.rngPosition == b.rngPosition && a.nowState == b.nowState &&
        a.resultPosition == b.resultPosition && SamePlayer(a.players[0], b.players[0]) &&
        SamePlayer(a.players[1], b.players[1]);
}
bool ErugiosuSearch::SameState(const ErugiosuState &a, const ErugiosuState &b) {
    return SameFuture(a, b) && a.equipmentChanges == b.equipmentChanges;
}
bool ErugiosuSearch::Legal(const Player &p, int action) {
    if (!encoding(action)) return false;
    const int id = action & B::ACTION_ID_MASK;
    if (p.paralysis || p.sleeping)
        return id == B::ATTACK_ALLY && (action & B::ACTION_BARE_HANDS) == equipmentBit(p);
    switch (id) {
        case B::ATTACK_ALLY: case B::DEFENCE: case B::DOUBLE_UP:
        case B::PSYCHE_UP_ALLY: case B::FLEE_ALLY: return true;
        case B::HEAL: return p.mp >= 2;
        case B::BUFF: case B::DEFENDING_CHAMPION: return p.mp >= 3;
        case B::MIDHEAL: case B::MAGIC_MIRROR:
        case B::MULTITHRUST: case B::INSULATE: return p.mp >= 4;
        case B::MORE_HEAL: return p.mp >= 8;
        case B::FULLHEAL: return p.mp >= 24;
        case B::MEDICINAL_HERBS: case B::SPECIAL_MEDICINE: return p.SpecialMedicineCount > 0;
        case B::SAGE_ELIXIR: return p.SageElixirCount > 0;
        case B::MAGIC_WATER: return p.MagicWaterCount > 0;
        case B::ELFIN_ELIXIR: return p.ElfinElixirCount > 0;
        case B::GOSPEL_SONG: return p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn > 0;
        default: return false; // Enemy-only, missing implementations, excluded thrusts.
    }
}

bool ErugiosuSearch::Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, ErugiosuState &state, BattleResult &log,
        int validateFrom) {
    if (!initial || !actions || !seed || length < 0 || length >= 350) return false;
    ErugiosuState sequential{};
    sequential.players[0] = initial[0]; sequential.players[1] = initial[1];
    lcg::init(seed, true);
    BattleResult one;
    for (int turn = 0; turn < length; ++turn) {
        if (sequential.players[0].hp <= 0 || sequential.players[1].hp <= 0) return false;
        if (!historicalAction(actions[turn]) || sequential.rngPosition >= 6500) return false;
        if (turn >= validateFrom && !Legal(sequential.players[0], actions[turn])) return false;
        const auto before = sequential;
        one.clear();
        B::Main(&sequential.rngPosition, 1, actions, sequential.players, &one,
                seed, nullptr, nullptr, -1, &sequential.nowState);
        sequential.resultPosition += one.position;
        sequential.equipmentChanges += before.players[0].defaultATK != sequential.players[0].defaultATK;
        if (turn >= validateFrom && !safeTransition(before, sequential, actions[turn], one)) return false;
    }
    state = ErugiosuState{}; state.players[0] = initial[0]; state.players[1] = initial[1];
    log.clear(); lcg::init(seed, true);
    if (length) B::Main(&state.rngPosition, length, actions, state.players, &log,
                       seed, nullptr, nullptr, -1, &state.nowState);
    state.resultPosition = log.position;
    // A final enemy-first reflection may kill before an ally log entry exists.
    // Count actual start-of-turn state transitions, NOT just output markers.
    state.equipmentChanges = sequential.equipmentChanges;
    return SameState(state, sequential);
}

int ErugiosuSearch::VariantCount() { return int(std::size(VariantNames)); }
const char *ErugiosuSearch::VariantName(int v) {
    return VariantNames[v >= 0 && v < VariantCount() ? v : DefaultVariant];
}

ErugiosuResult ErugiosuSearch::Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength, int budgetMs, int variant,
        int maxTotalTurns) {
    const auto start = Clock::now();
    const auto budget = std::chrono::microseconds(int64_t(std::max(0, budgetMs)) * 1000);
    const auto reserve = std::min(std::chrono::microseconds(20000), budget / 20);
    Context ctx{initial, seed, std::clamp(maxTotalTurns, 0, 99), start,
                start + budget - reserve, start + budget - reserve};
    ctx.result.actions.fill(-1); ctx.scratch.fill(-1);
    if (!initial || !prefix || !seed || prefixLength < 0 || prefixLength >= 350) {
        ctx.result.error = "Invalid initial world, seed, or prefix length";
    } else {
        ctx.result.prefixLength = ctx.result.length = prefixLength;
        std::copy_n(prefix, prefixLength, ctx.result.actions.begin());
        if (!Replay(initial, seed, ctx.result.actions.data(), prefixLength,
                    ctx.result.root, ctx.result.replay, prefixLength)) {
            ctx.result.error = "Prefix cannot be replayed exactly within the supplied emulator limits";
        } else {
            ctx.result.inputValid = true;
            ctx.result.finalState = ctx.result.root;
            if (ctx.result.root.players[1].hp == 0 && ctx.result.root.players[0].hp > 0) {
                ctx.result.victory = ctx.result.replayVerified = true;
                ctx.result.firstVictoryMs = ctx.result.bestVictoryMs = ctx.elapsed();
                ctx.result.zeroChange = {true, ctx.result.root.resultPosition, 0, prefixLength, ctx.elapsed(), ctx.elapsed()};
            } else if (ctx.result.root.players[0].hp > 0 && budgetMs > 0) {
                if (variant < 0 || variant >= VariantCount()) variant = DefaultVariant;
                const bool zeroOnly = variant == 1 || variant == 3;
                const bool portfolio = variant == 0 || variant == 1 || variant == 6;
                auto pass = [&](int config, size_t width, bool equip, int tail = 0) {
                    const auto now = Clock::now();
                    if (now >= ctx.deadline) return;
                    // Bounded passes cannot starve the other lane or lose the incumbent.
                    ctx.passDeadline = std::min(ctx.deadline, now + std::chrono::milliseconds(220));
                    if (tail) repair(ctx, Configs[config], width, equip, tail);
                    else beam(ctx, Configs[config], width, equip);
                };
                // Every equipment-capable variant retains an explicit zero-change lane.
                pass(0, 48, false);
                if (!zeroOnly) pass(0, 48, true);
                for (int round = 0; Clock::now() < ctx.deadline; ++round) {
                    const size_t width = std::min<size_t>(4096, size_t(96) << std::min(round / 2, 6));
                    int cfg = 0;
                    if (portfolio) cfg = round % 3;
                    else if (variant == 5) cfg = 3;
                    else if (variant == 7) cfg = 1;
                    else if (variant == 8) cfg = 2;
                    else if (variant == 9) cfg = 5;
                    else if (variant == 2) cfg = 4;
                    if (variant == 6 && round % 3 == 0) cfg = 4;
                    const bool equip = !zeroOnly && (round % 4 != 2);
                    pass(cfg, width, equip);
                    if (portfolio && ctx.result.victory && round % 2 == 1)
                        pass(cfg, std::min<size_t>(width, 1024), !zeroOnly, 3 + round % 7);
                }
            }
        }
    }
    ctx.result.elapsedMs = ctx.elapsed();
    return ctx.result;
}