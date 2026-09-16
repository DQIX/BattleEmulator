#include "NusisamaSearch.h"
#include "lcg.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <queue>
#include <tuple>
#include <vector>

// Compact path arena, exact-checked transpositions, iterative beam widening,
// tactical quotas and suffix repair are adapted from the supplied
// reokonn-GPT6-Pro, bilyouma-gpt6-pro and yo2-gpt6-pro search implementations.
// Only search techniques are imported. All transitions use this branch's Main.
namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoPath = UINT32_MAX;
constexpr int Actions[] = {
    BattleEmulator::MULTITHRUST, BattleEmulator::PSYCHE_UP_ALLY,
    BattleEmulator::DOUBLE_UP, BattleEmulator::BUFF,
    BattleEmulator::SPECIAL_MEDICINE, BattleEmulator::GOSPEL_SONG,
    BattleEmulator::MIDHEAL, BattleEmulator::MORE_HEAL, BattleEmulator::FULLHEAL,
    BattleEmulator::DEFENCE, BattleEmulator::DEFENDING_CHAMPION,
    BattleEmulator::FLEE_ALLY, BattleEmulator::INSULATE, BattleEmulator::MAGIC_MIRROR,
#if defined(RUBII)
    BattleEmulator::SAGE_ELIXIR,
#endif
};

auto fields(const Player &p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis,
        p.paralysisLevel, p.paralysisTurns, p.SpecialMedicineCount,
        p.defence, p.sleeping, p.sleepingTurn, p.BuffLevel, p.BuffTurns,
        p.hasMagicMirror, p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn,
        p.TensionLevel, p.rage, p.SageElixirCount, p.ElfinElixirCount,
        p.MagicWaterCount, p.InsulateLevel, p.InsulateTurns, p.inactive);
}

uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

uint64_t fingerprint(const NusisamaState &s) {
    // Only a routing fingerprint. Every collision compares ALL Player fields.
    // No padding bytes, HP buckets, RNG-only identity or history lookup.
    uint64_t h = mix(s.nowState) ^ mix(uint32_t(s.rngPosition));
    for (const auto &p : s.players) {
        for (int x : {p.hp, p.mp, p.AtkBuffLevel, p.AtkBuffTurn, p.BuffLevel,
                p.BuffTurns, p.TensionLevel, p.SpecialMedicineCount,
                p.specialChargeTurn, p.MagicMirrorTurn, p.InsulateTurns})
            h = (h ^ uint32_t(x)) * 0xd6e8feb86659fd93ULL;
    }
    return mix(h ^ uint32_t(s.resultPosition));
}

bool effective(const NusisamaState &before, const NusisamaState &after, int action) {
    // Preserve ACTION_TABLE's existing postcondition as well as its predicates.
    return action != BattleEmulator::PSYCHE_UP_ALLY ||
        after.players[0].TensionLevel > before.players[0].TensionLevel;
}

bool sameRecord(const BattleResult &a, int i, const BattleResult &b, int j) {
    return std::tie(a.actions[i], a.damages[i], a.isEnemy[i], a.AtkBuffTurns[i],
        a.BuffTurnss[i], a.MagicMirrorTurns[i], a.turns[i], a.initiative[i],
        a.ehp[i], a.ahp[i], a.state[i], a.scTurn[i], a.amp[i],
        a.defenseFlag[i], a.actorIndex[i], a.actorMp[i], a.aiResourceGateMask[i],
        a.aiOriginalSlot[i], a.aiResolvedSlot[i]) ==
        std::tie(b.actions[j], b.damages[j], b.isEnemy[j], b.AtkBuffTurns[j],
        b.BuffTurnss[j], b.MagicMirrorTurns[j], b.turns[j], b.initiative[j],
        b.ehp[j], b.ahp[j], b.state[j], b.scTurn[j], b.amp[j],
        b.defenseFlag[j], b.actorIndex[j], b.actorMp[j], b.aiResourceGateMask[j],
        b.aiOriginalSlot[j], b.aiResolvedSlot[j]);
}

int turnOf(const NusisamaState &s) { return int((s.nowState >> 12) & 0xfffff); }

