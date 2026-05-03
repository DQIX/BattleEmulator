//
// Flexible ActionOptimizer with Adaptive Constraint Management
// Solves deadlock issues with longer predefined action sequences
//

#include "ActionOptimizer.h"
#include <random>
#include <unordered_set>
#include <memory>

#include "BattleEmulator.h"
#include "LinearIdPool.h"
#include "Genome.h"
#include "EnhancedHashCalculator.h"
#include "EnhancedCostCalculator.h"
#include "EnhancedHeapQueue.h"
#include "lcg.h"

struct ActionEntry{
	int action;
	bool (*condition)(const Genome&);
	// 実行後、効果があったか（事後）
	bool (*isEffective)(
		const Genome& before,
		const Genome& after
	);
};

// 1要素の検証
constexpr bool isValid(const ActionEntry& e){
	return e.condition != nullptr
		&& e.isEffective != nullptr;
}

// テーブル全体の検証
template <size_t N>
constexpr bool validateActionTable(const ActionEntry (&table)[N]){
	for(size_t i = 0; i < N; ++i){
		if(!isValid(table[i])){
			return false;
		}
	}
	return true;
}

constexpr ActionEntry ACTION_TABLE[] = {
	{
		BattleEmulator::MIDHEAL, [](const Genome& g){
			return (g.AllyPlayer.hp / g.AllyPlayer.maxHp) < 0.7;
		},
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::SPECIAL_MEDICINE, [](const Genome& g){
			return g.AllyPlayer.SpecialMedicineCount > 0;
		},
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::MAGIC_WATER, [](const Genome& g){
			return g.AllyPlayer.MagicWaterCount > 0;
		},
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::FLEE_ALLY, [](const Genome&){ return true; },
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::DOUBLE_UP,
		[](const Genome& g){
			return g.AllyPlayer.AtkBuffLevel == 0;
		},
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::PSYCHE_UP_ALLY,
		[](const Genome& g){
			return g.EnemyPlayer.hp > 180 && g.AllyPlayer.TensionLevel <= 3;
		},
		[](const Genome& before, const Genome& after){
			return after.AllyPlayer.TensionLevel > before.AllyPlayer.TensionLevel;
		}
		// 事後: テンションが上がったか

	},
	{
		BattleEmulator::BUFF,
		[](const Genome& g){ return g.AllyPlayer.mp >= 10 && g.AllyPlayer.BuffLevel <= 1; },
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::MULTITHRUST,
		[](const Genome& g){ return g.AllyPlayer.mp >= 10 && g.AllyPlayer.AtkBuffLevel >= 2; },
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::DEFENCE,
		[](const Genome&){ return true; },
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::ATTACK_ALLY,
		[](const Genome& g){ return g.EnemyPlayer.hp <= 120; },
		[](const Genome& b, const Genome& a){ return b.EnemyPlayer.hp != a.EnemyPlayer.hp; }
	},
	{
		BattleEmulator::GOSPEL_SONG,
		[](const Genome& g){ return g.AllyPlayer.specialChargeTurn >= 1 && g.AllyPlayer.DazzleLevel != 0; },
		[](const Genome& b, const Genome& a){ return true; }
	},
};

// ★ ここが本体 ★
static_assert(
	validateActionTable(ACTION_TABLE),
	"ACTION_TABLE contains null function pointer"
);

static uint32_t Node_Used;

uint32_t ActionOptimizer::getNodesUsed(){
	return Node_Used;
}

