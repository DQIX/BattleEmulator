//
// Flexible ActionOptimizer with Adaptive Constraint Management
// Solves deadlock issues with longer predefined action sequences
//

#include "ActionOptimizer.h"
#include <vector>
#include <random>
#include <unordered_set>
#include <memory>

#include "BattleEmulator.h"
#include "Genome.h"
#include "EnhancedHashCalculator.h"
#include "EnhancedCostCalculator.h"
#include "EnhancedHeapQueue.h"
#include "lcg.h"

// Flexible A* Algorithm Implementation
Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                                     int actions[350], int seedOffset) {
    lcg::init(seed);
    //std::mt19937 rng(seed + seedOffset);

    // Cache enemy max HP (immutable value)
    const auto enemyMaxHp = static_cast<double>(players[1].maxHp);
    const auto playerMaxHp = static_cast<double>(players[0].maxHp);
    //const auto playerMaxMP = static_cast<double>(players[0].maxMp);

    std::unique_ptr<int> position = std::make_unique<int>(1);
    std::unique_ptr<uint64_t> nowState = std::make_unique<uint64_t>(0);

    // Enhanced A* priority queue and visited set
    EnhancedHeapQueue openSet(578000 * 5);
    std::unordered_set<uint64_t> closedSet;


    Player CopedPlayers[2] = {players[0], players[1]};
    *position = 1;
    *nowState = 0;

    // Execute one turn
    BattleEmulator::Main(position.get(), turns, actions, CopedPlayers,
                         (std::optional<BattleResult> &) std::nullopt, seed,
                         nullptr, nullptr, -2, nowState.get());

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
    initialGenome.position = *position;
    initialGenome.state = *nowState;

    // Set initial action array
    for (int i = 0; i < 350; ++i) {
        if (actions[i] == -1 || actions[i] == 0) {
            initialGenome.actions[i] = -1;
            break;
        } else {
            initialGenome.actions[i] = actions[i];
        }
    }

    // Create initial node with enhanced cost calculation
    EnhancedAStarNode initialNode;
    initialNode.genome = initialGenome;
    initialNode.gCost = 0;
    initialNode.hCost = EnhancedCostCalculator::calculateHCost(initialGenome, enemyMaxHp, playerMaxHp);
    initialNode.fCost = initialNode.gCost + initialNode.hCost;
    initialNode.stateHash = EnhancedHashCalculator::computeStateHash(initialGenome);

    openSet.push(initialNode);

    Genome bestSolution = {};
    bestSolution.turn = INT32_MAX;
    bool solutionFound = false;

    int counter = 0;
    double startT = turns + 40;
    double lastBestFCost = 1000000.0;
    auto percent = 0.0;
    auto percenttmp = 0.0;

    for (int i = 0; i < 10; ++i) {
        if (!solutionFound) {
            maxGenerations *= 2;
        }else {
            break;
        }
        while (!openSet.empty() && (maxGenerations == -1 || counter < maxGenerations)) {
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
            if (closedSet.count(currentNode.stateHash)) {
                continue;
            }
            closedSet.insert(currentNode.stateHash);

            Genome currentGenome = currentNode.genome;

            // Turn limit check
            if (currentGenome.turn > startT) {
                continue;
            }
            if (solutionFound && currentGenome.turn > bestSolution.turn) {
                continue;
            }

            // Victory condition check
            if (currentGenome.EnemyPlayer.hp <= 0) {
                if (!solutionFound || currentGenome.turn < bestSolution.turn) {
                    bestSolution = currentGenome;
                    solutionFound = true;
                }
                continue;
            }

            // Defeat condition check
            if (currentGenome.AllyPlayer.hp <= 0) {
                continue;
            }

            // Skip if worse than existing solution
            if (solutionFound && currentGenome.turn >= bestSolution.turn) {
                continue;
            }

            // Generate possible actions
            std::vector<int> possibleActions;
            //possibleActions.reserve(12); // 必要な上限に合わせて

            // Basic actions (always available)
            possibleActions.push_back(BattleEmulator::ATTACK_ALLY);
            possibleActions.push_back(BattleEmulator::PSYCHE_UP_ALLY);
            possibleActions.push_back(BattleEmulator::DEFENCE);
            possibleActions.push_back(BattleEmulator::FLEE_ALLY);
            possibleActions.push_back(BattleEmulator::DOUBLE_UP);
            if (currentGenome.AllyPlayer.mp >= 25) {
                possibleActions.push_back(BattleEmulator::FULLHEAL);
            }
            if (currentGenome.AllyPlayer.mp >= 20) {
                possibleActions.push_back(BattleEmulator::MORE_HEAL);
                possibleActions.push_back(BattleEmulator::MULTITHRUST);
            }
            possibleActions.push_back(BattleEmulator::SAGE_ELIXIR);

            // Conditional actions
            if (currentGenome.AllyPlayer.SpecialMedicineCount >= 1) {
                possibleActions.push_back(BattleEmulator::SPECIAL_MEDICINE);
            }

            if (currentGenome.AllyPlayer.mp >= 2) {
                possibleActions.push_back(BattleEmulator::HEAL);
            }
            if (currentGenome.AllyPlayer.mp >= 3) {
                //possibleActions.push_back(BattleEmulator::CRACK_ALLY);
                //possibleActions.push_back(BattleEmulator::WOOSH_ALLY);
            }

            if (currentGenome.AllyPlayer.mp >= 8) {
                //possibleActions.push_back(BattleEmulator::CRACKLE);
            }
            // Execute each action and generate new nodes
            for (int action: possibleActions) {
                // // Skip low-priority actions if we have many candidates
                // if (actionCandidates.size() > 6 && candidate.priority < 0.5) {
                //     continue;
                // }
                // if (rng() % 100 >= 80) {
                //     continue;
                // }

                Genome newGenome = currentGenome;
                newGenome.actions[currentGenome.turn - 1] = action;
                newGenome.Initialized = true;

                // Copy for battle emulator execution
                Player CopedPlayers1[2] = {currentGenome.AllyPlayer, currentGenome.EnemyPlayer};
                *position = currentGenome.position;
                *nowState = currentGenome.state;

                // Execute one turn
                BattleEmulator::Main(position.get(), newGenome.turn - newGenome.processed, newGenome.actions, CopedPlayers1,
                                     (std::optional<BattleResult> &) std::nullopt, seed,
                                     nullptr, nullptr, -2, nowState.get());

                if (CopedPlayers1[0].hp > 0) {
                    // Update genome with results
                    newGenome.position = *position;
                    newGenome.state = *nowState;
                    newGenome.turn = currentGenome.turn + 1;
                    newGenome.processed = currentGenome.turn;
                    newGenome.AllyPlayer = CopedPlayers1[0];
                    newGenome.EnemyPlayer = CopedPlayers1[1];

                    // Calculate enhanced state hash
                    uint64_t newStateHash = EnhancedHashCalculator::computeStateHash(newGenome);

                    // Skip already explored states
                    if (closedSet.count(newStateHash)) {
                        continue;
                    }

                    // Create new node with enhanced cost calculation
                    EnhancedAStarNode newNode;
                    newNode.genome = newGenome;
                    newNode.gCost = EnhancedCostCalculator::calculateGCost(newGenome, action, preGCost);

                    newNode.hCost = EnhancedCostCalculator::calculateHCost(newGenome, enemyMaxHp, playerMaxHp);
                    newNode.fCost = newNode.gCost + newNode.hCost;
                    newNode.stateHash = newStateHash;

                    // Add to open set
                    openSet.push(newNode);
                }
            }

            counter++;

            // Track best f-cost for progress monitoring
            if (currentNode.fCost < lastBestFCost) {
                lastBestFCost = currentNode.fCost;
            }
        }
    }

    if (solutionFound) {
        return bestSolution;
    }

    // Return best available node if no solution found
    if (!openSet.empty()) {
        return openSet.top().genome;
    }

    return initialGenome;
}

void ActionOptimizer::updateCompromiseScore(Genome &genome) {
    // Enemy action penalty processing (unchanged)
}