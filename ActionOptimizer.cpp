#include "ActionOptimizer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <stdexcept>

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
constexpr std::size_t kDominanceBucketCount = 8192;
constexpr std::size_t kDominanceRecordCapacity = 32768;
constexpr std::uint32_t kNoDominanceRecord = UINT32_MAX;

static_assert((kDominanceBucketCount & (kDominanceBucketCount - 1)) == 0);

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

struct DominanceRecord {
    std::uint64_t key{};
    std::uint32_t next{kNoDominanceRecord};
    std::uint16_t depth{};
    int allyHp{};
    int allyMp{};
    BattleEmulator::SearchState state{};
};

struct SearchWorkspace {
    BattleEmulator::SearchState root{};
    std::array<BattleEmulator::SearchState, kSearchPathCapacity + 1> states{};
    std::array<std::uint16_t, kSearchPathCapacity> path{};
    std::array<std::uint16_t, kSearchPathCapacity> solution{};
    int solutionDepth{-1};
    std::uint64_t visitedNodes{};
    std::uint64_t dominancePruned{};
    std::uint32_t maxDominanceRecords{};
    std::uint32_t dominanceOverflowIterations{};
    bool dominanceOverflow{};
    std::array<std::uint32_t, kDominanceBucketCount> bucketHeads{};
    std::array<DominanceRecord, kDominanceRecordCapacity> records{};
    std::uint32_t recordCount{};
};

// One synchronous search owns one worker/WASM instance. Keep search memory fixed
// and C++-owned: no per-node malloc, no 350-action Genome copies, no open set.
SearchWorkspace gWorkspace{};
std::uint32_t gNodesUsed{};

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

[[nodiscard]] bool SameAllyExceptHpMp(const Player& lhs, const Player& rhs) noexcept {
    Player left = lhs;
    Player right = rhs;
    left.hp = 0;
    right.hp = 0;
    left.mp = 0;
    right.mp = 0;
    return left == right;
}

[[nodiscard]] bool SameNonResourceState(
    const BattleEmulator::SearchState& lhs,
    const BattleEmulator::SearchState& rhs
) noexcept {
    return lhs.position == rhs.position
        && lhs.nowState == rhs.nowState
        && SameAllyExceptHpMp(lhs.players[0], rhs.players[0])
        && lhs.players[1] == rhs.players[1]
        && lhs.players[2] == rhs.players[2]
        && lhs.players[3] == rhs.players[3]
        && lhs.cameraRuntime == rhs.cameraRuntime;
}

[[nodiscard]] constexpr std::uint64_t Mix(
    std::uint64_t hash,
    const std::uint64_t value
) noexcept {
    return hash ^ (value + UINT64_C(0x9e3779b97f4a7c15) + (hash << 6) + (hash >> 2));
}

[[nodiscard]] std::uint64_t MixPlayerState(
    std::uint64_t hash,
    const Player& player,
    const bool includeHpMp
) noexcept {
    if (includeHpMp) {
        hash = Mix(hash, static_cast<std::uint32_t>(player.hp));
        hash = Mix(hash, static_cast<std::uint32_t>(player.mp));
    }
    hash = Mix(hash, static_cast<std::uint32_t>(player.maxHp));
    hash = Mix(hash, static_cast<std::uint32_t>(player.atk));
    hash = Mix(hash, static_cast<std::uint32_t>(player.defaultATK));
    hash = Mix(hash, static_cast<std::uint32_t>(player.def));
    hash = Mix(hash, static_cast<std::uint32_t>(player.defaultDEF));
    hash = Mix(hash, static_cast<std::uint32_t>(player.speed));
    hash = Mix(hash, static_cast<std::uint32_t>(player.HealPower));
    hash = Mix(hash, static_cast<std::uint32_t>(player.maxMp));
    hash = Mix(hash, static_cast<std::uint32_t>(player.specialCharge));
    hash = Mix(hash, static_cast<std::uint32_t>(player.dirtySpecialCharge));
    hash = Mix(hash, static_cast<std::uint32_t>(player.specialChargeTurn));
    hash = Mix(hash, static_cast<std::uint32_t>(player.paralysis));
    hash = Mix(hash, static_cast<std::uint32_t>(player.paralysisLevel));
    hash = Mix(hash, static_cast<std::uint32_t>(player.paralysisTurns));
    hash = Mix(hash, static_cast<std::uint32_t>(player.SpecialMedicineCount));
    hash = Mix(hash, std::bit_cast<std::uint64_t>(player.defence));
    hash = Mix(hash, static_cast<std::uint32_t>(player.sleeping));
    hash = Mix(hash, static_cast<std::uint32_t>(player.sleepingTurn));
    hash = Mix(hash, static_cast<std::uint32_t>(player.BuffLevel));
    hash = Mix(hash, static_cast<std::uint32_t>(player.BuffTurns));
    hash = Mix(hash, static_cast<std::uint32_t>(player.hasMagicMirror));
    hash = Mix(hash, static_cast<std::uint32_t>(player.MagicMirrorTurn));
    hash = Mix(hash, static_cast<std::uint32_t>(player.AtkBuffLevel));
    hash = Mix(hash, static_cast<std::uint32_t>(player.AtkBuffTurn));
    hash = Mix(hash, static_cast<std::uint32_t>(player.TensionLevel));
    hash = Mix(hash, static_cast<std::uint32_t>(player.rage));
    hash = Mix(hash, static_cast<std::uint32_t>(player.SageElixirCount));
    hash = Mix(hash, static_cast<std::uint32_t>(player.ElfinElixirCount));
    hash = Mix(hash, static_cast<std::uint32_t>(player.MagicWaterCount));
    hash = Mix(hash, static_cast<std::uint32_t>(player.InsulateLevel));
    hash = Mix(hash, static_cast<std::uint32_t>(player.InsulateTurns));
    hash = Mix(hash, static_cast<std::uint32_t>(player.inactive));
    hash = Mix(hash, static_cast<std::uint32_t>(player.rageTurns));
    hash = Mix(hash, static_cast<std::uint32_t>(player.magicResistanceLevel));
    hash = Mix(hash, static_cast<std::uint32_t>(player.confused));
    hash = Mix(hash, static_cast<std::uint32_t>(player.confusionTurns));
    hash = Mix(hash, static_cast<std::uint32_t>(player.guardedBy));
    return hash;
}