// Flexible A* Algorithm Implementation
Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                                     int actions[350], int seedOffset){
	lcg::init(seed, true);
	Node_Used = 0;
	//std::mt19937 rng(seed + seedOffset);

	// Cache enemy max HP (immutable value)
	const auto enemyMaxHp = static_cast<double>(players[1].maxHp);
	const auto playerMaxHp = static_cast<double>(players[0].maxHp);
	//const auto playerMaxMP = static_cast<double>(players[0].maxMp);

	LinearIdPool<Genome, 50000> Pool{};

	// Enhanced A* priority queue and visited set
	EnhancedHeapQueue openSet{};
	std::unordered_set<uint64_t> closedSet;

	Player CopedPlayers[2] = {players[0], players[1]};
	int position = 1;
	uint64_t nowState = 0;

	// Execute one turn
	BattleEmulator::Main(&position, turns, actions, CopedPlayers,
	                     nullptr, seed,
	                     nullptr, nullptr, -2, &nowState);

	// Initialize starting node
	Genome initialGenome = {};
	initialGenome.EnemyPlayer = CopedPlayers[1];
	initialGenome.AllyPlayer = CopedPlayers[0];
	initialGenome.EActions[0] = -1;
	initialGenome.EActions[1] = -1;
	initialGenome.Aactions = -1;
	initialGenome.fitness = 0;
	initialGenome.turn = turns + 1;
	initialGenome.processed = 0;
	initialGenome.Initialized = false;
	initialGenome.processed = turns;
	initialGenome.Visited = 0;
	initialGenome.position = position;
	initialGenome.state = nowState;

	// Set initial action array
	for(int i = 0; i < 350; ++i){
		if(actions[i] == -1 || actions[i] == 0){
			initialGenome.actions[i] = -1;
			break;
		}
		else{
			initialGenome.actions[i] = actions[i];
		}
	}

	// Create initial node with enhanced cost calculation
	EnhancedAStarNode initialNode{};
	initialNode.gCost = 0;
	initialNode.hCost = EnhancedCostCalculator::calculateHCost(initialGenome, enemyMaxHp, playerMaxHp);
	initialNode.fCost = initialNode.gCost + initialNode.hCost;
	initialNode.stateHash = EnhancedHashCalculator::computeStateHash(initialGenome);
	initialNode.allyHP = initialGenome.AllyPlayer.hp;
	initialNode.enemyHP = initialGenome.EnemyPlayer.hp;
	initialNode.nodeId = Pool.alloc(initialGenome);
	openSet.push(initialNode);

	Genome bestSolution = {};
	bestSolution.turn = INT32_MAX;
	bool solutionFound = false;

	int counter = 0;
	double startT = turns + 40;
	double lastBestFCost = 1000000.0;
	auto percent = 0.0;
	auto percenttmp = 0.0;

	Player CopedPlayers3[2];

	for(int i = 0; i < 1; ++i){
		if(!solutionFound){
			maxGenerations *= 2;
		}
		else{
			break;
		}
		while(!openSet.empty() && (maxGenerations == -1 || counter < maxGenerations)){
			// Get node with minimum f-cost
			EnhancedAStarNode currentNode = openSet.top();
			openSet.pop();

			auto preGCost = currentNode.gCost;

			//Progress reporting with constraint info
			// if (counter % 10 == 0) {
			//     percenttmp = counter / static_cast<double>(maxGenerations) * 100.0;
			//     if (percenttmp != percent) {
			//         std::cout << "[Node Info] " << percenttmp << "%"
			//                   << " | turn=" << currentNode.genome.turn
			//                   << " | hCost=" << currentNode.hCost
			//                   << " | gCost=" << currentNode.gCost
			//                   << " | enemyHP=" << currentNode.genome.EnemyPlayer.hp
			//                   << " | bestTurn=" << (solutionFound ? bestSolution.turn - 1: -1)
			//                   << std::endl;
			//         percent = percenttmp;
			//     }
			// }


			// if (counter % 1000000 == 0) {
			//     for (int i = 0; i < 350; ++i) {
			//         if (currentNode.genome.actions[i] == 0 || currentNode.genome.actions[i] == -1) {
			//             break;
			//         }
			//         std::cout << currentNode.genome.actions[i];
			//     }
			//     std::cout << std::endl;
			// }
			//       }

			// Skip already explored states
			if(closedSet.count(currentNode.stateHash)){
				continue;
			}
			closedSet.insert(currentNode.stateHash);

			const Genome currentGenome = Pool.get(currentNode.nodeId);

			if(currentGenome.AllyPlayer.isStunned){
				Genome newGenome = currentGenome;
				newGenome.actions[currentGenome.turn - 1] = BattleEmulator::ATTACK_ALLY;
				newGenome.Initialized = true;

				// Copy for battle emulator execution
				CopedPlayers3[0] = currentGenome.AllyPlayer;
				CopedPlayers3[1] = currentGenome.EnemyPlayer;
				position = currentGenome.position;
				nowState = currentGenome.state;

				// Execute one turn
				BattleEmulator::Main(&position, newGenome.turn - newGenome.processed, newGenome.actions, CopedPlayers3,
									 nullptr, seed,
									 nullptr, nullptr, -2, &nowState);

				if(CopedPlayers3[0].hp > 0){
					// Update genome with results
					newGenome.position = position;
					newGenome.state = nowState;
					newGenome.turn = currentGenome.turn + 1;
					newGenome.processed = currentGenome.turn;
					newGenome.AllyPlayer = CopedPlayers3[0];
					newGenome.EnemyPlayer = CopedPlayers3[1];

					// Calculate enhanced state hash
					uint64_t newStateHash = EnhancedHashCalculator::computeStateHash(newGenome);

					// Skip already explored states
					if(closedSet.count(newStateHash)){
						continue;
					}

					// Create new node with enhanced cost calculation
					EnhancedAStarNode newNode{};
					newNode.gCost = EnhancedCostCalculator::calculateGCost(newGenome, BattleEmulator::ATTACK_ALLY, preGCost);
					newNode.hCost = EnhancedCostCalculator::calculateHCost(newGenome, enemyMaxHp, playerMaxHp);
					newNode.fCost = newNode.gCost + newNode.hCost;
					newNode.stateHash = newStateHash;
					newNode.allyHP = newGenome.AllyPlayer.hp;
					newNode.enemyHP = newGenome.EnemyPlayer.hp;
					newNode.nodeId = Pool.alloc(newGenome);

					// Add to open set
					openSet.push(newNode);
					continue;
				}
			}

			// Turn limit check
			if(currentGenome.turn > startT){
				continue;
			}
			if(solutionFound && currentGenome.turn > bestSolution.turn - 1){
				continue;
			}

			// Victory condition check
			if(currentGenome.EnemyPlayer.hp <= 0){
				if(!solutionFound || currentGenome.turn < bestSolution.turn){
					bestSolution = currentGenome;
					solutionFound = true;
				}
				continue;
			}

			// Defeat condition check
			if(currentGenome.AllyPlayer.hp <= 0){
				continue;
			}

			// Skip if worse than existing solution
			if(solutionFound && currentGenome.turn >= bestSolution.turn){
				continue;
			}

			// Execute each action and generate new nodes
			for(const auto& entry : ACTION_TABLE){
				if(!entry.condition(currentGenome)){
					continue;
				}
				// // Skip low-priority actions if we have many candidates
				// if (actionCandidates.size() > 6 && candidate.priority < 0.5) {
				//     continue;
				// }
				// if (rng() % 100 >= 80) {
				//     continue;
				// }

				Genome newGenome = currentGenome;
				newGenome.actions[currentGenome.turn - 1] = entry.action;
				newGenome.Initialized = true;

				// Copy for battle emulator execution
				CopedPlayers3[0] = currentGenome.AllyPlayer;
				CopedPlayers3[1] = currentGenome.EnemyPlayer;
				position = currentGenome.position;
				nowState = currentGenome.state;

				// Execute one turn
				BattleEmulator::Main(&position, newGenome.turn - newGenome.processed, newGenome.actions, CopedPlayers3,
				                     nullptr, seed,
				                     nullptr, nullptr, -2, &nowState);

				if(CopedPlayers3[0].hp > 0){
					// Update genome with results
					newGenome.position = position;
					newGenome.state = nowState;
					newGenome.turn = currentGenome.turn + 1;
					newGenome.processed = currentGenome.turn;
					newGenome.AllyPlayer = CopedPlayers3[0];
					newGenome.EnemyPlayer = CopedPlayers3[1];

					// Calculate enhanced state hash
					uint64_t newStateHash = EnhancedHashCalculator::computeStateHash(newGenome);

					// Skip already explored states
					if(closedSet.count(newStateHash)){
						continue;
					}

					// Create new node with enhanced cost calculation
					EnhancedAStarNode newNode{};
					newNode.gCost = EnhancedCostCalculator::calculateGCost(newGenome, entry.action, preGCost);
					newNode.hCost = EnhancedCostCalculator::calculateHCost(newGenome, enemyMaxHp, playerMaxHp);
					newNode.fCost = newNode.gCost + newNode.hCost;
					newNode.stateHash = newStateHash;
					newNode.allyHP = newGenome.AllyPlayer.hp;
					newNode.enemyHP = newGenome.EnemyPlayer.hp;
					newNode.nodeId = Pool.alloc(newGenome);

					if(!entry.isEffective(currentGenome, newGenome)){
						continue;
					}

					// Add to open set
					openSet.push(newNode);
				}
			}

			counter++;

			// Track best f-cost for progress monitoring
			if(currentNode.fCost < lastBestFCost){
				lastBestFCost = currentNode.fCost;
			}
		}
	}

	Node_Used = Pool.getSize();

	if(solutionFound){
		return bestSolution;
	}

	// Return best available node if no solution found
	if(!openSet.empty()){
		return Pool.get(openSet.top().nodeId);
	}

	return initialGenome;
}

void ActionOptimizer::updateCompromiseScore(Genome& genome){
	// Enemy action penalty processing (unchanged)
}

std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(const Player players[2], uint64_t seed, int turns,
                                                          int maxGenerations, int actions[350], int numThreads,
                                                          bool dropbug){
	(void)numThreads;
	(void)dropbug;
	auto genome = RunAlgorithm(players, seed, turns, maxGenerations, actions, 0);
	return {0, genome};
}
