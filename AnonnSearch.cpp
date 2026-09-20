#include "AnonnSearch.h"
#include "BattleEmulator.h"
#include "lcg.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace AnonnSearch {
namespace {
using B = BattleEmulator;
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoParent = std::numeric_limits<uint32_t>::max();
constexpr int MaxTurns = 349;
thread_local int selectedVariant = 0;
thread_local Statistics stats;

struct Node {
    Player p[2];
    uint64_t state = 0;
    int rng = 1;
    int turn = 0;
    int events = 0;
    int changes = 0;
    uint32_t path = NoParent;
    double priority = 0;
};
struct Link { uint32_t parent; int action; };
struct Choice { int action; };

bool resting(const Player& p) {
    return p.isStunned || p.sleeping || p.paralysis;
}
bool bare(const Player& p) { return p.defaultATK == B::ANONN_BARE_HANDS_ATK; }

// Do not hash padding, and do not merge states on a fingerprint alone.
auto fields(const Player& p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis, p.paralysisLevel,
        p.paralysisTurns, p.SpecialMedicineCount, p.defence, p.sleeping,
        p.sleepingTurn, p.BuffLevel, p.BuffTurns, p.hasMagicMirror,
        p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn, p.TensionLevel,
        p.rage, p.SageElixirCount, p.ElfinElixirCount, p.MagicWaterCount,
        p.speedTurn, p.speedLevel, p.PoisonTurn, p.PoisonEnable,
        p.SpecialAntidoteCount, p.acrobaticStar, p.acrobaticStarTurn, p.isStunned);
}
uint64_t mix(uint64_t h, uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return h ^ ((x ^ (x >> 31)) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
}
template<class T> uint64_t valueBits(T v) { return static_cast<uint64_t>(v); }
uint64_t valueBits(double v) {
    uint64_t bits; std::memcpy(&bits, &v, sizeof(bits)); return bits;
}
uint64_t hashNode(const Node& n) {
    uint64_t h = mix(n.state, static_cast<uint64_t>(n.rng));
    for (const auto& p : n.p) {
        std::apply([&](const auto&... x) { ((h = mix(h, valueBits(x))), ...); }, fields(p));
    }
    return h;
}
bool sameState(const Node& a, const Node& b) {
    return a.rng == b.rng && a.state == b.state && a.turn == b.turn &&
        fields(a.p[0]) == fields(b.p[0]) && fields(a.p[1]) == fields(b.p[1]);
}
bool betterCost(const Node& a, const Node& b) {
    return std::tie(a.events, a.changes) < std::tie(b.events, b.changes);
}
bool rankedBefore(const Node& a, const Node& b) {
    return std::tie(a.priority, a.changes, a.rng) < std::tie(b.priority, b.changes, b.rng);
}

// Menu restrictions are search-side. This is not a new world validator.
bool safeCommand(const Player& p, int command) {
    const int a = command & B::ACTION_ID_MASK;
    const bool requestBare = (command & B::ACTION_BARE_HANDS) != 0;
    if (resting(p) && (a == B::FLEE_ALLY || requestBare != bare(p))) return false;
    if (a == B::MULTITHRUST && (requestBare || p.mp < 4)) return false;
    if (a == B::MIDHEAL && p.mp < 4) return false;
    if (a == B::HEAL && p.mp < 2) return false;
    if (a == B::BUFF && p.mp < 3) return false;
    if (a == B::SPECIAL_MEDICINE && p.SpecialMedicineCount <= 0) return false;
    if (a == B::MAGIC_WATER && p.MagicWaterCount <= 0) return false;
    return true;
}

int choices(const Node& n, std::array<Choice, 24>& out) {
    const auto& p = n.p[0];
    const bool held = bare(p);
    // The next command is unavailable during a carried rest. Its normal attack
    // placeholder must still run the actual status/RNG processing, not FLEE.
    if (resting(p)) {
        out[0].action = B::ATTACK_ALLY | (held ? B::ACTION_BARE_HANDS : 0);
        return 1;
    }
    int count = 0;
    auto add = [&](int a) {
        for (int e = 0; e != 2; ++e) {
            bool requestBare = e == 0 ? held : !held;
            int command = a | (requestBare ? B::ACTION_BARE_HANDS : 0);
            if (safeCommand(p, command)) out[count++].action = command;
        }
    };
    if (p.TensionLevel < 4) add(B::PSYCHE_UP_ALLY);
    if (p.AtkBuffLevel < 2) add(B::DOUBLE_UP);
    add(B::MULTITHRUST);
    add(B::ATTACK_ALLY);
    if (p.BuffLevel < 2) add(B::BUFF);
    if (p.hp < p.maxHp) {
        add(B::SPECIAL_MEDICINE);
        add(B::MIDHEAL);
        add(B::HEAL);
    }
    if (p.mp < p.maxMp) add(B::MAGIC_WATER);
    add(B::DEFENCE);
    add(B::FLEE_ALLY);
    return count;
}

// This is only an ordering heuristic. It never proves victory or prunes a
// candidate on estimated damage. All damage/status/RNG transitions use Main.
double remaining(const Node& n, int policy) {
    const auto& p = n.p[0];
    const double tension[] = {1., 1.5, 2.5, 4., 6.};
    const double atkScale[] = {.5, .75, 1., 1.25, 1.5};
    double best = 1000.;
    for (int up = 0; up != 2; ++up) {
        if (up && p.AtkBuffLevel == 2) continue;
        double atk = B::ANONN_EQUIPPED_ATK * atkScale[up ? 4 : p.AtkBuffLevel + 2];
        double hit = std::max(1., (2. * atk - n.p[1].def) * .25);
        const double hits = policy == 2 ? 4. : 3.65;
        double throughput = hits * (hit * .5 * 6. + 12.) / 5.;
        for (int t = p.TensionLevel; t <= 4; ++t) {
            double damage = hits * (std::floor(hit * .5) * tension[t] + t * 3.);
            double rest = std::max(0., n.p[1].hp - damage);
            double cost = up + (t - p.TensionLevel) + 1. + rest / throughput;
            best = std::min(best, cost);
        }
    }
    const double hpRatio = p.hp / std::max(1., p.maxHp);
    double care = policy == 1 ? .9 : policy == 2 ? .12 : .4;
    double result = best + (resting(p) ? 1. : 0.) + care * (1. - hpRatio);
    result -= .055 * p.BuffLevel;
    if (p.mp < 4) result += .8;
    return result;
}

class Search {
    const Player* initial;
    uint64_t seed;
    std::array<int, 350> fixed{};
    int prefixLength = 0;
    Node root;
    Genome best{};
    int bestEvents = std::numeric_limits<int>::max();
    int bestChanges = std::numeric_limits<int>::max();
    std::vector<Link> paths;
    Clock::time_point deadline;
    std::array<int, 350> scratch{};
    BattleResult oneTurn;
    // Relaxed upper damage: four critical hits, no MP/survival restrictions,
    // no buff expiration, free equipment selection. Used only to rule out
    // suffixes that cannot reach the required HP even under these advantages.
    std::array<std::array<std::array<double, 5>, 5>, 350> upperDamage{};

    bool expired() const { return Clock::now() >= deadline; }
    bool bounded(const Node& n) const {
        return n.turn >= MaxTurns || n.events >= bestEvents;
    }
    void buildDamageBounds() {
        constexpr double tension[] = {1., 1.5, 2.5, 4., 6.};
        constexpr double scale[] = {.5, .75, 1., 1.25, 1.5};
        const double equipped = std::max(B::ANONN_EQUIPPED_ATK, initial[0].defaultATK);
        for (int turns = 1; turns <= MaxTurns; ++turns) {
            for (int b = 0; b != 5; ++b) {
                // A negative ATK buff is allowed to disappear for free.
                const double atk = std::max<double>(initial[0].atk, equipped * scale[std::max(2, b)]);
                double base = (2. * atk - root.p[1].def) * .25;
                const double maxBase = std::max(1., base > atk / 16. ? base * 17. / 16. + 1. : atk / 16.);
                for (int t = 0; t != 5; ++t) {
                    double normal = std::max((maxBase * tension[t] + t * 3.) * 1.2, equipped * 1.05);
                    double multi = 4. * std::max((maxBase * .5 * tension[t] + t * 3.) * 1.2, maxBase);
                    upperDamage[turns][b][t] = std::max({
                        upperDamage[turns - 1][b][t],
                        upperDamage[turns - 1][b][std::min(4, t + 1)],
                        upperDamage[turns - 1][std::min(4, b + 2)][t],
                        std::max(normal, multi) + upperDamage[turns - 1][b][0]});
                }
            }
        }
    }
    bool advance(const Node& parent, int command, Node& child) {
        child = parent;
        scratch[parent.turn] = command;
        oneTurn.position = 0;
        oneTurn.turn = 0;
        B::Main(&child.rng, 1, scratch.data(), child.p, &oneTurn, seed,
            nullptr, nullptr, -1, &child.state);
        ++child.turn;
        child.events += oneTurn.position;
        for (int i = 0; i < oneTurn.position; ++i) {
            if ((oneTurn.actions[i] & B::ACTION_EQUIPMENT_CHANGED) != 0) ++child.changes;
        }
        ++stats.expanded;
        return child.p[0].hp > 0 || child.p[1].hp == 0;
    }
    void saveVictory(const Node& candidate, uint32_t parent, int lastAction) {
        if (candidate.events > bestEvents ||
            (candidate.events == bestEvents && candidate.changes >= bestChanges)) return;
        Genome replay{};
        std::copy(fixed.begin(), fixed.end(), replay.actions);
        int at = lastAction == -1 ? prefixLength - 1 : candidate.turn - 1;
        if (lastAction != -1) replay.actions[at--] = lastAction;
        for (uint32_t link = parent; link != NoParent; link = paths[link].parent) {
            replay.actions[at--] = paths[link].action;
        }
        if (at != prefixLength - 1) { ++stats.replayRejected; return; }
        // A prefix that has already won may contain unused commands after the
        // lethal turn. Keep them byte-for-byte; Main stops at the same victory.
        replay.actions[std::max(candidate.turn, prefixLength)] = -1;
        Player p[2] = {initial[0], initial[1]};
        int rng = 1;
        uint64_t state = 0;
        BattleResult log;
        lcg::init(seed, true);
        B::Main(&rng, candidate.turn, replay.actions, p, &log, seed,
            nullptr, nullptr, -1, &state);
        int changed = 0;
        for (int i = 0; i < log.position; ++i) {
            if ((log.actions[i] & B::ACTION_EQUIPMENT_CHANGED) != 0) ++changed;
        }
        Node verified = candidate;
        verified.p[0] = p[0]; verified.p[1] = p[1];
        verified.rng = rng; verified.state = state;
        if (p[1].hp != 0 || log.position != candidate.events || changed != candidate.changes ||
            !sameState(candidate, verified)) {
            ++stats.replayRejected;
            return;
        }
        replay.AllyPlayer = p[0]; replay.EnemyPlayer = p[1];
        replay.state = state; replay.position = rng;
        replay.turn = candidate.turn + 1; replay.processed = candidate.turn;
        replay.Initialized = true; replay.fitness = log.position;
        best = replay;
        bestEvents = log.position; bestChanges = changed;
        stats.victory = true;
        stats.exactPosition = bestEvents;
        stats.equipmentChanges = changed;
    }
public:
    Search(const Player p[2], uint64_t s, Clock::time_point end) : initial(p), seed(s), deadline(end) {
        fixed.fill(-1);
        root.p[0] = p[0]; root.p[1] = p[1];
        best.turn = std::numeric_limits<int>::max();
        best.AllyPlayer = p[0]; best.EnemyPlayer = p[1];
    }
    bool prepare(const int prefix[350]) {
        while (prefixLength < 350 && prefix[prefixLength] != -1) {
            if (prefix[prefixLength] == 0) return false;
            fixed[prefixLength] = prefix[prefixLength];
            ++prefixLength;
        }
        stats.prefixLength = prefixLength;
        if (prefixLength >= 350) return false;
        std::copy(fixed.begin(), fixed.end(), best.actions);
        lcg::init(seed, true);
        for (int t = 0; t < prefixLength; ++t) {
            if (root.p[1].hp == 0) break;
            if (root.p[0].hp == 0 || t >= MaxTurns) return false;
            if (!safeCommand(root.p[0], fixed[t])) return false;
            Node next;
            advance(root, fixed[t], next);
            root = next;
        }
        stats.inputValid = true;
        buildDamageBounds();
        if (root.p[1].hp == 0) saveVictory(root, NoParent, -1);
        return root.p[0].hp > 0 && root.p[1].hp != 0;
    }
    void beam(int width, int policy, Clock::time_point phaseEnd) {
        paths.clear();
        std::vector<Node> current{root};
        std::vector<Node> next;
        next.reserve(static_cast<size_t>(width) * 16);
        std::unordered_map<uint64_t, size_t> seen;
        seen.reserve(static_cast<size_t>(width) * 16);
        while (!current.empty() && !expired() && Clock::now() < phaseEnd) {
            next.clear(); seen.clear();
            // Path links avoid copying the 350-command array into every node.
            for (const Node& parent : current) {
                if (bounded(parent)) continue;
                std::array<Choice, 24> menu{};
                int count = choices(parent, menu);
                for (int k = 0; k < count; ++k) {
                    if ((stats.expanded & 127) == 0 && (expired() || Clock::now() >= phaseEnd)) return;
                    Node child;
                    if (!advance(parent, menu[k].action, child)) continue;
                    if (child.p[1].hp == 0) {
                        saveVictory(child, parent.path, menu[k].action);
                        continue;
                    }
                    if (bounded(child)) continue;
                    child.priority = remaining(child, policy) + .0004 * child.changes;
                    uint64_t hash = hashNode(child);
                    auto it = seen.find(hash);
                    if (it != seen.end() && sameState(child, next[it->second])) {
                        if (!betterCost(child, next[it->second])) continue;
                        child.path = next[it->second].path;
                        paths[child.path] = {parent.path, menu[k].action};
                        next[it->second] = child;
                        continue;
                    }
                    child.path = static_cast<uint32_t>(paths.size());
                    paths.push_back({parent.path, menu[k].action});
                    seen[hash] = next.size();
                    next.push_back(child);
                }
            }
            if (next.size() > static_cast<size_t>(width)) {
                std::nth_element(next.begin(), next.begin() + width, next.end(), rankedBefore);
                next.resize(width);
            }
            std::sort(next.begin(), next.end(), rankedBefore);
            current.swap(next);
        }
    }
    void bestFirst(double weight, int policy, Clock::time_point phaseEnd) {
        paths.clear();
        std::vector<Node> nodes;
        nodes.reserve(65536);
        struct Entry { double score; int changes; uint32_t id; };
        auto cmp = [](const Entry& a, const Entry& b) {
            return std::tie(a.score, a.changes, a.id) > std::tie(b.score, b.changes, b.id);
        };
        std::priority_queue<Entry, std::vector<Entry>, decltype(cmp)> open(cmp);
        std::unordered_map<uint64_t, uint32_t> seen;
        seen.reserve(65536);
        nodes.push_back(root);
        open.push({root.events + weight * 2. * remaining(root, policy), root.changes, 0});
        while (!open.empty() && !expired() && Clock::now() < phaseEnd) {
            Node parent = nodes[open.top().id];
            open.pop();
            if (bounded(parent)) continue;
            std::array<Choice, 24> menu{};
            int count = choices(parent, menu);
            for (int k = 0; k < count; ++k) {
                if ((stats.expanded & 127) == 0 && (expired() || Clock::now() >= phaseEnd)) return;
                Node child;
                if (!advance(parent, menu[k].action, child)) continue;
                if (child.p[1].hp == 0) {
                    saveVictory(child, parent.path, menu[k].action);
                    continue;
                }
                if (bounded(child)) continue;
                uint64_t hash = hashNode(child);
                auto it = seen.find(hash);
                if (it != seen.end() && sameState(child, nodes[it->second]) &&
                    !betterCost(child, nodes[it->second])) continue;
                if (nodes.size() >= 500000) return;
                child.path = static_cast<uint32_t>(paths.size());
                paths.push_back({parent.path, menu[k].action});
                uint32_t id = static_cast<uint32_t>(nodes.size());
                seen[hash] = id;
                nodes.push_back(child);
                double score = child.events + weight * 2. * remaining(child, policy);
                open.push({score, child.changes, id});
            }
        }
    }
    void boundedDepthFirst(Clock::time_point phaseEnd) {
        if (bestEvents == std::numeric_limits<int>::max()) return;
        paths.clear();
        struct Slot { Node node; int epoch = 0; };
        std::vector<Slot> seen(65536);
        int epoch = 0;
        bool stop = false;
        auto visit = [&](auto&& self, const Node& parent, int eventLimit) -> void {
            if (stop || bounded(parent)) return;
            if ((stats.expanded & 127) == 0 && (expired() || Clock::now() >= phaseEnd)) {
                stop = true; return;
            }
            eventLimit = std::min(eventLimit, bestEvents);
            if (eventLimit == bestEvents && parent.changes >= bestChanges) --eventLimit;
            int available = (eventLimit - parent.events + 1) / 2;
            // Only isStunned is guaranteed to consume this turn in Main.
            // Sleep may be removed by an earlier enemy hit; do not subtract
            // a turn from an upper bound on that state's possible damage.
            if (parent.p[0].isStunned) --available;
            if (available <= 0 ||
                upperDamage[std::min(available, MaxTurns)][parent.p[0].AtkBuffLevel + 2]
                    [parent.p[0].TensionLevel] < parent.p[1].hp) return;
            auto& slot = seen[hashNode(parent) & (seen.size() - 1)];
            if (slot.epoch == epoch && sameState(parent, slot.node) && !betterCost(parent, slot.node)) return;
            slot.node = parent; slot.epoch = epoch;
            std::array<Choice, 24> menu{};
            int count = choices(parent, menu);
            struct Child { Node node; int action; };
            std::array<Child, 24> children;
            int live = 0;
            for (int k = 0; k < count; ++k) {
                Node child;
                if (!advance(parent, menu[k].action, child)) continue;
                if (child.events > eventLimit) continue;
                if (child.p[1].hp == 0) {
                    saveVictory(child, parent.path, menu[k].action);
                } else if (!bounded(child)) {
                    child.priority = remaining(child, 2);
                    children[live++] = {child, menu[k].action};
                }
            }
            std::sort(children.begin(), children.begin() + live, [](const Child& a, const Child& b) {
                return rankedBefore(a.node, b.node);
            });
            for (int k = 0; k < live && !stop; ++k) {
                auto& child = children[k];
                child.node.path = static_cast<uint32_t>(paths.size());
                paths.push_back({parent.path, child.action});
                self(self, child.node, eventLimit);
                paths.pop_back();
            }
        };
        // Increasing event limits, not a known tactical sequence. The fixed
        // prefix and every generated action still pass through the real world.
        for (int limit = root.events + 1; limit <= bestEvents && !stop; ++limit) {
            if (expired() || Clock::now() >= phaseEnd) break;
            if (limit == bestEvents && bestChanges == root.changes) break;
            ++epoch;
            visit(visit, root, limit);
        }
    }
    bool hasVictory() const { return stats.victory; }
    Genome finish() { return best; }
};
}

