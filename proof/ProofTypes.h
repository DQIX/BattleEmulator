#pragma once

#include "../Player.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace d20proof {
    enum class SolveKind {
        Win,
        ProvedFalse,
        Unknown,
        InvalidInput,
        InvalidPrefix,
        UnsupportedInput,
        ModelError,
    };

    enum class MinimalityKind : std::uint8_t {
        NotChecked,
        Optimal,
        Unknown,
    };

    struct RuleId {
        std::string registryNamespace;
        std::uint32_t version = 0;

        bool operator==(const RuleId &) const = default;
    };

    struct RawState {
        Player players[2]{};
        int position = 1;
        std::uint64_t nowState = 0;
    };

    struct ActionConstraint {
        int absoluteTurn = 0;
        std::vector<int> allowedCommands;

        bool operator==(const ActionConstraint &) const = default;
    };

    struct BoundaryObservation {
        int absoluteTurn = 0;
        RawState expectedState;
    };

    struct ProblemConstraints {
        std::vector<ActionConstraint> actions;
        std::vector<BoundaryObservation> observations;
    };

    struct Problem {
        RuleId ruleId;
        std::uint64_t seed = 0;
        RawState s0;
        int startTurn = 0;
        ProblemConstraints constraints;
    };

    struct ReplayTurn {
        int selectedCommand = 0;
        int positionBefore = 0;
        int positionAfter = 0;
        int heroHpBefore = 0;
        int heroHpAfter = 0;
        int enemyHpBefore = 0;
        int enemyHpAfter = 0;
        RawState stateBefore;
        RawState stateAfter;
    };

    struct ReplayResult {
        bool valid = false;
        bool supported = false;
        bool won = false;
        bool lost = false;
        int firstWinningTurn = -1;
        std::string reason;
        RawState finalState;
        std::vector<int> checkedCommands;
        std::vector<ReplayTurn> turns;
    };

    struct PrefixReceipt {
        bool valid = false;
        bool observationsChecked = false;
        std::string reason;
        Problem initialProblem;
        std::vector<int> prefix;
        RawState terminalState;
        int terminalTurn = 0;
        ReplayResult replay;
    };

    struct Interval {
        std::int64_t lo = 0;
        std::int64_t hi = -1;

        [[nodiscard]] bool empty() const noexcept { return lo > hi; }

        bool operator==(const Interval &) const = default;
    };

    enum class ResourceAxis : std::uint8_t {
        EnemyHp,
        HeroHp,
        Mp,
        Herb,
    };

    enum class ModeAxis : std::uint8_t {
        Charge,
        Paralysis,
        Acro,
        Rage,
        Inactive,
        Camera,
    };

    constexpr std::uint16_t kChargeMaskAll = 0x00ff;
    constexpr std::uint16_t kParalysisMaskAll = 0x00ff;
    constexpr std::uint16_t kAcroMaskAll = 0x007f;
    constexpr std::uint16_t kRageMaskAll = 0x001f;
    constexpr std::uint16_t kInactiveMaskAll = 0x0003;
    constexpr std::uint16_t kCameraMaskAll = 0x003f;

    struct Box {
        Interval enemyHp;
        Interval heroHp;
        Interval mp;
        Interval herb;

        std::uint16_t chargeMask = 0;
        std::uint16_t paralysisMask = 0;
        std::uint16_t acroMask = 0;
        std::uint16_t rageMask = 0;
        std::uint16_t inactiveMask = 0;
        std::uint16_t cameraMask = 0;

        [[nodiscard]] bool empty() const noexcept;

        bool operator==(const Box &) const = default;
    };

    enum class PredicateKind : std::uint8_t {
        ResourceLe,
        ModeInMask,
    };

    struct Predicate {
        PredicateKind kind = PredicateKind::ResourceLe;
        ResourceAxis resource = ResourceAxis::EnemyHp;
        ModeAxis mode = ModeAxis::Charge;
        std::int64_t threshold = 0;
        std::uint16_t mask = 0;

        bool operator==(const Predicate &) const = default;
    };

    struct PartitionNode {
        bool leaf = true;
        std::uint32_t localCellId = 0;
        Predicate predicate;
        int trueChild = -1;
        int falseChild = -1;

        bool operator==(const PartitionNode &) const = default;
    };

    struct PredicatePartition {
        int rngPosition = 0;
        std::vector<PartitionNode> nodes;
        int root = -1;

        bool operator==(const PredicatePartition &) const = default;
    };

    struct PartitionFamily {
        std::uint64_t partitionVersion = 0;
        std::vector<PredicatePartition> trees;

        bool operator==(const PartitionFamily &) const = default;
    };

    struct CellKey {
        int rngPosition = 0;
        std::uint32_t localCellId = 0;

        bool operator==(const CellKey &) const = default;
        bool operator<(const CellKey &other) const noexcept {
            if (rngPosition != other.rngPosition) {
                return rngPosition < other.rngPosition;
            }
            return localCellId < other.localCellId;
        }
    };

    struct ProofBudget {
        int totalTimeMs = 15000;
        std::uint64_t maxWork = 8'000'000;
        std::uint64_t maxBytes = 128ull * 1024ull * 1024ull;
        std::uint32_t maxCandidates = 2048;
        std::uint32_t maxCandidateScans = 2'000'000;
        std::uint32_t maxRepairs = 16;
        std::uint32_t maxLeavesPerPosition = 8;
        std::uint32_t maxDetailedTermsPerAction = 8;
        std::uint32_t maxCompletionTermsPerAction = 3;
        std::uint32_t maxPriceEvaluations = 8;
        std::uint32_t maxModelTerms = 250'000;
    };

    struct BudgetReport {
        std::uint64_t work = 0;
        std::uint64_t bytes = 0;
        std::uint32_t candidates = 0;
        std::uint32_t candidateScans = 0;
        std::uint32_t repairs = 0;
        std::uint32_t priceEvaluations = 0;
        int elapsedMs = 0;
    };

    struct SolveResult {
        SolveKind kind = SolveKind::Unknown;
        MinimalityKind minimality = MinimalityKind::NotChecked;
        std::string reason;
        int horizon = 0;
        int firstWinningTurn = -1;
        int minimumTurnLowerBound = -1;
        int minimumTurnUpperBound = -1;
        std::vector<int> commands;
        ReplayResult replay;
        BudgetReport budget;
        std::int64_t provedFalseRootBound = 0;
        std::int64_t provedFalseDelta = 0;
        std::uint64_t partitionVersion = 0;
        std::uint64_t coverageVersion = 0;
        bool hasProblemKey = false;
        Problem problemKey;
        bool prefixConnected = false;
        PrefixReceipt prefixReceipt;
    };

    [[nodiscard]] bool sameRawState(const RawState &a, const RawState &b) noexcept;

    [[nodiscard]] bool sameProblemKey(const Problem &a, const Problem &b) noexcept;

    [[nodiscard]] bool commandAllowedByConstraints(
        const Problem &problem,
        int absoluteTurn,
        int command) noexcept;

    [[nodiscard]] const BoundaryObservation *boundaryObservationAt(
        const Problem &problem,
        int absoluteTurn) noexcept;

    [[nodiscard]] std::uint16_t allMask(ModeAxis axis) noexcept;

    [[nodiscard]] const Interval &intervalOf(const Box &box, ResourceAxis axis) noexcept;

    [[nodiscard]] Interval &intervalOf(Box &box, ResourceAxis axis) noexcept;

    [[nodiscard]] std::uint16_t modeMaskOf(const Box &box, ModeAxis axis) noexcept;

    [[nodiscard]] std::uint16_t &modeMaskOf(Box &box, ModeAxis axis) noexcept;

    [[nodiscard]] Box intersect(const Box &a, const Box &b) noexcept;

    [[nodiscard]] bool contains(const Box &outer, const Box &inner) noexcept;

    [[nodiscard]] std::pair<Box, Box> split(const Box &box, const Predicate &predicate);
} // namespace d20proof