// Bucket key only. A key match is never used as proof of state equality.
// SameNonResourceState() is mandatory before an HP/MP dominance prune.
[[nodiscard]] std::uint64_t CoarseNonResourceKey(
    const BattleEmulator::SearchState& state,
    const int depth
) noexcept {
    std::uint64_t hash = UINT64_C(0xcbf29ce484222325);
    hash = Mix(hash, static_cast<std::uint64_t>(depth));
    hash = Mix(hash, static_cast<std::uint32_t>(state.position));
    hash = Mix(hash, state.nowState);
    hash = MixPlayerState(hash, state.players[0], false);
    hash = MixPlayerState(hash, state.players[1], true);
    hash = MixPlayerState(hash, state.players[2], true);
    hash = MixPlayerState(hash, state.players[3], true);
    return hash;
}

void ResetDominance(SearchWorkspace& workspace) noexcept {
    workspace.bucketHeads.fill(kNoDominanceRecord);
    workspace.recordCount = 0;
    workspace.dominanceOverflow = false;
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

[[nodiscard]] bool AcceptByHpMpDominance(
    SearchWorkspace& workspace,
    const int depth,
    const BattleEmulator::SearchState& candidateState
) {
    const int candidateHp = candidateState.players[0].hp;
    const int candidateMp = candidateState.players[0].mp;
    const std::uint64_t key = CoarseNonResourceKey(candidateState, depth);
    const std::size_t bucket = static_cast<std::size_t>(key) & (kDominanceBucketCount - 1);

    for (std::uint32_t recordIndex = workspace.bucketHeads[bucket];
         recordIndex != kNoDominanceRecord;
         recordIndex = workspace.records[recordIndex].next) {
        const DominanceRecord& record = workspace.records[recordIndex];
        if (record.key != key || record.depth != depth) continue;
        if (record.allyHp < candidateHp || record.allyMp < candidateMp) continue;
        if (!SameNonResourceState(record.state, candidateState)) continue;

        // Exact same future-affecting state, with no less HP and no less MP.
        // The recorded path therefore has a superset of survival/command
        // possibilities and safely dominates this one.
        ++workspace.dominancePruned;
        return false;
    }

    if (workspace.recordCount >= kDominanceRecordCapacity) {
        // Capacity exhaustion must never become an approximate prune. Keep
        // exploring; only stop adding new dominance records.
        if (!workspace.dominanceOverflow) {
            workspace.dominanceOverflow = true;
            ++workspace.dominanceOverflowIterations;
        }
        return true;
    }

    DominanceRecord& record = workspace.records[workspace.recordCount];
    record.key = key;
    record.depth = static_cast<std::uint16_t>(depth);
    record.allyHp = candidateHp;
    record.allyMp = candidateMp;
    record.state = candidateState;
    record.next = workspace.bucketHeads[bucket];
    workspace.bucketHeads[bucket] = workspace.recordCount++;
    workspace.maxDominanceRecords = std::max(workspace.maxDominanceRecords, workspace.recordCount);
    return true;
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

    if (depth > 0 && !AcceptByHpMpDominance(workspace, depth, currentState)) return false;

    std::array<std::uint16_t, kScenarioCommands.size()> candidates{};
    const std::size_t candidateCount = BuildCandidates(currentState, candidates);
    for (std::size_t index = 0; index < candidateCount; ++index) {
        workspace.path[depth] = candidates[index];
        workspace.states[depth + 1] = currentState;
        if (!BattleEmulator::StepSearchStateInPlace(
                &workspace.states[depth + 1], UnpackCommand(candidates[index]))) {
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
    workspace.dominancePruned = 0;
    workspace.maxDominanceRecords = 0;
    workspace.dominanceOverflowIterations = 0;

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
        ResetDominance(workspace);
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
    (void)numThreads;
    (void)dropbug;
    return {0, RunAlgorithm(players, seed, turns, maxGenerations, actions, 0)};
}

void ActionOptimizer::updateCompromiseScore(Genome& genome) {
    // Compatibility no-op. The IDDFS implementation has no compromise score,
    // learned cost, heuristic ranking or approximate branch elimination.
    genome.compromiseScore = 0;
    genome.isEliminated = false;
}

uint64_t ActionOptimizer::getDominancePruned() {
    return gWorkspace.dominancePruned;
}

uint32_t ActionOptimizer::getDominanceRecordsMax() {
    return gWorkspace.maxDominanceRecords;
}

uint32_t ActionOptimizer::getDominanceOverflowIterations() {
    return gWorkspace.dominanceOverflowIterations;
}

std::uint32_t ActionOptimizer::getNodesUsed() {
    return gNodesUsed;
}
