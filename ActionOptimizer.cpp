#include "ActionOptimizer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <numeric>
#include <tuple>
#include <vector>

#include "BattleEmulator.h"
#include "lcg.h"

namespace {
using Clock = std::chrono::steady_clock;
constexpr int ActionCapacity = 350;
constexpr int NoPath = -1;

// Compare semantic fields, never padding or a hash alone. The hash is only an
// index into a per-layer transposition table; action history is not state.
auto fields(const Player& p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
                    p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp,
                    p.specialCharge, p.dirtySpecialCharge, p.specialChargeTurn,
                    p.paralysis, p.paralysisLevel, p.paralysisTurns,
                    p.SpecialMedicineCount, p.defence, p.sleeping, p.sleepingTurn,
                    p.BuffLevel, p.BuffTurns, p.hasMagicMirror, p.MagicMirrorTurn,
                    p.AtkBuffLevel, p.AtkBuffTurn, p.TensionLevel, p.rage,
                    p.SageElixirCount, p.ElfinElixirCount, p.MagicWaterCount,
                    p.speedTurn, p.speedLevel, p.PoisonTurn, p.PoisonEnable,
                    p.SpecialAntidoteCount, p.acrobaticStar, p.acrobaticStarTurn);
}

struct Node {
    Player players[2];
    uint64_t state = 0;
    int rngPosition = 1;
    int resultPosition = 0;
    int path = NoPath;
    int action = -1;
    double score = 0;
    uint64_t hash = 0;
};

struct Path {
    int parent;
    int action;
};

uint64_t mix(uint64_t h, int v) {
    return (h ^ static_cast<uint32_t>(v)) * 0x100000001b3ULL;
}

uint64_t stateHash(const Node& node) {
    uint64_t h = mix(0xcbf29ce484222325ULL, node.rngPosition);
    h = mix(h, static_cast<int>(node.state));
    h = mix(h, static_cast<int>(node.state >> 32));
    for (const Player& p : node.players) {
        h = mix(h, p.hp);
        h = mix(h, p.mp);
        h = mix(h, p.def);
        h = mix(h, p.SpecialMedicineCount);
        h = mix(h, p.BuffLevel);
        h = mix(h, p.BuffTurns);
        h = mix(h, p.specialChargeTurn);
        h = mix(h, p.acrobaticStarTurn);
        h = mix(h, static_cast<int>(p.specialCharge)
                   | (static_cast<int>(p.dirtySpecialCharge) << 1)
                   | (static_cast<int>(p.acrobaticStar) << 2)
                   | (static_cast<int>(p.rage) << 3)
                   | (static_cast<int>(p.sleeping) << 4)
                   | (static_cast<int>(p.paralysis) << 5));
    }
    return h ^ (h >> 32);
}

bool sameState(const Node& a, const Node& b) {
    return a.rngPosition == b.rngPosition && a.state == b.state
           && fields(a.players[0]) == fields(b.players[0])
           && fields(a.players[1]) == fields(b.players[1]);
}

int candidateActions(const Player& p, int (&actions)[10]) {
    int n = 0;
    actions[n++] = BattleEmulator::ATTACK_ALLY;
    actions[n++] = BattleEmulator::DRAGON_SLASH;
    actions[n++] = BattleEmulator::DEFENCE;
    // In particular, never exploit FLEE's pre-status skip.
    if (!p.sleeping && !p.paralysis) actions[n++] = BattleEmulator::FLEE_ALLY;
    if (p.SpecialMedicineCount > 0) actions[n++] = BattleEmulator::SPECIAL_MEDICINE;
    // These costs come from this branch's BattleEmulator, not a tactical filter.
    if (p.mp >= 2) actions[n++] = BattleEmulator::HEAL;
    if (p.mp >= 3) {
        actions[n++] = BattleEmulator::CRACK_ALLY;
        actions[n++] = BattleEmulator::WOOSH_ALLY;
    }
    if (p.mp >= 8) actions[n++] = BattleEmulator::CRACKLE;
    if (p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn > 0
        && !p.sleeping && !p.paralysis) {
        actions[n++] = BattleEmulator::ACROBATIC_STAR;
    }
    return n;
}

