#pragma once

#include "ProofTypes.h"

namespace d20proof {
    struct RuleBundle;

    class ExactReplay {
    public:
        static constexpr int kRngTapeSize = 5000;

        [[nodiscard]] static bool isSelectable(
            const RuleBundle &bundle,
            const RawState &state,
            int command) noexcept;

        [[nodiscard]] static std::string validateProblem(
            const RuleBundle &bundle,
            const Problem &problem,
            int remainingTurns);

        [[nodiscard]] static ReplayResult replay(
            const RuleBundle &bundle,
            const Problem &problem,
            const std::vector<int> &commands,
            bool rejectCommandsAfterTerminal,
            const ProofBudget *budget = nullptr,
            BudgetReport *report = nullptr);

        [[nodiscard]] static PrefixReceipt replayPrefix(
            const RuleBundle &bundle,
            const Problem &problem,
            const std::vector<int> &prefix,
            const ProofBudget *budget = nullptr,
            BudgetReport *report = nullptr);

        [[nodiscard]] static Problem bindSuffixProblem(
            const Problem &initialProblem,
            const PrefixReceipt &receipt);
    };
} // namespace d20proof
