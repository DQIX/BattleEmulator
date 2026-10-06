#ifndef NEWDIRECTORY_ACTIONOPTIMIZER_H
#define NEWDIRECTORY_ACTIONOPTIMIZER_H

#include <cstdint>

#include "Player.h"
#include "BattleResult.h"

class ActionOptimizer {
public:
	static constexpr int SearchMilliseconds = 1500;

	struct Result {
		bool solved = false;
		bool exhausted = false;
		int maxDepth = 0;
		int turn = 0;
		uint64_t nodesVisited = 0;
		uint64_t winningNodes = 0;
		int32_t actions[350] = {-1};
		BattleResult replay;
		int enemyRubbleCount = 0;
		int equipmentChanges = 0;
		int rngPosition = 1;
		double elapsedMs = 0;
		int passes = 0;
		int replayChecks = 0;
		int finalAllyHp = 0;
		int finalEnemyHp = 0;
	};

	static Result FindShortestWin(const Player initialPlayers[2], uint64_t seed,
	                              const int32_t fixedActions[350], int fixedTurns);
};

#endif //NEWDIRECTORY_ACTIONOPTIMIZER_H
