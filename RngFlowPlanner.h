#ifndef NEWDIRECTORY_RNG_FLOW_PLANNER_H
#define NEWDIRECTORY_RNG_FLOW_PLANNER_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "BattleEmulator.h"

namespace rngflow {

constexpr int kMaxPlanTurns = 30;

// Lightweight state used only by the yo2 early-battle critical optimizer.
// Keep only fields that can affect RNG flow, critical reward, or enemy rage.
// Ally HP/MP are still present so the public Step() can be checked against the
// full emulator one turn at a time. The DP forgets ally HP, but keeps MP exact
// because HEAL/CRACK availability is itself an RNG-adjustment resource.
struct RngPlayer {
    int hp = 0;
    int maxHp = 0;
    int atk = 0;
    int defaultATK = 0;
    int def = 0;
    int speed = 0;
    int mp = 0;
    int maxMp = 0;
    bool specialCharge = false;
    int specialChargeTurn = 0;
    bool paralysis = false;
    int paralysisLevel = 0;
    int paralysisTurns = -1;
    bool sleeping = false;
    double defence = 1.0;
    bool rage = false;
    bool acrobaticStar = false;
    int acrobaticStarTurn = 0;
    int rageTurns = -1;
    int medicinal_herbs_count = 0;
    bool inactive = false;

    static void reduceHp(RngPlayer& obj, int amount) noexcept {
        obj.hp = std::max(0, obj.hp - amount);
    }

