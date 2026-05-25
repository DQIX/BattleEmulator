#include "ActionOptimizer.h"

#include <cstdlib>

#include "BattleEmulator.h"
#include "lcg.h"

namespace {
	constexpr uint64_t kTurnBitsMask = 0xFFFFF000ULL;
	constexpr std::size_t kBoxBytes = 500ULL * 1024ULL * 1024ULL;
	constexpr int kMaxEncodedTurns = 32;
	constexpr int kHighDamageThreshold = 27;
	constexpr int kMaxStoredSolutions = 2000;
	constexpr int kAllyAttackAnimationCost = 10;
	constexpr int kAllyHealAnimationCost = 10;
	constexpr int kAllyFleeAnimationCost = 20;
	constexpr int kDefenceAnimationCost = 15;
	constexpr int kEnemyAttackAnimationCost = 12;
	constexpr int kEnemyRubbleAnimationCost = 16;
	constexpr int kFinalEnemyActionCost = 40;
	constexpr int kScoreCostLimit = 1024;
	constexpr int kBranchActions[] = {
		BattleEmulator::HEAL,
		BattleEmulator::ATTACK_ALLY,
		BattleEmulator::FLEE_ALLY,
		BattleEmulator::DEFENCE,
	};
	static_assert(sizeof(kBranchActions) / sizeof(kBranchActions[0]) == ActionOptimizer::BranchActionCount);

	struct SearchNode {
		uint64_t nowState;
		uint64_t pathBits;
		int position;
		int allyHp;
		int enemyHp;
		int allyMp;
		uint16_t highDamageMask;
		uint16_t enemyAttackMask;
		uint16_t enemyRubbleMask;
	};

	struct SolutionCandidate {
		uint64_t pathBits;
		uint16_t highDamageMask;
		uint16_t enemyAttackMask;
		uint16_t enemyRubbleMask;
		int16_t score;
	};

	struct SolutionStore {
		SolutionCandidate candidates[kMaxStoredSolutions];
		int count = 0;
		int worstIndex = -1;
		int16_t worstScore = 32767;
	};

	struct NodeBoxes {
		SearchNode *current = nullptr;
		SearchNode *next = nullptr;
		std::size_t capacity = 0;
	};

	NodeBoxes g_boxes;
	SolutionStore g_solutions;

	void releaseBoxes() {
		std::free(g_boxes.current);
		std::free(g_boxes.next);
		g_boxes.current = nullptr;
		g_boxes.next = nullptr;
		g_boxes.capacity = 0;
	}

	struct ReleaseBoxesOnExit {
		~ReleaseBoxesOnExit() {
			releaseBoxes();
		}
	};

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

