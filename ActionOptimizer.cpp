#include "ActionOptimizer.h"

#include <cstdlib>

#include "BattleEmulator.h"
#include "lcg.h"

namespace {
	constexpr uint64_t kTurnBitsMask = 0xFFFFF000ULL;
	constexpr std::size_t kBoxBytes = 500ULL * 1024ULL * 1024ULL;
	constexpr int kMaxEncodedTurns = 32;
	constexpr int kBranchActions[] = {
		BattleEmulator::HEAL,
		BattleEmulator::ATTACK_ALLY,
		BattleEmulator::FLEE_ALLY,
	};
	static_assert(sizeof(kBranchActions) / sizeof(kBranchActions[0]) == ActionOptimizer::BranchActionCount);

	struct SearchNode {
		uint64_t nowState;
		uint64_t pathBits;
		int position;
		int allyHp;
		int enemyHp;
		int allyMp;
	};

	struct NodeBoxes {
		SearchNode *current = nullptr;
		SearchNode *next = nullptr;
		std::size_t capacity = 0;
	};

	NodeBoxes g_boxes;

	uint64_t storeTurn(uint64_t nowState, int turn) {
		return (nowState & ~kTurnBitsMask) | (static_cast<uint64_t>(turn) << 12ULL);
	}

	std::size_t requiredCapacity(int maxDepth) {
		constexpr int branchCount = static_cast<int>(sizeof(kBranchActions) / sizeof(kBranchActions[0]));
		const std::size_t maxBoxCapacity = kBoxBytes / sizeof(SearchNode);
		std::size_t result = 1;
		for (int i = 1; i < maxDepth; ++i) {
			if (result > maxBoxCapacity / branchCount) {
				return maxBoxCapacity;
			}
			result *= branchCount;
		}
		return result < 1 ? 1 : result;
	}

	bool ensureBoxes(std::size_t capacity) {
		if (g_boxes.capacity >= capacity) {
			return true;
		}

		auto *current = static_cast<SearchNode *>(std::malloc(sizeof(SearchNode) * capacity));
		auto *next = static_cast<SearchNode *>(std::malloc(sizeof(SearchNode) * capacity));
		if (current == nullptr || next == nullptr) {
			std::free(current);
			std::free(next);
			return false;
		}

		std::free(g_boxes.current);
		std::free(g_boxes.next);
		g_boxes.current = current;
		g_boxes.next = next;
		g_boxes.capacity = capacity;
		return true;
	}

	void decodeActions(uint64_t pathBits, int depth, int32_t actions[350]) {
		for (int i = 0; i < 350; ++i) {
			actions[i] = 0;
		}
		for (int i = 0; i < depth; ++i) {
			const auto actionIndex = static_cast<int>((pathBits >> (i * 2)) & 0x3ULL);
			actions[i] = kBranchActions[actionIndex];
		}
		if (depth < 350) {
			actions[depth] = -1;
		}
	}
}

ActionOptimizer::Result ActionOptimizer::FindShortestWin(const Player startPlayers[2], uint64_t seed, int startPosition,
                                                         uint64_t startNowState, int startTurn, int maxDepth) {
	Result result;
	result.maxDepth = maxDepth;

	if (maxDepth <= 0 || maxDepth > kMaxEncodedTurns) {
		result.exhausted = true;
		return result;
	}

	const std::size_t capacity = requiredCapacity(maxDepth);
	if (!ensureBoxes(capacity)) {
		result.exhausted = true;
		return result;
	}

	lcg::init(seed, true);

	std::size_t currentCount = 1;
	g_boxes.current[0] = {
		startNowState,
		0,
		startPosition,
		startPlayers[0].hp,
		startPlayers[1].hp,
		startPlayers[0].mp,
	};

	for (int depth = 0; depth < maxDepth; ++depth) {
		std::size_t nextCount = 0;
		bool foundAtThisDepth = false;
		const bool needsNextLayer = depth + 1 < maxDepth;
		uint64_t bestPath = 0;

		for (std::size_t nodeIndex = 0; nodeIndex < currentCount; ++nodeIndex) {
			const SearchNode &node = g_boxes.current[nodeIndex];

			for (int actionIndex = 0; actionIndex < static_cast<int>(sizeof(kBranchActions) / sizeof(kBranchActions[0])); ++actionIndex) {
				const int action = kBranchActions[actionIndex];
				if (action == BattleEmulator::HEAL && node.allyMp <= 0) {
					continue;
				}

				Player players[2] = {startPlayers[0], startPlayers[1]};
				players[0].hp = node.allyHp;
				players[0].mp = node.allyMp;
				players[1].hp = node.enemyHp;

				int position = node.position;
				BattleEmulator::StepContext context;
				context.nowState = node.nowState;

				const BattleEmulator::StepResult step = BattleEmulator::StepAction(
					&position, startTurn + depth + 1, action, players, nullptr, nullptr, -2, &context);
				(void) step;
				++result.nodesVisited;

				const uint64_t pathBits = node.pathBits | (static_cast<uint64_t>(actionIndex) << (depth * 2));
				if (players[1].hp == 0) {
					if (!foundAtThisDepth) {
						bestPath = pathBits;
					}
					foundAtThisDepth = true;
					++result.winningNodes;
					continue;
				}
				if (players[0].hp == 0 || foundAtThisDepth || !needsNextLayer) {
					continue;
				}

				if (nextCount >= g_boxes.capacity) {
					result.exhausted = true;
					return result;
				}

				g_boxes.next[nextCount++] = {
					storeTurn(context.nowState, startTurn + depth + 1),
					pathBits,
					position,
					players[0].hp,
					players[1].hp,
					players[0].mp,
				};
			}
		}

		if (foundAtThisDepth) {
			result.solved = true;
			result.turn = depth + 1;
			decodeActions(bestPath, result.turn, result.actions);
			return result;
		}
		if (nextCount == 0) {
			return result;
		}

		SearchNode *swap = g_boxes.current;
		g_boxes.current = g_boxes.next;
		g_boxes.next = swap;
		currentCount = nextCount;
	}

	return result;
}