struct Config {
    const char *name;
    int hp, danger, mp, tension, setup, buff, quota, width, preview;
    bool repair, portfolio;
    int bestFirst = 0;
    int block = 1;
};
// Scores are heuristic estimates only, in hundredths of HP. They are never
// written into Player, RNG, nowState or BattleResult.
constexpr Config Configs[] = {
    {"tension-portfolio", 35, 60, 25, 100, 220, 55, 4, 64, 0, true, true},
    {"damage-greedy",     15, 20, 12,  55, 160, 30, 0, 64, 0, false,false},
    {"tension-balanced",  35, 60, 25, 100, 220, 55, 0, 64, 0, false,false},
    {"tension-quota",     35, 60, 25, 100, 220, 55, 2, 64, 0, false,false},
    {"attack-preview",    35, 60, 25,  80, 220, 55, 4, 64, 50,false,false},
    {"super-tension",     35, 60, 25, 130, 220, 55, 4, 64, 0, false,false},
    {"survival",          80,140, 40, 105, 220,100, 4, 64, 0, false,false},
    {"tension-repair",    35, 60, 25, 100, 220, 55, 4, 64, 0, true, false},
    {"weighted-best-first",35,60,25,100,220,55,0,64,0,false,false,10000},
    {"two-ply-tension",   35, 60, 25, 100, 220, 55, 4, 32, 0,false,false,0,2},
    {"low-credit-quota",  25, 35, 18,  70, 160, 35, 2, 64, 0,false,false},
    {"wide-tension",      35, 60, 25, 100, 220, 55, 4,256, 0,false,false},
    {"preview-heavy",     25, 35, 18,  70, 220, 55, 4, 64,100,false,false}
};

int64_t value(const NusisamaState &s, const Config &cfg) {
    const auto &p = s.players[0];
    const auto &e = s.players[1];
    const int level = std::clamp(p.TensionLevel, 0, 4);
    constexpr double factor[] = {1.0, 1.5, 2.5, 4.0, 6.0};
    // Compare stored tension to the uncharged attack rather than importing
    // another boss's learned weights. Use potential DOUBLE_UP only for value.
    const double attack = std::max(double(p.atk), p.defaultATK * 1.5);
    const double hit = std::max(0.0, (attack * .5 - e.def * .25) * .5);
    const double bank = 3.5 * (hit * (factor[level] - 1.0) + level * 6.0);
    const double credit = std::min(bank, std::max(0.0, e.hp - hit * 1.5));
    int64_t v = -int64_t(e.hp) * 100 + int64_t(credit * cfg.tension);
    v += int64_t(p.hp) * cfg.hp + int64_t(std::max(0, p.mp)) * cfg.mp;
    if (p.AtkBuffLevel >= 2)
        v += int64_t(cfg.setup) * 100 * std::clamp(p.AtkBuffTurn + 1, 0, 4) / 4;
    v += int64_t(cfg.buff) * 100 * p.BuffLevel * std::clamp(p.BuffTurns + 1, 0, 4) / 4;
    v += int64_t(p.SpecialMedicineCount) * (900 + level * 400);
    if (p.specialChargeTurn > 0) v += 2400;
    const int expectedHit = std::max(0, int(e.atk * .5 - p.def * .25));
    const int danger = std::max(0, std::min(int(p.maxHp), expectedHit * 2 + 35) - p.hp);
    v -= int64_t(danger) * danger * cfg.danger / 100;
    if (p.paralysis || p.sleeping || p.inactive) v -= 14000;
    if (level == 4) v += 5000; // Includes defensive value of super-high tension.
    if (p.mp < 10) v -= 160000; // No legal MULTITHRUST until MP recovers.
    // INSULATE and mirror get no defensive credit against this enemy's moves.
    // They are still expanded as ordinary legal RNG-adjustment actions.
    return v;
}

struct Link { uint32_t parent; uint8_t action; };
struct Node {
    NusisamaState state{};
    uint64_t hash = 0;
    int64_t score = 0;
    uint32_t path = NoPath;
    uint8_t action = 0;
};

std::vector<int32_t> pathOf(const Node &n, const std::vector<Link> &links) {
    std::vector<int32_t> out{int32_t(n.action)};
    for (auto p = n.path; p != NoPath; p = links[p].parent) out.push_back(links[p].action);
    std::reverse(out.begin(), out.end());
    return out;
}