void SetVariant(int variant) { selectedVariant = std::clamp(variant, 0, 9); }
const Statistics& LastStatistics() { return stats; }

Genome Run(const Player initial[2], uint64_t seed, const int prefix[350], int budgetMs) {
    const auto start = Clock::now();
    const auto deadline = start + std::chrono::milliseconds(std::max(1, budgetMs));
    stats = {};
    stats.variant = selectedVariant;
    Genome answer;
    {
    // Leave room for temporary frontier destruction and the caller's dump
    // replay. Statistics below include the search object's destruction.
    const auto end = deadline - std::chrono::milliseconds(std::min(50, std::max(0, budgetMs / 20)));
    Search search(initial, seed, end);
    if (search.prepare(prefix)) {
        switch (selectedVariant) {
        case 1: search.bestFirst(1.2, 0, deadline); break;
        case 2: search.bestFirst(1.8, 1, deadline); break;
        case 3: search.beam(512, 0, deadline); break;
        case 4: search.beam(4096, 1, deadline); break;
        case 5: search.beam(16384, 2, deadline); break;
        case 6: search.bestFirst(1., 2, deadline); break;
        case 7: search.beam(32768, 0, deadline); break;
        case 8:
            search.beam(512, 0, start + std::chrono::milliseconds(budgetMs / 10));
            search.boundedDepthFirst(deadline);
            break;
        case 9: search.beam(65536, 2, deadline); break;
        default:
            search.beam(512, 0, start + std::chrono::milliseconds(budgetMs / 10));
            if (!search.hasVictory()) {
                search.bestFirst(1.8, 1, start + std::chrono::milliseconds(budgetMs * 2 / 3));
            }
            if (!search.hasVictory()) search.beam(4096, 2, deadline);
            search.boundedDepthFirst(deadline);
            break;
        }
    }
    answer = search.finish();
    }
    stats.elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    return answer;
}
}