#include "GilyumeiSearch.h"
#include "lcg.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <tuple>
#include <vector>

// Arena paths, collision-checked transpositions and anytime beam/repair are
// adapted from the supplied Reokonn/Bilyouma/Erugiosu search implementations.
// Actions, resource accounting and equipment come from THIS compiled world.
namespace {
using B = BattleEmulator;
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoPath = UINT32_MAX;
// Inventory audited against ActionOptimizer::ACTION_TABLE. Ordinary attack
// is not a tactical action; it is used only for forced status progression.
constexpr int Actions[] = {
    B::PSYCHE_UP_ALLY, B::MULTITHRUST, B::DOUBLE_UP, B::BUFF,
    B::MIDHEAL, B::MORE_HEAL, B::FULLHEAL, B::SPECIAL_MEDICINE,
    B::DEFENDING_CHAMPION, B::MAGIC_MIRROR, B::DEFENCE,
    B::GOSPEL_SONG, B::INSULATE, B::FLEE_ALLY,
#if defined(RUBII)
    B::SAGE_ELIXIR,
#endif
};
constexpr int Equipment[] = {0, B::ACTION_BARE_HANDS, B::ACTION_GANANN};

auto fields(const Player &p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis,
        p.paralysisLevel, p.paralysisTurns, p.SpecialMedicineCount, p.defence,
        p.sleeping, p.sleepingTurn, p.BuffLevel, p.BuffTurns,
        p.hasMagicMirror, p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn,
        p.TensionLevel, p.rage, p.SageElixirCount, p.ElfinElixirCount,
        p.MagicWaterCount, p.InsulateLevel, p.InsulateTurns, p.inactive);
}
uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
uint64_t fingerprint(const GilyumeiState &s) {
    const auto &p = s.players[0];
    uint64_t h = mix(s.nowState) ^ mix(uint32_t(s.rngPosition));
    h ^= mix(uint64_t(uint32_t(p.hp)) << 32 | uint32_t(s.players[1].hp));
    h ^= mix(uint64_t(uint32_t(p.defaultATK)) << 32 | uint32_t(p.atk));
    h ^= mix(uint64_t(uint32_t(p.mp)) << 32 | uint32_t(p.def));
    h ^= mix(uint64_t(uint32_t(p.TensionLevel)) << 32 | uint32_t(p.BuffTurns));
    h ^= mix(uint64_t(uint32_t(p.AtkBuffTurn)) << 32 | uint32_t(p.MagicMirrorTurn));
    // Padding is never hashed. A match is followed by ALL field equality.
    return mix(h ^ uint32_t(s.resultPosition));
}
int turnOf(const GilyumeiState &s) { return int((s.nowState >> 12) & 0xfffff); }
bool disabled(const Player &p) { return p.paralysis || p.sleeping || p.inactive; }
int keepEquipment(const Player &p) {
    for (int request : Equipment)
        if (B::equipmentAttack(request) == p.defaultATK) return request;
    return -1;
}
bool encoding(int a) {
    constexpr int bits = B::ACTION_ID_MASK | B::ACTION_BARE_HANDS | B::ACTION_GANANN;
    return a > 0 && (a & ~bits) == 0 &&
        B::equipmentAttack(a & (B::ACTION_BARE_HANDS | B::ACTION_GANANN)) >= 0;
}
bool historicalAction(int a) {
    // Fixed input is not our tactical action inventory. In particular an
    // ATTACK in the supplied prefix is kept, never removed or re-optimized.
    if (!encoding(a)) return false;
    const int id = a & B::ACTION_ID_MASK;
    return id >= 1 && id <= B::MULTISLASH;
}
bool safeFlee(const GilyumeiState &before, const GilyumeiState &after,
              int action, const BattleResult &log, int begin = 0) {
    if ((action & B::ACTION_ID_MASK) != B::FLEE_ALLY) return true;
    if (disabled(before.players[0])) return false;
    // Existing FLEE skips status processing. Do not exploit enemy-first
    // UPWARD_SLICE (or another status) to escape that turn's forced skip.
    for (int i = begin; i < log.position; ++i)
        if (!log.isEnemy[i] && (log.actions[i] & B::ACTION_ID_MASK) == B::FLEE_ALLY)
            return log.initiative[i] || !disabled(after.players[0]);
    return true;
}
bool roomForTurn(const GilyumeiState &s) {
    // Existing fixed RNG cache: 7000 entries; existing log: 1000 events.
    // Leave more than a whole turn of RNG headroom, without changing either.
    return turnOf(s) < 349 && s.rngPosition < 6500 && s.resultPosition <= 997;
}