struct Context {
    const Player *initial;
    uint64_t seed;
    int maxTurns;
    Clock::time_point start, deadline;
    NusisamaResult result{};
    std::vector<int32_t> bestSuffix;
    BattleResult transitionLog{}, verificationLog{};
    std::array<int32_t, 350> scratch{};
    int bestPosition = INT32_MAX;

    double elapsed() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    bool expired() const { return Clock::now() >= deadline; }
    void step(NusisamaState &s, int action) {
        // Main indexes the WHOLE action sequence using the turn in nowState.
        // Do not pass the one-element gene used by the reference branches.
        const int turn = turnOf(s);
        scratch[turn] = action;
        transitionLog.clear();
        BattleEmulator::Main(&s.rngPosition, 1, scratch.data(), s.players,
            &transitionLog, seed, nullptr, nullptr, -1, &s.nowState);
        s.resultPosition += transitionLog.position;
        ++result.expanded;
    }
    void consider(const Node &n, const std::vector<Link> &links) {
        if (n.state.players[1].hp != 0 || n.state.resultPosition > bestPosition || expired()) return;
        const auto suffix = pathOf(n, links);
        if (n.state.resultPosition == bestPosition &&
            (suffix.size() > bestSuffix.size() ||
            (suffix.size() == bestSuffix.size() &&
                n.state.players[0].hp <= result.finalState.players[0].hp))) return;
        auto actions = result.actions;
        const int length = result.prefixLength + int(suffix.size());
        std::copy(suffix.begin(), suffix.end(), actions.begin() + result.prefixLength);
        std::fill(actions.begin() + length, actions.end(), -1);
        NusisamaState exact;
        if (!NusisamaSearch::Replay(initial, seed, actions.data(), length, exact, verificationLog) ||
            !NusisamaSearch::SameState(exact, n.state) ||
            !NusisamaSearch::VerifySuffix(seed, result.root, actions.data(),
                result.prefixLength, length, exact, verificationLog)) {
            ++result.rejectedReplay;
            return;
        }
        if (expired()) return;
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
        slots.assign(cap, NoPath); mask = cap - 1;
    }
    bool insert(const Node &n, const std::vector<Node> &nodes, NusisamaResult &stats) {
        size_t i = n.hash & mask;
        while (slots[i] != NoPath) {
            const auto &other = nodes[slots[i]];
            if (n.hash == other.hash && NusisamaSearch::SameState(n.state, other.state)) {
                ++stats.duplicates;
                return false;
            }
            i = (i + 1) & mask;
        }
        slots[i] = uint32_t(nodes.size());
        return true;
    }
};

int phase(const Node &n) {
    const auto &p = n.state.players[0];
    return std::clamp(p.TensionLevel, 0, 4) + (p.AtkBuffLevel >= 2 ? 5 : 0);
}

