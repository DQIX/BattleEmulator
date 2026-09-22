#ifndef GANASADAI2_SEARCH_H
#define GANASADAI2_SEARCH_H

#include <cstdint>
#include "BattleResult.h"
#include "Genome.h"

// All entry points use the same search and the same total wall-clock budget.
class Ganasadai2Search {
public:
    static constexpr int DefaultVariant = 22;
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

    // Variants 0..11 are development benchmarks; 22 is the production portfolio.
    // The -1 terminator, not an assumed prefix length, defines the fixed input.
    static Result Run(const Player initial[2], uint64_t seed,
                      const int actions[350], int budgetMs = 1500,
                      int variant = DefaultVariant);
};

#endif