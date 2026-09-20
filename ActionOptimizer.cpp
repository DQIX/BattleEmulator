#include "ActionOptimizer.h"
#include "BattleEmulator.h"
#include "lcg.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <numeric>
#include <tuple>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr int Capacity = 350;
constexpr int NoPath = -1;

// Compare fields, not padding bytes. Hash collisions never discard a distinct state.
auto playerKey(const Player& p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis, p.paralysisLevel,
        p.paralysisTurns, p.SpecialMedicineCount, p.defence, p.sleeping,
        p.sleepingTurn, p.BuffLevel, p.BuffTurns, p.hasMagicMirror,
        p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn, p.TensionLevel,
        p.rage, p.SageElixirCount, p.ElfinElixirCount, p.MagicWaterCount,
        p.speedTurn, p.speedLevel, p.PoisonTurn, p.PoisonEnable,
        p.SpecialAntidoteCount, p.acrobaticStar, p.acrobaticStarTurn, p.rageTurns);
}

struct Node {
    Player players[2];
    uint64_t state = 0;
    int rngCursor = 1;
    int events = 0;
    int depth = 0;
    int path = NoPath;
    int action = 0;
    double rank = 0;
};

struct Path {
    int parent;
    int action;
};

bool sameState(const Node& a, const Node& b) {
    return a.rngCursor == b.rngCursor && a.state == b.state && a.depth == b.depth
        && playerKey(a.players[0]) == playerKey(b.players[0])
        && playerKey(a.players[1]) == playerKey(b.players[1]);
}

uint64_t mix(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

uint64_t stateHash(const Node& n) {
    const Player& p = n.players[0];
    const Player& e = n.players[1];
    // A subset is sufficient for hashing: sameState checks *all* fields.
    uint64_t h = mix(n.state ^ (uint64_t(n.rngCursor) << 32));
    h ^= mix(uint32_t(p.hp) | (uint64_t(uint32_t(e.hp)) << 32));
    h ^= mix(uint32_t(p.mp) | (uint64_t(uint32_t(p.SpecialMedicineCount)) << 32));
    h ^= mix(uint32_t(p.specialChargeTurn) | (uint64_t(uint32_t(p.acrobaticStarTurn)) << 32));
    h ^= mix(uint32_t(e.rageTurns) | (uint64_t(p.specialCharge | (p.acrobaticStar << 1)
        | (e.rage << 2)) << 32));
    return h;
}

int candidates(const Player& p, std::array<int, 9>& out) {
    int size = 0;
    out[size++] = BattleEmulator::ATTACK_ALLY;
    // These are the existing actor's actions, not the emulator's entire enum.
    if (p.sleeping || p.paralysis) return size;
    out[size++] = BattleEmulator::DRAGON_SLASH;
    out[size++] = BattleEmulator::DEFENCE;
    // Never use FLEE's pre-action skip while incapacitated.
    out[size++] = BattleEmulator::FLEE_ALLY;
    if (p.SpecialMedicineCount > 0) out[size++] = BattleEmulator::SPECIAL_MEDICINE;
    // Costs are taken from BattleEmulator::callAttackFun.
    if (p.mp >= 2) out[size++] = BattleEmulator::HEAL;
    if (p.mp >= 3) out[size++] = BattleEmulator::CRACK_ALLY;
    if (p.mp >= 8) out[size++] = BattleEmulator::CRACKLE;
    if (p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn > 0)
        out[size++] = BattleEmulator::ACROBATIC_STAR;
    return size;
}

double rankState(const Node& n, int pass) {
    const Player& p = n.players[0];
    const Player& e = n.players[1];
    const double hit = std::max(1.0, p.atk * 0.5 - e.def * 0.25);
    const double incoming = std::max(1.0, e.atk * 0.5 - p.def * 0.25);
    const double healthWeight = pass % 3 == 0 ? 0.55 : (pass % 3 == 1 ? 0.85 : 0.35);
    const double star = p.acrobaticStar ? std::max(0, p.acrobaticStarTurn) : 0;
    const double charge = p.specialCharge && !p.dirtySpecialCharge
        && p.specialChargeTurn > 0 ? 1.0 : 0.0;
    // Ordering only. No heuristic HP, damage, or RNG value enters the world.
    return e.hp - healthWeight * p.hp - 0.7 * p.mp
        - hit * 0.12 * std::min(p.SpecialMedicineCount, 4)
        - (hit * 0.25 + incoming * 0.30) * star - hit * 0.9 * charge;
}

// BattleResult.position is an event count; rngCursor is deliberately separate.
bool advance(Node& n, int action, std::array<int, Capacity>& scratch,
             BattleResult& trace, uint64_t seed) {
    // Leave space for the sentinel and for one turn in the existing 5000-entry LCG cache.
    if (n.depth >= Capacity - 1 || n.rngCursor > 4900) return false;
    scratch[n.depth] = action;
    trace.clear();
    BattleEmulator::Main(&n.rngCursor, 1, scratch.data(), n.players, &trace,
        seed, nullptr, nullptr, -1, &n.state);
    n.events += trace.position;
    ++n.depth;
    return true;
}

Genome replay(const Player original[2], uint64_t seed,
              const std::array<int, Capacity>& actions, int length,
              BattleResult& trace) {
    Genome result{};
    result.turn = std::numeric_limits<int>::max();
    std::copy(actions.begin(), actions.end(), result.actions);
    Player players[2] = {original[0], original[1]};
    int cursor = 1;
    uint64_t state = 0;
    trace.clear();
    lcg::init(seed, true);
    if (length > 0)
        BattleEmulator::Main(&cursor, length, actions.data(), players, &trace,
            seed, nullptr, nullptr, -1, &state);
    result.AllyPlayer = players[0];
    result.EnemyPlayer = players[1];
    result.state = state;
    result.position = cursor;
    result.fitness = trace.position;
    result.processed = static_cast<int>((state >> 12) & 0xfffff);
    result.Initialized = players[1].hp == 0 && result.processed == length;
    if (result.Initialized) result.turn = length + 1;
    return result;
}
}

