//
// Deadline-bounded exact-transition search for the current branch.
//

#ifndef ACTION_OPTIMIZER_FIXED_H
#define ACTION_OPTIMIZER_FIXED_H

#include "Player.h"
#include "Genome.h"
#include <cstdint>

class ActionOptimizer {
public:
    // actions[0..first -1) is immutable prefix. About 1500 ms TOTAL budget.
    // Legacy turns/maxGenerations/seedOffset parameters remain ABI-compatible;
    // the prefix array and wall clock, rather than a node limit, govern search.
    // A successful Genome has Initialized=true, EnemyPlayer.hp==0, and fitness
    // equal to fresh replay's BattleResult.position. position stays RNG cursor.
    static Genome RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                               int actions[350], int seedOffset);

    // Helper function for compromise score updates
    static void updateCompromiseScore(Genome &genome);
};

#endif // ACTION_OPTIMIZER_FIXED_H