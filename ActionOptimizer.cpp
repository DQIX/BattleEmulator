//
// Flexible ActionOptimizer with Adaptive Constraint Management
// Solves deadlock issues with longer predefined action sequences
//

#include "ActionOptimizer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "BattleEmulator.h"
#include "LinearIdPool.h"
#include "Genome.h"
#include "EnhancedCostCalculator.h"
#include "EnhancedHeapQueue.h"
#include "EnhancedHashCalculator.h"
#include "lcg.h"

struct ActionEntry {
    int action;
    bool (*condition)(const Genome &);
};

constexpr ActionEntry ACTION_TABLE[] = {
    {BattleEmulator::ATTACK_ALLY, [](const Genome &) { return true; }},
    {BattleEmulator::DRAGON_SLASH, [](const Genome &) { return true; }},
    {BattleEmulator::DEFENCE, [](const Genome &) { return true; }},
    {BattleEmulator::FLEE_ALLY, [](const Genome &) { return true; }},

    {BattleEmulator::SPECIAL_ANTIDOTE,
     [](const Genome &g) {
         return g.AllyPlayer.SpecialMedicineCount >= 1 &&
                g.AllyPlayer.PoisonEnable;
     }},
    {BattleEmulator::SPECIAL_MEDICINE,
     [](const Genome &g) {
         return g.AllyPlayer.SpecialMedicineCount >= 1 &&
                !g.AllyPlayer.PoisonEnable;
     }},
    {BattleEmulator::HEAL,
     [](const Genome &g) { return g.AllyPlayer.mp >= 2; }},
    {BattleEmulator::CRACK_ALLY,
     [](const Genome &g) { return g.AllyPlayer.mp >= 3; }},
    {BattleEmulator::WOOSH_ALLY,
     [](const Genome &g) { return g.AllyPlayer.mp >= 3; }},
    {BattleEmulator::ACROBATIC_STAR,
     [](const Genome &g) {
         return g.AllyPlayer.specialCharge &&
                g.AllyPlayer.specialChargeTurn != 0;
     }}
};

static uint32_t Node_Used;

namespace {
    using Clock = std::chrono::steady_clock;

    constexpr int NO_SOLUTION_TURN = 100;
    constexpr auto TIME_BUDGET = std::chrono::milliseconds(900);
    constexpr int ROLLOUT_CHOICES = 6;
    constexpr int DFS_BRANCH_LIMIT = 10;
    constexpr int CRITICAL_SCAN_WIDTH = 24;
    constexpr int CRITICAL_BEAM_WIDTH = 768;
    constexpr auto SHORTENING_BUDGET = std::chrono::milliseconds(35);

#if defined(BattleEmulatorLV19)
    constexpr int ALLY_CRITICAL_THRESHOLD = 500;
#elif defined(BattleEmulatorLV13)
    constexpr int ALLY_CRITICAL_THRESHOLD = 200;
#else
    constexpr int ALLY_CRITICAL_THRESHOLD = 500;
#endif

    struct SearchCandidate {
        Genome genome{};
        int action = -1;
        int heuristic = std::numeric_limits<int>::min();
    };

    struct SearchContext {
        uint64_t seed = 0;
        Clock::time_point deadline{};
        int nodeBudget = std::numeric_limits<int>::max();
        int nodesVisited = 0;
        bool timedOut = false;
        double enemyMaxHp = 1.0;
        double playerMaxHp = 1.0;
        Genome bestGenome{};
        bool bestIsSolution = false;
        std::unordered_map<uint64_t, int> visitedDepth;
    };

    uint64_t mixRandom(uint64_t &state) {
        state ^= state << 7;
        state ^= state >> 9;
        state ^= state << 8;
        return state;
    }

    int actionPriority(int action) {
        switch (action) {
            case BattleEmulator::DRAGON_SLASH:
                return 58;
            case BattleEmulator::ATTACK_ALLY:
                return 56;
            case BattleEmulator::ACROBATIC_STAR:
                return 42;
            case BattleEmulator::CRACK_ALLY:
                return 36;
            case BattleEmulator::WOOSH_ALLY:
                return 34;
            case BattleEmulator::HEAL:
                return 18;
            case BattleEmulator::SPECIAL_MEDICINE:
                return 10;
            case BattleEmulator::SPECIAL_ANTIDOTE:
                return 9;
            case BattleEmulator::DEFENCE:
                return 4;
            case BattleEmulator::FLEE_ALLY:
                return -40;
            default:
                return 0;
        }
    }

