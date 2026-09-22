#include "Ganasadai2Search.h"
#include "BattleEmulator.h"
#include "lcg.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <numeric>
#include <tuple>
#include <vector>

namespace {
using BE = BattleEmulator;
using Clock = std::chrono::steady_clock;
constexpr int NoParent = -1;
constexpr int BucketCount = 60;

// No raw-struct hashing: padding is not state. Equality includes every Player
// field, even fields which happen to be constant in the current input.
#define PLAYER_FIELDS(F) \
    F(hp) F(maxHp) F(atk) F(defaultATK) F(def) F(defaultDEF) \
    F(speed) F(defaultSpeed) F(HealPower) F(mp) F(maxMp) \
    F(specialCharge) F(dirtySpecialCharge) F(specialChargeTurn) \
    F(paralysis) F(paralysisLevel) F(paralysisTurns) F(SpecialMedicineCount) \
    F(defence) F(sleeping) F(sleepingTurn) F(BuffLevel) F(BuffTurns) \
    F(hasMagicMirror) F(MagicMirrorTurn) F(AtkBuffLevel) F(AtkBuffTurn) \
    F(TensionLevel) F(rage) F(SageElixirCount) F(ElfinElixirCount) \
    F(MagicWaterCount) F(InsulateLevel) F(InsulateTurns) F(SpeedLevel) \
    F(SpeedTurns) F(inactive) F(rageTurns) F(BarrierLevel) F(BarrierTurns)

bool samePlayer(const Player& a, const Player& b) {
#define EQ(field) if (a.field != b.field) return false;
    PLAYER_FIELDS(EQ)
#undef EQ
    return true;
}

uint64_t mix(uint64_t v) {
    v = (v ^ (v >> 30)) * 0xbf58476d1ce4e5b9ULL;
    v = (v ^ (v >> 27)) * 0x94d049bb133111ebULL;
    return v ^ (v >> 31);
}

template<class T> uint64_t bits(T value) { return static_cast<uint64_t>(value); }
uint64_t bits(double value) {
    uint64_t out;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

uint64_t hashPlayer(uint64_t h, const Player& p) {
#define HASH(field) h = (h ^ bits(p.field)) * 0x100000001b3ULL;
    PLAYER_FIELDS(HASH)
#undef HASH
    return h;
}
#undef PLAYER_FIELDS

struct Node {
    Player players[2];
    uint64_t state = 0;
    uint64_t hash = 0;
    int rng = 1;
    int depth = 0;                  // already executed turns, including prefix
    int rows = 0;                   // BattleResult.position, NOT the RNG cursor
    int changes = 0;
    int parent = NoParent;
    int action = -1;
    int hashNext = -1;
    float score = 0;
};

struct Slot {
    uint64_t hash = 0;
    int index = -1;
    uint32_t generation = 0;
};

bool sameState(const Node& a, const Node& b) {
    return a.rng == b.rng && a.state == b.state && a.depth == b.depth &&
           samePlayer(a.players[0], b.players[0]) &&
           samePlayer(a.players[1], b.players[1]);
}

void setHash(Node& n) {
    n.hash = mix(hashPlayer(hashPlayer(n.state ^ mix(n.rng), n.players[0]), n.players[1]));
}

bool better(int rows, int changes, int oldRows, int oldChanges) {
    return rows < oldRows || (rows == oldRows && changes < oldChanges);
}

int countChanges(const BattleResult& r) {
    int count = 0;
    for (int i = 0; i < r.position; ++i)
        if (!r.isEnemy[i] && (r.actions[i] & BE::ACTION_EQUIPMENT_CHANGED)) ++count;
    return count;
}

// Check the recorded action too: fixed-prefix status aliases can be normalized
// by Main. This stays outside the emulator and covers only the requested rules.
bool legalRecordedAttacks(const BattleResult& r, const Player& initial, bool victory) {
    bool bare = initial.defaultATK == BE::GANASADAI2_BARE_HANDS_ATK;
    for (int i = 0; i < r.position; ++i) {
        if (r.isEnemy[i]) continue;
        if (r.actions[i] & BE::ACTION_EQUIPMENT_CHANGED)
            bare = (r.actions[i] & BE::ACTION_BARE_HANDS) != 0;
        const int action = r.actions[i] & BE::ACTION_ID_MASK;
        if ((action == BE::ATTACK_ALLY || action == BE::MULTITHRUST) && bare) return false;
        if (action == BE::ATTACK_ALLY && (!victory || r.turns[i] != r.turn)) return false;
    }
    return true;
}

bool locked(const Player& p) { return p.inactive || p.sleeping || p.paralysis; }

// This is the existing actor's candidate set, with actual emulator MP costs.
// Attack is a terminal probe only. No enemy-only action is added.
constexpr int ActionSet[] = {
    BE::MULTITHRUST, BE::PSYCHE_UP_ALLY, BE::MAGIC_MIRROR, BE::DOUBLE_UP,
    BE::BUFF, BE::MIDHEAL, BE::MORE_HEAL, BE::FULLHEAL,
    BE::SPECIAL_MEDICINE, BE::GOSPEL_SONG, BE::INSULATE,
    BE::DEFENDING_CHAMPION, BE::DEFENCE, BE::FLEE_ALLY, BE::MAGIC_WATER,
    BE::ATTACK_ALLY
};

int mpCost(int action) {
    switch (action) {
    case BE::MULTITHRUST: case BE::MIDHEAL: case BE::MAGIC_MIRROR:
    case BE::INSULATE: return 4;
    case BE::MORE_HEAL: return 8;
    case BE::FULLHEAL: return 24;
    case BE::BUFF: case BE::DEFENDING_CHAMPION: return 3;
    default: return 0;
    }
}

bool allowed(const Player& p, int encoded) {
    const int action = encoded & BE::ACTION_ID_MASK;
    const bool bare = (encoded & BE::ACTION_BARE_HANDS) != 0;
    if (locked(p) && bare != (p.defaultATK == BE::GANASADAI2_BARE_HANDS_ATK)) return false;
    if ((action == BE::MULTITHRUST || action == BE::ATTACK_ALLY) && bare) return false;
    if (action == BE::FLEE_ALLY && locked(p)) return false;
    if (p.mp < mpCost(action)) return false;
    if (action == BE::SPECIAL_MEDICINE && p.SpecialMedicineCount <= 0) return false;
    if (action == BE::MAGIC_WATER && p.MagicWaterCount <= 0) return false;
    // Main decrements this counter before actions, so an expired charge is not usable.
    if (action == BE::GOSPEL_SONG &&
        (!p.specialCharge || p.dirtySpecialCharge || p.specialChargeTurn < 1)) return false;
    return true;
}

int bucket(const Node& n) {
    const Player& p = n.players[0];
    return std::clamp(p.TensionLevel, 0, 4) * 4 +
           (p.hasMagicMirror ? 2 : 0) + (p.AtkBuffLevel >= 2 ? 1 : 0);
}

float evaluate(const Node& n, int profile, uint64_t salt) {
    const Player& p = n.players[0];
    const Player& e = n.players[1];
    constexpr float tension[] = {1, 1.5f, 2.5f, 4, 6};
    // Ordering only; never used as an admissible bound or a victory test.
    // An equipped follow-up is available unless equipment is locked.
    const float atk = (!locked(p) && p.defaultATK > 0)
        ? float(p.atk) * BE::GANASADAI2_EQUIPPED_ATK / p.defaultATK : float(p.atk);
    const float base = std::max(1.0f, atk * 0.5f - e.def * 0.25f) * 1.75f;
    const int t = std::clamp(p.TensionLevel, 0, 4);
    const float burst = base * tension[t] + t * 17.5f;
    const float charged = std::min(float(e.hp), std::max(0.0f, burst - base));
    const float mirror = p.hasMagicMirror
        ? std::min(4, std::max(1, p.MagicMirrorTurn)) * 60.0f : 0.0f;
    float value;
    if (profile == 0) {
        value = e.hp - 0.65f * charged - 0.65f * mirror - 0.3f * base;
    } else if (profile == 1) {
        value = e.hp - 1.05f * charged - 0.8f * mirror - 0.55f * base - 25 * t;
    } else if (profile == 2) {
        value = e.hp - 0.9f * charged - 1.55f * mirror - 0.4f * base - 15 * t;
    } else if (profile == 3) {
        value = e.hp - 0.4f * charged - 1.0f * mirror - 0.15f * base;
    } else {
        // A different ordering: estimate a *future* charge/buff/burst plan,
        // rather than just rewarding damage already dealt. Plans are never
        // actions, cached answers, or pruning bounds; Main still runs every edge.
        float bestTurns = std::numeric_limits<float>::max();
        for (int addMirror = 0; addMirror <= (p.hasMagicMirror ? 0 : 1); ++addMirror) {
            for (int addBuff = 0; addBuff <= (p.AtkBuffLevel >= 2 ? 0 : 1); ++addBuff) {
                for (int target = t; target <= 4; ++target) {
                    const float setup = float(target - t + addMirror + addBuff) +
                        (target == 4 && t != 4 ? 0.4f : 0.0f);
                    const float plannedAtk = addBuff ? BE::GANASADAI2_EQUIPPED_ATK * 1.5f : atk;
                    const float plainDamage = std::max(1.0f, plannedAtk * 0.5f - e.def * 0.25f) * 1.75f;
                    const float firstHit = plainDamage * tension[target] + target * 17.5f;
                    const float reflection = (p.hasMagicMirror || addMirror) ? 190.0f : 0.0f;
                    const float remaining = e.hp - reflection * (setup + 1) - firstHit;
                    const float turns = setup + 1 + std::max(0.0f, remaining) / (plainDamage + reflection);
                    bestTurns = std::min(bestTurns, turns);
                }
            }
        }
        value = bestTurns * 330 + e.hp * 0.035f - charged * 0.08f - mirror * 0.25f;
    }
    value -= 15 * std::max(0, p.BuffLevel) + 8 * std::max(0, p.InsulateLevel);
    // There is deliberately no HP/MP reserve reward or threshold here.
    // Restarts vary tie ordering, not the emulator RNG or the supplied prefix.
    if (salt) value += float(mix(n.hash ^ salt) & 1023) * (32.0f / 1024.0f);
    return value;
}

struct Search {
    const Player* initial;
    uint64_t seed;
    Clock::time_point begin, deadline;
    Ganasadai2Search::Result out;
    std::array<int, 350> prefix{}, scratchGene{};
    Node root;
    BattleResult edge;
    std::vector<Node> arena, candidates;
    std::vector<int> current, order, selected;
    // A generation-stamped flat table avoids per-node allocation/destruction
    // consuming the wall-clock budget. Hash collisions still compare full states.
    std::vector<Slot> slots;
    uint32_t generation = 0;

    Search(const Player* p, uint64_t s, int ms, int variant)
        : initial(p), seed(s), begin(Clock::now()),
          // Include destruction/return and scheduling jitter in the total budget.
          // At 1500ms this leaves ~1400ms for real search, not a first-win exit.
          deadline(begin + std::chrono::milliseconds(
              std::max(0, ms - std::min(100, std::max(1, ms / 10))))) {
        out.variant = variant;
        prefix.fill(-1);
        scratchGene.fill(-1);
        std::fill(std::begin(out.genome.actions), std::end(out.genome.actions), -1);
    }

    double elapsed() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    }

    bool expired() const { return Clock::now() >= deadline; }

    void copyGenome(const Node& n, const int* genes) {
        out.genome.AllyPlayer = n.players[0];
        out.genome.EnemyPlayer = n.players[1];
        out.genome.state = n.state;
        out.genome.position = n.rng;
        out.genome.turn = n.depth + 1;
        out.genome.processed = n.depth;
        out.genome.Initialized = true;
        std::copy_n(genes, 350, out.genome.actions);
    }

    bool prepare(const int* actions) {
        while (out.prefixLength < 350 && actions[out.prefixLength] != -1) {
            prefix[out.prefixLength] = actions[out.prefixLength];
            ++out.prefixLength;
        }
        if (out.prefixLength == 350) return out.inputValid = false;
        root.players[0] = initial[0];
        root.players[1] = initial[1];
        lcg::init(seed, true);
        // Prefix is replayed without changing its actions, order, or equipment.
        // Only the additional constraints of this task are checked here.
        for (int i = 0; i < out.prefixLength; ++i) {
            if (root.players[1].hp == 0 || root.players[0].hp == 0 ||
                prefix[i] == 0 || !allowed(root.players[0], prefix[i]))
                return out.inputValid = false;
            BE::Main(&root.rng, 1, prefix.data(), root.players, nullptr,
                     seed, nullptr, nullptr, -2, &root.state);
            if ((prefix[i] & BE::ACTION_ID_MASK) == BE::ATTACK_ALLY &&
                (root.players[1].hp != 0 || i + 1 != out.prefixLength))
                return out.inputValid = false;
        }
        root.depth = out.prefixLength;
        Player fresh[2] = {initial[0], initial[1]};
        int rng = 1;
        uint64_t state = 0;
        out.replay.clear();
        if (out.prefixLength)
            BE::Main(&rng, out.prefixLength, prefix.data(), fresh, &out.replay,
                     seed, nullptr, nullptr, -1, &state);
        if (rng != root.rng || state != root.state ||
            !samePlayer(fresh[0], root.players[0]) || !samePlayer(fresh[1], root.players[1]) ||
            !legalRecordedAttacks(out.replay, initial[0], root.players[1].hp == 0))
            return out.inputValid = false;
        root.rows = out.replay.position;
        root.changes = countChanges(out.replay);
        setHash(root);
        copyGenome(root, prefix.data());
        if (root.players[1].hp == 0) {
            out.victory = true;
            out.equipmentChanges = root.changes;
            out.firstVictoryMs = elapsed();
        }
        return true;
    }

    void accept(const Node& leaf) {
        if (out.victory && !better(leaf.rows, leaf.changes,
                                  out.replay.position, out.equipmentChanges)) return;
        std::array<int, 350> genes = prefix;
        genes[leaf.depth - 1] = leaf.action;
        for (int id = leaf.parent; id != NoParent && arena[id].parent != NoParent; id = arena[id].parent)
            genes[arena[id].depth - 1] = arena[id].action;
        genes[leaf.depth] = -1;
        // An independent full replay is authoritative for *every* accepted best.
        Player players[2] = {initial[0], initial[1]};
        int rng = 1;
        uint64_t state = 0;
        BattleResult replay;
        BE::Main(&rng, leaf.depth, genes.data(), players, &replay,
                 seed, nullptr, nullptr, -1, &state);
        const int changes = countChanges(replay);
        if (players[1].hp != 0 || rng != leaf.rng || state != leaf.state ||
            replay.position != leaf.rows || changes != leaf.changes ||
            !legalRecordedAttacks(replay, initial[0], true) ||
            !samePlayer(players[0], leaf.players[0]) || !samePlayer(players[1], leaf.players[1])) {
            ++out.replayRejected;
            return;
        }
        if (out.victory && !better(replay.position, changes,
                                  out.replay.position, out.equipmentChanges)) return;
        if (!out.victory) out.firstVictoryMs = elapsed();
        out.victory = true;
        ++out.improvements;
        out.replay = replay;
        out.equipmentChanges = changes;
        copyGenome(leaf, genes.data());
    }

    void insert(Node n, int profile, uint64_t salt) {
        setHash(n);
        size_t at = n.hash & (slots.size() - 1);
        while (slots[at].generation == generation && slots[at].hash != n.hash)
            at = (at + 1) & (slots.size() - 1);
        Slot& slot = slots[at];
        if (slot.generation == generation) {
            for (int id = slot.index; id != -1; id = candidates[id].hashNext) {
                if (!sameState(n, candidates[id])) continue;
                if (better(n.rows, n.changes, candidates[id].rows, candidates[id].changes)) {
                    n.hashNext = candidates[id].hashNext;
                    n.score = evaluate(n, profile, salt);
                    candidates[id] = n;
                }
                return;
            }
            n.hashNext = slot.index;
        } else {
            n.hashNext = -1;
            slot.generation = generation;
            slot.hash = n.hash;
        }
        slot.index = static_cast<int>(candidates.size());
        n.score = evaluate(n, profile, salt);
        candidates.push_back(n);
    }

    bool pass(int width, int profile, bool stratified, uint64_t salt,
              int changeLimit = 1000, bool coreOnly = false, bool healthDiversity = false) {
        bool clipped = false;
        arena.clear(); current.clear();
        arena.push_back(root); current.push_back(0);
        // width is a power of two; 32 encoded actions per parent at most.
        if (slots.size() < size_t(width) * 64) slots.resize(size_t(width) * 64);
        ++out.passes;
        while (!current.empty() && !expired()) {
            candidates.clear(); ++generation;
            for (const int id : current) {
                // Copy before arena changes; selected ancestors remain stable this pass.
                const Node parent = arena[id];
                if (parent.depth >= 349 || parent.players[0].hp <= 0 ||
                    (out.victory && parent.rows >= out.replay.position)) continue;
                ++out.expanded;
                for (int action : ActionSet) {
                    if (coreOnly && action != BE::MULTITHRUST && action != BE::PSYCHE_UP_ALLY &&
                        action != BE::MAGIC_MIRROR && action != BE::DOUBLE_UP &&
                        action != BE::ATTACK_ALLY) continue;
                    for (int eq = 0; eq < 2; ++eq) {
                        const bool bare = eq != 0;
                        const int encoded = action | (bare ? BE::ACTION_BARE_HANDS : 0);
                        if (!allowed(parent.players[0], encoded)) continue;
                        if ((out.generated & 63) == 0 && expired()) return false;
                        Node n = parent;
                        n.parent = id; n.action = encoded; n.depth = parent.depth + 1;
                        scratchGene[parent.depth] = encoded;
                        edge.clear();
                        BE::Main(&n.rng, 1, scratchGene.data(), n.players, &edge,
                                 seed, nullptr, nullptr, -1, &n.state);
                        ++out.generated;
                        n.rows = parent.rows + edge.position;
                        n.changes = parent.changes + countChanges(edge);
                        if (n.changes > changeLimit) continue;
                        if (n.players[0].mp < 0) continue;
                        if (n.players[1].hp == 0) { accept(n); continue; }
                        // ATTACK_ALLY must not create a non-terminal search node.
                        if (action == BE::ATTACK_ALLY || n.players[0].hp <= 0) continue;
                        // This is an exact monotone bound on recorded events, not on HP.
                        if (out.victory && n.rows >= out.replay.position) continue;
                        insert(n, profile, salt);
                    }
                }
            }
            if (expired()) return false;
            if (candidates.empty()) return !clipped;
            order.resize(candidates.size());
            std::iota(order.begin(), order.end(), 0);
            auto less = [&](int a, int b) {
                const Node& x = candidates[a]; const Node& y = candidates[b];
                if (x.score != y.score) return x.score < y.score;
                if (x.changes != y.changes) return x.changes < y.changes;
                return x.hash < y.hash;
            };
            const int keep = std::min(width, static_cast<int>(order.size()));
            clipped = clipped || keep < int(order.size());
            selected.clear();
            if (stratified && int(order.size()) > keep) {
                std::array<std::vector<int>, BucketCount> groups;
                for (int idx : order) {
                    const Player& p = candidates[idx].players[0];
                    const int band = healthDiversity ? std::min(2, p.hp * 3 / std::max(1, p.maxHp)) : 0;
                    groups[bucket(candidates[idx]) + 20 * band].push_back(idx);
                }
                const int quota = std::max(1, keep / ((healthDiversity ? 60 : 20) * 2));
                for (auto& group : groups) {
                    if (expired()) return false;
                    const int count = std::min(quota, static_cast<int>(group.size()));
                    if (count < int(group.size()))
                        std::nth_element(group.begin(), group.begin() + count, group.end(), less);
                    selected.insert(selected.end(), group.begin(), group.begin() + count);
                }
            }
            // Half of the width preserves different tactical stages; the remainder
            // follows the global ordering. No tactic has to maintain an HP reserve.
            std::vector<unsigned char> used(candidates.size(), 0);
            for (int idx : selected) used[idx] = 1;
            if (keep < int(order.size()))
                std::nth_element(order.begin(), order.begin() + keep, order.end(), less);
            std::sort(order.begin(), order.begin() + keep, less);
            for (int i = 0; i < keep && int(selected.size()) < keep; ++i)
                if (!used[order[i]]) selected.push_back(order[i]);
            if (expired()) return false;
            std::sort(selected.begin(), selected.end(), less);
            current.clear();
            for (int idx : selected) {
                current.push_back(static_cast<int>(arena.size()));
                arena.push_back(candidates[idx]);
            }
        }
        return !expired() && !clipped;
    }

    Ganasadai2Search::Result run(const int* actions) {
        if (prepare(actions) && !out.victory && root.players[0].hp > 0) {
            // Iterative widening continues after victory. There is no generation,
            // first-win, or millisecond-scale early exit for a live search problem.
            for (int round = 0; !expired(); ++round) {
                const int profile = out.variant == 22 ? (round % 3) :
                    (out.variant == 10 ? 4 : out.variant == 11 ? (round % 2 ? 4 : 1) :
                     out.variant >= 8 ? out.variant - 7 : out.variant % 4);
                const int exponent = out.variant == 22 ? round / 3 : round;
                const int width = 128 << std::min(exponent, 6);
                const uint64_t salt = exponent >= 6 ? mix(uint64_t(round) + 1) : 0;
                const bool exhausted = pass(width, profile, profile != 0, salt,
                     (out.variant == 4 || out.variant == 5) ? 0 : 1000,
                     out.variant == 6 || out.variant == 7, out.variant >= 8 && out.variant <= 9);
                // Only actual exhaustion with no width truncation ends early;
                // finding a win, stagnation, or a node quota never does.
                if (exhausted) break;
            }
        }
        out.elapsedMs = elapsed();
        return out;
    }
};
}

Ganasadai2Search::Result Ganasadai2Search::Run(const Player initial[2], uint64_t seed,
                                             const int actions[350], int budgetMs, int variant) {
    const auto begin = Clock::now();
    Result result;
    {
        Search search(initial, seed, budgetMs, variant);
        result = search.run(actions);
    }
    result.elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    return result;
}