struct Config {
    double tension, attackBuff, hp, danger, defence, resource;
    int quota;
    double preview;
};
constexpr Config Configs[] = {
    {.94, 80, .40, .024, 45, .35, 3, 0},
    {.84, 95, .75, .045, 65, .70, 3, 0},
    {1.02, 60, .20, .012, 25, .20, 3, 0},
    {.78, 70, .45, .025, 40, .40, 3, .55}
};
double value(const GilyumeiState &s, const Config &c, bool equipment) {
    const auto &p = s.players[0];
    const auto &e = s.players[1];
    // Heuristic estimates only. Neither this damage nor these attack values
    // are put into a state or admitted as a victory.
    constexpr double tension[] = {1, 1.5, 2.5, 4, 6};
    double atk = p.atk;
    if (equipment && p.defaultATK > 0)
        atk = std::max(atk, double(p.atk) * B::equipmentAttack(0) / p.defaultATK);
    const double hit = std::max(0.0, atk * .5 - e.def * .25) * .5;
    const int level = std::clamp(p.TensionLevel, 0, 4);
    const double ordinary = hit * 3.5 * 1.375;
    const double burst = (hit * tension[level] + level * 5) * 3.5 * 1.375;
    double credit = std::max(0., std::min(double(e.hp), burst) -
                                  std::min(double(e.hp), ordinary));
    if (p.mp < 4) credit *= .25;
    double v = -double(e.hp) + credit * c.tension + p.hp * c.hp;
    const double danger = std::max(0., std::min(p.maxHp * .55, e.defaultATK * .65) - p.hp);
    v -= danger * danger * c.danger;
    v += std::max(0, p.mp) * c.resource + p.SpecialMedicineCount * 10;
    v += p.SageElixirCount * c.resource * 15;
    v += std::max(0, p.AtkBuffLevel) * c.attackBuff *
         std::clamp(p.AtkBuffTurn + 1, 1, 4) / 4.;
    v += p.BuffLevel * c.defence * std::clamp(p.BuffTurns + 1, 1, 4) / 4.;
    if (p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn > 0) v += 22;
    if (level == 4) v += 35;
    if (p.paralysis) v -= 130 + 20 * std::max(0, p.paralysisTurns);
    if (p.sleeping) v -= 100 + 20 * std::max(0, p.sleepingTurn);
    if (p.inactive) v -= 100;
    return v;
}

struct Link { uint32_t parent; int32_t action; };
struct Node {
    GilyumeiState state{};
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
    GilyumeiResult result{};
    std::vector<int32_t> bestSuffix;
    std::array<int32_t, 350> scratch{};
    BattleResult transitionLog{}, verificationLog{};
    int bestPosition = INT32_MAX;
    int bestChanges = INT32_MAX;

