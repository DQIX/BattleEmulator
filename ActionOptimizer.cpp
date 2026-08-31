#include "ActionOptimizer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <future>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "BattleEmulator.h"
#include "lcg.h"

namespace {

constexpr int kActionHistoryCapacity = 350;
// Workspace capacity only, never a silent search cutoff. Geruniku is normally
// decided in roughly 15 total turns; 32 slots leave ample headroom without
// carrying the old 350-turn path in every dominance record. If IDDFS ever
// reaches this capacity without a win, fail loudly instead of claiming that no
// solution exists.
constexpr int kSearchPathCapacity = 32;
constexpr int kMaxSearchThreads = 8;
constexpr std::uint32_t kParallelNodeCapacity = 16384;
constexpr std::uint32_t kInvalidParallelNode = UINT32_MAX;


// Scenario profile, not a heuristic. IDDFS explores every selectable command in
// this explicit profile at every depth. The profile contains the known victory
// backbone, instant-death attempts and practical RNG-adjustment commands.
#if defined(gerunikku)
constexpr std::array<BattleEmulator::SearchCommand, 36> kScenarioCommands{{
    {BattleEmulator::MAGIC_MIRROR, -1},
    {BattleEmulator::BUFF, -1},
    {BattleEmulator::PSYCHE_UP_ALLY, -1},
    {BattleEmulator::DOUBLE_UP, -1},
    {BattleEmulator::MULTITHRUST, 2},

    {BattleEmulator::ZAKI, 1},
    {BattleEmulator::ZAKI, 3},
    {BattleEmulator::ZARAKI, 1},
    {BattleEmulator::ZARAKI, 3},

    {BattleEmulator::ATTACK_ALLY, 1},
    {BattleEmulator::ATTACK_ALLY, 2},
    {BattleEmulator::ATTACK_ALLY, 3},
    {BattleEmulator::THUNDER_THRUST, 1},
    {BattleEmulator::THUNDER_THRUST, 2},
    {BattleEmulator::THUNDER_THRUST, 3},
    {BattleEmulator::BEAST_THRUST, 1},
    {BattleEmulator::BEAST_THRUST, 2},
    {BattleEmulator::BEAST_THRUST, 3},
    {BattleEmulator::VITAL_POINT_THRUST, 1},
    {BattleEmulator::VITAL_POINT_THRUST, 2},
    {BattleEmulator::VITAL_POINT_THRUST, 3},
    {BattleEmulator::MERCURIAL_THRUST, 1},
    {BattleEmulator::MERCURIAL_THRUST, 2},
    {BattleEmulator::MERCURIAL_THRUST, 3},

    // Boss battles cannot actually escape here. In BattleEmulator this skips
    // the hero pre-action path while turn-order/enemy/turn-end RNG still runs,
    // making FLEE a useful RNG-adjustment command rather than a terminal state.
    {BattleEmulator::FLEE_ALLY, -1},

    {BattleEmulator::MIDHEAL, -1},
    {BattleEmulator::MORE_HEAL, -1},
    {BattleEmulator::FULLHEAL, -1},
    {BattleEmulator::SPECIAL_MEDICINE, -1},
    {BattleEmulator::MAGIC_WATER, -1},
    {BattleEmulator::SAGE_ELIXIR, -1},
    {BattleEmulator::ELFIN_ELIXIR, -1},
    {BattleEmulator::DEFENCE, -1},
    {BattleEmulator::DEFENDING_CHAMPION, -1},
    {BattleEmulator::INSULATE, -1},
    {BattleEmulator::GOSPEL_SONG, -1},
}};
#else
constexpr std::array<BattleEmulator::SearchCommand, 20> kScenarioCommands{{
    {BattleEmulator::ATTACK_ALLY, 1},
    {BattleEmulator::THUNDER_THRUST, 1},
    {BattleEmulator::BEAST_THRUST, 1},
    {BattleEmulator::VITAL_POINT_THRUST, 1},
    {BattleEmulator::MERCURIAL_THRUST, 1},
    {BattleEmulator::FLEE_ALLY, -1},
    {BattleEmulator::MAGIC_MIRROR, -1},
    {BattleEmulator::BUFF, -1},
    {BattleEmulator::PSYCHE_UP_ALLY, -1},
    {BattleEmulator::DOUBLE_UP, -1},
    {BattleEmulator::MULTITHRUST, 1},
    {BattleEmulator::MIDHEAL, -1},
    {BattleEmulator::MORE_HEAL, -1},
    {BattleEmulator::FULLHEAL, -1},
    {BattleEmulator::SPECIAL_MEDICINE, -1},
    {BattleEmulator::MAGIC_WATER, -1},
    {BattleEmulator::SAGE_ELIXIR, -1},
    {BattleEmulator::ELFIN_ELIXIR, -1},
    {BattleEmulator::DEFENCE, -1},
    {BattleEmulator::DEFENDING_CHAMPION, -1},
}};
#endif

struct SearchWorkspace {
    BattleEmulator::SearchState root{};
    std::array<BattleEmulator::SearchState, kSearchPathCapacity + 1> states{};
    std::array<std::uint16_t, kSearchPathCapacity> path{};
    std::array<std::uint16_t, kSearchPathCapacity> solution{};
    int solutionDepth{-1};
    std::uint64_t visitedNodes{};
};

// One synchronous search owns one worker/WASM instance. Keep search memory fixed
// and C++-owned: no per-node malloc, no 350-action Genome copies, no open set.
SearchWorkspace gWorkspace{};
std::uint32_t gNodesUsed{};

struct ParallelNode {
    BattleEmulator::SearchState state{};
    std::array<std::uint16_t, kSearchPathCapacity> path{};
    std::uint8_t depth{};
    std::uint32_t next{kInvalidParallelNode};
};

struct ParallelSearchWorkspace {
    BattleEmulator::SearchState root{};
    std::array<ParallelNode, kParallelNodeCapacity> nodes{};
    // Tagged stack heads: high 32 bits are an ABA counter, low 32 bits are a slot index.
    std::atomic<std::uint64_t> freeHead{};
    std::array<std::atomic<std::uint64_t>, kMaxSearchThreads> taskHeads{};
    std::atomic<std::uint64_t> outstanding{};
    std::atomic<bool> solutionFound{};
    std::atomic<bool> abortRequested{};
    std::array<std::uint64_t, kMaxSearchThreads> workerVisited{};
    std::array<std::uint16_t, kSearchPathCapacity> solution{};
    BattleEmulator::SearchState solutionState{};
    int solutionDepth{-1};
    int depthLimit{};
    int activeThreadCount{};
    std::uint64_t seed{};
};

ParallelSearchWorkspace gParallelWorkspace{};

[[nodiscard]] constexpr std::uint64_t TaggedHead(
    const std::uint32_t tag,
    const std::uint32_t index
) noexcept {
    return (static_cast<std::uint64_t>(tag) << 32) | index;
}

[[nodiscard]] constexpr std::uint32_t HeadIndex(const std::uint64_t head) noexcept {
    return static_cast<std::uint32_t>(head);
}

[[nodiscard]] constexpr std::uint32_t HeadTag(const std::uint64_t head) noexcept {
    return static_cast<std::uint32_t>(head >> 32);
}

void PushParallelStack(
    ParallelSearchWorkspace& workspace,
    std::atomic<std::uint64_t>& head,
    const std::uint32_t nodeIndex
) noexcept {
    std::uint64_t observed = head.load(std::memory_order_relaxed);
    for (;;) {
        workspace.nodes[nodeIndex].next = HeadIndex(observed);
        const std::uint64_t desired = TaggedHead(HeadTag(observed) + 1U, nodeIndex);
        if (head.compare_exchange_weak(
                observed, desired,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            return;
        }
    }
}

void PushParallelBatch(
    ParallelSearchWorkspace& workspace,
    std::atomic<std::uint64_t>& head,
    const std::uint32_t firstNodeIndex,
    const std::uint32_t lastNodeIndex
) noexcept {
    std::uint64_t observed = head.load(std::memory_order_relaxed);
    for (;;) {
        workspace.nodes[lastNodeIndex].next = HeadIndex(observed);
        const std::uint64_t desired = TaggedHead(HeadTag(observed) + 1U, firstNodeIndex);
        if (head.compare_exchange_weak(
                observed, desired,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            return;
        }
    }
}


[[nodiscard]] std::uint32_t PopParallelStack(
    ParallelSearchWorkspace& workspace,
    std::atomic<std::uint64_t>& head
) noexcept {
    std::uint64_t observed = head.load(std::memory_order_acquire);
    for (;;) {
        const std::uint32_t nodeIndex = HeadIndex(observed);
        if (nodeIndex == kInvalidParallelNode) return kInvalidParallelNode;
        const std::uint32_t next = workspace.nodes[nodeIndex].next;
        const std::uint64_t desired = TaggedHead(HeadTag(observed) + 1U, next);
        if (head.compare_exchange_weak(
                observed, desired,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return nodeIndex;
        }
    }
}

void ResetParallelArena(ParallelSearchWorkspace& workspace) noexcept {
    for (std::uint32_t index = 0; index < kParallelNodeCapacity; ++index) {
        workspace.nodes[index].next = index + 1U < kParallelNodeCapacity
            ? index + 1U
            : kInvalidParallelNode;
    }
    workspace.freeHead.store(TaggedHead(0, 0), std::memory_order_relaxed);
    for (auto& taskHead : workspace.taskHeads) {
        taskHead.store(TaggedHead(0, kInvalidParallelNode), std::memory_order_relaxed);
    }
    workspace.outstanding.store(0, std::memory_order_relaxed);
    workspace.solutionFound.store(false, std::memory_order_relaxed);
    workspace.abortRequested.store(false, std::memory_order_relaxed);
    workspace.workerVisited.fill(0);
    workspace.solution.fill(0);
    workspace.solutionDepth = -1;
}

[[nodiscard]] bool ParallelStopRequested(const ParallelSearchWorkspace& workspace) noexcept {
    return workspace.solutionFound.load(std::memory_order_acquire)
        || workspace.abortRequested.load(std::memory_order_acquire);
}

[[nodiscard]] constexpr std::uint16_t PackCommand(
    const BattleEmulator::SearchCommand command
) noexcept {
    return static_cast<std::uint16_t>(
        BattleEmulator::PackHeroAction(command.action, command.target));
}

[[nodiscard]] constexpr BattleEmulator::SearchCommand UnpackCommand(
    const std::uint16_t packed
) noexcept {
    return {
        BattleEmulator::HeroActionId(static_cast<int>(packed)),
        BattleEmulator::HeroTargetId(static_cast<int>(packed)),
    };
}

[[nodiscard]] bool BattleWon(const BattleEmulator::SearchState& state) noexcept {
#if defined(gerunikku)
    return state.players[0].hp > 0
        && state.players[1].hp <= 0
        && state.players[2].hp <= 0
        && state.players[3].hp <= 0;
#else
    return state.players[0].hp > 0 && state.players[1].hp <= 0;
#endif
}


[[nodiscard]] std::size_t BuildCandidates(
    const BattleEmulator::SearchState& state,
    std::array<std::uint16_t, kScenarioCommands.size()>& candidates
) noexcept {
    const Player& hero = state.players[0];

    // These states do not present a normal hero command choice. The supplied
    // action is only a placeholder; BattleEmulator replaces it with the real
    // status action during the turn.
    if (hero.paralysis || hero.sleeping || hero.inactive || hero.confused) {
        candidates[0] = PackCommand({BattleEmulator::ATTACK_ALLY, -1});
        return 1;
    }

    std::size_t count = 0;
    for (const auto command : kScenarioCommands) {
        if (!BattleEmulator::IsHeroCommandSelectable(state, command)) continue;
        candidates[count++] = PackCommand(command);
    }
    return count;
}

void ParallelSearchWorker(
    ParallelSearchWorkspace& workspace,
    const int workerIndex
) {
    // OPTIMIZE_MODE historically keeps its LCG cache in TLS, so those workers
    // initialize once. Normal/MSVC/WebAssembly builds share the cache that the
    // caller fully precomputed before worker launch; it is read-only here.
#if defined(OPTIMIZE_MODE)
    lcg::init(workspace.seed, true);
#endif

    std::uint64_t visited = 0;
    std::array<std::uint16_t, kScenarioCommands.size()> candidates{};
    std::array<std::uint32_t, kScenarioCommands.size()> readyChildren{};
    std::array<std::uint32_t, 64> freeCache{};
    std::size_t freeCacheSize = 0;

    auto acquireNode = [&]() noexcept -> std::uint32_t {
        if (freeCacheSize != 0) return freeCache[--freeCacheSize];
        return PopParallelStack(workspace, workspace.freeHead);
    };
    auto releaseNode = [&](const std::uint32_t nodeIndex) noexcept {
        if (freeCacheSize < freeCache.size()) {
            freeCache[freeCacheSize++] = nodeIndex;
        } else {
            PushParallelStack(workspace, workspace.freeHead, nodeIndex);
        }
    };

    auto acquireTask = [&]() noexcept -> std::uint32_t {
        if (const std::uint32_t own = PopParallelStack(
                workspace,
                workspace.taskHeads[static_cast<std::size_t>(workerIndex)]);
            own != kInvalidParallelNode) {
            return own;
        }
        for (int offset = 1; offset < workspace.activeThreadCount; ++offset) {
            const int victim = (workerIndex + offset) % workspace.activeThreadCount;
            if (const std::uint32_t stolen = PopParallelStack(
                    workspace,
                    workspace.taskHeads[static_cast<std::size_t>(victim)]);
                stolen != kInvalidParallelNode) {
                return stolen;
            }
        }
        return kInvalidParallelNode;
    };

    while (!ParallelStopRequested(workspace)) {
        const std::uint32_t nodeIndex = acquireTask();
        if (nodeIndex == kInvalidParallelNode) {
            if (workspace.outstanding.load(std::memory_order_acquire) == 0) break;
            std::this_thread::yield();
            continue;
        }

        ParallelNode& node = workspace.nodes[nodeIndex];
        ++visited;

        if (BattleWon(node.state)) {
            bool expected = false;
            if (workspace.solutionFound.compare_exchange_strong(
                    expected, true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                std::copy_n(node.path.begin(), node.depth, workspace.solution.begin());
                workspace.solutionState = node.state;
                workspace.solutionDepth = node.depth;
            }
        } else if (node.state.players[0].hp > 0 && node.depth < workspace.depthLimit) {
            const std::size_t candidateCount = BuildCandidates(node.state, candidates);
            std::size_t readyCount = 0;
            // Global task storage is a LIFO stack. Publish in reverse command
            // order so the serial-first command remains the first likely task.
            for (std::size_t reverse = candidateCount;
                 reverse > 0 && !ParallelStopRequested(workspace);
                 --reverse) {
                const std::size_t candidateIndex = reverse - 1;
                const std::uint32_t childIndex = acquireNode();
                if (childIndex == kInvalidParallelNode) {
                    // Capacity is only a memory-safety boundary. Never turn pool
                    // exhaustion into a missing branch / approximate result.
                    workspace.abortRequested.store(true, std::memory_order_release);
                    break;
                }

                ParallelNode& child = workspace.nodes[childIndex];
                child.depth = static_cast<std::uint8_t>(node.depth + 1);
                std::copy_n(node.path.begin(), node.depth, child.path.begin());
                child.path[node.depth] = candidates[candidateIndex];

                if (!BattleEmulator::StepSearchState(
                        node.state,
                        UnpackCommand(candidates[candidateIndex]),
                        &child.state)) {
                    releaseNode(childIndex);
                    continue;
                }
                readyChildren[readyCount++] = childIndex;
            }

            if (workspace.abortRequested.load(std::memory_order_acquire)
                || workspace.solutionFound.load(std::memory_order_acquire)) {
                for (std::size_t index = 0; index < readyCount; ++index) {
                    releaseNode(readyChildren[index]);
                }
            } else if (readyCount != 0) {
                for (std::size_t index = readyCount; index > 1; --index) {
                    workspace.nodes[readyChildren[index - 1]].next = readyChildren[index - 2];
                }
                workspace.outstanding.fetch_add(readyCount, std::memory_order_relaxed);
                PushParallelBatch(
                    workspace,
                    workspace.taskHeads[static_cast<std::size_t>(workerIndex)],
                    readyChildren[readyCount - 1],
                    readyChildren[0]
                );
            }
        }

        releaseNode(nodeIndex);
        workspace.outstanding.fetch_sub(1, std::memory_order_acq_rel);
    }

    workspace.workerVisited[static_cast<std::size_t>(workerIndex)] = visited;
}

[[nodiscard]] bool RunParallelDepth(
    ParallelSearchWorkspace& workspace,
    const int depthLimit,
    const int threadCount
) {
    ResetParallelArena(workspace);
    workspace.depthLimit = depthLimit;
    workspace.activeThreadCount = threadCount;

    const std::uint32_t rootIndex = PopParallelStack(workspace, workspace.freeHead);
    if (rootIndex == kInvalidParallelNode) {
        throw std::runtime_error("parallel IDDFS node arena has no root slot");
    }
    ParallelNode& rootNode = workspace.nodes[rootIndex];
    rootNode.state = workspace.root;
    rootNode.path.fill(0);
    rootNode.depth = 0;
    workspace.outstanding.store(1, std::memory_order_relaxed);
    PushParallelStack(workspace, workspace.taskHeads[0], rootIndex);

    std::array<std::future<void>, kMaxSearchThreads> futures{};
    for (int worker = 0; worker < threadCount; ++worker) {
        futures[static_cast<std::size_t>(worker)] = std::async(
            std::launch::async,
            [&workspace, worker] { ParallelSearchWorker(workspace, worker); }
        );
    }
    for (int worker = 0; worker < threadCount; ++worker) {
        futures[static_cast<std::size_t>(worker)].get();
    }

    if (workspace.abortRequested.load(std::memory_order_acquire)) {
        throw std::runtime_error(
            "parallel IDDFS node arena exhausted; increase kParallelNodeCapacity instead of dropping branches");
    }
    return workspace.solutionFound.load(std::memory_order_acquire);
}


[[nodiscard]] bool DepthFirstSearch(
    SearchWorkspace& workspace,
    const int depth,
    const int depthLimit
) {
    ++workspace.visitedNodes;

    const BattleEmulator::SearchState& currentState = workspace.states[depth];
    if (BattleWon(currentState)) {
        workspace.solutionDepth = depth;
        std::copy_n(workspace.path.begin(), depth, workspace.solution.begin());
        return true;
    }
    if (currentState.players[0].hp <= 0 || depth >= depthLimit) return false;

    std::array<std::uint16_t, kScenarioCommands.size()> candidates{};
    const std::size_t candidateCount = BuildCandidates(currentState, candidates);
    for (std::size_t index = 0; index < candidateCount; ++index) {
        workspace.path[depth] = candidates[index];
        if (!BattleEmulator::StepSearchState(
                currentState, UnpackCommand(candidates[index]), &workspace.states[depth + 1])) {
            continue;
        }
        if (DepthFirstSearch(workspace, depth + 1, depthLimit)) return true;
    }
    return false;
}

[[nodiscard]] Genome ToGenome(
    const BattleEmulator::SearchState& state,
    const int knownTurns,
    const int solutionDepth,
    const int actions[kActionHistoryCapacity],
    const SearchWorkspace& workspace
) {
    Genome result{};
    result.AllyPlayer = state.players[0];
#if defined(gerunikku)
    result.IronKnightA = state.players[1];
    result.EnemyPlayer = state.players[2];
    result.IronKnightB = state.players[3];
#else
    result.EnemyPlayer = state.players[1];
    result.IronKnightA = {};
    result.IronKnightB = {};
#endif
    result.state = state.nowState;
    result.position = state.position;
    result.turn = knownTurns + solutionDepth + 1;
    result.processed = knownTurns + solutionDepth;
    result.Initialized = true;

    std::fill(std::begin(result.actions), std::end(result.actions), -1);
    for (int index = 0; index < knownTurns && index < kActionHistoryCapacity; ++index) {
        result.actions[index] = actions[index];
    }
    for (int index = 0;
         index < solutionDepth && knownTurns + index < kActionHistoryCapacity;
         ++index) {
        result.actions[knownTurns + index] = static_cast<int>(workspace.solution[index]);
    }
    return result;
}

[[nodiscard]] Genome RunIddfs(
    const Player players[4],
    const std::uint64_t seed,
    const int knownTurns,
    const int actions[kActionHistoryCapacity]
) {
    SearchWorkspace& workspace = gWorkspace;
    gNodesUsed = 0;
    workspace.path.fill(0);
    workspace.solution.fill(0);
    workspace.solutionDepth = -1;
    workspace.visitedNodes = 0;


    // The seed is fixed by the brute-force phase. Generate the cache once;
    // every branch is then determined by SearchState::position.
    lcg::init(seed, true);
    // The ROM baseline is injected with setSeedFromInitial(seed, position=1).
    // That live seed is the value already consumed at #1, so the first battle
    // RNG read is #2. SearchState::position denotes the next cached entry.
    if (!BattleEmulator::InitializeSearchState(&workspace.root, players, 2)) return {};

    // Build the exact search root once by replaying the already-known prefix.
    for (int index = 0; index < knownTurns; ++index) {
        const int packed = actions[index];
        if (packed <= 0) break;
        if (!BattleEmulator::StepSearchStateInPlace(
                &workspace.root,
                {BattleEmulator::HeroActionId(packed), BattleEmulator::HeroTargetId(packed)})) {
            return {};
        }
    }

    if (BattleWon(workspace.root)) {
        workspace.solutionDepth = 0;
        gNodesUsed = 1;
        return ToGenome(workspace.root, knownTurns, 0, actions, workspace);
    }

    const int resultCapacity = kActionHistoryCapacity - knownTurns - 1;
    const int depthCap = std::max(0, std::min(kSearchPathCapacity, resultCapacity));
    for (int depthLimit = 1; depthLimit <= depthCap; ++depthLimit) {
        workspace.states[0] = workspace.root;
        if (!DepthFirstSearch(workspace, 0, depthLimit)) continue;

        gNodesUsed = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            workspace.visitedNodes,
            std::numeric_limits<std::uint32_t>::max()));
        return ToGenome(workspace.states[workspace.solutionDepth], knownTurns,
                        workspace.solutionDepth, actions, workspace);
    }

    if (resultCapacity > kSearchPathCapacity) {
        throw std::runtime_error(
            "IDDFS search workspace exhausted without a win; increase kSearchPathCapacity instead of treating this as no solution");
    }

    gNodesUsed = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        workspace.visitedNodes,
        std::numeric_limits<std::uint32_t>::max()));
    return ToGenome(workspace.root, knownTurns, 0, actions, workspace);
}

[[nodiscard]] Genome RunParallelIddfs(
    const Player players[4],
    const std::uint64_t seed,
    const int knownTurns,
    const int actions[kActionHistoryCapacity],
    const int requestedThreads
) {
    const int threadCount = std::clamp(requestedThreads, 1, kMaxSearchThreads);
    if (threadCount <= 1) return RunIddfs(players, seed, knownTurns, actions);

    ParallelSearchWorkspace& parallel = gParallelWorkspace;
    SearchWorkspace& output = gWorkspace;
    gNodesUsed = 0;
    output.solution.fill(0);
    output.solutionDepth = -1;
    output.visitedNodes = 0;

    parallel.seed = seed;
    lcg::init(seed, true);
    if (!BattleEmulator::InitializeSearchState(&parallel.root, players, 2)) return {};

    for (int index = 0; index < knownTurns; ++index) {
        const int packed = actions[index];
        if (packed <= 0) break;
        if (!BattleEmulator::StepSearchStateInPlace(
                &parallel.root,
                {BattleEmulator::HeroActionId(packed), BattleEmulator::HeroTargetId(packed)})) {
            return {};
        }
    }

    if (BattleWon(parallel.root)) {
        output.solutionDepth = 0;
        output.visitedNodes = 1;
        gNodesUsed = 1;
        return ToGenome(parallel.root, knownTurns, 0, actions, output);
    }

    const int resultCapacity = kActionHistoryCapacity - knownTurns - 1;
    const int depthCap = std::max(0, std::min(kSearchPathCapacity, resultCapacity));
    for (int depthLimit = 1; depthLimit <= depthCap; ++depthLimit) {
        const bool found = RunParallelDepth(parallel, depthLimit, threadCount);
        for (int worker = 0; worker < threadCount; ++worker) {
            output.visitedNodes += parallel.workerVisited[static_cast<std::size_t>(worker)];
        }
        if (!found) continue;

        output.solution = parallel.solution;
        output.solutionDepth = parallel.solutionDepth;
        gNodesUsed = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            output.visitedNodes,
            std::numeric_limits<std::uint32_t>::max()));
        return ToGenome(
            parallel.solutionState,
            knownTurns,
            parallel.solutionDepth,
            actions,
            output
        );
    }

    if (resultCapacity > kSearchPathCapacity) {
        throw std::runtime_error(
            "parallel IDDFS search workspace exhausted without a win; increase kSearchPathCapacity instead of treating this as no solution");
    }

    gNodesUsed = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        output.visitedNodes,
        std::numeric_limits<std::uint32_t>::max()));
    return ToGenome(parallel.root, knownTurns, 0, actions, output);
}

} // namespace