    static void heal(RngPlayer& obj, int amount) noexcept {
        obj.hp = std::min(obj.maxHp, obj.hp + amount);
    }
};

struct State {
    RngPlayer players[2]{};
    int position = 1;
    std::uint8_t cameraCounter = 0;
};

struct StepResult {
    State state{};
    int criticalGain = 0;
    bool heroAlive = true;
    bool enemyAlive = true;
};

struct TapeSummary {
    int attackCriticalSlots = 0;
    int dragonCriticalSlots = 0;
    int attackGapGcd = 0;
    int dragonGapGcd = 0;
};

struct Plan {
    int optimisticCriticals = 0;
    int maxCriticals = 0;
    int actionCount = 0;
    std::array<int, kMaxPlanTurns> actions{};
    std::uint64_t expandedStates = 0;
    std::uint64_t feasibilityStates = 0;
    TapeSummary tape{};
};

struct DiversePlan {
    Plan plan{};
    State endpoint{};
};

struct OptimisticBoundResult {
    int criticalUpper = 0;
    // Lower bound on the number of future turns required to kill the enemy.
    // The projected window is exact; beyond it only an overestimating static
    // damage envelope is used. remainingTurns+1 means even that optimistic
    // envelope cannot kill inside the requested horizon.
    int optimisticKillTurns = kMaxPlanTurns + 1;
};

// Exact, heuristic-free decision result for the supported yo2 projected
// battle model.  The solver keeps only a reversible 64-bit encoding of each
// future-relevant turn-boundary state and removes exact duplicates by
// sort/unique.  `complete == false` means the compact codec or precomputed LCG
// tape could not represent a reached live state; callers must then treat the
// result as UNKNOWN rather than as an UNSAT proof.
struct ExactKillDecisionResult {
    bool killReachable = false;
    bool complete = true;
    int firstKillTurn = -1;
    // Largest horizon d for which E(d)=false has been fully exhausted by the
    // authoritative depth frontier.  If a run times out, this remains valid even
    // though complete=false makes the requested larger horizon UNKNOWN.
    int completedNoKillDepth = 0;
    int actionCount = 0;
    std::array<int, kMaxPlanTurns> actions{};
    std::uint64_t expandedStates = 0;
    std::uint64_t generatedStates = 0;
    std::uint64_t duplicateStates = 0;
    std::uint64_t dominatedStates = 0;
    std::uint64_t peakFrontier = 0;
    std::array<std::uint64_t, kMaxPlanTurns + 1> frontierByDepth{};
    std::array<std::uint64_t, kMaxPlanTurns + 1> generatedByDepth{};
    std::array<std::uint64_t, kMaxPlanTurns + 1> duplicatesByDepth{};
    std::array<std::uint64_t, kMaxPlanTurns + 1> dominatedByDepth{};
    std::array<std::uint64_t, kMaxPlanTurns + 1> nonResourceGroupsByDepth{};
    std::array<std::uint64_t, kMaxPlanTurns + 1> positionStatusGroupsByDepth{};
    std::uint64_t witnessExpandedStates = 0;
    std::uint64_t witnessGeneratedStates = 0;
    std::uint64_t observedLiveTransitionDeltaMask = 0;
    // Diagnostic-only certificate for a complete no-kill run.  This is a real
    // authoritative path to the frontier state that came closest to escaping
    // the proof-safe optimistic damage bound; it is never treated as a SAT
    // witness.  Values are exact integers from SearchState, not bucketed scores.
    int closestRejectedDepth = -1;
    int closestRejectedEnemyHp = -1;
    int closestRejectedHeroHp = -1;
    int closestRejectedPosition = -1;
    int closestRejectedDamageUpper = -1;
    int closestRejectedShortfall = -1;
    int diagnosticActionCount = 0;
    std::array<int, kMaxPlanTurns> diagnosticActions{};
};


[[nodiscard]] State FromSearchState(const BattleEmulator::SearchState& state) noexcept;

// Executes one complete yo2 turn without calling BattleEmulator::Main.
// The only intentional early-boss specialization is FUN_0207564c: it always
// follows the common two-RNG path, matching the optimization in this worktree.
[[nodiscard]] StepResult Step(const State& source, int heroAction);

[[nodiscard]] std::size_t LegalActions(const State& state,
                                       std::array<int, 8>& actions) noexcept;

[[nodiscard]] TapeSummary AnalyzeTape(int beginPosition, int endPosition);

// Critical-first DP over the projected early-boss transition above. Ally HP is
// intentionally forgotten between projected turns; ally MP and enemy HP stay
// exact because MP gates RNG-adjustment actions and enemy HP changes the exact
// turn on which rage alters RNG consumption. Herb count is handled as an exact
// resource dimension inside the memo value rather than the state hash.
[[nodiscard]] Plan FindMaxCriticalPlan(const State& root, int maxTurns);

// Exact maximum critical reward reachable by the projected planner within the
// requested horizon. Reuses one memo for the currently initialized LCG tape.
[[nodiscard]] int ExactCriticalUpperBound(const State& root, int maxTurns,
                                          std::uint64_t* newlyExpanded = nullptr);

// Pure LCG-tape upper bound: every player critical counted by this planner
// consumes a roll below the normal-attack threshold, and one projected turn
// advances at most 64 RNG positions.  No battle-state heuristic is involved.
[[nodiscard]] int StaticCriticalSlotUpperBound(int position, int maxTurns);

// Cheap state-aware tightening of the pure position bound.  Exactly one legal
// projected turn is enumerated, then the remaining horizon is relaxed back to
// StaticCriticalSlotUpperBound.  Since every legal first action is retained,
// this remains an admissible critical-count upper bound.
[[nodiscard]] int OneStepCriticalUpperBound(const State& root, int maxTurns);

// Optimistic lower bound on remaining turns-to-kill.  It deliberately
// overestimates damage by allowing one normal player action and one normal
// counter every turn, then upgrades as many of those hits to criticals as the
// static LCG critical-slot upper bound permits.  Therefore the returned turn
// count can only be earlier than (or equal to) a real kill.
[[nodiscard]] int StaticOptimisticKillTurns(const State& root, int maxTurns);

// Optimistic-planning bound for one global exact node.  The first
// `windowTurns` are exhaustively projected.  Every projected leaf contributes
// its exact critical gain inside the window plus the static LCG-slot upper
// bound for the remaining turns.  The maximum leaf value is returned.
[[nodiscard]] int OptimisticCriticalUpperBound(const State& root,
                                               int windowTurns,
                                               int remainingTurns,
                                               std::uint64_t* newlyExpanded = nullptr);

[[nodiscard]] OptimisticBoundResult OptimisticCriticalUpperBoundDetailed(
    const State& root, int windowTurns, int remainingTurns,
    std::uint64_t* newlyExpanded = nullptr);

// Read the same detailed optimistic result without expanding the projected
// solver. Returns false only when the corresponding memo entry has not been
// computed yet. This is used by the global lazy search to avoid an otherwise
// redundant pop/refine/requeue cycle for already-known child states.
[[nodiscard]] bool TryCachedOptimisticCriticalUpperBoundDetailed(
    const State& root, int windowTurns, int remainingTurns,
    OptimisticBoundResult& result);

// Decide E(maxTurns) for the supported yo2 projected model without heuristic
// ordering, approximate state merging, or a probabilistic objective.  A
// complete false result (`killReachable == false && complete == true`) is an
// exhaustive proof that no legal action sequence in this model kills within
// maxTurns.  This is intentionally separate from the optimistic D bounds: if
// exact quotienting alone is fast enough, no additional bound is needed.
[[nodiscard]] ExactKillDecisionResult DecideExactKillWithin(
    const State& root, int maxTurns, int timeLimitMs = 0);

// Same exhaustive quotient search, but with the already-proven ally-HP
// relaxation applied at every turn boundary.  Every surviving real path is
// contained in this model, so a complete UNSAT result is a valid proof for the
// real battle.  SAT is only "possibly reachable" and must not be accepted as a
// witness without exact replay.
[[nodiscard]] ExactKillDecisionResult ProveNoKillWithin(
    const State& root, int maxTurns, int timeLimitMs = 0);

// Final proof path: transitions are executed by the authoritative full
// BattleEmulator::StepSearchState, while only the future-relevant turn-boundary
// state is stored in a reversible 64-bit integer.  No rngflow::Step equivalence
// assumption is needed for a complete UNSAT result.
[[nodiscard]] ExactKillDecisionResult ProveNoKillWithinBattleExact(
    const BattleEmulator::SearchState& root, int maxTurns, int timeLimitMs = 0);

// Proof-safe resource relaxation for structure testing and, if it closes,
// direct UNSAT proof. Hero HP and every authoritative transition field remain
// exact, while MP and medicinal-herb counts are reset upward at each turn
// boundary. Every real executable path is therefore represented in this
// superset. SAT is only abstract; complete UNSAT proves the real horizon.
[[nodiscard]] ExactKillDecisionResult ProveNoKillWithinBattleResourceRelaxed(
    const BattleEmulator::SearchState& root, int maxTurns, int timeLimitMs = 0);

// One-pass exact shortest-kill decision on the authoritative BattleEmulator
// transition.  Unlike ProveNoKillWithinBattleExact, hero HP is retained in the
// reversible state, so SAT is real as well as UNSAT.  Because frontiers are
// exhausted strictly by depth, firstKillTurn=T implies E(T-1)=false.
[[nodiscard]] ExactKillDecisionResult FindShortestKillBattleExact(
    const BattleEmulator::SearchState& root, int maxTurns, int timeLimitMs = 0);

// Run the same authoritative exact frontier as FindShortestKillBattleExact,
// while retaining parents only for diagnosis.  On complete UNSAT it records
// one real path to the state with the smallest positive gap between exact
// enemy HP and the proof-safe optimistic damage envelope.  This function does
// not relax HP and does not manufacture a witness from the diagnostic path.
[[nodiscard]] ExactKillDecisionResult DiagnoseNoKillBattleExact(
    const BattleEmulator::SearchState& root, int maxTurns, int timeLimitMs = 0);

// Exact shortest-kill search using the proven HP/MP/herb dominance relation.
// Hero HP is removed from the key, but every key carries the maximum HP of a
// real path that reached it.  Therefore SAT is real and complete UNSAT remains
// a proof; this is an exact dominance quotient, not the optimistic HP reset.
[[nodiscard]] ExactKillDecisionResult FindShortestKillBattleDominantHp(
    const BattleEmulator::SearchState& root, int maxTurns, int timeLimitMs = 0);

// Proof-only variant of the exact HP/MP/herb dominance quotient.  It stores no
// parent/action history: each non-HP future state carries only the maximum real
// reachable hero HP.  A complete no-kill result is therefore an exact UNSAT
// proof, while a reached kill is reported only as SAT existence.
[[nodiscard]] ExactKillDecisionResult ProveNoKillWithinBattleDominantHp(
    const BattleEmulator::SearchState& root, int maxTurns, int timeLimitMs = 0);

// Find one real executable witness using only authoritative battle transitions.
// Ordering is heuristic-only: every legal branch remains in the DFS, and the
// proof-safe optimistic lower bound is the only pruning rule.
[[nodiscard]] ExactKillDecisionResult FindKillWitnessBattleExact(
    const BattleEmulator::SearchState& root, int maxTurns, int timeLimitMs = 0);

// Find one witness in the proven hero-HP-relaxed superset.  SAT from this
// function is only a candidate action sequence; callers must replay it with the
// authoritative non-relaxed state before accepting E(t)=true.
[[nodiscard]] ExactKillDecisionResult FindKillWitnessBattleRelaxed(
    const BattleEmulator::SearchState& root, int maxTurns, int timeLimitMs = 0);

// Replay one candidate action sequence on the authoritative, non-relaxed battle
// state.  This is the acceptance gate for any witness produced by a relaxation.
[[nodiscard]] bool ReplayBattleWitnessExact(
    const BattleEmulator::SearchState& root,
    const std::array<int, kMaxPlanTurns>& actions,
    int actionCount);

// End-to-end shortest-kill solver. First obtains an incumbent witness U, then
// proves E(U-1) with the exact reversible-frontier solver. If that proof itself
// finds a shorter T, a real witness is recovered inside horizon T; the completed
// depth frontier already proves E(T-1)=false.
[[nodiscard]] ExactKillDecisionResult SolveShortestKillBattleExact(
    const BattleEmulator::SearchState& root, int maxTurns, int timeLimitMs = 0);


// Produce multiple genuinely different chunk candidates.  Candidates are
// seeded by distinct forced action prefixes and each suffix is solved from the
// resulting RNG-flow state, so every candidate uses the LCG tape at its own
// exact position.  Endpoint positions are interleaved in the returned order so
// one local optimum cannot consume the whole beam.
[[nodiscard]] std::vector<DiversePlan> FindDiverseCriticalPlans(
    const State& root, int maxTurns, int maxPlans);

} // namespace rngflow

#endif // NEWDIRECTORY_RNG_FLOW_PLANNER_H
