#include "ReokonnSearch.h"
#include "lcg.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <numeric>
#include <queue>
#include <tuple>
#include <vector>

// Search techniques adapted from the supplied bilyouma-gpt6-pro and
// yo2-gpt6-pro branches. No enemy data, action set or world rules are imported.
namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoPath = UINT32_MAX;
constexpr int Actions[] = {
    BattleEmulator::ATTACK_ALLY, BattleEmulator::DRAGON_SLASH,
    BattleEmulator::DEFENCE, BattleEmulator::FLEE_ALLY,
    BattleEmulator::MEDICINAL_HERBS, BattleEmulator::HEAL,
    BattleEmulator::CRACK_ALLY
};

auto fields(const Player &p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis,
        p.SpecialMedicineCount, p.defence, p.sleeping, p.sleepingTurn,
        p.BuffLevel, p.BuffTurns, p.TensionLevel, p.rage, p.MagicWaterCount,
        p.speedLevel, p.PoisonTurn, p.PoisonEnable, p.SpecialAntidoteCount,
        p.acrobaticStar, p.acrobaticStarTurn, p.medicinal_herbs_count);
}

uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

uint64_t fingerprint(const ReokonnState &s) {
    // All semantic fields are compared after a hash match. Padding may only
    // miss a duplicate, never cause an unequal state to be discarded.
    const auto *bytes = reinterpret_cast<const unsigned char *>(s.players);
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    size_t i = 0;
    for (; i + 8 <= sizeof(s.players); i += 8) {
        uint64_t v;
        std::memcpy(&v, bytes + i, sizeof(v));
        h = (h ^ v) * 0xd6e8feb86659fd93ULL;
    }
    for (; i < sizeof(s.players); ++i) h = (h ^ bytes[i]) * 1099511628211ULL;
    return mix(h ^ mix(s.nowState) ^ mix(uint32_t(s.rngPosition))
                 ^ mix(uint32_t(s.resultPosition)));
}

// Heuristic units are hundredths of enemy HP, not emulator state.
struct Config {
    const char *name;
    int hp, danger, mp, herbs, firstWidth, quota, block;
    bool repair, portfolio;
    int bestFirstWeight = 0;
    int lookahead = 0;
    int blend = 0;
};
constexpr Config Configs[] = {
    {"portfolio-repair", 24, 5, 18, 24, 128, 0, 1, true, true},
    {"damage-greedy",     8, 1,  5,  6, 128, 0, 1, false,false},
    {"balanced",         30, 8, 18, 24, 128, 0, 1, false,false},
    {"survival",         65,15, 30, 60, 128, 0, 1, false,false},
    {"resource",         25, 5, 60,100, 128, 0, 1, false,false},
    {"balanced-repair",  30, 8, 18, 24, 128, 0, 1, true, false},
    {"greedy-repair",     8, 1,  5,  6,  64, 0, 1, true, false},
    {"resource-quota",   24, 5, 18, 24, 128, 4, 1, true, true},
    {"compact-best-first-700", 24,5,18,24,128,0,1,false,false,700},
    {"compact-best-first-450", 24,5,18,24,128,0,1,false,false,450},
    {"two-ply",          24, 5, 18, 24,  64, 0, 2, true, false},
    {"pure-offence",     12, 0,  0,  0,  64, 0, 1, false,false},
    {"micro-portfolio",  24, 5, 18, 24,  32, 0, 1, true, true},
    {"wide-portfolio",   24, 5, 18, 24, 512, 0, 1, true, true},
    {"attack-preview-half",30,8,18,24,128,0,1,false,false,0,50},
    {"attack-preview-full",30,8,18,24,128,0,1,false,false,0,100},
    {"greedy-attack-preview",8,1,5,6,64,0,1,false,false,0,75},
    {"dual-value-beam",     8,1,5,6,128,0,1,false,false,0,0,4},
    {"greedy-preview-light",8,1,5,6,64,0,1,false,false,0,25},
    {"greedy-preview-heavy",8,1,5,6,64,0,1,false,false,0,150},
    {"greedy-preview-wide", 8,1,5,6,256,0,1,false,false,0,75},
    {"greedy-preview-repair",8,1,5,6,64,0,1,true,false,0,75},
    {"greedy-preview-blend",8,1,5,6,64,0,1,false,false,0,75,4},
    {"offensive-preview", 4,1,5,6,64,0,1,false,false,0,75}
};

int64_t value(const ReokonnState &s, const Config &cfg) {
    const auto &p = s.players[0];
    int64_t v = -int64_t(s.players[1].hp) * 100;
    v += int64_t(p.hp) * cfg.hp + int64_t(std::max(0, p.mp)) * cfg.mp;
    v += int64_t(p.medicinal_herbs_count) * cfg.herbs;
    const int danger = std::max(0, std::min(p.maxHp / 2, 24) - p.hp);
    v -= int64_t(danger) * danger * cfg.danger;
    if (p.sleeping || p.paralysis) v -= 1500;
    if (p.PoisonEnable) v -= 400;
    return v;
}

