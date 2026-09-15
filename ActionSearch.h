#ifndef NEWDIRECTORY_ACTIONSEARCH_H
#define NEWDIRECTORY_ACTIONSEARCH_H

#include <array>
#include <cstdint>

#include "Player.h"

struct ActionSearchState {
    Player players[2];
    uint64_t nowState = 0;
    int position = 1;
};

struct ActionSearchResult {
    std::array<int32_t, 350> actions{};
    int length = 0;
    bool victory = false;
    int completedBeamWidth = 0;
    uint64_t expanded = 0;
    int64_t score = 0;
};

class ActionSearch {
public:
    static ActionSearchResult Run(const ActionSearchState &start, uint64_t seed, int maxTurns,
                                  int timeBudgetMs);
};

#endif // NEWDIRECTORY_ACTIONSEARCH_H
