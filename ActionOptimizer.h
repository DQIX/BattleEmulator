//
// Exact shortest-turn search API.
//

#ifndef ACTION_OPTIMIZER_FIXED_H
#define ACTION_OPTIMIZER_FIXED_H

#include "Player.h"
#include "Genome.h"
#include <cstdint>
#include <utility>

class ActionOptimizer {
public:
    // Heuristic-free IDDFS. maxGenerations/seedOffset remain only for ABI compatibility.
    static Genome RunAlgorithm(const Player players[4], uint64_t seed, int turns, int maxGenerations,
                               int actions[350], int seedOffset);
    static std::pair<int, Genome> RunAlgorithmAsync(const Player players[4], uint64_t seed, int turns,
                                                    int maxGenerations, int actions[350], int numThreads,
                                                    bool dropbug);

    // Compatibility no-op retained for older callers.
    static void updateCompromiseScore(Genome &genome);

    static uint32_t getNodesUsed();
};

#endif // ACTION_OPTIMIZER_FIXED_H