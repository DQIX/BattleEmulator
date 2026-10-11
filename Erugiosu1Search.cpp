#include "Erugiosu1Search.h"
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
using BE = BattleEmulator;
using Clock = std::chrono::steady_clock;
constexpr int ActionCapacity = 350;
constexpr int NoLink = -1;
constexpr int TacticalBuckets = 20;

// Compare every semantic field of this branch's Player, never its padding.
auto fields(const Player& p) {
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
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

struct Node {
    Player players[2]{};
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

// Only the current and next frontier retain full Player states. Ancestors
// retain an action and parent index instead of 350 actions per generated node.
struct Link { int parent; int action; };
struct Slot {
    uint64_t hash = 0;
    int index = -1;
    uint32_t generation = 0;
};

bool sameFuture(const Node& a, const Node& b) {
    return a.rng == b.rng && a.state == b.state && a.depth == b.depth &&
           fields(a.players[0]) == fields(b.players[0]) &&
           fields(a.players[1]) == fields(b.players[1]);
}

void setHash(Node& n) {
    const Player& p = n.players[0];
    auto pair = [](int a, int b) {
        return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
    };
    // A cheap fingerprint is followed by full equality, including fields not
    // in this fingerprint. A collision never discards a different state.
    n.hash = mix(n.state ^ mix(n.rng) ^ mix(pair(p.hp, n.players[1].hp)) ^
        mix(pair(p.defaultATK, p.atk)) ^ mix(pair(p.mp, p.def)) ^
        mix(pair(p.TensionLevel, p.BuffTurns)) ^
        mix(pair(p.AtkBuffTurn, p.MagicMirrorTurn)));
}

bool better(int rows, int changes, int oldRows, int oldChanges) {
    return rows < oldRows || (rows == oldRows && changes < oldChanges);
}

bool locked(const Player& p) { return p.inactive || p.paralysis || p.sleeping; }

bool canStillAttack(const Player& p) {
    // Multithrust is the only damaging move in this search's legal action
    // set. This boss supplies no MP recovery or reflected/self damage.
    // Once both MP and all MP items are gone, waiting cannot produce a win.
    return p.mp >= 4 || p.MagicWaterCount > 0 ||
           p.SageElixirCount > 0 || p.ElfinElixirCount > 0;
}

int equipment(const Player& p) {
    if (p.defaultATK == BE::ERUGI1_BARE_HANDS_ATK) return BE::ACTION_BARE_HANDS;
    if (p.defaultATK == BE::ERUGI1_GANNAN_ATK) return BE::ACTION_GANNAN;
    return 0;
}

// Retain the existing useful actions. The three extra recovery actions have
// their own implemented MP cost/inventory in the supplied world. No normal
// attack, enemy-only move, or untracked medicinal-herb inventory is introduced.
constexpr int ActionSet[] = {
    BE::MULTITHRUST, BE::PSYCHE_UP_ALLY, BE::DOUBLE_UP,
    BE::BUFF, BE::MAGIC_MIRROR, BE::HEAL, BE::MIDHEAL, BE::MORE_HEAL,
    BE::FULLHEAL, BE::SPECIAL_MEDICINE, BE::GOSPEL_SONG,
    BE::DEFENDING_CHAMPION, BE::DEFENCE, BE::INSULATE,
    BE::MAGIC_WATER, BE::SAGE_ELIXIR, BE::ELFIN_ELIXIR, BE::FLEE_ALLY
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
    // There is no command/equipment selection while already unable to act.
    // This marker runs Main's own recovery handling without normal attacks,
    // a FLEE status bypass, or DEFENCE's pre-action damage reduction.
    if (locked(p)) return action == BE::INACTIVE_ALLY && gear == equipment(p);
    if (action == BE::INACTIVE_ALLY) return false;
    if (action == BE::MULTITHRUST && gear != 0) return false;
    if (p.mp < mpCost(action)) return false;
    if (action == BE::SPECIAL_MEDICINE && p.SpecialMedicineCount <= 0) return false;
    if (action == BE::MAGIC_WATER && p.MagicWaterCount <= 0) return false;
    if (action == BE::SAGE_ELIXIR && p.SageElixirCount <= 0) return false;
    if (action == BE::ELFIN_ELIXIR && p.ElfinElixirCount <= 0) return false;
    if (action == BE::GOSPEL_SONG &&
        (!p.specialCharge || p.dirtySpecialCharge || p.specialChargeTurn < 1)) return false;
    return std::find(std::begin(ActionSet), std::end(ActionSet), action) != std::end(ActionSet);
}

bool safeFlee(int action, const Player& after, const BattleResult& log) {
    if (action != BE::FLEE_ALLY || !locked(after)) return true;
    for (int i = 0; i < log.position; ++i)
        if (!log.isEnemy[i] && !log.initiative[i] &&
            (log.actions[i] & BE::ACTION_ID_MASK) == BE::FLEE_ALLY) return false;
    return true;
}

int bucket(const Node& n) {
    const Player& p = n.players[0];
    return std::clamp(p.TensionLevel, 0, 4) * 4 +
           (p.AtkBuffLevel >= 2 ? 2 : 0) + (p.BuffLevel > 0 ? 1 : 0);
}

float evaluate(const Node& n, int profile) {
    const Player& p = n.players[0];
    const Player& e = n.players[1];
    constexpr float tension[] = {1, 1.5f, 2.5f, 4, 6};
    const int level = std::clamp(p.TensionLevel, 0, 4);
    // Future on-gear attack power is an ordering estimate only. Each edge
    // still applies real equipment and damage through this variant's Main.
    const float atk = p.defaultATK > 0
        ? float(p.atk) * BE::ERUGI1_EQUIPPED_ATK / p.defaultATK : float(p.atk);
    const float perHit = std::max(1.0f, atk * 0.25f - e.def * 0.125f);
    const float base = perHit * 3.5f * 1.25f * 0.96f;
    const float burst = (perHit * tension[level] + level * 5) * 3.5f * 1.25f * 0.96f;
    const float charged = std::max(0.0f, std::min(float(e.hp), burst) -
                                                   std::min(float(e.hp), base));
    float value;
    if (profile == 0) {
        value = e.hp - 0.72f * charged - 0.55f * base;
    } else if (profile == 1) {
        value = e.hp - 0.98f * charged - 0.70f * base - 22 * level;
    } else {
        float turns = std::numeric_limits<float>::max();
        for (int buff = 0; buff <= (p.AtkBuffLevel >= 2 ? 0 : 1); ++buff) {
            const float nextAtk = buff ? BE::ERUGI1_EQUIPPED_ATK * 1.5f : atk;
            const float nextHit = std::max(1.0f, nextAtk * 0.25f - e.def * 0.125f);
            const float ordinary = nextHit * 3.5f * 1.25f * 0.96f;
            for (int target = level; target <= 4; ++target) {
                const float setup = float(target - level + buff) +
                                    (target == 4 && level < 4 ? 0.5f : 0);
                const float hit = (nextHit * tension[target] + target * 5) * 3.5f * 1.25f * 0.96f;
                turns = std::min(turns, setup + 1 + std::max(0.0f, e.hp - hit) / ordinary);
            }
        }
        value = 260 * turns + e.hp * 0.08f - charged * 0.12f;
    }
    const float danger = std::max(0.0f, float(p.maxHp * 0.35) - p.hp);
    value += danger * danger * (profile == 0 ? 0.01f : 0.025f);
    value -= p.hp * (profile == 0 ? 0.2f : 0.4f);
    value -= 45 * std::max(0, p.BuffLevel) + 0.3f * std::max(0, p.mp);
    value -= 6 * p.SpecialMedicineCount + 8 * p.ElfinElixirCount + 5 * p.SageElixirCount;
    // This boss changes Kaswoosh into Disruptive Wave when mirror is active;
    // it does not supply the donor branch's Kafrizz-reflection damage.
    if (p.hasMagicMirror) value -= 25;
    if (p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn >= 1) value -= 25;
    // Near a finishing multithrust, acting first can save two enemy trace
    // rows. Main reads these same two speed draws before any attack draws.
    // Probe a copied cursor only; this is ordering, never a victory bound.
    if (!locked(p) && p.mp >= 4 && burst >= e.hp * 0.8f) {
        int probe = n.rng;
        const double allySpeed = p.speed * lcg::floatRand(&probe, 0.51, 1.0);
        const double enemySpeed = e.speed * lcg::floatRand(&probe, 0.51, 1.0);
        if (allySpeed > enemySpeed) value -= 50;
    }
    if (locked(p)) value += 140;
    return value;
}

class Search {
    const Player* initial;
    uint64_t seed;
    Clock::time_point begin, deadline;
    int variant;
    Erugiosu1Search::Result out;
    std::array<int, ActionCapacity> prefix{}, scratch{};
    Node root;
    BattleResult edge, verification;
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
        out.genome.position = n.rng; // Existing Genome.position is the RNG cursor.
        out.genome.turn = n.depth + 1;
        out.genome.processed = n.depth;
        out.genome.Initialized = true;
        out.genome.EActions[0] = out.genome.EActions[1] = -1;
        out.genome.Aactions = -1;
        std::copy_n(genes, ActionCapacity, out.genome.actions);
    }

    bool replay(const int* genes, int length, Node& fresh, BattleResult& log) {
        Node sequential;
        sequential.players[0] = initial[0];
        sequential.players[1] = initial[1];
        lcg::init(seed, true);
        for (int turn = 0; turn < length; ++turn) {
            if (sequential.players[0].hp == 0 || sequential.players[1].hp == 0) break;
            const int gear = genes[turn] & (BE::ACTION_BARE_HANDS | BE::ACTION_GANNAN);
            const int action = genes[turn] & BE::ACTION_ID_MASK;
            if (turn >= out.prefixLength && !allowed(sequential.players[0], action, gear)) return false;
            const int before = sequential.players[0].defaultATK;
            edge.clear();
            BE::Main(&sequential.rng, 1, genes, sequential.players, &edge,
                     seed, nullptr, nullptr, -1, &sequential.state);
            sequential.rows += edge.position;
            sequential.changes += before != sequential.players[0].defaultATK;
            if (turn >= out.prefixLength && !safeFlee(action, sequential.players[0], edge)) return false;
        }
        sequential.depth = int((sequential.state >> 12) & 0xfffff);
        fresh = Node{};
        fresh.players[0] = initial[0];
        fresh.players[1] = initial[1];
        log.clear();
        lcg::init(seed, true);
        if (length && fresh.players[0].hp != 0 && fresh.players[1].hp != 0)
            BE::Main(&fresh.rng, length, genes, fresh.players, &log,
                     seed, nullptr, nullptr, -1, &fresh.state);
        fresh.rows = log.position;
        fresh.depth = int((fresh.state >> 12) & 0xfffff);
        // Count real turn-start changes, including a terminal turn without an
        // ally log entry. Requested gear and displayed flags are not the count.
        fresh.changes = sequential.changes;
        return sameFuture(fresh, sequential) && fresh.rows == sequential.rows;
    }

    bool prepare(const int* actions) {
        if (!initial || !actions || !seed) return out.inputValid = false;
        while (out.prefixLength < ActionCapacity && actions[out.prefixLength] != -1) {
            // A zero asks the old emulator for implicit fallback commands and
            // is not an explicit fixed action. Never silently shorten it.
            if (actions[out.prefixLength] == 0) return out.inputValid = false;
            prefix[out.prefixLength] = actions[out.prefixLength];
            ++out.prefixLength;
        }
        if (out.prefixLength == ActionCapacity) return out.inputValid = false;
        if (!replay(prefix.data(), out.prefixLength, root, out.replay))
            return out.inputValid = false;
        out.replayVerified = true;
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
        if (expired() || (out.victory && !better(leaf.rows, leaf.changes,
                                      out.replay.position, out.equipmentChanges))) return;
        std::array<int, ActionCapacity> genes = prefix;
        int at = leaf.depth - 1;
        genes[at] = leaf.action;
        for (int link = leaf.link; link != NoLink; link = links[link].parent)
            genes[--at] = links[link].action;
        if (at != out.prefixLength) { ++out.replayRejected; return; }
        genes[leaf.depth] = -1;
        Node fresh;
        if (!replay(genes.data(), leaf.depth, fresh, verification) ||
            fresh.players[1].hp != 0 || !sameFuture(fresh, leaf) ||
            fresh.rows != leaf.rows || fresh.changes != leaf.changes) {
            ++out.replayRejected;
            return;
        }
        if (out.victory && !better(fresh.rows, fresh.changes,
                                  out.replay.position, out.equipmentChanges)) return;
        if (!out.victory) out.firstVictoryMs = elapsed();
        out.victory = out.replayVerified = true;
        ++out.improvements;
        out.replay = verification;
        out.equipmentChanges = fresh.changes;
        copyGenome(fresh, genes.data());
    }

    void insert(Node n, int profile) {
        setHash(n);
        size_t at = n.hash & (slots.size() - 1);
        while (slots[at].generation == generation && slots[at].hash != n.hash)
            at = (at + 1) & (slots.size() - 1);
        Slot& slot = slots[at];
        if (slot.generation == generation) {
            for (int id = slot.index; id != -1; id = candidates[id].hashNext) {
                if (!sameFuture(n, candidates[id])) continue;
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

    bool cannotImprove(const Node& n) const {
        // Any further win needs at least one additional recorded action.
        // This bound concerns exact trace rows, not RNG or heuristic damage.
        return out.victory && (n.rows + 1 > out.replay.position ||
            (n.rows + 1 == out.replay.position && n.changes >= out.equipmentChanges));
    }

    // True means the relevant branches were exhausted without beam clipping.
    // Reaching an API capacity or a deadline never claims optimality.
    bool pass(int width, int profile) {
        bool clipped = false;
        current.clear();
        links.clear();
        current.push_back(root);
        const size_t capacity = size_t(width) * 128;
        if (slots.size() < capacity) slots.resize(capacity);
        ++out.passes;
        while (!current.empty() && !expired()) {
            candidates.clear();
            ++generation;
            for (const Node& parent : current) {
                if (parent.players[0].hp == 0 || !canStillAttack(parent.players[0]) ||
                    cannotImprove(parent)) continue;
                if (parent.depth >= ActionCapacity - 1) { clipped = true; continue; }
                ++out.expanded;
                const bool unable = locked(parent.players[0]);
                const int ownGear = equipment(parent.players[0]);
                const int ownIndex = ownGear == 0 ? 0 : ownGear == BE::ACTION_BARE_HANDS ? 1 : 2;
                const int actionCount = unable ? 1 : int(std::size(ActionSet));
                for (int a = 0; a < actionCount; ++a) {
                    const int action = unable ? BE::INACTIVE_ALLY : ActionSet[a];
                    for (int k = 0; k < (unable ? 1 : 3); ++k) {
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
                        out.maxSuffixDepth = std::max(out.maxSuffixDepth, n.depth - out.prefixLength);
                        n.rows = parent.rows + edge.position;
                        n.changes = parent.changes + (parent.players[0].defaultATK != n.players[0].defaultATK);
                        if (!safeFlee(action, n.players[0], edge) || n.players[0].mp < 0) continue;
                        if (n.players[1].hp == 0) { accept(n); continue; }
                        if (n.players[0].hp == 0 || !canStillAttack(n.players[0]) ||
                            cannotImprove(n)) continue;
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
                if (x.rows != y.rows) return x.rows < y.rows;
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
    Search(const Player* players, uint64_t inputSeed, int budgetMs, int inputVariant,
           Clock::time_point started)
        : initial(players), seed(inputSeed), begin(started), variant(inputVariant) {
        const int budget = std::clamp(budgetMs, 0, 1500);
        // Headroom covers the last replay, frontier selection, dump generation
        // and freeing retained buffers. There is no minimum search duration.
        deadline = begin + std::chrono::milliseconds(budget - std::min(75, budget / 10));
        if (variant < 0 || variant > 3) variant = Erugiosu1Search::DefaultVariant;
        prefix.fill(-1);
        scratch.fill(-1);
        std::fill_n(out.genome.actions, ActionCapacity, -1);
    }

    Erugiosu1Search::Result run(const int* actions) {
        if (prepare(actions) && !out.victory && root.players[0].hp > 0 && !expired()) {
            constexpr std::array<int, 3> profileOrder = {0, 1, 2};
            // Iterative widening is finite. Per-depth storage/work is bounded
            // by width times the legal actions, not an exponential path tree.
            // Gouketu's damage ordering is widened first. Charge/planning
            // orderings can still improve its exact incumbent in the same
            // budget, without tuning for hayate/kabuto.
            const int profiles = variant == 3 ? 3 : 1;
            for (int i = 0; i < profiles && !expired(); ++i) {
                const int profile = variant == 3 ? profileOrder[i] : variant;
                int stableRounds = 0;
                for (int width : {32, 128, 512, 2048}) {
                    const int before = out.improvements;
                    if (pass(width, profile)) {
                        out.elapsedMs = elapsed();
                        return out;
                    }
                    if (expired()) break;
                    if (out.victory && out.improvements == before) ++stableRounds;
                    else stableRounds = 0;
                    if (stableRounds >= 2) break;
                }
            }
        }
        out.elapsedMs = elapsed();
        return out;
    }
};
}

Erugiosu1Search::Result Erugiosu1Search::Run(const Player initial[2], uint64_t seed,
                                           const int actions[350], int budgetMs, int variant) {
    const auto begin = Clock::now();
    Result result;
    {
        // Trace buffers and frontier storage stay off the WebAssembly stack.
        auto search = std::make_unique<Search>(initial, seed, budgetMs, variant, begin);
        result = search->run(actions);
    }
    result.elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    return result;
}