// true means there was no width pruning; repeating wider cannot add a path.
bool beam(Context &ctx, const Config &cfg, size_t width,
          const std::vector<int32_t> &anchor = {}) {
    std::vector<Node> current, next, candidates;
    std::vector<Link> links;
    std::vector<uint32_t> order, chosen, groups[10];
    std::vector<uint8_t> flags;
    StateSet seen;
    current.reserve(width); next.reserve(width);
    candidates.reserve(width * std::size(Actions));
    links.reserve(width * size_t(std::max(1, ctx.maxTurns - ctx.result.prefixLength)));
    Node root; root.state = ctx.result.root;
    for (int action : anchor) {
        if (ctx.expired() || !NusisamaSearch::Legal(root.state, action) ||
            !NusisamaSearch::Safe(root.state.players[0], action)) return false;
        ctx.step(root.state, action);
        if (root.state.players[0].hp == 0 || root.state.players[1].hp == 0) return false;
        links.push_back({root.path, uint8_t(action)});
        root.path = uint32_t(links.size() - 1);
    }
    current.push_back(root);
    bool pruned = false;
    for (int turn = turnOf(root.state) + 1;
         turn <= ctx.maxTurns && !current.empty(); ++turn) {
        if (ctx.expired()) return false;
        candidates.clear(); seen.reset(current.size() * std::size(Actions));
        for (const Node &parent : current) {
            if (parent.state.resultPosition + 1 > ctx.bestPosition) continue;
            for (int action : Actions) {
                if (!NusisamaSearch::Legal(parent.state, action)) continue;
                if (!NusisamaSearch::Safe(parent.state.players[0], action)) {
                    ++ctx.result.unsafeFleeSkipped; continue;
                }
                if ((ctx.result.expanded & 63) == 0 && ctx.expired()) return false;
                Node n; n.state = parent.state; n.path = parent.path; n.action = uint8_t(action);
                ctx.step(n.state, action);
                if (!effective(parent.state, n.state, action) || n.state.resultPosition > ctx.bestPosition) continue;
                if (n.state.players[1].hp == 0) { ctx.consider(n, links); continue; }
                if (n.state.players[0].hp == 0 || n.state.resultPosition + 1 > ctx.bestPosition) continue;
                n.hash = fingerprint(n.state);
                if (!seen.insert(n, candidates, ctx.result)) continue;
                n.score = value(n.state, cfg);
                if (cfg.preview && turn < ctx.maxTurns &&
                    NusisamaSearch::Legal(n.state, BattleEmulator::MULTITHRUST)) {
                    auto forecast = n.state;
                    ctx.step(forecast, BattleEmulator::MULTITHRUST);
                    n.score += int64_t(n.state.players[1].hp - forecast.players[1].hp) * cfg.preview;
                    if (forecast.players[0].hp == 0) n.score -= cfg.hp * 75;
                }
                candidates.push_back(n);
            }
        }
        size_t layerWidth = width;
        if (cfg.block == 2 && (turn - turnOf(root.state)) % 2) layerWidth *= std::size(Actions);
        const size_t keep = std::min(layerWidth, candidates.size());
        if (!keep) break;
        if (ctx.expired()) return false;
        pruned |= keep < candidates.size();
        order.resize(candidates.size()); std::iota(order.begin(), order.end(), 0);
        const auto better = [&](uint32_t a, uint32_t b) {
            if (candidates[a].score != candidates[b].score) return candidates[a].score > candidates[b].score;
            if (candidates[a].state.resultPosition != candidates[b].state.resultPosition)
                return candidates[a].state.resultPosition < candidates[b].state.resultPosition;
            return candidates[a].hash < candidates[b].hash;
        };
        chosen.clear(); flags.assign(candidates.size(), 0);
        if (cfg.quota && keep < candidates.size()) {
            const size_t quota = keep / (10 * cfg.quota);
            for (auto &g : groups) g.clear();
            for (uint32_t i = 0; i < candidates.size(); ++i) groups[phase(candidates[i])].push_back(i);
            for (auto &g : groups) {
                const size_t k = std::min(quota, g.size());
                if (k < g.size()) std::nth_element(g.begin(), g.begin() + k, g.end(), better);
                for (size_t i = 0; i < k; ++i) { chosen.push_back(g[i]); flags[g[i]] = 1; }
            }
        }
        if (keep < order.size()) {
            std::nth_element(order.begin(), order.begin() + keep, order.end(), better);
            order.resize(keep);
        }
        std::sort(order.begin(), order.end(), better);
        if (ctx.expired()) return false;
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
    for (int tail : {3, 5, 7}) {
        if (ctx.expired()) return;
        const size_t cut = ctx.bestSuffix.size() > size_t(tail) ? ctx.bestSuffix.size() - tail : 0;
        if (!cut) continue;
        const std::vector<int32_t> anchor(ctx.bestSuffix.begin(), ctx.bestSuffix.begin() + cut);
        beam(ctx, cfg, std::min<size_t>(width, 2048), anchor);
    }
}

void bestFirst(Context &ctx, const Config &cfg) {
    struct Entry {
        int64_t key; uint32_t id;
        bool operator<(const Entry &b) const { return key != b.key ? key > b.key : id > b.id; }
    };
    constexpr size_t limit = 131072;
    std::priority_queue<Entry> open;
    std::vector<Node> nodes; nodes.reserve(limit);
    std::vector<Link> links; links.reserve(limit);
    StateSet seen; seen.reset(limit);
    Node root; root.state = ctx.result.root; root.hash = fingerprint(root.state);
    seen.insert(root, nodes, ctx.result); nodes.push_back(root); open.push({0, 0});
    while (!open.empty() && nodes.size() + std::size(Actions) < limit && !ctx.expired()) {
        const Node parent = nodes[open.top().id]; open.pop();
        if (turnOf(parent.state) >= ctx.maxTurns || parent.state.resultPosition + 1 > ctx.bestPosition) continue;
        for (int action : Actions) {
            if (!NusisamaSearch::Legal(parent.state, action)) continue;
            if (!NusisamaSearch::Safe(parent.state.players[0], action)) {
                ++ctx.result.unsafeFleeSkipped; continue;
            }
            Node n; n.state = parent.state; n.path = parent.path; n.action = uint8_t(action);
            ctx.step(n.state, action);
            if (!effective(parent.state, n.state, action) || n.state.resultPosition > ctx.bestPosition) continue;
            if (n.state.players[1].hp == 0) { ctx.consider(n, links); continue; }
            if (n.state.players[0].hp == 0 || n.state.resultPosition + 1 > ctx.bestPosition) continue;
            n.hash = fingerprint(n.state);
            if (!seen.insert(n, nodes, ctx.result)) continue;
            const auto id = uint32_t(nodes.size());
            links.push_back({n.path, n.action}); n.path = uint32_t(links.size() - 1);
            const int64_t key = int64_t(n.state.resultPosition) * cfg.bestFirst - value(n.state, cfg);
            nodes.push_back(n); open.push({key, id});
        }
    }
}
} // namespace