    int criticalThreshold(int action) {
        switch (action) {
            case BattleEmulator::ATTACK_ALLY:
                return ALLY_CRITICAL_THRESHOLD;
            case BattleEmulator::DRAGON_SLASH:
                return ALLY_CRITICAL_THRESHOLD / 2;
            case BattleEmulator::CRACK_ALLY:
            case BattleEmulator::HEAL:
                return 100;
            case BattleEmulator::WOOSH_ALLY:
                return 100;
            default:
                return 0;
        }
    }

    int percentAt(int position, int max) {
        int probePosition = position;
        return lcg::getPercent(&probePosition, max);
    }

    int criticalWindowScore(int position, int action, int scanWidth) {
        const int threshold = criticalThreshold(action);
        if (threshold <= 0 || position <= 0) {
            return 0;
        }

        int bestScore = 0;
        for (int offset = 0; offset <= scanWidth; ++offset) {
            const int value = percentAt(position + offset, 0x2710);
            if (value >= threshold) {
                continue;
            }

            const int closeness = threshold - value;
            const int distanceBonus = scanWidth - offset;
            bestScore = std::max(bestScore, closeness * 9 + distanceBonus * 18);
        }
        return bestScore;
    }

    int futureCriticalScore(const Genome &genome) {
        int score = 0;
        score = std::max(score, criticalWindowScore(genome.position, BattleEmulator::ATTACK_ALLY, CRITICAL_SCAN_WIDTH));
        score = std::max(score, criticalWindowScore(genome.position, BattleEmulator::DRAGON_SLASH, CRITICAL_SCAN_WIDTH));
        if (genome.AllyPlayer.mp >= 3) {
            score = std::max(score, criticalWindowScore(genome.position, BattleEmulator::CRACK_ALLY, CRITICAL_SCAN_WIDTH / 2));
            score = std::max(score, criticalWindowScore(genome.position, BattleEmulator::WOOSH_ALLY, CRITICAL_SCAN_WIDTH / 2));
        }
        return score;
    }

    void initializeGenomeActions(Genome &genome) {
        std::fill(std::begin(genome.actions), std::end(genome.actions), -1);
    }

    bool isSolution(const Genome &genome) {
        return genome.EnemyPlayer.hp <= 0 && genome.AllyPlayer.hp > 0;
    }

    bool isBetterProgress(const Genome &candidate, const Genome &best) {
        const auto candidateSolved = isSolution(candidate);
        const auto bestSolved = isSolution(best);

        if (candidateSolved != bestSolved) {
            return candidateSolved;
        }
        if (candidateSolved && bestSolved) {
            if (candidate.turn != best.turn) {
                return candidate.turn < best.turn;
            }
            if (candidate.AllyPlayer.hp != best.AllyPlayer.hp) {
                return candidate.AllyPlayer.hp > best.AllyPlayer.hp;
            }
            return candidate.AllyPlayer.mp > best.AllyPlayer.mp;
        }
        if (candidate.EnemyPlayer.hp != best.EnemyPlayer.hp) {
            return candidate.EnemyPlayer.hp < best.EnemyPlayer.hp;
        }
        if (candidate.AllyPlayer.hp != best.AllyPlayer.hp) {
            return candidate.AllyPlayer.hp > best.AllyPlayer.hp;
        }
        if (candidate.AllyPlayer.mp != best.AllyPlayer.mp) {
            return candidate.AllyPlayer.mp > best.AllyPlayer.mp;
        }
        return candidate.turn < best.turn;
    }

    void updateBestGenome(SearchContext &context, const Genome &candidate) {
        if (!context.bestIsSolution && !isSolution(context.bestGenome) &&
            context.bestGenome.turn == 0) {
            context.bestGenome = candidate;
            context.bestIsSolution = isSolution(candidate);
            return;
        }
        if (isBetterProgress(candidate, context.bestGenome)) {
            context.bestGenome = candidate;
            context.bestIsSolution = isSolution(candidate);
        }
    }

