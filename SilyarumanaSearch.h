#ifndef SILYARUMANA_SEARCH_H
#define SILYARUMANA_SEARCH_H

#include "Genome.h"
#include "BattleResult.h"

namespace SilyarumanaSearch {
// 0: selected tension beam (same ranking as 1); 2: damage/turn beam;
// 3: status-diverse beam. All share one wall-clock budget, including replay.
struct Result {
    Genome genome{};
    BattleResult replay{};
    bool victory = false;
    bool inputValid = true;
    int prefixLength = 0;
    int equipmentChanges = 0;
    int prefixHP = 0;
    int prefixMP = 0;
    int prefixDazzle = 0;
    bool prefixStunned = false;
    unsigned long long nodes = 0;
    unsigned long long rejectedReplays = 0;
    double elapsedMs = 0;
    int variant = 0;
};
Result Run(const Player initial[2], uint64_t seed, const int actions[350],
           int budgetMs = 1500, int variant = 0);
unsigned long long NodesUsed();
}
#endif