struct Link { uint32_t parent; uint8_t action; };
struct Node {
    ReokonnState state{};
    uint64_t hash = 0;
    int64_t score = 0;
    uint32_t path = NoPath;
    uint8_t action = 0;
};

std::vector<int32_t> materialize(const Node &n, const std::vector<Link> &links) {
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
    ReokonnResult result{};
    std::vector<int32_t> bestSuffix;
    BattleResult transitionLog{}, verificationLog{};
    int bestPosition = INT32_MAX;

    double elapsed() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    bool expired() const { return Clock::now() >= deadline; }
    void step(ReokonnState &s, int action) {
        const int32_t one[2] = {action, -1};
        transitionLog.clear();
        BattleEmulator::Main(&s.rngPosition, 1, one, s.players, &transitionLog,
            seed, nullptr, nullptr, -1, &s.nowState, true);
        s.resultPosition += transitionLog.position;
        ++result.expanded;
    }
    void consider(const Node &n, const std::vector<Link> &links) {
        if (n.state.players[1].hp != 0 || n.state.resultPosition > bestPosition || expired()) return;
        const auto suffix = materialize(n, links);
        if (n.state.resultPosition == bestPosition &&
            (suffix.size() > bestSuffix.size() ||
            (suffix.size() == bestSuffix.size() &&
             n.state.players[0].hp <= result.finalState.players[0].hp))) return;
        auto actions = result.actions;
        const int length = result.prefixLength + int(suffix.size());
        std::copy(suffix.begin(), suffix.end(), actions.begin() + result.prefixLength);
        std::fill(actions.begin() + length, actions.end(), -1);
        ReokonnState exact;
        if (!ReokonnSearch::Replay(initial, seed, actions.data(), length, exact, verificationLog)
            || !ReokonnSearch::SameState(exact, n.state)) {
            ++result.rejectedReplay;
            return;
        }
        if (expired()) return; // A candidate found after the deadline is never adopted.
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
    bool insert(const Node &n, const std::vector<Node> &nodes, ReokonnResult &stats) {
        size_t i = n.hash & mask;
        while (slots[i] != NoPath) {
            const auto &other = nodes[slots[i]];
            if (n.hash == other.hash) {
                if (ReokonnSearch::SameState(n.state, other.state)) {
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
    return (p.hp >= p.maxHp / 2 ? 1 : 0) + (p.mp >= 3 ? 2 : 0);
}

// Returns true only if the traversal was unpruned (so wider repeats cannot help).
bool beam(Context &ctx, const Config &cfg, size_t width,
          const std::vector<int32_t> &anchor = {}) {
    std::vector<Node> current, next, candidates;
    std::vector<Link> links;
    std::vector<uint32_t> order, chosen, group;
    std::vector<uint8_t> flags;
    StateSet seen;
    current.reserve(width); next.reserve(width); candidates.reserve(width * 7);
    links.reserve(width * size_t(std::max(1, ctx.maxTurns - ctx.result.prefixLength)));
    Node root; root.state = ctx.result.root;
    for (int a : anchor) {
        if (ctx.expired()) return false;
        if (!ReokonnSearch::Legal(root.state.players[0], a) ||
            !ReokonnSearch::Safe(root.state.players[0], a)) return false;
        ctx.step(root.state, a);
        links.push_back({root.path, uint8_t(a)});
        root.path = uint32_t(links.size() - 1);
    }
    current.push_back(root);
    bool pruned = false;
    for (int turn = ctx.result.prefixLength + int(anchor.size()) + 1;
         turn <= ctx.maxTurns && !current.empty(); ++turn) {
        if (ctx.expired()) return false;
        candidates.clear(); seen.reset(current.size() * 7);
        for (const Node &parent : current) {
            if (parent.state.resultPosition + 1 > ctx.bestPosition) continue;
            for (int action : Actions) {
                if (!ReokonnSearch::Legal(parent.state.players[0], action)) continue;
                if (!ReokonnSearch::Safe(parent.state.players[0], action)) {
                    ++ctx.result.unsafeFleeSkipped; continue;
                }
                if ((ctx.result.expanded & 127) == 0 && ctx.expired()) return false;
                Node n; n.state = parent.state; n.path = parent.path; n.action = uint8_t(action);
                ctx.step(n.state, action);
                if (n.state.resultPosition > ctx.bestPosition) continue;
                if (n.state.players[1].hp == 0) { ctx.consider(n, links); continue; }
                if (n.state.players[0].hp == 0 || n.state.resultPosition + 1 > ctx.bestPosition) continue;
                n.hash = fingerprint(n.state);
                if (!seen.insert(n, candidates, ctx.result)) continue;
                n.score = value(n.state, cfg);
                if (cfg.lookahead && turn < ctx.maxTurns) {
                    // A real-emulator forecast changes only the ordering score;
                    // the candidate retains its exact, unmodified current state.
                    auto forecast = n.state;
                    ctx.step(forecast, BattleEmulator::ATTACK_ALLY);
                    n.score += int64_t(n.state.players[1].hp - forecast.players[1].hp) * cfg.lookahead;
                    if (forecast.players[0].hp == 0) n.score -= cfg.hp * 12;
                }
                candidates.push_back(n);
            }
        }
        size_t layerWidth = width;
        if (cfg.block == 2 && (turn - ctx.result.prefixLength - int(anchor.size())) % 2)
            layerWidth *= 7;
        const size_t keep = std::min(layerWidth, candidates.size());
        if (!keep) break;
        if (ctx.expired()) return false;
        pruned |= keep < candidates.size();
        order.resize(candidates.size()); std::iota(order.begin(), order.end(), 0);
        const auto better = [&](uint32_t a, uint32_t b) {
            if (candidates[a].score != candidates[b].score)
                return candidates[a].score > candidates[b].score;
            if (candidates[a].state.resultPosition != candidates[b].state.resultPosition)
                return candidates[a].state.resultPosition < candidates[b].state.resultPosition;
            return candidates[a].hash < candidates[b].hash;
        };
        chosen.clear(); flags.assign(candidates.size(), 0);
        if (cfg.blend && keep < candidates.size()) {
            group = order;
            const size_t k = keep / cfg.blend;
            const auto safer = [&](uint32_t a, uint32_t b) {
                const auto va = value(candidates[a].state, Configs[3]);
                const auto vb = value(candidates[b].state, Configs[3]);
                return va != vb ? va > vb : candidates[a].hash < candidates[b].hash;
            };
            std::nth_element(group.begin(), group.begin() + k, group.end(), safer);
            for (size_t i = 0; i < k; ++i) { chosen.push_back(group[i]); flags[group[i]] = 1; }
        }
        if (cfg.quota && keep < candidates.size()) {
            const size_t quota = std::max<size_t>(1, keep / (4 * cfg.quota));
            for (int p = 0; p < 4; ++p) {
                group.clear();
                for (uint32_t i = 0; i < candidates.size(); ++i)
                    if (phase(candidates[i]) == p) group.push_back(i);
                const size_t k = std::min(quota, group.size());
                if (k < group.size()) std::nth_element(group.begin(), group.begin() + k, group.end(), better);
                for (size_t i = 0; i < k; ++i) { chosen.push_back(group[i]); flags[group[i]] = 1; }
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
    for (int tail : {3, 5, 7, 9}) {
        if (ctx.expired()) return;
        const size_t cut = ctx.bestSuffix.size() > size_t(tail) ? ctx.bestSuffix.size() - tail : 0;
        const std::vector<int32_t> anchor(ctx.bestSuffix.begin(), ctx.bestSuffix.begin() + cut);
        beam(ctx, cfg, std::min<size_t>(width, 2048), anchor);
    }
}

void bestFirst(Context &ctx, const Config &cfg) {
    struct Entry {
        int64_t key; uint32_t id;
        bool operator<(const Entry &b) const { return key != b.key ? key > b.key : id > b.id; }
    };
    constexpr size_t limit = 262144;
    std::priority_queue<Entry> open;
    std::vector<Node> nodes; nodes.reserve(limit);
    std::vector<Link> links; links.reserve(limit);
    StateSet seen; seen.reset(limit);
    Node root; root.state = ctx.result.root; root.hash = fingerprint(root.state);
    seen.insert(root, nodes, ctx.result); nodes.push_back(root); open.push({0, 0});
    while (!open.empty() && nodes.size() + 7 < limit && !ctx.expired()) {
        const Node parent = nodes[open.top().id]; open.pop();
        const int turn = int((parent.state.nowState >> 12) & 0xfffff);
        if (turn >= ctx.maxTurns || parent.state.resultPosition + 1 > ctx.bestPosition) continue;
        for (int action : Actions) {
            if (!ReokonnSearch::Legal(parent.state.players[0], action)) continue;
            if (!ReokonnSearch::Safe(parent.state.players[0], action)) {
                ++ctx.result.unsafeFleeSkipped; continue;
            }
            Node n; n.state = parent.state; n.path = parent.path; n.action = uint8_t(action);
            ctx.step(n.state, action);
            if (n.state.resultPosition > ctx.bestPosition) continue;
            if (n.state.players[1].hp == 0) { ctx.consider(n, links); continue; }
            if (n.state.players[0].hp == 0 || n.state.resultPosition + 1 > ctx.bestPosition) continue;
            n.hash = fingerprint(n.state);
            if (!seen.insert(n, nodes, ctx.result)) continue;
            const auto id = uint32_t(nodes.size());
            links.push_back({n.path, n.action}); n.path = uint32_t(links.size() - 1);
            const int64_t priority = int64_t(n.state.resultPosition) * cfg.bestFirstWeight - value(n.state, cfg);
            nodes.push_back(n); open.push({priority, id});
        }
    }
}
} // namespace

bool ReokonnSearch::SamePlayer(const Player &a, const Player &b) { return fields(a) == fields(b); }
bool ReokonnSearch::SameState(const ReokonnState &a, const ReokonnState &b) {
    return a.rngPosition == b.rngPosition && a.nowState == b.nowState &&
        a.resultPosition == b.resultPosition && SamePlayer(a.players[0], b.players[0]) &&
        SamePlayer(a.players[1], b.players[1]);
}
bool ReokonnSearch::Legal(const Player &p, int a) {
    switch (a) {
        case BattleEmulator::ATTACK_ALLY:
        case BattleEmulator::DRAGON_SLASH:
        case BattleEmulator::DEFENCE:
        case BattleEmulator::FLEE_ALLY: return true;
        case BattleEmulator::MEDICINAL_HERBS: return p.medicinal_herbs_count >= 1;
        case BattleEmulator::HEAL: return p.mp >= 2;
        case BattleEmulator::CRACK_ALLY: return p.mp >= 3;
        default: return false;
    }
}
bool ReokonnSearch::Safe(const Player &p, int a) {
    return a != BattleEmulator::FLEE_ALLY || (!p.sleeping && !p.paralysis);
}
bool ReokonnSearch::Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, ReokonnState &state, BattleResult &log) {
    if (!initial || !actions || !seed || length < 0 || length >= 350) return false;
    state = ReokonnState{}; state.players[0] = initial[0]; state.players[1] = initial[1];
    log.clear(); lcg::init(seed, true);
    if (length && initial[0].hp > 0 && initial[1].hp > 0)
        BattleEmulator::Main(&state.rngPosition, length, actions, state.players,
            &log, seed, nullptr, nullptr, -1, &state.nowState);
    state.resultPosition = log.position;
    return true;
}
const char *ReokonnSearch::VariantName(int v) {
    return Configs[v >= 0 && v < VariantCount() ? v : DefaultVariant].name;
}
int ReokonnSearch::VariantCount() { return int(std::size(Configs)); }
ReokonnResult ReokonnSearch::Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength, int budgetMs, int variant, int maxTotalTurns) {
    const auto start = Clock::now();
    const auto budget = std::chrono::microseconds(int64_t(std::max(0, budgetMs)) * 1000);
    // The identical reserve is applied to the timed baseline. It includes
    // candidate replay, container destruction and returning the result.
    const auto reserve = budget / 10;
    const int horizon = std::min(99, maxTotalTurns < 0 ? prefixLength + 40 : maxTotalTurns);
    Context ctx{initial, seed, horizon, start, start + budget - reserve};
    ctx.result.actions.fill(-1);
    if (!initial || !prefix || prefixLength < 0 || prefixLength >= 350 || !seed) return ctx.result;
    ctx.result.prefixLength = ctx.result.length = prefixLength;
    std::copy_n(prefix, prefixLength, ctx.result.actions.begin());
    if (!Replay(initial, seed, ctx.result.actions.data(), prefixLength, ctx.result.root, ctx.result.replay))
        return ctx.result;
    ctx.result.inputValid = true;
    ctx.result.finalState = ctx.result.root;
    if (ctx.result.root.players[1].hp == 0) {
        ctx.result.victory = ctx.result.replayVerified = true;
        ctx.result.firstVictoryMs = ctx.result.bestVictoryMs = 0;
    } else if (ctx.result.root.players[0].hp > 0 && prefixLength < horizon && budgetMs > 0) {
        const auto &cfg = Configs[variant >= 0 && variant < VariantCount() ? variant : DefaultVariant];
        if (cfg.bestFirstWeight) bestFirst(ctx, cfg);
        else for (size_t width = cfg.firstWidth; !ctx.expired(); width = std::min<size_t>(32768, width * 4)) {
            if (beam(ctx, cfg, width)) break;
            if (cfg.repair) repair(ctx, cfg, width);
            if (cfg.portfolio && !ctx.expired() && beam(ctx, Configs[1], width)) break;
            if (width == 32768) break;
        }
    }
    ctx.result.elapsedMs = ctx.elapsed();
    return ctx.result;
}