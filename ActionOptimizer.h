#ifndef NEWDIRECTORY_ACTIONOPTIMIZER_H
#define NEWDIRECTORY_ACTIONOPTIMIZER_H

#include <cstdint>

#include "Player.h"

class ActionOptimizer {
public:
	struct Result {
		bool solved = false;
		bool exhausted = false;
		int maxDepth = 0;
		int turn = 0;
		uint64_t nodesVisited = 0;
		uint64_t winningNodes = 0;
		int32_t actions[350] = {};
	};

	static Result FindShortestWin(const Player copiedPlayers[2], uint64_t seed, int maxDepth);
};

#endif //NEWDIRECTORY_ACTIONOPTIMIZER_H
