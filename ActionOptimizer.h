//
// ActionOptimizer compatibility API
// Delegates to the selected exact-replay Silyarumana search
//

#ifndef ACTION_OPTIMIZER_FIXED_H
#define ACTION_OPTIMIZER_FIXED_H

#include "Player.h"
#include "Genome.h"
#include <cstdint>
#include <utility>

class ActionOptimizer {
public:
    // Preserved entry point; the -1-terminated actions are a fixed prefix.
    static Genome RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                               int actions[350], int seedOffset);
    static std::pair<int, Genome> RunAlgorithmAsync(const Player players[2], uint64_t seed, int turns,
                                                    int maxGenerations, int actions[350], int numThreads,
                                                    bool dropbug);

    // Legacy API retained; the selected search does not use compromise scores.
    static void updateCompromiseScore(Genome &genome);

    static uint32_t getNodesUsed();
};

#endif // ACTION_OPTIMIZER_FIXED_H