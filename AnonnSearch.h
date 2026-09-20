#ifndef ANONN_SEARCH_H
#define ANONN_SEARCH_H

#include "Genome.h"
#include <cstdint>

// One budget, including prefix replay and verification, for the anonn_v6 world.
namespace AnonnSearch {
struct Statistics {
    bool inputValid = false;
    bool victory = false;
    int variant = 0;
    int prefixLength = 0;
    int exactPosition = -1;
    int equipmentChanges = -1;
    int replayRejected = 0;
    uint32_t expanded = 0;
    double elapsedMs = 0;
};

// 0 selects the production search. Other values are benchmark alternatives.
void SetVariant(int variant);
const Statistics& LastStatistics();
Genome Run(const Player initial[2], uint64_t seed, const int prefix[350], int budgetMs);
}
#endif