Genome ActionOptimizer::RunAlgorithm(
    const Player players[4],
    const std::uint64_t seed,
    const int turns,
    const int maxGenerations,
    int actions[350],
    const int seedOffset
) {
    // Legacy parameters remain in the public ABI only. Search order and pruning
    // are independent of learned weights, generation counts and seed offsets.
    (void)maxGenerations;
    (void)seedOffset;
    return RunIddfs(players, seed, turns, actions);
}

std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(
    const Player players[4],
    const std::uint64_t seed,
    const int turns,
    const int maxGenerations,
    int actions[350],
    const int numThreads,
    const bool dropbug
) {
    (void)dropbug;
    (void)maxGenerations;
    return {0, RunParallelIddfs(players, seed, turns, actions, numThreads)};
}

ActionOptimizer::DepthProbeResult ActionOptimizer::ProbeDepth(
    const Player players[4],
    const std::uint64_t seed,
    const int depthLimit,
    const int numThreads
) {
    if (depthLimit < 1 || depthLimit > kSearchPathCapacity) {
        throw std::invalid_argument("IDDFS probe depth must be 1..kSearchPathCapacity");
    }

    const int threadCount = std::clamp(numThreads, 1, kMaxSearchThreads);
    if (threadCount == 1) {
        SearchWorkspace& workspace = gWorkspace;
        workspace.path.fill(0);
        workspace.solution.fill(0);
        workspace.solutionDepth = -1;
        workspace.visitedNodes = 0;
        lcg::init(seed, true);
        if (!BattleEmulator::InitializeSearchState(&workspace.root, players, 2)) return {};
        workspace.states[0] = workspace.root;
        const bool win = DepthFirstSearch(workspace, 0, depthLimit);
        return {workspace.visitedNodes, win, workspace.solutionDepth};
    }

    ParallelSearchWorkspace& workspace = gParallelWorkspace;
    workspace.seed = seed;
    lcg::init(seed, true);
    if (!BattleEmulator::InitializeSearchState(&workspace.root, players, 2)) return {};
    const bool win = RunParallelDepth(workspace, depthLimit, threadCount);
    std::uint64_t nodes = 0;
    for (int worker = 0; worker < threadCount; ++worker) {
        nodes += workspace.workerVisited[static_cast<std::size_t>(worker)];
    }
    return {nodes, win, workspace.solutionDepth};
}

void ActionOptimizer::updateCompromiseScore(Genome& genome) {
    // Compatibility no-op. The IDDFS implementation has no compromise score,
    // learned cost, heuristic ranking or approximate branch elimination.
    genome.compromiseScore = 0;
    genome.isEliminated = false;
}

uint64_t ActionOptimizer::getDominancePruned() {
    return 0;
}

uint32_t ActionOptimizer::getDominanceRecordsMax() {
    return 0;
}

uint32_t ActionOptimizer::getDominanceOverflowIterations() {
    return 0;
}

std::uint32_t ActionOptimizer::getNodesUsed() {
    return gNodesUsed;
}