		releaseBoxes();
		g_boxes.current = current;
		g_boxes.next = next;
		g_boxes.capacity = capacity;
		return true;
	}

	int countBits(uint16_t value) {
		int count = 0;
		while (value != 0) {
			value = static_cast<uint16_t>(value & static_cast<uint16_t>(value - 1));
			++count;
		}
		return count;
	}

	int actionAnimationCost(int action) {
		switch (action) {
			case BattleEmulator::HEAL:
				return kAllyHealAnimationCost;
			case BattleEmulator::FLEE_ALLY:
				return kAllyFleeAnimationCost;
			case BattleEmulator::DEFENCE:
				return kDefenceAnimationCost;
			case BattleEmulator::ATTACK_ALLY:
			default:
				return kAllyAttackAnimationCost;
		}
	}

	int estimateAnimationCost(uint64_t pathBits, int depth, uint16_t enemyAttackMask, uint16_t enemyRubbleMask) {
		int cost = 0;
		for (int i = 0; i < depth; ++i) {
			const auto actionIndex = static_cast<int>((pathBits >> (i * 2)) & 0x3ULL);
			cost += actionAnimationCost(kBranchActions[actionIndex]);
		}
		cost += countBits(enemyAttackMask) * kEnemyAttackAnimationCost;
		cost += countBits(enemyRubbleMask) * kEnemyRubbleAnimationCost;
		if (depth > 0) {
			const auto finalTurnBit = static_cast<uint16_t>(1U << (depth - 1));
			if (((enemyAttackMask | enemyRubbleMask) & finalTurnBit) != 0) {
				cost += kFinalEnemyActionCost;
			}
		}
		return cost;
	}

	int16_t makeCandidateScore(uint64_t pathBits, int depth, uint16_t highDamageMask,
	                           uint16_t enemyAttackMask, uint16_t enemyRubbleMask) {
		int cost = estimateAnimationCost(pathBits, depth, enemyAttackMask, enemyRubbleMask);
		if (cost > kScoreCostLimit) {
			cost = kScoreCostLimit;
		}
		return static_cast<int16_t>((kScoreCostLimit - cost) * 16 + countBits(highDamageMask));
	}

	void resetSolutions() {
		g_solutions.count = 0;
		g_solutions.worstIndex = -1;
		g_solutions.worstScore = 32767;
	}

	bool hasValidWorstSolution() {
		return g_solutions.worstIndex >= 0
		       && g_solutions.worstIndex < g_solutions.count
		       && g_solutions.worstIndex < kMaxStoredSolutions;
	}

	void refreshWorstSolution() {
		if (g_solutions.count <= 0) {
			g_solutions.worstIndex = -1;
			g_solutions.worstScore = 32767;
			return;
		}
		if (g_solutions.count > kMaxStoredSolutions) {
			g_solutions.count = kMaxStoredSolutions;
		}
		int worstIndex = 0;
		int16_t worstScore = g_solutions.candidates[0].score;
		for (int i = 1; i < g_solutions.count; ++i) {
			if (g_solutions.candidates[i].score < worstScore) {
				worstScore = g_solutions.candidates[i].score;
				worstIndex = i;
			}
		}
		g_solutions.worstIndex = worstIndex;
		g_solutions.worstScore = worstScore;
	}

	void storeSolution(uint64_t pathBits, int depth, uint16_t highDamageMask,
	                   uint16_t enemyAttackMask, uint16_t enemyRubbleMask) {
		const int16_t score = makeCandidateScore(pathBits, depth, highDamageMask, enemyAttackMask, enemyRubbleMask);
		const SolutionCandidate candidate = {
			pathBits,
			highDamageMask,
			enemyAttackMask,
			enemyRubbleMask,
			score,
		};

		if (g_solutions.count < kMaxStoredSolutions) {
			g_solutions.candidates[g_solutions.count++] = candidate;
			if (g_solutions.worstIndex == -1 || score < g_solutions.worstScore) {
				g_solutions.worstIndex = g_solutions.count - 1;
				g_solutions.worstScore = score;
			}
			return;
		}

		if (!hasValidWorstSolution()) {
			refreshWorstSolution();
		}
		if (!hasValidWorstSolution()) {
			return;
		}
		if (score <= g_solutions.worstScore) {
			return;
		}

		g_solutions.candidates[g_solutions.worstIndex] = candidate;
		refreshWorstSolution();
	}

	uint64_t bestSolutionPath() {
		if (g_solutions.count <= 0) {
			return 0;
		}
		int bestIndex = 0;
		int16_t bestScore = g_solutions.candidates[0].score;
		for (int i = 1; i < g_solutions.count; ++i) {
			if (g_solutions.candidates[i].score > bestScore) {
				bestScore = g_solutions.candidates[i].score;
				bestIndex = i;
			}
		}
		return g_solutions.candidates[bestIndex].pathBits;
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
	const ReleaseBoxesOnExit releaseBoxesOnExit;
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
	resetSolutions();

	std::size_t currentCount = 1;
	g_boxes.current[0] = {
		startNowState,
		0,
		startPosition,
		startPlayers[0].hp,
		startPlayers[1].hp,
		startPlayers[0].mp,
		0,
		0,
		0,
	};

	for (int depth = 0; depth < maxDepth; ++depth) {
		std::size_t nextCount = 0;
		bool foundAtThisDepth = false;
		const bool needsNextLayer = depth + 1 < maxDepth;

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
				BattleEmulator::StepSummary summary;
				BattleEmulator::StepContext context;
				context.nowState = node.nowState;
				context.summary = &summary;

				const BattleEmulator::StepResult step = BattleEmulator::StepAction(
					&position, startTurn + depth + 1, action, players, nullptr, nullptr, -2, &context);
				(void) step;
				++result.nodesVisited;

				const uint64_t pathBits = node.pathBits | (static_cast<uint64_t>(actionIndex) << (depth * 2));
				const auto turnBit = static_cast<uint16_t>(1U << depth);
				uint16_t highDamageMask = node.highDamageMask;
				uint16_t enemyAttackMask = node.enemyAttackMask;
				uint16_t enemyRubbleMask = node.enemyRubbleMask;
				if (summary.allyAction != 0 && summary.allyAction != BattleEmulator::HEAL
				    && summary.allyDamage >= kHighDamageThreshold) {
					highDamageMask = static_cast<uint16_t>(highDamageMask | turnBit);
				}
				if (summary.enemyAction == BattleEmulator::ATTACK_ENEMY) {
					enemyAttackMask = static_cast<uint16_t>(enemyAttackMask | turnBit);
				} else if (summary.enemyAction == BattleEmulator::RUBBLE) {
					enemyRubbleMask = static_cast<uint16_t>(enemyRubbleMask | turnBit);
				}
				if (players[1].hp == 0) {
					foundAtThisDepth = true;
					++result.winningNodes;
					storeSolution(pathBits, depth + 1, highDamageMask, enemyAttackMask, enemyRubbleMask);
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
					highDamageMask,
					enemyAttackMask,
					enemyRubbleMask,
				};
			}
		}

		if (foundAtThisDepth) {
			result.solved = true;
			result.turn = depth + 1;
			decodeActions(bestSolutionPath(), result.turn, result.actions);
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
