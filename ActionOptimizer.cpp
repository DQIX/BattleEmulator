#include "ActionOptimizer.h"
#include "SilyarumanaSearch.h"
#include <algorithm>
#include <limits>

Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns,
                                    int maxGenerations, int actions[350], int seedOffset) {
    // Keep the public API. The -1-terminated prefix, not a caller's generation
    // limit or cached turn count, defines the exact starting state.
    (void)turns; (void)maxGenerations; (void)seedOffset;
    return SilyarumanaSearch::Run(players, seed, actions).genome;
}
std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(const Player players[2], uint64_t seed,
        int turns, int maxGenerations, int actions[350], int numThreads, bool dropbug) {
    (void)numThreads; (void)dropbug;
    return {0, RunAlgorithm(players, seed, turns, maxGenerations, actions, 0)};
}
uint32_t ActionOptimizer::getNodesUsed() {
    return static_cast<uint32_t>(std::min<unsigned long long>(SilyarumanaSearch::NodesUsed(), UINT32_MAX));
}
void ActionOptimizer::updateCompromiseScore(Genome&) {}
