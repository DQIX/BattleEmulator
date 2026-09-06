#pragma once

#include "../BattleInitialPlayers.h"
#include "ProofTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace d20proof {
    inline Problem initialProblem(std::uint64_t seed) {
        Problem problem;
        problem.ruleId = {"yo2_be.d20", 1};
        problem.seed = seed;
        problem.s0.players[0] = BasePlayers[0];
        problem.s0.players[1] = BasePlayers[1];
        problem.s0.position = 1;
        problem.s0.nowState = 0;
        problem.startTurn = 0;
        return problem;
    }

    bool parseU64(const char *text, std::uint64_t &value);

    bool parseInt(const char *text, int &value);

    std::vector<int> parseActions(int argc, char **argv, int firstArgument, bool &ok);

    const char *solveKindName(SolveKind kind) noexcept;

    const char *minimalityKindName(MinimalityKind kind) noexcept;

    std::string renderSolveResult(const SolveResult &result);
} // namespace d20proof