bool NusisamaSearch::SamePlayer(const Player &a, const Player &b) { return fields(a) == fields(b); }
bool NusisamaSearch::SameState(const NusisamaState &a, const NusisamaState &b) {
    return a.rngPosition == b.rngPosition && a.nowState == b.nowState &&
        a.resultPosition == b.resultPosition && SamePlayer(a.players[0], b.players[0]) &&
        SamePlayer(a.players[1], b.players[1]);
}

bool NusisamaSearch::Legal(const NusisamaState &s, int action) {
    const auto &p = s.players[0];
    // ACTION_TABLE predicates, including existing MP thresholds and turn limits.
    // The TableF/TableG switch for INSULATE is a portfolio gate, not legality:
    // this search expands their union without changing the old tables.
    switch (action) {
        case BattleEmulator::MIDHEAL: return p.hp / p.maxHp < .7 && p.mp >= 4;
        case BattleEmulator::DEFENDING_CHAMPION: return p.mp >= 2;
        case BattleEmulator::MAGIC_MIRROR: return p.mp >= 4 && p.MagicMirrorTurn <= 2;
        case BattleEmulator::MORE_HEAL: return p.mp >= 8;
        case BattleEmulator::FULLHEAL: return p.mp >= 24;
        case BattleEmulator::SPECIAL_MEDICINE: return p.SpecialMedicineCount > 0;
#if defined(RUBII)
        case BattleEmulator::SAGE_ELIXIR: return p.SageElixirCount > 0;
#endif
        case BattleEmulator::FLEE_ALLY: return true;
        case BattleEmulator::DOUBLE_UP: return p.AtkBuffLevel == 0;
        case BattleEmulator::PSYCHE_UP_ALLY: return s.players[1].hp > 180 && p.TensionLevel <= 3;
        case BattleEmulator::BUFF: return p.mp >= 10 && p.BuffLevel <= 1;
        case BattleEmulator::MULTITHRUST: return p.mp >= 10 && p.AtkBuffLevel >= 2;
        case BattleEmulator::DEFENCE: return true;
        case BattleEmulator::GOSPEL_SONG: return p.specialChargeTurn >= 1;
        case BattleEmulator::INSULATE: return p.InsulateLevel <= 1 && p.InsulateTurns <= 2 && p.mp >= 4;
        default: return false;
    }
}

bool NusisamaSearch::Safe(const Player &p, int action) {
    // Do not exploit Main's pre-action FLEE skip while unable to act.
    // Nusisama2's four enemy moves cannot newly inflict these statuses.
    return action != BattleEmulator::FLEE_ALLY || (!p.paralysis && !p.sleeping && !p.inactive);
}

