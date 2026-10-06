#include "ActionOptimizer.h"
#include "BurudogaSearch.h"

ActionOptimizer::Result ActionOptimizer::FindShortestWin(const Player initialPlayers[2], uint64_t seed,
                                                         const int32_t fixedActions[350], int fixedTurns) {
	return BurudogaSearch::Run(initialPlayers, seed, fixedActions, fixedTurns);
}
