#include "ActionOptimizer.h"
#include "AnonnSearch.h"

uint32_t ActionOptimizer::getNodesUsed() {
    return AnonnSearch::LastStatistics().expanded;
}

Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns,
    int budgetMs, int actions[350], int seedOffset) {
    (void)turns;
    (void)seedOffset;
    return AnonnSearch::Run(players, seed, actions, budgetMs);
}

void ActionOptimizer::updateCompromiseScore(Genome&) {}

std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(const Player players[2], uint64_t seed,
    int turns, int budgetMs, int actions[350], int numThreads, bool dropbug) {
    // The target's RNG cache is shared. One search owns the total time budget.
    (void)numThreads;
    (void)dropbug;
    auto result = RunAlgorithm(players, seed, turns, budgetMs, actions, 0);
    return {static_cast<int>(getNodesUsed()), result};
}