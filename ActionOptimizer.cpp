#include "ActionOptimizer.h"
#include "IsinobanninnSearch.h"

Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns,
                                     int maxGenerations, int actions[350], int seedOffset,
                                     int searchVariant, int budgetMs) {
    // The sentinel determines the fixed prefix, not legacy generation limits.
    (void)turns;
    (void)maxGenerations;
    (void)seedOffset;
    IsinobanninnSearch::Options options;
    options.variant = searchVariant;
    options.budgetMs = budgetMs;
    return IsinobanninnSearch::Run(players, seed, actions, options);
}

uint32_t ActionOptimizer::getNodesUsed() {
    return static_cast<uint32_t>(IsinobanninnSearch::GetStatistics().transitions);
}

void ActionOptimizer::updateCompromiseScore(Genome&) {}

std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(const Player players[2], uint64_t seed,
        int turns, int maxGenerations, int actions[350], int numThreads, bool dropbug) {
    (void)numThreads;
    (void)dropbug;
    return {0, RunAlgorithm(players, seed, turns, maxGenerations, actions, 0)};
}