#ifndef ERUGIOSU1_SEARCH_H
#define ERUGIOSU1_SEARCH_H

#include <cstdint>
#include "BattleResult.h"
#include "Genome.h"

class Erugiosu1Search {
public:
    // Production is connected only to erugiosu_1_gouketu (-Derugi1=1).
    static constexpr int DefaultVariant = 3;

    struct Result {
        Genome genome{};
        BattleResult replay{};
        bool inputValid = true;
        bool victory = false;
        bool replayVerified = false;
        int prefixLength = 0;
        int equipmentChanges = 0;
        int passes = 0;
        int improvements = 0;
        int maxSuffixDepth = 0;
        uint64_t expanded = 0;
        uint64_t generated = 0;
        uint64_t replayRejected = 0;
        double firstVictoryMs = -1;
        double elapsedMs = 0;
    };

    // The -1 terminator defines the immutable prefix. All passes and fresh
    // replay share this budget, including prefix construction and cleanup.
    // Variants 0/1/2 select damage/charge/planning ordering for benchmarking;
    // production variant 3 shares one exact incumbent across those orderings.
    static Result Run(const Player initial[2], uint64_t seed,
                      const int actions[350], int budgetMs = 1500,
                      int variant = DefaultVariant);
};

#endif
