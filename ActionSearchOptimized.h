#ifndef ACTION_SEARCH_OPTIMIZED_H
#define ACTION_SEARCH_OPTIMIZED_H

#include "ActionSearch.h"
#include <cstdint>

struct ActionSearchMetrics {
    double firstVictoryMs = -1;
    double bestVictoryMs = -1;
    double elapsedMs = 0;
    uint64_t updates = 0;
    uint64_t duplicates = 0;
    uint64_t hashCollisions = 0;
    int suffixResultPosition = 0; // BattleResult event count, NOT the RNG position.
    ActionSearchState finalState{};
};

class ActionSearchOptimized {
public:
    // variant=0 is the selected default; positive variants are retained experiments.
    // LCG is initialized by the caller, exactly as for ActionSearch::Run.
    static ActionSearchResult Run(const ActionSearchState &start, uint64_t seed,
                                  int maxTurns, int timeBudgetMs,
                                  ActionSearchMetrics *metrics = nullptr, int variant = 0);
    static bool SameState(const ActionSearchState &a, const ActionSearchState &b);
    static const char *VariantName(int variant);
};

#endif