#ifndef ISINOBANNINN_SEARCH_H
#define ISINOBANNINN_SEARCH_H

#include "Genome.h"
#include <cstdint>

namespace IsinobanninnSearch {
// Variants are search strategies, not different battle worlds.
struct Options {
    int variant = 4;
    int budgetMs = 1500;
    int maxTotalTurns = 349;
};

struct Statistics {
    bool victory = false;
    bool inputValid = true;
    int prefixLength = 0;
    int battlePosition = 0;
    int equipmentChanges = 0;
    int turns = 0;
    int rngPosition = 1;
    int variant = 0;
    uint64_t transitions = 0;
    uint64_t replays = 0;
    uint64_t replayMismatches = 0;
    double elapsedMs = 0;
};

Genome Run(const Player initial[2], uint64_t seed, const int actions[350],
           const Options& options = {});
const Statistics& GetStatistics();
const char* VariantName(int variant);
}
#endif