#pragma once

#include "ExactReplay.h"
#include "RuleProgram.h"
#include "SymbolicStepper.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace d20proof {
    struct CheckResult {
        bool accepted = false;
        std::string reason;
    };

    struct CheckedPartition {
        CheckResult check;
        std::vector<std::pair<std::uint32_t, Box> > leaves;
    };

    enum class CompletionTargetKind : std::uint8_t {
        AllLeavesInCheckedRange,
    };

    struct CompletionProofCase {
        std::uint32_t caseId = 0;
        CompletionTargetKind targetKind = CompletionTargetKind::AllLeavesInCheckedRange;

        bool operator==(const CompletionProofCase &) const = default;
    };

    enum class RootProofKind : std::uint8_t {
        Completion,
        FullyDetailed,
    };

    struct RootProofRecord {
        RootProofKind kind = RootProofKind::Completion;
        int elapsedTurn = 0;
        CellKey source;
        int selectedCommand = 0;
        std::uint64_t partitionVersion = 0;
        std::uint64_t coverageVersion = 0;
        ProgramPoint expectedPc;
        CompletionCutId cut = CompletionCutId::TurnEntry;
        std::vector<CompletionProofCase> completionCases;

        bool operator==(const RootProofRecord &) const = default;
    };

    struct CheckedRootRecord {
        RootProofKind proofKind = RootProofKind::Completion;
        int elapsedTurn = 0;
        CellKey source;
        int selectedCommand = 0;
        Box rootDomain;
        std::uint64_t partitionVersion = 0;
        std::uint64_t coverageVersion = 0;
        ProgramPoint verifiedPc;
        CompletionCutId verifiedCut = CompletionCutId::TurnEntry;

        bool operator==(const CheckedRootRecord &) const = default;
    };

    enum class CheckedEdgeKind : std::uint8_t {
        Detailed,
        Completion,
    };

    struct CheckedEdge {
        CheckedEdgeKind kind = CheckedEdgeKind::Detailed;
        CompletionCutId completionCut = CompletionCutId::TurnEntry;
        int elapsedTurn = 0;
        CellKey source;
        int selectedCommand = 0;
        Box rootDomain;
        Box continuingOutput;
        int firstOutputPosition = 0;
        int lastOutputPosition = -1;
        bool hasContinuingOutput = false;
        bool mayReachGoal = false;
        bool mayReachFailure = false;
        std::vector<CellKey> targets;
        std::vector<CompletionWeightTerm> weightTerms;

        bool operator==(const CheckedEdge &) const = default;
    };

    struct CompletionCheckpoint {
        int elapsedTurn = 0;
        CellKey source;
        int selectedCommand = 0;
        CompletionCutId cut = CompletionCutId::TurnEntry;
        Box rootDomain;
        SymbolicFrame frame;

        bool operator==(const CompletionCheckpoint &) const = default;
    };

    struct ProofTemplate {
        int elapsedTurn = 0;
        int rngPosition = 0;
        int selectedCommand = 0;
        Box coveredDomain;
        RootProofKind kind = RootProofKind::Completion;
        CompletionCutId cut = CompletionCutId::TurnEntry;

        bool operator==(const ProofTemplate &) const = default;
    };

    struct CheckedKernelCache {
        bool bound = false;
        Problem problemKey;
        std::vector<ProofTemplate> templates;
    };

    struct CheckedSnapshot {
        CheckResult check;
        PartitionFamily partitions;
        std::uint64_t coverageVersion = 0;
        CellKey root;
        std::vector<std::vector<CellKey>> support;
        std::vector<RootProofRecord> proofs;
        std::vector<CheckedRootRecord> coverage;
        std::vector<std::vector<CheckedEdge>> edgesByElapsedTurn;
        std::vector<CompletionCheckpoint> completionCheckpoints;
    };

    struct FalseCertificate {
        Problem problem;
        int horizon = 0;
        PartitionFamily partitions;
        std::uint64_t coverageVersion = 0;
        std::vector<RootProofRecord> proofs;
        std::vector<CheckedRootRecord> coverage;
        int q = 256;
        int u = 0;
        int v = 0;
        int w = 0;
        std::vector<std::vector<std::int64_t>> bByRemainingTurns;
        std::int64_t rootBound = 0;
        std::int64_t delta = 0;
    };

    struct FalseCheckResult {
        CheckResult check;
        bool provedFalse = false;
        FalseCertificate certificate;
        CheckedSnapshot snapshot;
    };

    class ProofKernel {
    public:
        [[nodiscard]] static Box baseBox(const RuleBundle &bundle, const RawState &state);

        [[nodiscard]] static std::optional<Box> alphaSingleton(const RawState &state, std::string &error);

        [[nodiscard]] static CheckedPartition validatePartition(
            const PredicatePartition &partition,
            const Box &base,
            std::uint32_t maxLeaves);

        [[nodiscard]] static CheckResult validateFamily(
            const PartitionFamily &family,
            const Box &base,
            int firstPosition,
            int lastPosition,
            std::uint32_t maxLeaves);

        [[nodiscard]] static std::optional<std::uint32_t> project(
            const RawState &state,
            const PredicatePartition &partition,
            std::string &error);

        [[nodiscard]] static PartitionFamily makeInitialFamily(
            const RuleBundle &bundle,
            const RawState &state,
            int horizon,
            std::uint32_t maxLeaves,
            std::string &error);

        [[nodiscard]] static CheckedSnapshot rebuildSupport(
            const RuleBundle &bundle,
            const Problem &problem,
            int horizon,
            const PartitionFamily &partitions,
            const ProofBudget &limits,
            BudgetReport &budget,
            const std::vector<RootProofRecord> *submittedProofs = nullptr,
            const std::vector<ProofTemplate> *generationTemplates = nullptr,
            std::uint64_t coverageVersion = 1);

        [[nodiscard]] static CheckResult advanceCompletionCheckpoint(
            const RuleBundle &bundle,
            const Problem &problem,
            const CompletionCheckpoint &checkpoint,
            const ProofBudget &limits,
            BudgetReport &budget,
            ProofTemplate &advancedTemplate);

        [[nodiscard]] static CheckResult refineLeaf(
            const PartitionFamily &current,
            const Box &base,
            int rngPosition,
            std::uint32_t localCellId,
            const Predicate &predicate,
            std::uint32_t maxLeaves,
            PartitionFamily &refined);

        [[nodiscard]] static CheckResult bindCheckedCache(
            CheckedKernelCache &cache,
            const Problem &problem);

        [[nodiscard]] static CheckResult rememberCheckedTemplate(
            CheckedKernelCache &cache,
            const Problem &problem,
            const ProofTemplate &proofTemplate,
            const ProofBudget &limits,
            BudgetReport &budget,
            bool &changed);

        [[nodiscard]] static CheckResult rememberCheckedSnapshot(
            CheckedKernelCache &cache,
            const Problem &problem,
            const CheckedSnapshot &snapshot,
            const ProofBudget &limits,
            BudgetReport &budget);

        [[nodiscard]] static FalseCheckResult tryFalseZeroPrice(
            const RuleBundle &bundle,
            const Problem &problem,
            int horizon,
            const PartitionFamily &partitions,
            const ProofBudget &limits,
            BudgetReport &budget,
            const std::vector<ProofTemplate> *generationTemplates = nullptr,
            std::uint64_t coverageVersion = 1);

        [[nodiscard]] static CheckResult checkTrivialFalse(
            const RuleBundle &bundle,
            const Problem &problem,
            int horizon,
            bool &provedFalse);

        [[nodiscard]] static CheckResult verifyFalseCertificate(
            const RuleBundle &bundle,
            const FalseCertificate &certificate,
            const ProofBudget &limits,
            BudgetReport &budget);

        [[nodiscard]] static CheckResult selfCheck(const RuleBundle &bundle);
    };
} // namespace d20proof