// Different resource valuations preserve both critical-heavy short routes and
// routes which spend an action on healing/Acrobatic Star. They order a bounded
// beam, not the final objective and not the world's action legality.
double score(const Node& node, int pass) {
    const Player& a = node.players[0];
    const Player& e = node.players[1];
    constexpr double healthWeights[] = {0.35, 0.65, 0.15, 0.45};
    constexpr double starWeights[] = {5.0, 8.0, 2.0, 11.0};
    const int style = pass % 4;
    const double resourceScale = std::min(1.0, e.hp / std::max(1.0, double(a.defaultATK) * 2.0));
    double value = e.hp;
    value -= resourceScale * (healthWeights[style] * a.hp + 0.6 * a.mp
                             + 1.5 * a.SpecialMedicineCount);
    if (a.acrobaticStar) value -= resourceScale * starWeights[style] * std::max(0, a.acrobaticStarTurn);
    if (a.specialCharge && !a.dirtySpecialCharge && a.specialChargeTurn > 0)
        value -= resourceScale * starWeights[style] * 2.0;
    // Stable, small tie diversity; no seed/prefix identification or answer data.
    value += double((node.hash >> (style * 8)) & 255) * 0.0001;
    return value;
}
} // namespace

Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns,
                                     int maxGenerations, int actions[350], int seedOffset) {
    const auto started = Clock::now();
    // Leave time for the final independent replay and the caller's dump replay.
    const auto deadline = started + std::chrono::milliseconds(1490);
    (void)turns;
    (void)maxGenerations;
    (void)seedOffset;

    Genome best{};
    best.AllyPlayer = players[0];
    best.EnemyPlayer = players[1];
    best.turn = std::numeric_limits<int>::max();
    best.fitness = std::numeric_limits<int>::max();
    std::fill(std::begin(best.actions), std::end(best.actions), -1);

    int prefixLength = 0;
    while (prefixLength < ActionCapacity && actions[prefixLength] != -1) {
        best.actions[prefixLength] = actions[prefixLength];
        ++prefixLength;
    }
    // The caller's array, not a separately supplied turn count, defines prefix.
    std::array<int, ActionCapacity> gene;
    gene.fill(-1);
    std::copy_n(actions, prefixLength, gene.begin());

    Node root;
    root.players[0] = players[0];
    root.players[1] = players[1];
    BattleResult log;
    lcg::init(seed, true);
    if (players[1].hp != 0 && players[0].hp > 0 && prefixLength > 0) {
        BattleEmulator::Main(&root.rngPosition, prefixLength, gene.data(), root.players,
                             &log, seed, nullptr, nullptr, -1, &root.state);
        root.resultPosition = log.position;
    }

    const auto exactCandidate = [&](const Node& candidate, int length,
                                    const std::vector<Path>& paths) {
        std::array<int, ActionCapacity> full;
        full.fill(-1);
        std::copy_n(actions, prefixLength, full.begin());
        if (length > prefixLength) {
            full[length - 1] = candidate.action;
            int at = length - 2;
            for (int id = candidate.path; id != NoPath; id = paths[id].parent)
                full[at--] = paths[id].action;
        }
        Player replay[2] = {players[0], players[1]};
        int cursor = 1;
        uint64_t state = 0;
        BattleResult result;
        lcg::init(seed, true);
        // One whole-sequence call from the original world, independently of
        // incremental search nodes. This branch logs at most 875 rows in 350
        // turns (two/one alternating enemy actions plus the ally action).
        if (length > 0 && replay[0].hp > 0 && replay[1].hp != 0) {
            BattleEmulator::Main(&cursor, length, full.data(), replay, &result, seed,
                                 nullptr, nullptr, -1, &state);
        }
        const int count = result.position;
        const int played = static_cast<int>((state >> 12) & 0xfffff);
        if (replay[1].hp != 0 || count != candidate.resultPosition
            || cursor != candidate.rngPosition || state != candidate.state
            || fields(replay[0]) != fields(candidate.players[0])
            || fields(replay[1]) != fields(candidate.players[1])) return;
        if (best.Initialized && count >= best.fitness) return;
        best.Initialized = true;
        best.AllyPlayer = replay[0];
        best.EnemyPlayer = replay[1];
        best.position = cursor; // Genome.position remains the RNG cursor.
        best.state = state;
        best.fitness = count;   // Only a fresh BattleResult row count ranks wins.
        best.turn = played + 1;
        best.processed = played;
        std::copy(full.begin(), full.end(), std::begin(best.actions));
    };

    if (root.players[1].hp == 0) {
        exactCandidate(root, prefixLength, {});
        return best;
    }
    if (root.players[0].hp <= 0 || prefixLength >= ActionCapacity) return best;

    std::vector<Path> paths;
    std::vector<Node> beam, next;
    std::vector<int> table;
    std::vector<int> order;
    std::vector<uint64_t> groupKeys;
    std::vector<int> groupCounts;
    uint64_t transitions = 0;
    bool expired = false;
    // A narrow first pass establishes an incumbent quickly; wider, differently
    // ordered passes improve it under ONE shared wall-clock deadline.
    for (int pass = 0; pass < 10 && !expired && Clock::now() < deadline; ++pass) {
        const int widths[] = {128, 512, 2048, 4096, 8192, 16384, 32768, 32768};
        const int width = widths[std::min(pass, 7)];
        beam.clear();
        next.clear();
        paths.clear();
        beam.reserve(width);
        next.reserve(width * 10);
        paths.reserve(width * 32);
        size_t tableSize = 1;
        while (tableSize < static_cast<size_t>(width * 32)) tableSize <<= 1;
        table.resize(tableSize);
        groupKeys.resize(tableSize);
        groupCounts.resize(tableSize);
        beam.push_back(root);

        for (int depth = prefixLength; depth < ActionCapacity && !beam.empty(); ++depth) {
            if (Clock::now() >= deadline) { expired = true; break; }
            next.clear();
            std::fill(table.begin(), table.end(), -1);
            for (const Node& current : beam) {
                // Every subsequent living-battle turn adds at least one row.
                if (best.Initialized && current.resultPosition + 1 >= best.fitness) continue;
                int possible[10];
                const int n = candidateActions(current.players[0], possible);
                for (int a = 0; a < n; ++a) {
                    if ((++transitions & 255) == 0 && Clock::now() >= deadline) {
                        expired = true;
                        break;
                    }
                    Node child = current;
                    child.action = possible[a];
                    gene[depth] = child.action;
                    log.clear();
                    BattleEmulator::Main(&child.rngPosition, 1, gene.data(), child.players,
                                         &log, seed, nullptr, nullptr, -1, &child.state);
                    child.resultPosition += log.position;
                    // A first-moving Drain Magic must not buy an unaffordable
                    // spell through the emulator's intentionally permissive API.
                    bool insufficientMP = child.players[0].mp < 0;
                    for (int r = 0; r < log.position; ++r)
                        insufficientMP = insufficientMP || log.amp[r] < 0;
                    if (insufficientMP) continue;
                    if (child.players[1].hp == 0) {
                        if (!best.Initialized || child.resultPosition < best.fitness)
                            exactCandidate(child, depth + 1, paths);
                        continue;
                    }
                    if (child.players[0].hp <= 0) continue;
                    if (best.Initialized && child.resultPosition + 1 >= best.fitness) continue;
                    child.hash = stateHash(child);
                    child.score = score(child, pass);
                    size_t slot = child.hash & (tableSize - 1);
                    while (table[slot] != -1) {
                        Node& other = next[table[slot]];
                        if (child.hash == other.hash && sameState(child, other)) break;
                        slot = (slot + 1) & (tableSize - 1);
                    }
                    if (table[slot] == -1) {
                        table[slot] = static_cast<int>(next.size());
                        next.push_back(child);
                    } else if (child.resultPosition < next[table[slot]].resultPosition) {
                        next[table[slot]] = child;
                    }
                }
                if (expired) break;
            }
            if (expired) break;
            const auto better = [&](int ai, int bi) {
                const Node& a = next[ai];
                const Node& b = next[bi];
                if (a.score != b.score) return a.score < b.score;
                if (a.resultPosition != b.resultPosition) return a.resultPosition < b.resultPosition;
                return a.hash < b.hash;
            };
            order.resize(next.size());
            std::iota(order.begin(), order.end(), 0);
            std::sort(order.begin(), order.end(), better);
            std::fill(groupKeys.begin(), groupKeys.end(), 0);
            beam.clear();
            const int perGroup = 2 + width / 2048;
            for (int index : order) {
                Node& node = next[index];
                const uint64_t key = 1 + static_cast<uint64_t>(node.rngPosition)
                    + ((node.state & 0xfff) << 13)
                    + (uint64_t(node.players[0].specialCharge) << 25)
                    + (uint64_t(node.players[0].acrobaticStar) << 26);
                size_t slot = (key * 0x9e3779b97f4a7c15ULL >> 32) & (tableSize - 1);
                while (groupKeys[slot] && groupKeys[slot] != key) slot = (slot + 1) & (tableSize - 1);
                if (!groupKeys[slot]) {
                    groupKeys[slot] = key;
                    groupCounts[slot] = 0;
                }
                if (groupCounts[slot] >= perGroup) continue;
                ++groupCounts[slot];
                paths.push_back({node.path, node.action});
                node.path = static_cast<int>(paths.size()) - 1;
                beam.push_back(node);
                if (beam.size() == static_cast<size_t>(width)) break;
            }
        }
    }
    return best;
}

void ActionOptimizer::updateCompromiseScore(Genome&) {}