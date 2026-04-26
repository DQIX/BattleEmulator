//
// Fixed ActionOptimizer Header
// Contains declarations for the hybrid action search
//

#ifndef ACTION_OPTIMIZER_FIXED_H
#define ACTION_OPTIMIZER_FIXED_H

#include "Player.h"
#include "Genome.h"
#include <cstdint>
#include <utility>

class ActionOptimizer {
public:
    // Main hybrid search entry point
    static Genome RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                               int actions[350], int seedOffset);
    static std::pair<int, Genome> RunAlgorithmAsync(const Player players[2], uint64_t seed, int turns,
                                                    int maxGenerations, int actions[350], int numThreads,
                                                    bool dropbug);

    // Helper function for compromise score updates
    static void updateCompromiseScore(Genome &genome);

    static uint32_t getNodesUsed();
};

#endif // ACTION_OPTIMIZER_FIXED_H
