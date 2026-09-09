#include "BattleSearch.h"

#include <algorithm>
#include <atomic>
#include <barrier>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <thread>
#include <tuple>
#include <vector>

namespace battle_search {
namespace {
using Clock = std::chrono::steady_clock;

auto fields(const Player& p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.defaultSpeed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis, p.paralysisLevel,
        p.paralysisTurns, p.SpecialMedicineCount, p.defence, p.sleeping,
        p.sleepingTurn, p.BuffLevel, p.BuffTurns, p.hasMagicMirror,
        p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn, p.TensionLevel,
        p.rage, p.SageElixirCount, p.ElfinElixirCount, p.MagicWaterCount,
        p.speedTurn, p.speedLevel, p.PoisonTurn, p.PoisonEnable,
        p.SpecialAntidoteCount, p.acrobaticStar, p.acrobaticStarTurn,
        p.rageTurns, p.medicinal_herbs_count, p.inactive);
}

uint64_t mix(uint64_t x) {
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

uint64_t stateKey(const State& s) {
    // This is only a lookup accelerator. Collisions are checked with full,
    // field-by-field equality; padding and approximate keys never prune states.
    uint64_t h = mix(s.nowState ^ (uint64_t(s.position) << 32));
    for (const Player& p : s.players) {
        h = mix(h ^ uint32_t(p.hp) ^ (uint64_t(uint32_t(p.mp)) << 32));
        h = mix(h ^ uint32_t(p.acrobaticStarTurn)
            ^ (uint64_t(uint32_t(p.specialChargeTurn)) << 32));
        h = mix(h ^ uint32_t(p.paralysisTurns)
            ^ (uint64_t(uint32_t(p.medicinal_herbs_count)) << 32)
            ^ (uint64_t(p.specialCharge) << 9) ^ (uint64_t(p.acrobaticStar) << 10)
            ^ (uint64_t(p.paralysis) << 11) ^ (uint64_t(p.inactive) << 12));
    }
    return h;
}

void transition(const State& s, int32_t action, uint64_t seed,
                State& next, BattleResult* trace = nullptr) {
    next = s;
    // logicalTurnStart + RunCount=1 reads exactly Gene[0]. Avoid clearing a
    // 350-command buffer on every hot-path transition.
    const int32_t gene[1] = {action};
    BattleEmulator::Main(&next.position, 1, gene, next.players, trace, seed,
        nullptr, nullptr, trace ? -1 : -2, &next.nowState, true);
}

bool canSkip(const BattleResult& probe) {
    // An enemy acting first can inflict paralysis/inactivity this turn.
    // Probe the ordinary attack with the real emulator before issuing FLEE.
    for (int i = 0; i < probe.position; ++i) {
        if (!probe.isEnemy[i] && probe.actions[i] != BattleEmulator::ATTACK_ALLY)
            return false;
    }
    return true;
}

bool better(const State& candidate, int depth, const Result& best) {
    const bool alive = candidate.players[0].hp > 0;
    const bool won = alive && candidate.players[1].hp == 0;
    if (won != best.won) return won;
    if (won) {
        if (depth != best.length) return depth < best.length;
        if (candidate.players[0].hp != best.finalState.players[0].hp)
            return candidate.players[0].hp > best.finalState.players[0].hp;
        return candidate.position < best.finalState.position;
    }
    // With a positive budget, retain an actual explored continuation even if
    // the initial state cannot make progress before becoming incapacitated.
    if (best.length == 0 && depth > 0) return true;
    if (alive != (best.finalState.players[0].hp > 0)) return alive;
    if (candidate.players[1].hp != best.finalState.players[1].hp)
        return candidate.players[1].hp < best.finalState.players[1].hp;
    if (candidate.players[0].hp != best.finalState.players[0].hp)
        return candidate.players[0].hp > best.finalState.players[0].hp;
    return depth < best.length;
}

struct TraceLink {
    int parent;
    int32_t action;
};

struct Node {
    State state;
    uint64_t key = 0;
    int link = -1;
    int32_t action = 0;
    float rank = 0;
    float sustain = 0;
    float offense = 0;
    float star = 0;
};

struct Shared {
    std::mutex mutex;
    Result best;
    std::atomic<int> horizon;
    std::atomic<bool> settled{false};
    Clock::time_point deadline;
    explicit Shared(const State& start, int turns, Clock::time_point end)
        : horizon(turns), deadline(end) {
        best.finalState = start;
        best.won = start.players[0].hp > 0 && start.players[1].hp == 0;
    }
};

struct Worker {
    Shared& shared;
    const State& root;
    uint64_t seed;
    int id;
    uint64_t work = 0, duplicates = 0, passes = 0;
    Result localBest;
    std::vector<Node> beam, children, next;
    std::vector<TraceLink> history;
    std::vector<int> slots, order;
    std::vector<unsigned char> selected;
    BattleResult probe;

    Worker(Shared& sh, const State& start, uint64_t s, int worker)
        : shared(sh), root(start), seed(s), id(worker), localBest(sh.best) {}

    bool expired() const {
        return shared.settled.load(std::memory_order_relaxed) || Clock::now() >= shared.deadline;
    }

    void publish(const Node& n, int depth, const std::array<int32_t, 350>& prefix,
                 int prefixLength) {
        if (!better(n.state, depth, localBest)) return;
        localBest.finalState = n.state;
        localBest.length = depth;
        localBest.won = n.state.players[0].hp > 0 && n.state.players[1].hp == 0;
        localBest.actions = prefix;
        int cursor = depth - 1;
        localBest.actions[cursor--] = n.action;
        for (int link = n.link; link >= 0 && cursor >= prefixLength;
             link = history[link].parent) {
            localBest.actions[cursor--] = history[link].action;
        }
        std::fill(localBest.actions.begin() + depth, localBest.actions.end(), 0);
        std::lock_guard lock(shared.mutex);
        if (better(n.state, depth, shared.best)) {
            shared.best = localBest;
            if (localBest.won) shared.horizon.store(depth, std::memory_order_relaxed);
        }
    }

    void rank(Node& n, int profile, uint64_t salt) const {
        const Player& a = n.state.players[0];
        const Player& e = n.state.players[1];
        const float attack = std::max(1.0f, a.atk * 0.5f - e.def * 0.25f);
        const float hit = std::max(8.0f, e.atk * 0.5f - a.def * 0.25f);
        const float hp = std::min(float(a.hp), hit * 2.6f);
        const float lowHp = std::max(0.0f, hit * 1.2f - a.hp);
        const float resources = std::min(6.0f,
            std::max(0, a.mp) * 0.5f + std::max(0, a.medicinal_herbs_count));
        const float active = a.acrobaticStar ?
            std::min(5, std::max(0, a.acrobaticStarTurn)) / 5.0f : 0.0f;
        const float charge = a.specialCharge && a.specialChargeTurn > 0 ?
            std::min(3, a.specialChargeTurn) / 3.0f : 0.0f;
        const float disabled = a.paralysis ?
            std::max(1, a.paralysisTurns + 1) * (attack + hit * 0.45f) : 0;
        const float inactive = a.inactive ? attack * 0.65f : 0;
        const float future = std::min(1.0f, e.hp / (attack * 5.0f));
        const float reserve = (hp * 0.52f - lowHp * 1.9f + resources * 1.7f
            + active * (attack * 1.4f + hit * 0.8f)
            + charge * (a.acrobaticStar ? attack * 0.65f : attack * 1.3f)
            - disabled - inactive) * future;
        n.offense = -float(e.hp) - lowHp * 0.6f - disabled * 0.45f;
        n.sustain = -float(e.hp) + reserve * 1.8f;
        n.star = -float(e.hp) + reserve + future *
            (active * attack * 2.0f + charge * attack);
        constexpr float weights[] = {0.85f, 1.35f, 0.4f, 1.0f};
        const float noise = float(mix(n.key ^ salt) >> 40) / 16777216.0f;
        n.rank = -float(e.hp) + reserve * weights[profile % 4]
            + (profile % 4 == 3 ? active * attack * future : 0)
            + noise * attack * (profile >= 4 ? 0.65f : 0.12f);
    }

    bool insert(Node&& n) {
        size_t slot = n.key & (slots.size() - 1);
        while (slots[slot] >= 0) {
            const Node& old = children[slots[slot]];
            if (old.key == n.key && sameState(old.state, n.state)) {
                ++duplicates;
                return false;
            }
            slot = (slot + 1) & (slots.size() - 1);
        }
        slots[slot] = int(children.size());
        children.push_back(std::move(n));
        return true;
    }

    bool select(int width) {
        next.clear();
        const int count = int(children.size());
        const int keep = std::min(width, count);
        next.reserve(keep);
        if (count <= width) {
            for (Node& n : children) {
                history.push_back({n.link, n.action});
                n.link = int(history.size()) - 1;
            }
            beam.swap(children);
            return true;
        }
        selected.assign(count, 0);
        order.resize(count);
        std::iota(order.begin(), order.end(), 0);
        auto take = [&](int amount, auto score) {
            if (amount <= 0 || order.empty()) return;
            amount = std::min(amount, int(order.size()));
            auto compare = [&](int a, int b) {
                const float av = score(children[a]), bv = score(children[b]);
                return av != bv ? av > bv : children[a].key < children[b].key;
            };
            if (amount < int(order.size()))
                std::nth_element(order.begin(), order.begin() + amount, order.end(), compare);
            for (int i = 0; i < amount; ++i) {
                int index = order[i];
                selected[index] = 1;
                Node n = children[index];
                history.push_back({n.link, n.action});
                n.link = int(history.size()) - 1;
                next.push_back(std::move(n));
            }
            order.erase(std::remove_if(order.begin(), order.end(),
                [&](int index) { return selected[index] != 0; }), order.end());
        };
        // Reserve slots for different long-term strategies instead of allowing
        // one health/charge tradeoff to monopolize the frontier.
        take(keep / 2, [](const Node& n) { return n.rank; });
        if (expired()) return false;
        // Different random positions expose different future counters, charges
        // and criticals. Reserve a quarter of the beam for their best distinct
        // representatives so minor resource variants cannot crowd them out.
        auto group = [](const Node& n) {
            const Player& a = n.state.players[0];
            return mix(uint64_t(n.state.position) ^ (n.state.nowState << 17)
                ^ (uint64_t(a.acrobaticStar) << 60) ^ (uint64_t(a.specialCharge) << 59)
                ^ (uint64_t(a.paralysis) << 58) ^ (uint64_t(a.inactive) << 57));
        };
        std::fill(slots.begin(), slots.end(), -1);
        for (int index : order) {
            if ((index & 1023) == 0 && expired()) return false;
            const uint64_t key = group(children[index]);
            size_t slot = key & (slots.size() - 1);
            while (slots[slot] >= 0 && group(children[slots[slot]]) != key)
                slot = (slot + 1) & (slots.size() - 1);
            if (slots[slot] < 0 || children[index].rank > children[slots[slot]].rank)
                slots[slot] = index;
        }
        order.clear();
        for (int index : slots) if (index >= 0) order.push_back(index);
        take(keep / 4, [](const Node& n) { return n.rank; });
        if (expired()) return false;
        order.clear();
        for (int i = 0; i < count; ++i) if (!selected[i]) order.push_back(i);
        take(keep / 10, [](const Node& n) { return n.offense; });
        take(keep / 16, [](const Node& n) { return n.sustain; });
        take(keep - int(next.size()), [](const Node& n) { return n.star; });
        beam.swap(next);
        return true;
    }

    void pass(int width, int profile, const State& start,
              const std::array<int32_t, 350>& prefix, int prefixLength) {
        ++passes;
        beam.clear();
        history.clear();
        beam.push_back(Node{start});
        bool truncated = false;
        const uint64_t salt = mix(seed + passes * 12347 + id * 7919);
        for (int depth = prefixLength + 1;
             depth <= shared.horizon.load(std::memory_order_relaxed) && !beam.empty(); ++depth) {
            if (expired()) return;
            children.clear();
            const size_t capacity = beam.size() * 8;
            children.reserve(capacity);
            size_t tableSize = 16;
            while (tableSize < capacity * 2) tableSize *= 2;
            slots.assign(tableSize, -1);
            for (const Node& parent : beam) {
                if ((work & 63) == 0 && expired()) return;
                std::array<int32_t, 8> actions;
                const int count = legalActions(parent.state, actions);
                bool skipAllowed = false;
                for (int i = 0; i < count; ++i) {
                    if (actions[i] == BattleEmulator::FLEE_ALLY && !skipAllowed) continue;
                    Node child;
                    child.link = parent.link;
                    child.action = actions[i];
                    if (i == 0) {
                        probe.clear();
                        transition(parent.state, actions[i], seed, child.state, &probe);
                        skipAllowed = canSkip(probe);
                    } else {
                        transition(parent.state, actions[i], seed, child.state);
                    }
                    ++work;
                    publish(child, depth, prefix, prefixLength);
                    if (child.state.players[0].hp <= 0 || child.state.players[1].hp <= 0)
                        continue;
                    if (depth >= shared.horizon.load(std::memory_order_relaxed)) continue;
                    child.key = stateKey(child.state);
                    rank(child, profile, salt);
                    insert(std::move(child));
                }
            }
            if (expired()) return;
            truncated = truncated || children.size() > size_t(width);
            if (!select(width)) return;
        }
        // If no beam selection discarded a state, repeating the same complete
        // root tree cannot improve either a win or an unavoidable loss.
        if (prefixLength == 0 && !truncated)
            shared.settled.store(true, std::memory_order_relaxed);
    }

    void run() {
        int width = 96;
        int iteration = 0;
        while (!expired()) {
            State start = root;
            std::array<int32_t, 350> prefix{};
            int prefixLength = 0;
            // Reoptimize a discovered suffix between complete root searches.
            // Prefixes are discovered in this call, never supplied solutions.
            if (iteration % 3 == 2) {
                Result incumbent;
                {
                    std::lock_guard lock(shared.mutex);
                    incumbent = shared.best;
                }
                if (incumbent.length > 5) {
                    prefixLength = 1 + int(mix(seed + iteration + id * 71) %
                        uint64_t(incumbent.length * 2 / 3));
                    prefix = incumbent.actions;
                    for (int i = 0; i < prefixLength; ++i) {
                        State nextState;
                        transition(start, prefix[i], seed, nextState);
                        start = nextState;
                        ++work;
                    }
                }
            }
            pass(width, id + iteration / 3, start, prefix, prefixLength);
            ++iteration;
            if (iteration % 3 != 2) width = std::min(32768, width * 2);
        }
    }
};
} // namespace

bool sameState(const State& a, const State& b) {
    return a.position == b.position && a.nowState == b.nowState &&
        fields(a.players[0]) == fields(b.players[0]) &&
        fields(a.players[1]) == fields(b.players[1]);
}

int legalActions(const State& s, std::array<int32_t, 8>& actions) {
    if (s.players[0].hp <= 0 || s.players[1].hp <= 0) return 0;
    const Player& p = s.players[0];
    int n = 0;
    actions[n++] = BattleEmulator::ATTACK_ALLY;
    actions[n++] = BattleEmulator::DEFENCE;
    actions[n++] = BattleEmulator::DRAGON_SLASH;
    if (p.mp >= 2) actions[n++] = BattleEmulator::HEAL;
    if (p.mp >= 3) actions[n++] = BattleEmulator::CRACK_ALLY;
    if (p.medicinal_herbs_count > 0) actions[n++] = BattleEmulator::MEDICINAL_HERBS;
    // Main decrements specialChargeTurn before selecting this turn's command.
    if (p.specialCharge && p.specialChargeTurn > 0)
        actions[n++] = BattleEmulator::ACROBATIC_STAR;
    if (!p.paralysis && !p.inactive) actions[n++] = BattleEmulator::FLEE_ALLY;
    return n;
}

bool advance(const State& s, int32_t action, uint64_t seed, State& next, BattleResult* trace) {
    std::array<int32_t, 8> actions;
    const int count = legalActions(s, actions);
    if (std::find(actions.begin(), actions.begin() + count, action) == actions.begin() + count)
        return false;
    if (action == BattleEmulator::FLEE_ALLY) {
        thread_local BattleResult probe;
        probe.clear();
        State tested;
        transition(s, BattleEmulator::ATTACK_ALLY, seed, tested, &probe);
        if (!canSkip(probe)) return false;
    }
    transition(s, action, seed, next, trace);
    return true;
}

Result search(const State& start, uint64_t seed, const Options& options) {
    const auto begin = Clock::now();
    const int pastTurns = int((start.nowState >> 12) & 0xfffff);
    const int limit = std::clamp(options.maxTurns, 0, std::max(0, 350 - pastTurns));
    Shared shared(start, limit, begin + std::max(options.budget, std::chrono::milliseconds(0)));
    if (limit == 0 || options.budget.count() <= 0 ||
        start.players[0].hp <= 0 || start.players[1].hp <= 0) return shared.best;
    // Multiple workers require explicit initialization of the emulator context.
    const int count = options.initializeWorker ? std::clamp(options.threads, 1, 32) : 1;
    std::vector<std::unique_ptr<Worker>> workers;
    for (int i = 0; i < count; ++i)
        workers.push_back(std::make_unique<Worker>(shared, start, seed, i));
    if (count == 1) {
        workers[0]->run();
    } else {
        std::mutex initialization;
        std::barrier ready(count);
        std::vector<std::jthread> threads;
        for (int i = 1; i < count; ++i) {
            threads.emplace_back([&, i] {
                {
                    std::lock_guard lock(initialization);
                    options.initializeWorker();
                }
                ready.arrive_and_wait();
                workers[i]->run();
            });
        }
        ready.arrive_and_wait();
        workers[0]->run();
    }
    for (const auto& worker : workers) {
        shared.best.transitions += worker->work;
        shared.best.duplicates += worker->duplicates;
        shared.best.passes += worker->passes;
    }
    shared.best.elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
    return shared.best;
}

bool replay(const State& start, uint64_t seed, const Result& result,
            BattleResult& trace, State& finalState) {
    trace.clear();
    finalState = start;
    const int pastTurns = int((start.nowState >> 12) & 0xfffff);
    if (result.length < 0 || result.length > std::max(0, 350 - pastTurns)) return false;
    for (int i = 0; i < result.length; ++i) {
        State nextState;
        if (!advance(finalState, result.actions[i], seed, nextState, &trace)) return false;
        finalState = nextState;
    }
    return sameState(finalState, result.finalState) &&
        result.won == (finalState.players[0].hp > 0 && finalState.players[1].hp == 0);
}

Options environmentOptions(int threads, int maxTurns) {
    Options options;
    options.threads = threads;
    options.maxTurns = maxTurns;
    if (const char* value = std::getenv("BATTLE_SEARCH_MS")) {
        char* end = nullptr;
        const long long ms = std::strtoll(value, &end, 10);
        if (end != value && *end == '\0' && ms >= 0 && ms <= 3600000)
            options.budget = std::chrono::milliseconds(ms);
    }
    return options;
}

void printResult(const State& start, uint64_t seed, const Result& result,
                 const int32_t prefix[350], int pastTurns, TableFormatter table) {
    if (pastTurns < 0 || pastTurns > 350 || result.length < 0 || result.length > 350 - pastTurns) {
        std::cerr << "Invalid search table range\n";
        return;
    }
    BattleResult trace;
    State finalState;
    if (!replay(start, seed, result, trace, finalState)) {
        std::cerr << "Search replay verification failed\n";
        return;
    }
    std::array<int32_t, 350> gene{};
    std::copy_n(prefix, pastTurns, gene.begin());
    std::copy_n(result.actions.begin(), result.length, gene.begin() + pastTurns);
    // The authoritative table comes first; all metrics follow it.
    std::cout << table(trace, gene.data(), pastTurns)
              << "Search: " << (result.won ? "WIN" : "partial")
              << ", turns=" << result.length
              << ", HP=" << finalState.players[0].hp
              << ", enemyHP=" << finalState.players[1].hp
              << ", MP=" << finalState.players[0].mp
              << ", position=" << finalState.position
              << ", transitions=" << result.transitions
              << ", duplicates=" << result.duplicates
              << ", passes=" << result.passes
              << ", search_ms=" << result.elapsedMs
              << ", replay=exact\n";
}

} // namespace battle_search