    double elapsed() const { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
    bool expired() const { return Clock::now() >= passDeadline; }
    bool step(GilyumeiState &s, int action) {
        if (!roomForTurn(s)) return false;
        const auto before = s;
        scratch[turnOf(s)] = action;
        transitionLog.clear();
        B::Main(&s.rngPosition, 1, scratch.data(), s.players, &transitionLog,
                seed, nullptr, nullptr, -1, &s.nowState);
        s.resultPosition += transitionLog.position;
        s.equipmentChanges += before.players[0].defaultATK != s.players[0].defaultATK;
        ++result.expanded;
        ++result.actionExpansions[action & B::ACTION_ID_MASK];
        if (before.players[0].defaultATK != s.players[0].defaultATK) ++result.equipmentBranches;
        if (!safeFlee(before, s, action, transitionLog)) { ++result.unsafeFlee; return false; }
        return true;
    }
    void consider(const Node &n, const std::vector<Link> &links, bool equipment) {
        if (n.state.players[1].hp != 0 || expired()) return;
        const int changes = n.state.equipmentChanges;
        const bool better = n.state.resultPosition < bestPosition ||
            (n.state.resultPosition == bestPosition && changes < bestChanges);
        auto &lane = equipment ? result.equipment : result.unchanged;
        const bool laneBetter = !lane.victory || n.state.resultPosition < lane.position ||
            (n.state.resultPosition == lane.position && changes < lane.equipmentChanges);
        if (!better && !laneBetter) return;
        const auto suffix = materialize(n, links);
        const int length = result.prefixLength + int(suffix.size());
        auto actions = result.actions;
        std::copy(suffix.begin(), suffix.end(), actions.begin() + result.prefixLength);
        std::fill(actions.begin() + length, actions.end(), -1);
        GilyumeiState exact;
        if (!GilyumeiSearch::Replay(initial, seed, actions.data(), length, result.prefixLength,
                    exact, verificationLog) || !GilyumeiSearch::SameState(exact, n.state)) {
            ++result.rejectedReplay; return;
        }
        const auto updateLane = [&](GilyumeiLaneResult &l) {
            if (!l.victory || exact.resultPosition < l.position ||
                (exact.resultPosition == l.position && exact.equipmentChanges < l.equipmentChanges))
                l = {true, exact.resultPosition, exact.equipmentChanges};
        };
        updateLane(lane);
        if (changes == result.root.equipmentChanges) updateLane(result.unchanged);
        if (!better) return;
        if (!result.victory) result.firstVictoryMs = elapsed();
        bestPosition = exact.resultPosition; bestChanges = exact.equipmentChanges;
        bestSuffix = suffix;
        result.victory = result.replayVerified = true;
        result.actions = actions; result.length = length;
        result.finalState = exact; result.replay = verificationLog;
        ++result.updates;
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
    bool insert(const Node &n, std::vector<Node> &nodes, GilyumeiResult &stats) {
        size_t i = n.hash & mask;
        while (slots[i] != NoPath) {
            auto &o = nodes[slots[i]];
            if (n.hash == o.hash) {
                if (GilyumeiSearch::SameFuture(n.state, o.state)) {
                    ++stats.duplicates;
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
    if (disabled(p)) return 10;
    return std::clamp(p.TensionLevel, 0, 4) * 2 + int(p.AtkBuffLevel > 0);
}

bool beam(Context &ctx, const Config &cfg, size_t width, bool equipment,
          const std::vector<int32_t> &anchor = {}) {
    std::vector<Node> current, next, candidates;
    std::vector<Link> links;
    std::vector<uint32_t> order, chosen;
    std::array<std::vector<uint32_t>, 11> groups;
    std::vector<uint8_t> selected;
    StateSet seen;
    const size_t branching = std::size(Actions) * (equipment ? 3 : 1);
    current.reserve(width); next.reserve(width); candidates.reserve(width * branching);
    links.reserve(width * size_t(std::max(1, ctx.maxTurns - ctx.result.prefixLength)));
    Node root; root.state = ctx.result.root;
    for (int action : anchor) {
        if (ctx.expired() || !GilyumeiSearch::Legal(root.state.players[0], action) ||
            (!equipment && (action & (B::ACTION_BARE_HANDS | B::ACTION_GANANN)) != keepEquipment(root.state.players[0])) ||
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
            const int keep = keepEquipment(p);
            const bool forced = disabled(p);
            const size_t actionCount = forced ? 1 : std::size(Actions);
            for (size_t a = 0; a < actionCount; ++a) {
                const int action = forced ? B::ATTACK_ALLY : Actions[a];
                // Keep first, then other available weapon states. Disabled
                // states never generate an equipment-change branch.
                const int requests[] = {keep, 0, B::ACTION_BARE_HANDS, B::ACTION_GANANN};
                const int choices = equipment && !forced ? 4 : 1;
                for (int choice = 0; choice < choices; ++choice) {
                    const int request = requests[choice];
                    if (request < 0 || (choice > 0 && request == keep) || B::equipmentAttack(request) < 0) continue;
                    const int encoded = action | request;
                    if (!GilyumeiSearch::Legal(p, encoded)) continue;
                    if ((ctx.result.expanded & 127) == 0 && ctx.expired()) return false;
                    Node n; n.state = parent.state; n.path = parent.path; n.action = encoded;
                    if (!ctx.step(n.state, encoded)) continue;
                    if (n.state.resultPosition > ctx.bestPosition) continue;
                    if (n.state.players[1].hp == 0) { ctx.consider(n, links, equipment); continue; }
                    if (n.state.players[0].hp <= 0 || n.state.resultPosition + 1 > ctx.bestPosition) continue;
                    n.hash = fingerprint(n.state);
                    n.score = value(n.state, cfg, equipment);
                    if (cfg.preview > 0 && turn + 1 < ctx.maxTurns) {
                        const int preview = B::MULTITHRUST | (equipment ? 0 : keepEquipment(n.state.players[0]));
                        if (GilyumeiSearch::Legal(n.state.players[0], preview)) {
                            auto forecast = n.state;
                            if (ctx.step(forecast, preview)) {
                                n.score += cfg.preview * (n.state.players[1].hp - forecast.players[1].hp);
                                if (forecast.players[0].hp == 0) n.score -= 90;
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
            if (x.state.resultPosition != y.state.resultPosition) return x.state.resultPosition < y.state.resultPosition;
            if (x.state.equipmentChanges != y.state.equipmentChanges) return x.state.equipmentChanges < y.state.equipmentChanges;
            return x.hash < y.hash;
        };
        chosen.clear(); selected.assign(candidates.size(), 0);
        if (cfg.quota && keep < candidates.size()) {
            const size_t quota = std::max<size_t>(1, keep / (11 * cfg.quota));
            for (auto &group : groups) group.clear();
            for (uint32_t i = 0; i < candidates.size(); ++i) groups[phase(candidates[i])].push_back(i);
            for (auto &group : groups) {
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
} // namespace

bool GilyumeiSearch::SamePlayer(const Player &a, const Player &b) { return fields(a) == fields(b); }
bool GilyumeiSearch::SameFuture(const GilyumeiState &a, const GilyumeiState &b) {
    return a.rngPosition == b.rngPosition && a.nowState == b.nowState &&
        a.resultPosition == b.resultPosition && SamePlayer(a.players[0], b.players[0]) && SamePlayer(a.players[1], b.players[1]);
}
bool GilyumeiSearch::SameState(const GilyumeiState &a, const GilyumeiState &b) {
    return SameFuture(a, b) && a.equipmentChanges == b.equipmentChanges;
}
bool GilyumeiSearch::Legal(const Player &p, int action) {
    if (!encoding(action)) return false;
    const int request = action & (B::ACTION_BARE_HANDS | B::ACTION_GANANN);
    const int id = action & B::ACTION_ID_MASK;
    if (disabled(p)) return request == keepEquipment(p) && id == B::ATTACK_ALLY;
    switch (id) {
        case B::MIDHEAL: return p.mp >= 4;
        case B::MORE_HEAL: return p.mp >= 8;
        case B::FULLHEAL: return p.mp >= 24;
        case B::DEFENDING_CHAMPION: case B::BUFF: return p.mp >= 3;
        case B::MAGIC_MIRROR: case B::INSULATE: return p.mp >= 4;
        case B::MULTITHRUST: return request == 0 && p.mp >= 4;
        case B::SPECIAL_MEDICINE: return p.SpecialMedicineCount > 0;
#if defined(RUBII)
        case B::SAGE_ELIXIR: return p.SageElixirCount > 0;
#endif
        case B::GOSPEL_SONG: return p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn > 0;
        case B::PSYCHE_UP_ALLY: case B::DOUBLE_UP: case B::DEFENCE: case B::FLEE_ALLY: return true;
        default: return false;
    }
}

bool GilyumeiSearch::Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, int prefixLength,
        GilyumeiState &out, BattleResult &log) {
    if (!actions || length < 0 || length >= 350 || prefixLength < 0 || prefixLength > length || seed == 0) return false;
    out = {}; out.players[0] = initial[0]; out.players[1] = initial[1];
    log.clear(); lcg::init(seed, true);
    // Turnwise audit obtains actual changes even when a requested change is
    // blocked, and checks resource/weapon legality at the CURRENT state.
    for (int i = 0; i < length && out.players[1].hp != 0 && out.players[0].hp != 0; ++i) {
        if (!roomForTurn(out) || !historicalAction(actions[i]) ||
            (i >= prefixLength && !Legal(out.players[0], actions[i]))) return false;
        const auto before = out;
        const int begin = log.position;
        B::Main(&out.rngPosition, 1, actions, out.players, &log, seed, nullptr, nullptr, -1, &out.nowState);
        out.resultPosition = log.position;
        out.equipmentChanges += before.players[0].defaultATK != out.players[0].defaultATK;
        if (i >= prefixLength && !safeFlee(before, out, actions[i], log, begin)) return false;
    }
    // Independent continuous replay from the original world, not the search
    // state. Exact equality also detects incremental-resumption mistakes.
    GilyumeiState continuous;
    continuous.players[0] = initial[0]; continuous.players[1] = initial[1];
    log.clear(); lcg::init(seed, true);
    if (length > 0 && initial[0].hp != 0 && initial[1].hp != 0)
        B::Main(&continuous.rngPosition, length, actions, continuous.players, &log,
                seed, nullptr, nullptr, -1, &continuous.nowState);
    continuous.resultPosition = log.position;
    int loggedChanges = 0;
    for (int i = 0; i < log.position; ++i)
        if (!log.isEnemy[i] && (log.actions[i] & B::ACTION_EQUIPMENT_CHANGED)) ++loggedChanges;
    // A living actor has a logged action for every completed equipment change.
    // A defeated prefix can end before that actor's log entry; its actual
    // count still comes from the independently replayed equipment states.
    if (out.players[0].hp > 0 && loggedChanges != out.equipmentChanges) return false;
    continuous.equipmentChanges = out.equipmentChanges;
    return SameState(out, continuous);
}

GilyumeiResult GilyumeiSearch::Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength, int budgetMs, int variant) {
    const auto start = Clock::now();
    // Leave a small part of the request budget for container teardown and
    // final output, rather than starting another large pass at the deadline.
    const int budget = std::max(1, budgetMs - 5);
    Context ctx{initial, seed, 349, start,
        start + std::chrono::milliseconds(budget), start + std::chrono::milliseconds(budget)};
    ctx.result.actions.fill(-1);
    ctx.result.prefixLength = prefixLength;
    if (!prefix || prefixLength < 0 || prefixLength >= 350) return ctx.result;
    ctx.maxTurns = std::min(349, prefixLength + 40);
    std::copy_n(prefix, prefixLength, ctx.result.actions.begin());
    ctx.result.length = prefixLength;
    if (!Replay(initial, seed, ctx.result.actions.data(), prefixLength, prefixLength,
                ctx.result.root, ctx.verificationLog)) return ctx.result;
    ctx.result.inputValid = true;
    if (ctx.result.root.players[1].hp == 0) {
        ctx.result.victory = ctx.result.replayVerified = true;
        ctx.result.finalState = ctx.result.root; ctx.result.replay = ctx.verificationLog;
        ctx.result.unchanged = {true, ctx.result.root.resultPosition, ctx.result.root.equipmentChanges};
        ctx.result.firstVictoryMs = ctx.elapsed();
        ctx.result.elapsedMs = ctx.elapsed(); return ctx.result;
    }
    if (ctx.result.root.players[0].hp == 0) { ctx.result.elapsedMs = ctx.elapsed(); return ctx.result; }
    // Widening passes alternate no-change and equipment-aware searches.
    // Each lane explores all legal inventory entries, with phase reservations
    // before score truncation. All passes share one incumbent and deadline.
    size_t width = ProductionWidth;
    int pass = 0;
    while (Clock::now() < ctx.deadline) {
        if (ctx.deadline - Clock::now() < std::chrono::milliseconds(2)) break;
        const bool equipment = (pass & 1) != 0;
        const int config = variant > 0 ? (variant - 1) % int(std::size(Configs)) : (pass / 2) % int(std::size(Configs));
        const int slice = std::max(15, std::min(budget / 3, 90 + pass * 25));
        ctx.passDeadline = std::min(ctx.deadline, Clock::now() + std::chrono::milliseconds(slice));
        std::vector<int32_t> anchor;
        // Repair ONLY a suffix of a discovered answer; immutable user prefix
        // is never moved into the search space.
        if (equipment && pass >= 5 && pass % 4 == 1 && ctx.bestSuffix.size() > 4)
            anchor.assign(ctx.bestSuffix.begin(), ctx.bestSuffix.end() - 4);
        beam(ctx, Configs[config], width, equipment, anchor);
        ++pass;
        if (pass % 2 == 0) width = std::min<size_t>(3072, width * 2);
    }
    ctx.result.elapsedMs = ctx.elapsed();
    return ctx.result;
}