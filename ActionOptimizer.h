//
// Fixed ActionOptimizer Header
// Contains declarations for the enhanced A* algorithm
//

#ifndef ACTION_OPTIMIZER_FIXED_H
#define ACTION_OPTIMIZER_FIXED_H

#include "Player.h"
#include "Genome.h"
#include <cstdint>

class ActionOptimizer {
public:
    // Main A* algorithm with fixes for f-cost stagnation
    static Genome RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                               int actions[350], int seedOffset);
    
    // Helper function for compromise score updates
    static void updateCompromiseScore(Genome &genome);
};

#endif // ACTION_OPTIMIZER_FIXED_H