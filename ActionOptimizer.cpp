//
// Flexible ActionOptimizer with Adaptive Constraint Management
// Solves deadlock issues with longer predefined action sequences
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
#include "FlexibleConstraintManager.h"
#include "AdaptiveActionGenerator.h"
#include "lcg.h"

// Flexible A* Algorithm Implementation
Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                                     int actions[350], int seedOffset) {
    lcg::init(seed, true);
    // Initialize flexible constraint manager
    FlexibleConstraintManager::initialize(actions);

    // Cache enemy max HP (immutable value)
    const auto enemyMaxHp = static_cast<double>(players[1].maxHp);
    const auto playerMaxHp = static_cast<double>(players[0].maxHp);

    std::unique_ptr<int> position = std::make_unique<int>(1);
    std::unique_ptr<uint64_t> nowState = std::make_unique<uint64_t>(0);

    // Enhanced A* priority queue and visited set
    EnhancedHeapQueue openSet(578000 * 2);
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
    initialGenome.compromiseScore = 0;
    initialGenome.isEliminated = false;
    initialGenome.processed = turns;
    initialGenome.Visited = 0;
    initialGenome.position = *position;
    initialGenome.state = *nowState;
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
    double lastBestFCost = 1000000.0;

    while (!openSet.empty() && (maxGenerations == -1 || counter < maxGenerations)) {
        // Get node with minimum f-cost
        EnhancedAStarNode currentNode = openSet.top();
        openSet.pop();

        // Update constraint state based on progress
        FlexibleConstraintManager::updateConstraintState(currentNode.genome, currentNode.fCost, counter);

        // Progress reporting with constraint info
        if (counter % 10000 == 0) {
            std::cout << "[Node Info] counter=" << counter
                      << " | turn=" << currentNode.genome.turn
                      << " | hCost=" << currentNode.hCost
                      << " | gCost=" << currentNode.gCost
                      << " | enemyHP=" << currentNode.genome.EnemyPlayer.hp
                      << " | bestTurn=" << (solutionFound ? bestSolution.turn - 4 : -1)
                      << std::endl;

            if (counter % 1000000 == 0) {
                for (int i = 0; i < 350; ++i) {
                    if (currentNode.genome.actions[i] == 0 || currentNode.genome.actions[i] == -1) {
                        break;
                    }
                    std::cout << currentNode.genome.actions[i];
                }
                std::cout << std::endl;
            }
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

        // Generate actions using adaptive action generator
        std::vector<ActionCandidate> actionCandidates =
            AdaptiveActionGenerator::generateActions(currentGenome, players, counter);

        // Execute each action candidate
        for (const ActionCandidate& candidate : actionCandidates) {
            // Skip low-priority actions if we have many candidates
            if (actionCandidates.size() > 6 && candidate.priority < 0.5) {
                continue;
            }

            Genome newGenome = currentGenome;
            newGenome.actions[currentGenome.turn - 1] = candidate.action;
            newGenome.Initialized = true;

            // Copy for battle emulator execution
            Player CopedPlayers[2] = {currentGenome.AllyPlayer, currentGenome.EnemyPlayer};
            *position = currentGenome.position;
            *nowState = currentGenome.state;

            if (newGenome.turn - newGenome.processed != 1) {
                std::cout << newGenome.turn - newGenome.processed << std::endl;
            }

            // Execute one turn
            BattleEmulator::Main(position.get(), newGenome.turn - newGenome.processed, newGenome.actions, CopedPlayers,
                                 (std::optional<BattleResult> &) std::nullopt, seed,
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
                newNode.gCost = EnhancedCostCalculator::calculateGCost(newGenome, candidate.action);

                // Add constraint cost to g-cost
                newNode.gCost += candidate.constraintCost;

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