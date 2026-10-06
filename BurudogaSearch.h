#ifndef BATTLEEMULATOR_BURUDOGASEARCH_H
#define BATTLEEMULATOR_BURUDOGASEARCH_H

#include "ActionOptimizer.h"

namespace BurudogaSearch {
	ActionOptimizer::Result Run(const Player initialPlayers[2], uint64_t seed,
	                            const int32_t fixedActions[350], int fixedTurns);
}

#endif
