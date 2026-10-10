#ifndef BARUBOROSU_SEARCH_H
#define BARUBOROSU_SEARCH_H

#include <cstdint>
#include "BattleResult.h"
#include "Genome.h"

class BaruborosuSearch {
public:
    static constexpr int DefaultVariant = 3;

    struct Result {
        Genome genome{};
        BattleResult replay{};
        bool inputValid = true;
        bool victory = false;
        int prefixLength = 0;
        int equipmentChanges = 0;
        int variant = DefaultVariant;
        int passes = 0;
        int improvements = 0;
        uint64_t expanded = 0;
        uint64_t generated = 0;
        uint64_t replayRejected = 0;
        double firstVictoryMs = -1;
        double elapsedMs = 0;
    };

    // The -1 terminator defines the immutable prefix. The budget includes
    // prefix replay, all search passes, verification and result construction.
    // 0: damage beam, 1: charge/diversity beam, 2: planning beam,
    // 3: the production portfolio, sharing one incumbent and one deadline.
    static Result Run(const Player initial[2], uint64_t seed,
                      const int actions[350], int budgetMs = 1500,
                      int variant = DefaultVariant);
};

#endif
