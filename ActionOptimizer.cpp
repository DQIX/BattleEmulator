//
// Fixed ActionOptimizer with Enhanced A* Algorithm
// Addresses f-cost stagnation issues identified in analysis
//

#include "ActionOptimizer.h"
#include <vector>
#include <random>
#include <unordered_set>
#include <memory>
#include <queue>
#include <iostream>

#include "BattleEmulator.h"
#include "Genome.h"
#include "ActionBanManager.h"
#include "EnhancedHashCalculator.h"
#include "EnhancedCostCalculator.h"
#include "EnhancedHeapQueue.h"
#include "lcg.h"

// Fixed A* Algorithm Implementation
Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                                     int actions[350], int seedOffset) {
    lcg::init(seed, true);
    // Cache enemy max HP (immutable value)
    const auto enemyMaxHp = static_cast<double>(players[1].maxHp);
    const auto playerMaxHp = static_cast<double>(players[0].maxHp);

    std::unique_ptr<int> position = std::make_unique<int>(1);
    std::unique_ptr<uint64_t> nowState = std::make_unique<uint64_t>(0);

    // Enhanced A* priority queue and visited set
    EnhancedHeapQueue openSet(578000 * 2);
    std::unordered_set<uint64_t> closedSet;

    // Initialize starting node
    Genome initialGenome = {};
    initialGenome.EnemyPlayer = players[1];
    initialGenome.AllyPlayer = players[0];
    initialGenome.EActions[0] = -1;
    initialGenome.EActions[1] = -1;
    initialGenome.Aactions = -1;
    initialGenome.fitness = 0;
    initialGenome.turn = turns;
    initialGenome.processed = 0;
    initialGenome.Initialized = false;
    initialGenome.compromiseScore = 0;
    initialGenome.isEliminated = false;
    initialGenome.processed = turns - 1;
    initialGenome.Visited = 0;
    initialGenome.position = 1;
    initialGenome.state = BattleEmulator::TYPE_2A;
    initialGenome.canMove = false;

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

    while (!openSet.empty() && (maxGenerations == -1 || counter < maxGenerations)) {
        // Get node with minimum f-cost
        EnhancedAStarNode currentNode = openSet.top();
        openSet.pop();

        // Progress reporting
        if (counter % 10000 == 0) {
            // std::cout << counter << "," << currentNode.genome.turn << "," << currentNode.hCost << ", "
            //           << currentNode.gCost << "," << currentNode.genome.EnemyPlayer.hp << std::endl;
            //
            // for (int i = 0; i < 350; ++i) {
            //     if (currentNode.genome.actions[i] == 0 || currentNode.genome.actions[i] == -1) {
            //         break;
            //     }
            //     std::cout << currentNode.genome.actions[i];
            // }
            // std::cout << std::endl;

            std::cout
    << "[Node Info] "
    << "counter=" << counter
    << " | turn=" << currentNode.genome.turn
    << " | hCost=" << currentNode.hCost
    << " | gCost=" << currentNode.gCost
    << " | enemyHP=" << currentNode.genome.EnemyPlayer.hp
    << " | bestTurn=" << bestSolution.turn
    << std::endl;

        }

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

        // Basic actions (always available)
        possibleActions.push_back(BattleEmulator::ATTACK_ALLY);
        possibleActions.push_back(BattleEmulator::DRAGON_SLASH);
        possibleActions.push_back(BattleEmulator::DEFENCE);
        possibleActions.push_back(BattleEmulator::FLEE_ALLY);

        // Conditional actions
        if (players[0].medicinal_herbs_count >= 1) {
            possibleActions.push_back(BattleEmulator::MEDICINAL_HERBS);
        }
        if (currentGenome.AllyPlayer.mp >= 2) {
            possibleActions.push_back(BattleEmulator::HEAL);
        }
        if (currentGenome.AllyPlayer.mp >= 3) {
            possibleActions.push_back(BattleEmulator::CRACK_ALLY);
        }
        if (currentGenome.AllyPlayer.specialCharge == true && 
            currentGenome.AllyPlayer.specialChargeTurn != 0 &&
            currentGenome.AllyPlayer.acrobaticStar == false) {
            possibleActions.push_back(BattleEmulator::ACROBATIC_STAR);
        }

        // Execute each action and generate new nodes
        for (int action : possibleActions) {
            Genome newGenome = currentGenome;
            newGenome.actions[currentGenome.turn] = action;
            newGenome.Initialized = true;

            // Copy for battle emulator execution
            Player CopedPlayers[2] = {currentGenome.AllyPlayer, currentGenome.EnemyPlayer};
            *position = currentGenome.position;
            *nowState = currentGenome.state;

            // Execute one turn
            BattleEmulator::Main(position.get(), newGenome.turn - newGenome.processed, newGenome.actions, CopedPlayers,
                                 (std::optional<BattleResult> &) std::nullopt, 0,
                                 nullptr, nullptr, -2, nowState.get());

            if (CopedPlayers[0].hp > 0) {
                // Update genome with results
                newGenome.position = *position;
                newGenome.state = *nowState;
                newGenome.turn = currentGenome.turn + 1;
                newGenome.processed = currentGenome.turn;
                newGenome.AllyPlayer = CopedPlayers[0];
                newGenome.EnemyPlayer = CopedPlayers[1];

                // Calculate enhanced state hash
                uint64_t newStateHash = EnhancedHashCalculator::computeStateHash(newGenome);

                // Skip already explored states
                if (closedSet.count(newStateHash)) {
                    continue;
                }

                // Create new node with enhanced cost calculation
                EnhancedAStarNode newNode;
                newNode.genome = newGenome;
                newNode.gCost = EnhancedCostCalculator::calculateGCost(newGenome, action);
                newNode.hCost = EnhancedCostCalculator::calculateHCost(newGenome, enemyMaxHp, playerMaxHp);
                newNode.fCost = newNode.gCost + newNode.hCost;
                newNode.stateHash = newStateHash;

                // Add to open set
                openSet.push(newNode);
            }
        }

        counter++;
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