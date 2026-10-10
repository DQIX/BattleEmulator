#include "BaruborosuSearch.h"
#include "BattleEmulator.h"
#include "lcg.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>

namespace {
using BE = BattleEmulator;
using Clock = std::chrono::steady_clock;
constexpr int NoLink = -1;
constexpr int ActionCapacity = 350;
constexpr int TacticalBuckets = 20;

// Compare semantic fields, not struct padding, and never treat a hash collision
// as state equality. Equipment is represented by defaultATK in this world.
#define PLAYER_FIELDS(F) \
    F(hp) F(maxHp) F(atk) F(defaultATK) F(def) F(defaultDEF) \
    F(speed) F(defaultSpeed) F(HealPower) F(mp) F(maxMp) \
    F(specialCharge) F(dirtySpecialCharge) F(specialChargeTurn) \
    F(paralysis) F(paralysisLevel) F(paralysisTurns) F(SpecialMedicineCount) \
    F(defence) F(sleeping) F(sleepingTurn) F(BuffLevel) F(BuffTurns) \
    F(hasMagicMirror) F(MagicMirrorTurn) F(AtkBuffLevel) F(AtkBuffTurn) \
    F(TensionLevel) F(rage) F(SageElixirCount) F(ElfinElixirCount) \
    F(MagicWaterCount) F(InsulateLevel) F(InsulateTurns) F(SpeedLevel) \
    F(SpeedTurns) F(inactive) F(rageTurns)

bool samePlayer(const Player& a, const Player& b) {
#define EQUAL(field) if (a.field != b.field) return false;
    PLAYER_FIELDS(EQUAL)
#undef EQUAL
    return true;
}

uint64_t mix(uint64_t x) {
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

template<class T> uint64_t bits(T x) { return static_cast<uint64_t>(x); }
uint64_t bits(double x) {
    uint64_t value;
    std::memcpy(&value, &x, sizeof(value));
    return value;
}

uint64_t hashPlayer(uint64_t hash, const Player& p) {
#define HASH(field) hash = (hash ^ bits(p.field)) * 0x100000001b3ULL;
    PLAYER_FIELDS(HASH)
#undef HASH
    return hash;
}
#undef PLAYER_FIELDS

struct Node {
    Player players[2];
    uint64_t state = 0;
    uint64_t hash = 0;
    int rng = 1;
    int depth = 0;
    int rows = 0;
    int changes = 0;
    int link = NoLink;
    int action = -1;
    int hashNext = -1;
    float score = 0;
};

// Only the frontier stores Player states. Ancestors need two integers, not a
// complete Genome and a copy of the 350-action sequence for every child.
struct Link { int parent; int action; };
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

int countChanges(const BattleResult& replay) {
    int count = 0;
    for (int i = 0; i < replay.position; ++i)
        if (!replay.isEnemy[i] && (replay.actions[i] & BE::ACTION_EQUIPMENT_CHANGED)) ++count;
    return count;
}

int equipment(const Player& p) {
    if (p.defaultATK == BE::BARUBOROSU_BARE_HANDS_ATK) return BE::ACTION_BARE_HANDS;
    if (p.defaultATK == BE::BARUBOROSU_GANNAN_ATK) return BE::ACTION_GANNAN;
    return 0;
}

bool locked(const Player& p) { return p.inactive || p.paralysis || p.sleeping; }

// Preserve the existing useful actions, with actual MP costs. HEAL and normal
// attacks are also supported own-side actions. FLEE needs an exact edge guard:
// this emulator's unconditional pre-action skip can bypass a same-turn WAR_CRY.
constexpr int ActionSet[] = {
    BE::MULTITHRUST, BE::PSYCHE_UP_ALLY, BE::DOUBLE_UP,
    BE::MAGIC_MIRROR, BE::BUFF, BE::INSULATE, BE::HEAL,
    BE::MIDHEAL, BE::MORE_HEAL, BE::FULLHEAL, BE::SPECIAL_MEDICINE,
    BE::GOSPEL_SONG, BE::DEFENDING_CHAMPION, BE::DEFENCE, BE::MAGIC_WATER,
    BE::FLEE_ALLY
};
constexpr int EquipmentSet[] = {0, BE::ACTION_BARE_HANDS, BE::ACTION_GANNAN};

int mpCost(int action) {
    switch (action) {
    case BE::HEAL: return 2;
    case BE::BUFF: case BE::DEFENDING_CHAMPION: return 3;
    case BE::MULTITHRUST: case BE::MAGIC_MIRROR: case BE::MIDHEAL:
    case BE::INSULATE: return 4;
    case BE::MORE_HEAL: return 8;
    case BE::FULLHEAL: return 24;
    default: return 0;
    }
}

bool allowed(const Player& p, int action, int gear) {
    // A carried status does not open a new command/equipment menu. Main will
    // turn this neutral command into the actual status/recovery action.
    if (locked(p)) return action == BE::ATTACK_ALLY && gear == equipment(p);
    // Multithrust requires the equipped spear; fists and the sceptre cannot use it.
    if (action == BE::MULTITHRUST && gear != 0) return false;
    if (p.mp < mpCost(action)) return false;
    if (action == BE::SPECIAL_MEDICINE && p.SpecialMedicineCount <= 0) return false;
    if (action == BE::MAGIC_WATER && p.MagicWaterCount <= 0) return false;
    if (action == BE::GOSPEL_SONG &&
        (!p.specialCharge || p.dirtySpecialCharge || p.specialChargeTurn < 1)) return false;
    return true;
}

bool skippedStatusWithFlee(const Node& child, const BattleResult& replay) {
    if ((child.action & BE::ACTION_ID_MASK) != BE::FLEE_ALLY || !locked(child.players[0])) return false;
    for (int i = 0; i < replay.position; ++i) {
        // When the enemy acts first, a remaining status after the recorded FLEE
        // means the known unconditional skip bypassed its pre-action handler.
        // If the ally acted first, a later enemy status is legitimate instead.
        if (!replay.isEnemy[i] && !replay.initiative[i] &&
            (replay.actions[i] & BE::ACTION_ID_MASK) == BE::FLEE_ALLY) return true;
    }
    return false;
}

int bucket(const Node& n) {
    const Player& p = n.players[0];
    // Equipment remains part of the exact state, but does not consume three
    // separate tactical quotas at the expense of promising combat plans.
    return std::clamp(p.TensionLevel, 0, 4) * 4 +
           (p.AtkBuffLevel >= 2 ? 2 : 0) + (p.hasMagicMirror ? 1 : 0);
}

float evaluate(const Node& n, int profile) {
    const Player& p = n.players[0];
    const Player& e = n.players[1];
    constexpr float tension[] = {1, 1.5f, 2.5f, 4, 6};
    const int level = std::clamp(p.TensionLevel, 0, 4);
    const float atk = !locked(p) && p.defaultATK > 0
        ? float(p.atk) * BE::BARUBOROSU_EQUIPPED_ATK / p.defaultATK : float(p.atk);
    const float base = std::max(1.0f, atk * 0.5f - e.def * 0.25f) * 1.75f;
    // Ordering only. These Baruborosu estimates are never pruning bounds;
    // every edge, damage amount and final objective comes from Main.
    const float burst = base * tension[level] + level * 21.0f;
    const float charged = std::min(float(e.hp), std::max(0.0f, burst - base));
    const float mirror = p.hasMagicMirror
        ? 24.0f * std::min(4, std::max(1, p.MagicMirrorTurn)) : 0;
    float value;
    if (profile == 0) {
        value = e.hp - 0.75f * charged - 0.55f * base - mirror;
    } else if (profile == 1) {
        value = e.hp - 1.10f * charged - 0.70f * base - mirror - 25 * level;
    } else {
        float turns = std::numeric_limits<float>::max();
        for (int buff = 0; buff <= (p.AtkBuffLevel >= 2 ? 0 : 1); ++buff) {
            const float nextAtk = buff ? BE::BARUBOROSU_EQUIPPED_ATK * 1.5f : atk;
            const float damage = std::max(1.0f, nextAtk * 0.5f - e.def * 0.25f) * 1.75f;
            for (int target = level; target <= 4; ++target) {
                const float setup = float(target - level + buff) +
                                    (target == 4 && level < 4 ? 0.4f : 0);
                const float hit = damage * tension[target] + target * 21.0f;
                const float remaining = std::max(0.0f, e.hp - hit);
                turns = std::min(turns, setup + 1 + remaining / damage);
            }
        }
        value = 260 * turns + e.hp * 0.08f - charged * 0.12f - mirror;
    }
    value -= 22 * std::max(0, p.BuffLevel) + 14 * std::max(0, p.InsulateLevel);
    // Health orders otherwise promising states, but there is no reserve-HP
    // requirement: every living state and every exact victory is eligible.
    value -= 0.12f * p.hp;
    return value;
}

class Search {
    const Player* initial;
    uint64_t seed;
    Clock::time_point begin, deadline;
    BaruborosuSearch::Result out;
    std::array<int, ActionCapacity> prefix{}, scratch{};
    Node root;
    BattleResult edge;
    std::vector<Node> current, candidates;
    std::vector<Link> links;
    std::vector<Slot> slots;
    std::vector<int> order, selected;
    std::vector<unsigned char> used;
    uint32_t generation = 0;

    double elapsed() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    }
    bool expired() const { return Clock::now() >= deadline; }

    void copyGenome(const Node& n, const int* genes) {
        out.genome.AllyPlayer = n.players[0];
        out.genome.EnemyPlayer = n.players[1];
        out.genome.state = n.state;
        out.genome.position = n.rng; // Keep the existing Genome cursor contract.
        out.genome.turn = n.depth + 1;
        out.genome.processed = n.depth;
        out.genome.Initialized = true;
        std::copy_n(genes, ActionCapacity, out.genome.actions);
    }

    bool prepare(const int* actions) {
        if (!actions) return out.inputValid = false;
        while (out.prefixLength < ActionCapacity && actions[out.prefixLength] != -1) {
            prefix[out.prefixLength] = actions[out.prefixLength];
            ++out.prefixLength;
        }
        if (out.prefixLength == ActionCapacity) return out.inputValid = false;
        root.players[0] = initial[0];
        root.players[1] = initial[1];
        lcg::init(seed, true);
        // Prefix commands are evidence, not candidates. Replay the complete
        // supplied prefix verbatim, including status aliases and equipment bits.
        // Main does not stop at -1 by itself: pass the exact length explicitly.
        if (out.prefixLength && root.players[0].hp != 0 && root.players[1].hp != 0)
            BE::Main(&root.rng, out.prefixLength, prefix.data(), root.players,
                     &out.replay, seed, nullptr, nullptr, -1, &root.state);
        // Main may stop before the end of a prefix that already ends the
        // battle. Keep all supplied commands, but report actual executed turns.
        root.depth = static_cast<int>((root.state >> 12) & 0xfffff);
        root.rows = out.replay.position;
        root.changes = countChanges(out.replay);
        out.equipmentChanges = root.changes;
        setHash(root);
        copyGenome(root, prefix.data());
        if (root.players[1].hp == 0) {
            out.victory = true;
            out.firstVictoryMs = elapsed();
        }
        return true;
    }

    void accept(const Node& leaf) {
        if (out.victory && !better(leaf.rows, leaf.changes,
                                  out.replay.position, out.equipmentChanges)) return;
        std::array<int, ActionCapacity> genes = prefix;
        int at = leaf.depth - 1;
        genes[at] = leaf.action;
        for (int link = leaf.link; link != NoLink; link = links[link].parent)
            genes[--at] = links[link].action;
        genes[leaf.depth] = -1;
        Player fresh[2] = {initial[0], initial[1]};
        int rng = 1;
        uint64_t state = 0;
        auto replay = std::make_unique<BattleResult>();
        BE::Main(&rng, leaf.depth, genes.data(), fresh, replay.get(),
                 seed, nullptr, nullptr, -1, &state);
        const int changes = countChanges(*replay);
        if (fresh[1].hp != 0 || rng != leaf.rng || state != leaf.state ||
            replay->position != leaf.rows || changes != leaf.changes ||
            !samePlayer(fresh[0], leaf.players[0]) || !samePlayer(fresh[1], leaf.players[1])) {
            ++out.replayRejected;
            return;
        }
        if (out.victory && !better(replay->position, changes,
                                  out.replay.position, out.equipmentChanges)) return;
        if (!out.victory) out.firstVictoryMs = elapsed();
        out.victory = true;
        ++out.improvements;
        out.replay = *replay;
        out.equipmentChanges = changes;
        copyGenome(leaf, genes.data());
    }

    void insert(Node n, int profile) {
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
                    n.score = evaluate(n, profile);
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
        n.score = evaluate(n, profile);
        candidates.push_back(n);
    }

    // Returns true only if every relevant branch was exhausted without any
    // width truncation. A heuristic score is never used to claim exhaustion.
    bool pass(int width, int profile) {
        bool clipped = false;
        current.clear(); links.clear();
        current.push_back(root);
        const size_t capacity = size_t(width) * 128;
        if (slots.size() < capacity) slots.resize(capacity);
        ++out.passes;
        while (!current.empty() && !expired()) {
            candidates.clear();
            ++generation;
            for (const Node& parent : current) {
                if (parent.depth >= ActionCapacity - 1 || parent.players[0].hp == 0) continue;
                if (out.victory && (parent.rows + 1 > out.replay.position ||
                    (parent.rows + 1 == out.replay.position && parent.changes >= out.equipmentChanges))) continue;
                ++out.expanded;
                // Current gear first preserves a no-change tie when equipment
                // is irrelevant to an enemy-first terminal reflection.
                const int ownGear = equipment(parent.players[0]);
                const int ownIndex = ownGear == 0 ? 0 : ownGear == BE::ACTION_BARE_HANDS ? 1 : 2;
                for (int action : ActionSet) {
                    for (int k = 0; k < 3; ++k) {
                        const int gear = EquipmentSet[(ownIndex + k) % 3];
                        if (!allowed(parent.players[0], action, gear)) continue;
                        if ((out.generated & 31) == 0 && expired()) return false;
                        Node n = parent;
                        n.action = action | gear;
                        n.depth = parent.depth + 1;
                        scratch[parent.depth] = n.action;
                        edge.clear();
                        BE::Main(&n.rng, 1, scratch.data(), n.players, &edge,
                                 seed, nullptr, nullptr, -1, &n.state);
                        ++out.generated;
                        n.rows = parent.rows + edge.position;
                        n.changes = parent.changes + countChanges(edge);
                        if (skippedStatusWithFlee(n, edge)) continue;
                        if (n.players[0].mp < 0) continue;
                        if (n.players[1].hp == 0) { accept(n); continue; }
                        if (n.players[0].hp == 0) continue;
                        if (out.victory && (n.rows + 1 > out.replay.position ||
                            (n.rows + 1 == out.replay.position && n.changes >= out.equipmentChanges))) continue;
                        insert(n, profile);
                    }
                }
            }
            if (expired()) return false;
            if (candidates.empty()) return !clipped;
            order.resize(candidates.size());
            std::iota(order.begin(), order.end(), 0);
            auto less = [&](int a, int b) {
                const Node& x = candidates[a];
                const Node& y = candidates[b];
                if (x.score != y.score) return x.score < y.score;
                if (x.changes != y.changes) return x.changes < y.changes;
                return x.hash < y.hash;
            };
            const int keep = std::min(width, static_cast<int>(order.size()));
            clipped = clipped || keep < int(order.size());
            selected.clear();
            if (profile != 0 && int(order.size()) > keep) {
                std::array<std::vector<int>, TacticalBuckets> groups;
                for (int id : order) groups[bucket(candidates[id])].push_back(id);
                const int quota = std::max(1, keep / (TacticalBuckets * 2));
                for (auto& group : groups) {
                    if (expired()) return false;
                    const int count = std::min(quota, static_cast<int>(group.size()));
                    if (count < int(group.size()))
                        std::nth_element(group.begin(), group.begin() + count, group.end(), less);
                    selected.insert(selected.end(), group.begin(), group.begin() + count);
                }
            }
            used.assign(candidates.size(), 0);
            for (int id : selected) used[id] = 1;
            if (keep < int(order.size()))
                std::nth_element(order.begin(), order.begin() + keep, order.end(), less);
            std::sort(order.begin(), order.begin() + keep, less);
            for (int i = 0; i < keep && int(selected.size()) < keep; ++i)
                if (!used[order[i]]) selected.push_back(order[i]);
            if (expired()) return false;
            std::sort(selected.begin(), selected.end(), less);
            current.clear();
            for (int id : selected) {
                Node n = candidates[id];
                links.push_back({n.link, n.action});
                n.link = static_cast<int>(links.size()) - 1;
                current.push_back(n);
            }
        }
        return !expired() && !clipped;
    }

public:
    Search(const Player* players, uint64_t inputSeed, int budgetMs, int variant,
           Clock::time_point started)
        : initial(players), seed(inputSeed), begin(started) {
        const int budget = std::clamp(budgetMs, 0, 1500);
        // Leave bounded cleanup/replay headroom; there is no minimum runtime.
        deadline = begin + std::chrono::milliseconds(budget - std::min(100, budget / 10));
        out.variant = variant >= 0 && variant <= 3 ? variant : BaruborosuSearch::DefaultVariant;
        prefix.fill(-1);
        scratch.fill(-1);
        std::fill_n(out.genome.actions, ActionCapacity, -1);
    }

    BaruborosuSearch::Result run(const int* actions) {
        if (prepare(actions) && !out.victory && root.players[0].hp > 0) {
            // Finite iterative widening, with three distinct orderings for the
            // portfolio. Width bounds space and work per depth even at 10+ turns.
            // Stop after two complete wider rounds produce no objective gain;
            // a small/exhausted suffix may therefore return in milliseconds.
            int stableRounds = 0;
            std::array<int, 3> profilesByGain = {1, 0, 2};
            for (int width : {64, 256, 1024, 4096}) {
                const int before = out.improvements;
                const int profiles = out.variant == 3 ? 3 : 1;
                int improvingProfile = profilesByGain[0];
                for (int i = 0; i < profiles && !expired(); ++i) {
                    const int profile = out.variant == 3 ? profilesByGain[i] : out.variant;
                    const int previousImprovements = out.improvements;
                    if (pass(width, profile)) {
                        out.elapsedMs = elapsed();
                        return out;
                    }
                    if (out.improvements > previousImprovements) improvingProfile = profile;
                }
                if (expired()) break;
                const auto preferred = std::find(profilesByGain.begin(), profilesByGain.end(), improvingProfile);
                std::rotate(profilesByGain.begin(), preferred, preferred + 1);
                if (out.victory && out.improvements == before) ++stableRounds;
                else stableRounds = 0;
                if (stableRounds >= 2) break;
            }
        }
        out.elapsedMs = elapsed();
        return out;
    }
};
}

BaruborosuSearch::Result BaruborosuSearch::Run(const Player initial[2], uint64_t seed,
                                             const int actions[350], int budgetMs, int variant) {
    const auto begin = Clock::now();
    Result result;
    {
        // Keep the emulator's large trace records off the WebAssembly stack.
        auto search = std::make_unique<Search>(initial, seed, budgetMs, variant, begin);
        result = search->run(actions);
    }
    result.elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    return result;
}
