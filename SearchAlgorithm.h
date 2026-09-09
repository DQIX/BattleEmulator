#ifndef YO2_BE_SEARCH_ALGORITHM_H
#define YO2_BE_SEARCH_ALGORITHM_H

#include <array>
#include <cstdint>

#include "Player.h"

namespace SearchAlgorithm {
    struct State {
        Player players[2]{};
        int position = 1;
        uint64_t nowState = 0;
    };

    struct Result {
        std::array<int32_t, 350> actions{};
        int actionCount = 0;
        bool victory = false;
        bool timedOut = false;
        uint64_t expanded = 0;
        double elapsedMs = 0.0;
        State finalState{};
    };

    Result Run(const State &startState, uint64_t seed, int absoluteStartTurn, int budgetMs);
}

#endif // YO2_BE_SEARCH_ALGORITHM_H
