//
// Deadline-bounded beam search with replay-verified results.
//

#ifndef ACTION_OPTIMIZER_FIXED_H
#define ACTION_OPTIMIZER_FIXED_H

#include "Player.h"
#include "Genome.h"
#include <cstdint>

class ActionOptimizer {
public:
    // One total wall-clock budget, including the fixed prefix and exact replays.
    // Initialized means a fresh replay defeated the enemy; fitness is its event count.
    static Genome RunAlgorithm(const Player players[2], uint64_t seed,
                               const int actions[350], int budgetMs = 1500);

    // Helper function for compromise score updates
    static void updateCompromiseScore(Genome &genome);
};

#endif // ACTION_OPTIMIZER_FIXED_H