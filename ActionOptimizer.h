#ifndef NEWDIRECTORY_ACTIONOPTIMIZER_H
#define NEWDIRECTORY_ACTIONOPTIMIZER_H

#include <cstdint>

#include "Player.h"

namespace ActionOptimizerDetail {
	constexpr int EstimateMaxDepth(int branchCount, uint64_t nodeBudget) {
		uint64_t layerNodes = 1;
		uint64_t totalNodes = 0;
		int depth = 0;
		while (layerNodes <= nodeBudget / static_cast<uint64_t>(branchCount)) {
			layerNodes *= static_cast<uint64_t>(branchCount);
			if (totalNodes > nodeBudget - layerNodes) {
				break;
			}
			totalNodes += layerNodes;
			++depth;
		}
		return depth;
	}
}

class ActionOptimizer {
public:
	static constexpr int BranchActionCount = 3;
	static constexpr uint64_t EstimatedTurnsPerSecond = 20000000ULL;
	static constexpr uint64_t SearchSecondsNumerator = 3ULL;
	static constexpr uint64_t SearchSecondsDenominator = 2ULL;
	static constexpr uint64_t SearchNodeBudget =
			EstimatedTurnsPerSecond * SearchSecondsNumerator / SearchSecondsDenominator;

	static constexpr int MaxSearchDepth = ActionOptimizerDetail::EstimateMaxDepth(BranchActionCount, SearchNodeBudget);

	struct Result {
		bool solved = false;
		bool exhausted = false;
		int maxDepth = 0;
		int turn = 0;
		uint64_t nodesVisited = 0;
		uint64_t winningNodes = 0;
		int32_t actions[350] = {};
	};

	static Result FindShortestWin(const Player startPlayers[2], uint64_t seed, int startPosition,
	                              uint64_t startNowState, int startTurn, int maxDepth);
};

#endif //NEWDIRECTORY_ACTIONOPTIMIZER_H
