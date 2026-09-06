#pragma once

#include "ProofKernel.h"

#include <chrono>

namespace d20proof {
    class WorldlineSolver {
    public:
        explicit WorldlineSolver(const RuleBundle &bundle) : bundle_(bundle) {
        }

        [[nodiscard]] SolveResult solveSuffix(
            const Problem &problem,
            int horizon,
            const ProofBudget &budget,
            const std::vector<int> &candidateHint = {}) const;

        [[nodiscard]] SolveResult proveMinimum(
            const Problem &problem,
            int horizonLimit,
            const ProofBudget &budget,
            const std::vector<int> &candidateHint = {}) const;

        [[nodiscard]] SolveResult extendPrefix(
            const Problem &initialProblem,
            const std::vector<int> &prefix,
            int horizon,
            const ProofBudget &budget,
            const std::vector<int> &candidateHint = {},
            bool proveMinimal = false) const;

    private:
        [[nodiscard]] SolveResult solveSuffixWithBudget(
            const Problem &problem,
            int horizon,
            const ProofBudget &budget,
            const std::vector<int> &candidateHint,
            std::chrono::steady_clock::time_point start,
            BudgetReport report,
            CheckedKernelCache &checkedCache) const;

        [[nodiscard]] SolveResult tightenMinimumWithBudget(
            const Problem &problem,
            SolveResult witness,
            const ProofBudget &budget,
            std::chrono::steady_clock::time_point start,
            CheckedKernelCache &checkedCache) const;

        const RuleBundle &bundle_;
    };
} // namespace d20proof
