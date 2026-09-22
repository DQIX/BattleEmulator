#include "ActionOptimizer.h"
#include "ZuoSearch.h"

namespace { thread_local uint32_t nodesUsed = 0; }

uint32_t ActionOptimizer::getNodesUsed() { return nodesUsed; }

// Keep the existing API for callers outside SearchRequest. The old generation
// and seedOffset parameters are not separate time budgets or alternate worlds.
Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed,
        int turns, int maxGenerations, int actions[350], int seedOffset) {
    (void)maxGenerations;
    (void)seedOffset;
    const auto result = ZuoSearch::Run(players, seed, actions, turns);
    nodesUsed = static_cast<uint32_t>(result.expanded);
    return result.toGenome();
}

void ActionOptimizer::updateCompromiseScore(Genome &) {}
