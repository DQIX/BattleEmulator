#include "CliCommon.h"

#include <limits>
#include <sstream>

namespace d20proof {
    bool parseU64(const char *text, std::uint64_t &value) {
        try {
            std::size_t consumed = 0;
            const std::uint64_t parsed = std::stoull(text, &consumed, 0);
            if (text[consumed] != '\0') {
                return false;
            }
            value = parsed;
            return true;
        } catch (...) {
            return false;
        }
    }

    bool parseInt(const char *text, int &value) {
        try {
            std::size_t consumed = 0;
            const long long parsed = std::stoll(text, &consumed, 10);
            if (text[consumed] != '\0' || parsed < std::numeric_limits<int>::min() ||
                parsed > std::numeric_limits<int>::max()) {
                return false;
            }
            value = static_cast<int>(parsed);
            return true;
        } catch (...) {
            return false;
        }
    }

    std::vector<int> parseActions(int argc, char **argv, int firstArgument, bool &ok) {
        std::vector<int> actions;
        ok = true;

        for (int argument = firstArgument; argument < argc; ++argument) {
            int action = 0;
            if (!parseInt(argv[argument], action)) {
                ok = false;
                return {};
            }
            actions.push_back(action);
        }
        return actions;
    }

    const char *solveKindName(SolveKind kind) noexcept {
        switch (kind) {
            case SolveKind::Win:
                return "WIN";
            case SolveKind::ProvedFalse:
                return "PROVED_FALSE";
            case SolveKind::Unknown:
                return "UNKNOWN";
            case SolveKind::InvalidInput:
                return "INVALID_INPUT";
            case SolveKind::InvalidPrefix:
                return "INVALID_PREFIX";
            case SolveKind::UnsupportedInput:
                return "UNSUPPORTED_INPUT";
            case SolveKind::ModelError:
                return "MODEL_ERROR";
        }
        return "MODEL_ERROR";
    }

    const char *minimalityKindName(MinimalityKind kind) noexcept {
        switch (kind) {
            case MinimalityKind::NotChecked:
                return "NOT_CHECKED";
            case MinimalityKind::Optimal:
                return "OPTIMAL";
            case MinimalityKind::Unknown:
                return "UNKNOWN";
        }
        return "UNKNOWN";
    }

    std::string renderSolveResult(const SolveResult &result) {
        std::ostringstream output;
        output << solveKindName(result.kind) << " horizon=" << result.horizon;

        if (result.firstWinningTurn >= 0) {
            output << " firstWinningTurn=" << result.firstWinningTurn;
        }
        if (result.minimality != MinimalityKind::NotChecked) {
            output << " minimality=" << minimalityKindName(result.minimality);
        }
        if (result.minimumTurnLowerBound >= 0 && result.minimumTurnUpperBound >= 0) {
            output << " minimum_interval=" << result.minimumTurnLowerBound
                   << ".." << result.minimumTurnUpperBound;
        }
        if (!result.reason.empty()) {
            output << " reason=\"" << result.reason << "\"";
        }

        output << " elapsed_ms=" << result.budget.elapsedMs
                << " work=" << result.budget.work
                << " bytes=" << result.budget.bytes
                << " price_evals=" << result.budget.priceEvaluations
                << " candidates=" << result.budget.candidates
                << " scans=" << result.budget.candidateScans
                << " repairs=" << result.budget.repairs;

        if (!result.commands.empty()) {
            output << " commands=";
            for (std::size_t index = 0; index < result.commands.size(); ++index) {
                if (index != 0) {
                    output << ',';
                }
                output << result.commands[index];
            }
        }
        return output.str();
    }
} // namespace d20proof