Genome ActionOptimizer::RunAlgorithm(const Player original[2], uint64_t seed,
                                     const int actions[350], int budgetMs) {
    const auto started = Clock::now();
    // Reserve a small part of the *same* budget for final replay and output cleanup.
    const auto deadline = started + std::chrono::milliseconds(std::max(0, budgetMs - 10));
    Genome best{};
    best.turn = std::numeric_limits<int>::max();
    best.AllyPlayer = original[0];
    best.EnemyPlayer = original[1];
    std::fill(std::begin(best.actions), std::end(best.actions), -1);

    std::array<int, Capacity> prefix;
    prefix.fill(-1);
    int prefixLength = 0;
    while (prefixLength < Capacity && actions[prefixLength] != -1) {
        prefix[prefixLength] = actions[prefixLength];
        best.actions[prefixLength] = actions[prefixLength];
        ++prefixLength;
    }
    if (prefixLength == Capacity || seed == 0) return best;
    lcg::init(seed, true);
    Node root;
    root.players[0] = original[0];
    root.players[1] = original[1];
    // Keep the large trace off the nested native/WebAssembly call stack.
    const auto traceStorage = std::make_unique<BattleResult>();
    BattleResult& trace = *traceStorage;
    std::array<int, Capacity> scratch = prefix;
    for (int i = 0; i < prefixLength; ++i) {
        // Do not truncate an impossible prefix, or restart from turn one.
        if (root.players[0].hp == 0 || root.players[1].hp == 0
            || !advance(root, prefix[i], scratch, trace, seed)) return best;
    }
    if (root.players[1].hp == 0) return replay(original, seed, prefix, prefixLength, trace);
    if (root.players[0].hp == 0) return best;

    int bestEvents = std::numeric_limits<int>::max();
    std::vector<Path> paths;
    std::vector<Node> beam, next;
    std::vector<int> slots, order;
    uint64_t expanded = 0;
    bool timedOut = false;

    // Narrow passes quickly establish an incumbent; wider passes retain alternatives
    // that spend a turn on healing, a charge, or a different natural RNG transition.
    for (int pass = 0; !timedOut && Clock::now() < deadline; ++pass) {
        const int width = std::min(8192, 256 << std::min(pass, 5));
        beam.clear();
        beam.push_back(root);
        paths.clear();
        size_t tableSize = 1;
        while (tableSize < static_cast<size_t>(width) * 24) tableSize <<= 1;
        slots.resize(tableSize);
        next.reserve(static_cast<size_t>(width) * 9);

        while (!beam.empty() && !timedOut) {
            next.clear();
            std::fill(slots.begin(), slots.end(), -1);
            for (const Node& parent : beam) {
                if ((expanded++ & 63) == 0 && Clock::now() >= deadline) {
                    timedOut = true;
                    break;
                }
                if (parent.events + 1 >= bestEvents) continue;
                std::array<int, 9> options;
                const int count = candidates(parent.players[0], options);
                for (int i = 0; i < count; ++i) {
                    Node child = parent;
                    child.action = options[i];
                    if (!advance(child, child.action, scratch, trace, seed)) continue;
                    if (child.players[1].hp == 0 && child.events < bestEvents) {
                        std::array<int, Capacity> complete = prefix;
                        complete[child.depth - 1] = child.action;
                        int turn = child.depth - 2;
                        for (int p = parent.path; p != NoPath; p = paths[p].parent)
                            complete[turn--] = paths[p].action;
                        if (turn != prefixLength - 1) continue;
                        const Genome verified = replay(original, seed, complete, child.depth, trace);
                        if (verified.Initialized && verified.fitness == child.events
                            && verified.position == child.rngCursor && verified.state == child.state
                            && playerKey(verified.AllyPlayer) == playerKey(child.players[0])
                            && playerKey(verified.EnemyPlayer) == playerKey(child.players[1])) {
                            best = verified;
                            bestEvents = verified.fitness;
                        }
                        continue;
                    }
                    if (child.players[0].hp == 0 || child.players[1].hp == 0
                        || child.events + 1 >= bestEvents) continue;
                    child.rank = rankState(child, pass);
                    size_t slot = stateHash(child) & (tableSize - 1);
                    while (slots[slot] != -1 && !sameState(next[slots[slot]], child))
                        slot = (slot + 1) & (tableSize - 1);
                    if (slots[slot] == -1) {
                        slots[slot] = static_cast<int>(next.size());
                        next.push_back(child);
                    } else if (child.events < next[slots[slot]].events) {
                        next[slots[slot]] = child;
                    }
                }
            }
            if (timedOut || next.empty()) break;
            order.resize(next.size());
            std::iota(order.begin(), order.end(), 0);
            const size_t keep = std::min(next.size(), static_cast<size_t>(width));
            auto less = [&](int a, int b) {
                if (next[a].rank != next[b].rank) return next[a].rank < next[b].rank;
                return a < b;
            };
            if (keep < order.size())
                std::nth_element(order.begin(), order.begin() + keep, order.end(), less);
            order.resize(keep);
            std::sort(order.begin(), order.end(), less);
            beam.clear();
            for (int index : order) {
                Node& n = next[index];
                paths.push_back({n.path, n.action});
                n.path = static_cast<int>(paths.size()) - 1;
                beam.push_back(n);
            }
        }
        // No remaining suffix is possible within the input/LCG capacity.
        if (pass == 0 && paths.empty() && bestEvents == std::numeric_limits<int>::max()) break;
    }

    if (best.Initialized) {
        std::array<int, Capacity> complete;
        std::copy(std::begin(best.actions), std::end(best.actions), complete.begin());
        best = replay(original, seed, complete, best.processed, trace);
    }
    return best;
}

void ActionOptimizer::updateCompromiseScore(Genome&) {}