    bool advanceGenome(const Genome &currentGenome, int action, uint64_t seed, Genome &nextGenome) {
        nextGenome = currentGenome;
        nextGenome.actions[currentGenome.turn - 1] = action;
        nextGenome.Initialized = true;

        Player copiedPlayers[2] = {currentGenome.AllyPlayer, currentGenome.EnemyPlayer};
        int position = currentGenome.position;
        uint64_t nowState = currentGenome.state;

        BattleEmulator::Main(&position, currentGenome.turn - currentGenome.processed, nextGenome.actions, copiedPlayers,
                             nullptr, seed, nullptr, nullptr, -2, &nowState);

        nextGenome.position = position;
        nextGenome.state = nowState;
        nextGenome.turn = currentGenome.turn + 1;
        nextGenome.processed = currentGenome.turn;
        nextGenome.AllyPlayer = copiedPlayers[0];
        nextGenome.EnemyPlayer = copiedPlayers[1];
        return nextGenome.AllyPlayer.hp > 0;
    }

    int evaluateCandidate(const Genome &currentGenome, const Genome &nextGenome, int action,
                          const SearchContext &context) {
        const int damageDealt = currentGenome.EnemyPlayer.hp - nextGenome.EnemyPlayer.hp;
        const int selfDamage = currentGenome.AllyPlayer.hp - nextGenome.AllyPlayer.hp;
        const double gCost = EnhancedCostCalculator::calculateGCost(nextGenome, action, 0.0);
        const double hCost = EnhancedCostCalculator::calculateHCost(nextGenome, context.enemyMaxHp, context.playerMaxHp);

        int score = -static_cast<int>((gCost + hCost) * 10000.0);
        score += damageDealt * 64;
        score -= selfDamage * 32;
        score += nextGenome.AllyPlayer.hp * 4;
        score += nextGenome.AllyPlayer.mp * 2;
        score += actionPriority(action);
        score += criticalWindowScore(currentGenome.position, action, CRITICAL_SCAN_WIDTH / 2);
        score += futureCriticalScore(nextGenome);

        if (nextGenome.EnemyPlayer.hp <= 0) {
            score += 1'000'000 - nextGenome.turn * 1024;
        } else if (damageDealt >= currentGenome.EnemyPlayer.hp / 2) {
            score += 80'000 + damageDealt * 96;
        } else if (damageDealt >= 40) {
            score += 25'000 + damageDealt * 48;
        }
        if (nextGenome.AllyPlayer.specialCharge) {
            score += 220;
        }
        if (!nextGenome.AllyPlayer.PoisonEnable) {
            score += 48;
        }
        if (nextGenome.AllyPlayer.sleeping || nextGenome.AllyPlayer.paralysis) {
            score -= 400;
        }
        if (action == BattleEmulator::HEAL &&
            currentGenome.AllyPlayer.hp > currentGenome.AllyPlayer.maxHp * 7 / 10) {
            score -= 120;
        }
        if (action == BattleEmulator::DEFENCE &&
            currentGenome.AllyPlayer.hp > currentGenome.AllyPlayer.maxHp / 2) {
            score -= 60;
        }
        return score;
    }

    int collectCandidates(const Genome &currentGenome, const SearchContext &context,
                          std::array<SearchCandidate, std::size(ACTION_TABLE)> &candidates) {
        int count = 0;
        for (const auto &entry: ACTION_TABLE) {
            if (!entry.condition(currentGenome)) {
                continue;
            }

            Genome nextGenome{};
            initializeGenomeActions(nextGenome);
            if (!advanceGenome(currentGenome, entry.action, context.seed, nextGenome)) {
                continue;
            }

            candidates[count].genome = nextGenome;
            candidates[count].action = entry.action;
            candidates[count].heuristic = evaluateCandidate(currentGenome, nextGenome, entry.action, context);
            ++count;
        }

        std::sort(candidates.begin(), candidates.begin() + count,
                  [](const SearchCandidate &lhs, const SearchCandidate &rhs) {
                      if (lhs.heuristic != rhs.heuristic) {
                          return lhs.heuristic > rhs.heuristic;
                      }
                      return lhs.action < rhs.action;
                  });
        return count;
    }