bool NusisamaSearch::Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, NusisamaState &state, BattleResult &log) {
    if (!initial || !actions || length < 0 || length > 349) return false;
    state = NusisamaState{}; state.players[0] = initial[0]; state.players[1] = initial[1];
    log.clear(); lcg::init(seed, true);
    if (length && initial[0].hp > 0 && initial[1].hp > 0)
        BattleEmulator::Main(&state.rngPosition, length, actions, state.players,
            &log, seed, nullptr, nullptr, -1, &state.nowState);
    state.resultPosition = log.position;
    return true;
}

bool NusisamaSearch::VerifySuffix(uint64_t seed, const NusisamaState &root,
        const int32_t actions[350], int prefixLength, int length,
        const NusisamaState &expected, const BattleResult &replay) {
    if (!actions || prefixLength < 0 || length < prefixLength || length > 349 ||
        turnOf(root) != prefixLength) return false;
    lcg::init(seed, true);
    NusisamaState s = root;
    BattleResult log;
    for (int i = prefixLength; i < length; ++i) {
        if (!Legal(s, actions[i]) || !Safe(s.players[0], actions[i]) ||
            s.players[0].hp <= 0 || s.players[1].hp <= 0) return false;
        const NusisamaState before = s;
        log.clear();
        BattleEmulator::Main(&s.rngPosition, 1, actions, s.players,
            &log, seed, nullptr, nullptr, -1, &s.nowState);
        if (!effective(before, s, actions[i]) || s.resultPosition + log.position > replay.position) return false;
        for (int j = 0; j < log.position; ++j)
            if (!sameRecord(log, j, replay, s.resultPosition + j)) return false;
        s.resultPosition += log.position;
    }
    return SameState(s, expected) && s.resultPosition == replay.position;
}

const char *NusisamaSearch::VariantName(int v) {
    return Configs[v >= 0 && v < VariantCount() ? v : DefaultVariant].name;
}
int NusisamaSearch::VariantCount() { return int(std::size(Configs)); }

NusisamaResult NusisamaSearch::Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength, int budgetMs, int variant, int maxTotalTurns) {
    const auto start = Clock::now();
    const auto budget = std::chrono::microseconds(int64_t(std::max(0, budgetMs)) * 1000);
    const auto reserve = std::min(std::chrono::microseconds(8000), budget / 25);
    const int horizon = std::clamp(maxTotalTurns < 0 ? prefixLength + 40 : maxTotalTurns, 0, 90);
    Context ctx{initial, seed, horizon, start, start + budget - reserve};
    ctx.result.actions.fill(-1);
    if (!initial || !prefix || prefixLength < 0 || prefixLength > 90) return ctx.result;
    for (int i = 0; i < prefixLength; ++i) if (prefix[i] == 0 || prefix[i] == -1) return ctx.result;
    ctx.result.prefixLength = ctx.result.length = prefixLength;
    std::copy_n(prefix, prefixLength, ctx.result.actions.begin());
    Replay(initial, seed, ctx.result.actions.data(), prefixLength, ctx.result.root, ctx.result.replay);
    ctx.result.finalState = ctx.result.root;
    ctx.result.inputValid = true;
    if (ctx.result.root.players[1].hp == 0) {
        ctx.result.victory = ctx.result.replayVerified = true;
        ctx.result.firstVictoryMs = ctx.result.bestVictoryMs = ctx.elapsed();
    } else if (ctx.result.root.players[0].hp > 0 && turnOf(ctx.result.root) == prefixLength &&
            prefixLength < horizon && budgetMs > 0 && !ctx.expired()) {
        const auto &cfg = Configs[variant >= 0 && variant < VariantCount() ? variant : DefaultVariant];
        if (cfg.bestFirst) {
            bestFirst(ctx, cfg);
        } else {
            for (size_t width = cfg.width; width <= 16384 && !ctx.expired(); width *= 4) {
                if (beam(ctx, cfg, width)) break;
                if (cfg.repair) repair(ctx, cfg, width);
                if (cfg.portfolio && !ctx.expired()) beam(ctx, Configs[1], width);
            }
        }
    }
    ctx.result.elapsedMs = ctx.elapsed();
    return ctx.result;
}