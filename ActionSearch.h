#ifndef NEWDIRECTORY_ACTIONSEARCH_H
#define NEWDIRECTORY_ACTIONSEARCH_H

#include <array>
#include <cstdint>

#include "Player.h"

struct ActionSearchResult {
    std::array<int, 350> actions{};
    int length = 0;
    bool victory = false;
    uint64_t explored = 0;
    int64_t elapsedMicros = 0;
    int allyHp = 0;
    int enemyHp = 0;
};

class ActionSearch {
public:
    static ActionSearchResult Run(const Player startPlayers[2], uint64_t seed, int startPosition,
                                  uint64_t startState, int64_t budgetMicros);
};

#endif //NEWDIRECTORY_ACTIONSEARCH_H