    Genome runRollout(const Genome &initialGenome, SearchContext &context, uint64_t randomState, bool deterministic) {
        Genome currentGenome = initialGenome;
        const int turnCap = context.bestIsSolution ? context.bestGenome.turn : NO_SOLUTION_TURN;

        while (Clock::now() < context.deadline &&
               context.nodesVisited < context.nodeBudget &&
               currentGenome.AllyPlayer.hp > 0 &&
               currentGenome.EnemyPlayer.hp > 0 &&
               currentGenome.turn < turnCap) {
            std::array<SearchCandidate, std::size(ACTION_TABLE)> candidates{};
            const int candidateCount = collectCandidates(currentGenome, context, candidates);
            context.nodesVisited += candidateCount;

            if (candidateCount == 0) {
                break;
            }

            int index = 0;
            if (!deterministic) {
                const int selectableCount = std::min(candidateCount, ROLLOUT_CHOICES);
                index = static_cast<int>(mixRandom(randomState) % static_cast<uint64_t>(selectableCount));
            }
            currentGenome = candidates[index].genome;
            updateBestGenome(context, currentGenome);

            if (isSolution(currentGenome)) {
                break;
            }
        }

        return currentGenome;
    }

    bool depthFirstSearch(SearchContext &context, const Genome &currentGenome, int remainingTurns) {
        if (Clock::now() >= context.deadline || context.nodesVisited >= context.nodeBudget) {
            context.timedOut = true;
            return false;
        }

        ++context.nodesVisited;
        updateBestGenome(context, currentGenome);

        if (isSolution(currentGenome)) {
            return true;
        }
        if (currentGenome.AllyPlayer.hp <= 0 || remainingTurns <= 0) {
            return false;
        }
        if (context.bestIsSolution && currentGenome.turn >= context.bestGenome.turn) {
            return false;
        }

        const auto stateHash = EnhancedHashCalculator::computeStateHash(currentGenome);
        const auto it = context.visitedDepth.find(stateHash);
        if (it != context.visitedDepth.end() && it->second >= remainingTurns) {
            return false;
        }
        context.visitedDepth[stateHash] = remainingTurns;

        std::array<SearchCandidate, std::size(ACTION_TABLE)> candidates{};
        const int candidateCount = collectCandidates(currentGenome, context, candidates);
        context.nodesVisited += candidateCount;

        const int branchCount = std::min(candidateCount, DFS_BRANCH_LIMIT);
        for (int i = 0; i < branchCount; ++i) {
            const auto &candidate = candidates[i].genome;
            updateBestGenome(context, candidate);

            if (isSolution(candidate)) {
                return true;
            }
            if (remainingTurns <= 1) {
                continue;
            }
            if (context.bestIsSolution && candidate.turn >= context.bestGenome.turn) {
                continue;
            }
            if (depthFirstSearch(context, candidate, remainingTurns - 1)) {
                return true;
            }
            if (context.timedOut) {
                return false;
            }
        }

        return false;
    }

    bool runCriticalBeam(SearchContext &context, const Genome &initialGenome, int maxExtraTurns) {
        std::vector<Genome> beam;
        std::vector<SearchCandidate> nextLayer;
        std::unordered_set<uint64_t> layerHashes;
        beam.reserve(CRITICAL_BEAM_WIDTH);
        nextLayer.reserve(CRITICAL_BEAM_WIDTH * std::size(ACTION_TABLE));
        layerHashes.reserve(CRITICAL_BEAM_WIDTH * 2);
        beam.push_back(initialGenome);

        for (int depth = 1;
             depth <= maxExtraTurns &&
             Clock::now() < context.deadline &&
             context.nodesVisited < context.nodeBudget; ++depth) {
            nextLayer.clear();
            layerHashes.clear();

            for (const auto &genome: beam) {
                std::array<SearchCandidate, std::size(ACTION_TABLE)> candidates{};
                const int candidateCount = collectCandidates(genome, context, candidates);
                context.nodesVisited += candidateCount;

                for (int i = 0; i < candidateCount; ++i) {
                    const auto &candidate = candidates[i];
                    updateBestGenome(context, candidate.genome);
                    if (isSolution(candidate.genome)) {
                        return true;
                    }

                    const uint64_t stateHash = EnhancedHashCalculator::computeStateHash(candidate.genome);
                    if (!layerHashes.insert(stateHash).second) {
                        continue;
                    }
                    nextLayer.push_back(candidate);
                }

                if (Clock::now() >= context.deadline || context.nodesVisited >= context.nodeBudget) {
                    break;
                }
            }

            if (nextLayer.empty()) {
                return false;
            }

            std::sort(nextLayer.begin(), nextLayer.end(),
                      [](const SearchCandidate &lhs, const SearchCandidate &rhs) {
                          if (lhs.heuristic != rhs.heuristic) {
                              return lhs.heuristic > rhs.heuristic;
                          }
                          return lhs.genome.EnemyPlayer.hp < rhs.genome.EnemyPlayer.hp;
                      });

            const int keepCount = std::min(static_cast<int>(nextLayer.size()), CRITICAL_BEAM_WIDTH);
            beam.clear();
            for (int i = 0; i < keepCount; ++i) {
                beam.push_back(nextLayer[i].genome);
            }
        }

        return false;
    }

    bool runIterativeShortSearch(SearchContext &context, const Genome &initialGenome,
                                 int firstTargetTurn, int lastTargetTurn) {
        for (int targetTurn = firstTargetTurn;
             targetTurn <= lastTargetTurn &&
             Clock::now() < context.deadline &&
             context.nodesVisited < context.nodeBudget; ++targetTurn) {
            context.visitedDepth.clear();
            context.timedOut = false;
            if (depthFirstSearch(context, initialGenome, targetTurn - initialGenome.processed)) {
                if (context.bestIsSolution && context.bestGenome.turn - 1 <= targetTurn) {
                    return true;
                }
            }
            if (context.timedOut) {
                return false;
            }
        }
        return false;
    }

    Genome runLegacyBestFirst(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                              int actions[350], uint32_t &legacyNodesUsed) {
        const auto enemyMaxHp = static_cast<double>(players[1].maxHp);
        const auto playerMaxHp = static_cast<double>(players[0].maxHp);

        std::unique_ptr<int> position = std::make_unique<int>(1);
        std::unique_ptr<uint64_t> nowState = std::make_unique<uint64_t>(0);

        LinearIdPool<Genome, 180000> pool{};
        EnhancedHeapQueue openSet{};
        std::unordered_set<uint64_t> closedSet;

        Player copiedPlayers[2] = {players[0], players[1]};
        *position = 1;
        *nowState = 0;

        BattleEmulator::Main(position.get(), turns, actions, copiedPlayers,
                             nullptr, seed, nullptr, nullptr, -2, nowState.get());

        Genome initialGenome{};
        initializeGenomeActions(initialGenome);
        initialGenome.EnemyPlayer = copiedPlayers[1];
        initialGenome.AllyPlayer = copiedPlayers[0];
        initialGenome.EActions[0] = -1;
        initialGenome.EActions[1] = -1;
        initialGenome.Aactions = -1;
        initialGenome.fitness = 0;
        initialGenome.turn = turns + 1;
        initialGenome.processed = turns;
        initialGenome.Initialized = false;
        initialGenome.Visited = 0;
        initialGenome.position = *position;
        initialGenome.state = *nowState;

        for (int i = 0; i < 350; ++i) {
            if (actions[i] == -1 || actions[i] == 0) {
                initialGenome.actions[i] = -1;
                break;
            }
            initialGenome.actions[i] = actions[i];
        }

        EnhancedAStarNode initialNode{};
        initialNode.gCost = 0;
        initialNode.hCost = EnhancedCostCalculator::calculateHCost(initialGenome, enemyMaxHp, playerMaxHp);
        initialNode.fCost = initialNode.gCost + initialNode.hCost;
        initialNode.stateHash = EnhancedHashCalculator::computeStateHash(initialGenome);
        initialNode.allyHP = initialGenome.AllyPlayer.hp;
        initialNode.enemyHP = initialGenome.EnemyPlayer.hp;
        initialNode.nodeId = pool.alloc(initialGenome);
        openSet.push(initialNode);

        Genome bestSolution{};
        bestSolution.turn = INT32_MAX;
        bool solutionFound = false;
        int counter = 0;
        const double startT = turns + 40;

        Player copiedPlayers3[2];

        for (int i = 0; i < 10; ++i) {
            if (!solutionFound) {
                maxGenerations *= 2;
            } else {
                break;
            }
            while (!openSet.empty() && (maxGenerations == -1 || counter < maxGenerations)) {
                const EnhancedAStarNode currentNode = openSet.top();
                openSet.pop();

                const auto preGCost = currentNode.gCost;
                if (closedSet.count(currentNode.stateHash)) {
                    continue;
                }
                closedSet.insert(currentNode.stateHash);

                const Genome currentGenome = pool.get(currentNode.nodeId);
                if (currentGenome.turn > startT) {
                    continue;
                }
                if (solutionFound && currentGenome.turn > bestSolution.turn - 1) {
                    continue;
                }
                if (currentGenome.EnemyPlayer.hp <= 0) {
                    if (!solutionFound || currentGenome.turn < bestSolution.turn) {
                        bestSolution = currentGenome;
                        solutionFound = true;
                    }
                    continue;
                }
                if (currentGenome.AllyPlayer.hp <= 0) {
                    continue;
                }
                if (solutionFound && currentGenome.turn >= bestSolution.turn) {
                    continue;
                }

                for (const auto &entry: ACTION_TABLE) {
                    if (!entry.condition(currentGenome)) {
                        continue;
                    }

                    Genome newGenome = currentGenome;
                    newGenome.actions[currentGenome.turn - 1] = entry.action;
                    newGenome.Initialized = true;

                    copiedPlayers3[0] = currentGenome.AllyPlayer;
                    copiedPlayers3[1] = currentGenome.EnemyPlayer;
                    *position = currentGenome.position;
                    *nowState = currentGenome.state;

                    BattleEmulator::Main(position.get(), newGenome.turn - newGenome.processed, newGenome.actions,
                                         copiedPlayers3, nullptr, seed, nullptr, nullptr, -2, nowState.get());

                    if (copiedPlayers3[0].hp <= 0) {
                        continue;
                    }

                    newGenome.position = *position;
                    newGenome.state = *nowState;
                    newGenome.turn = currentGenome.turn + 1;
                    newGenome.processed = currentGenome.turn;
                    newGenome.AllyPlayer = copiedPlayers3[0];
                    newGenome.EnemyPlayer = copiedPlayers3[1];

                    const uint64_t newStateHash = EnhancedHashCalculator::computeStateHash(newGenome);
                    if (closedSet.count(newStateHash)) {
                        continue;
                    }

                    EnhancedAStarNode newNode{};
                    newNode.gCost = EnhancedCostCalculator::calculateGCost(newGenome, entry.action, preGCost);
                    newNode.hCost = EnhancedCostCalculator::calculateHCost(newGenome, enemyMaxHp, playerMaxHp);
                    newNode.fCost = newNode.gCost + newNode.hCost;
                    newNode.stateHash = newStateHash;
                    newNode.allyHP = newGenome.AllyPlayer.hp;
                    newNode.enemyHP = newGenome.EnemyPlayer.hp;
                    newNode.nodeId = pool.alloc(newGenome);
                    openSet.push(newNode);
                }

                counter++;
            }
        }

        legacyNodesUsed = pool.getSize();

        if (solutionFound) {
            return bestSolution;
        }
        if (!openSet.empty()) {
            return pool.get(openSet.top().nodeId);
        }

        initialGenome.turn = NO_SOLUTION_TURN;
        return initialGenome;
    }
}

uint32_t ActionOptimizer::getNodesUsed() {
    return Node_Used;
}

Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                                     int actions[350], int seedOffset) {
    (void) seedOffset;
    lcg::init(seed, true);
    Node_Used = 0;

    Player copiedPlayers[2] = {players[0], players[1]};
    int position = 1;
    uint64_t nowState = 0;

    BattleEmulator::Main(&position, turns, actions, copiedPlayers, nullptr, seed, nullptr, nullptr, -2, &nowState);

    Genome initialGenome{};
    initializeGenomeActions(initialGenome);
    initialGenome.EnemyPlayer = copiedPlayers[1];
    initialGenome.AllyPlayer = copiedPlayers[0];
    initialGenome.EActions[0] = -1;
    initialGenome.EActions[1] = -1;
    initialGenome.Aactions = -1;
    initialGenome.fitness = 0;
    initialGenome.turn = turns + 1;
    initialGenome.processed = turns;
    initialGenome.Initialized = false;
    initialGenome.Visited = 0;
    initialGenome.position = position;
    initialGenome.state = nowState;

    for (int i = 0; i < 350; ++i) {
        if (actions[i] == -1 || actions[i] == 0) {
            initialGenome.actions[i] = -1;
            break;
        }
        initialGenome.actions[i] = actions[i];
    }

    if (initialGenome.EnemyPlayer.hp <= 0) {
        return initialGenome;
    }

    SearchContext context{};
    context.seed = seed;
    context.deadline = Clock::now() + TIME_BUDGET;
    context.nodeBudget = maxGenerations <= 0 ? std::numeric_limits<int>::max() : std::max(32768, maxGenerations * 120);
    context.enemyMaxHp = static_cast<double>(players[1].maxHp);
    context.playerMaxHp = static_cast<double>(players[0].maxHp);
    context.bestGenome = initialGenome;
    context.bestIsSolution = false;
    context.visitedDepth.reserve(2048);

    updateBestGenome(context, initialGenome);

    uint64_t randomState = seed ^ (static_cast<uint64_t>(turns) << 32) ^ 0x9E3779B97F4A7C15ULL;

    Genome greedyGenome = runRollout(initialGenome, context, randomState, true);
    updateBestGenome(context, greedyGenome);

    runCriticalBeam(context, initialGenome, 24);
    if (context.bestIsSolution) {
        const int bestTargetTurn = context.bestGenome.turn - 1;
        if (bestTargetTurn > turns + 1) {
            const auto originalDeadline = context.deadline;
            context.deadline = std::min(context.deadline, Clock::now() + SHORTENING_BUDGET);
            runIterativeShortSearch(context, initialGenome, turns + 1, bestTargetTurn - 1);
            context.deadline = originalDeadline;
        }
        Node_Used = static_cast<uint32_t>(context.nodesVisited);
        return context.bestGenome;
    }

    const int firstTargetLimit = std::min(turns + 24, 99);
    if (runIterativeShortSearch(context, initialGenome, turns + 1, firstTargetLimit - 1)) {
        Node_Used = static_cast<uint32_t>(context.nodesVisited);
        return context.bestGenome;
    }

    const int rolloutLimit = maxGenerations <= 0 ? 256 : std::clamp(maxGenerations / 32, 64, 512);
    for (int iteration = 0;
         iteration < rolloutLimit &&
         Clock::now() < context.deadline &&
         context.nodesVisited < context.nodeBudget; ++iteration) {
        randomState ^= static_cast<uint64_t>(iteration + 1) * 0xBF58476D1CE4E5B9ULL;
        Genome rolloutGenome = runRollout(initialGenome, context, randomState, false);
        updateBestGenome(context, rolloutGenome);
        if (context.bestIsSolution && context.bestGenome.turn <= turns + 2) {
            break;
        }
    }

    const int currentBestTurn = context.bestIsSolution ? context.bestGenome.turn - 1 : std::min(turns + 24, 99);
    runIterativeShortSearch(context, initialGenome, turns + 1, currentBestTurn - 1);

    Node_Used = static_cast<uint32_t>(context.nodesVisited);

    if (context.bestIsSolution) {
        return context.bestGenome;
    }

    uint32_t legacyNodesUsed = 0;
    Genome fallbackGenome = runLegacyBestFirst(players, seed, turns, maxGenerations, actions, legacyNodesUsed);
    if (isSolution(fallbackGenome)) {
        Node_Used = legacyNodesUsed;
        return fallbackGenome;
    }

    context.bestGenome.turn = NO_SOLUTION_TURN;
    return context.bestGenome;
}

void ActionOptimizer::updateCompromiseScore(Genome &genome) {
    // Enemy action penalty processing (unchanged)
}

std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(const Player players[2], uint64_t seed, int turns,
                                                          int maxGenerations, int actions[350], int numThreads,
                                                          bool dropbug) {
    (void) numThreads;
    (void) dropbug;
    auto genome = RunAlgorithm(players, seed, turns, maxGenerations, actions, 0);
    return {0, genome};
}
