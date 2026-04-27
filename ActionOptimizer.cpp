//
// Deterministic trace-style action optimizer.
// Builds a fast incumbent first, then proves optimality with IDDFS + branch and bound.
//

#include "ActionOptimizer.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstring>

#include "BattleEmulator.h"
#include "Genome.h"
#include "lcg.h"

namespace {
    struct ActionEntry {
        int action;
        bool (*condition)(const Player &ally);
    };

    const ActionEntry ACTION_TABLE[] = {
        {BattleEmulator::ATTACK_ALLY, [](const Player &) { return true; }},
        {BattleEmulator::DRAGON_SLASH, [](const Player &) { return true; }},
        {BattleEmulator::DEFENCE, [](const Player &) { return true; }},
        {BattleEmulator::FLEE_ALLY, [](const Player &) { return true; }},
        {BattleEmulator::SPECIAL_ANTIDOTE, [](const Player &ally) {
             return ally.SpecialAntidoteCount >= 1 && ally.PoisonEnable;
         }},
        {BattleEmulator::SPECIAL_MEDICINE, [](const Player &ally) {
             return ally.SpecialMedicineCount >= 1 && !ally.PoisonEnable;
         }},
        {BattleEmulator::HEAL, [](const Player &ally) { return ally.mp >= 2; }},
        {BattleEmulator::CRACK_ALLY, [](const Player &ally) { return ally.mp >= 3; }},
        {BattleEmulator::WOOSH_ALLY, [](const Player &ally) { return ally.mp >= 3; }},
        {BattleEmulator::ACROBATIC_STAR, [](const Player &ally) {
             return ally.specialCharge && ally.specialChargeTurn != 0;
         }},
    };

    const int ACTION_TABLE_SIZE = static_cast<int>(std::size(ACTION_TABLE));
    constexpr int MAX_ACTIONS = 350;
    constexpr int MAX_EXTRA_TURNS = 35;
    constexpr int MAX_BRANCHING = ACTION_TABLE_SIZE;
    constexpr int MAX_DAMAGE_PER_TURN_UPPER = 85;
    constexpr int MAX_DAMAGE_ACROBATSTAR_PER_TURN_UPPER = 120;
    constexpr int NO_SOLUTION = INT_MAX;

    struct SearchState {
        Player ally;
        Player enemy;
        int position = 1;
        uint64_t nowState = 0;
        int processedTurns = 0;
    };

    struct Candidate {
        SearchState state;
        int action = BattleEmulator::ATTACK_ALLY;
        int damage = 0;
        int score = INT_MIN;
        bool kill = false;
        bool alive = false;
    };

    static uint32_t Node_Used = 0;

    inline int computeProcessedTurns(const uint64_t nowState) {
        return static_cast<int>((nowState >> 12) & 0xFFFFF);
    }

    inline void clearActionTail(int actions[MAX_ACTIONS], const int startIndex) {
        actions[startIndex] = -1;
    }

    inline int actionPriority(const int action) {
        switch (action) {
            case BattleEmulator::ATTACK_ALLY:
                return 0;
            case BattleEmulator::DRAGON_SLASH:
                return 1;
            case BattleEmulator::CRACK_ALLY:
                return 2;
            case BattleEmulator::WOOSH_ALLY:
                return 3;
            case BattleEmulator::ACROBATIC_STAR:
                return 4;
            case BattleEmulator::HEAL:
                return 5;
            case BattleEmulator::SPECIAL_MEDICINE:
                return 6;
            case BattleEmulator::SPECIAL_ANTIDOTE:
                return 7;
            case BattleEmulator::DEFENCE:
                return 8;
            case BattleEmulator::FLEE_ALLY:
                return 9;
            default:
                return 10;
        }
    }

    inline bool isSupportAction(const int action) {
        return action == BattleEmulator::HEAL ||
               action == BattleEmulator::SPECIAL_MEDICINE ||
               action == BattleEmulator::SPECIAL_ANTIDOTE ||
               action == BattleEmulator::DEFENCE ||
               action == BattleEmulator::FLEE_ALLY;
    }

    inline int computeImmediateScore(const SearchState &currentState, const Candidate &candidate) {
        int score = candidate.damage * 10000;
        score += candidate.state.ally.hp * 32;
        score -= candidate.state.enemy.hp * 8;

        if (candidate.kill) {
            score += 100000000;
        }
        if (!candidate.alive) {
            score -= 100000000;
        }

        if (candidate.state.ally.hp > currentState.ally.hp) {
            score += (candidate.state.ally.hp - currentState.ally.hp) * 512;
        }
        if (candidate.state.ally.specialCharge && !currentState.ally.specialCharge) {
            score += 1200;
        }
        if (candidate.state.enemy.rage && !currentState.enemy.rage) {
            score += 300;
        }
        if (currentState.ally.hp <= 28 && isSupportAction(candidate.action)) {
            score += 1600;
        }
        if (candidate.action == BattleEmulator::ACROBATIC_STAR && currentState.ally.specialCharge) {
            score += 400;
        }

        score -= actionPriority(candidate.action);
        return score;
    }

    inline bool simulateTurn(const SearchState &currentState, const int action, int actions[MAX_ACTIONS], const uint64_t seed,
                             SearchState &nextState) {
        const int actionIndex = currentState.processedTurns;
        actions[actionIndex] = action;

        Player players[2] = {currentState.ally, currentState.enemy};
        int position = currentState.position;
        uint64_t nowState = currentState.nowState;

        BattleEmulator::Main(&position, 1, actions, players, nullptr, seed, nullptr, nullptr, -2, &nowState);

        actions[actionIndex] = -1;

        nextState.ally = players[0];
        nextState.enemy = players[1];
        nextState.position = position;
        nextState.nowState = nowState;
        nextState.processedTurns = computeProcessedTurns(nowState);
        return true;
    }

    inline int optimisticDamageUpperBound(const SearchState &state, const int remainingTurns) {
        if (remainingTurns <= 0 || state.ally.hp <= 0) {
            return 0;
        }

        int damageTurns = remainingTurns;

        // 睡眠中は1ターン行動不能とみなす
        if (state.ally.sleeping) {
            damageTurns -= 1;
        }

        if (damageTurns <= 0) {
            return 0;
        }

        int boostedTurns = 0;

        if (state.ally.acrobaticStar && state.ally.acrobaticStarTurn > 0) {
            boostedTurns = std::min(damageTurns, state.ally.acrobaticStarTurn);
        }

        const int normalTurns = damageTurns - boostedTurns;

        return boostedTurns * MAX_DAMAGE_ACROBATSTAR_PER_TURN_UPPER + normalTurns * MAX_DAMAGE_PER_TURN_UPPER;
    }

    inline int optimisticLowerBoundTurns(const SearchState &state) {
        if (state.enemy.hp <= 0) {
            return 0;
        }

        int hp = state.enemy.hp;
        int turns = 0;

        int starTurns = 0;
        if (state.ally.acrobaticStar && state.ally.acrobaticStarTurn > 0) {
            starTurns = state.ally.acrobaticStarTurn;
        }

        if (state.ally.sleeping) {
            ++turns;

            // 睡眠で1ターン消費する仕様なら、星の残りターンも減る前提にする
            if (starTurns > 0) {
                --starTurns;
            }
        }

        const int starDamage = MAX_DAMAGE_ACROBATSTAR_PER_TURN_UPPER; // 150
        const int normalDamage = MAX_DAMAGE_PER_TURN_UPPER;            // 80

        if (starTurns > 0 && hp > 0) {
            const int useStarTurns = std::min(starTurns, (hp + starDamage - 1) / starDamage);
            hp -= useStarTurns * starDamage;
            turns += useStarTurns;
        }

        if (hp > 0) {
            turns += (hp + normalDamage - 1) / normalDamage;
        }

        return turns;
    }

    int buildCandidates(const SearchState &currentState, int actions[MAX_ACTIONS], const uint64_t seed,
                        Candidate outCandidates[MAX_BRANCHING]) {
        if (currentState.ally.sleeping) {
            SearchState nextState;
            simulateTurn(currentState, BattleEmulator::ATTACK_ALLY, actions, seed, nextState);

            Candidate candidate;
            candidate.state = nextState;
            candidate.action = BattleEmulator::ATTACK_ALLY;
            candidate.damage = currentState.enemy.hp - nextState.enemy.hp;
            candidate.kill = nextState.enemy.hp <= 0;
            candidate.alive = nextState.ally.hp > 0;
            candidate.score = computeImmediateScore(currentState, candidate);
            outCandidates[0] = candidate;
            return 1;
        }

        int count = 0;
        for (const auto &entry: ACTION_TABLE) {
            if (!entry.condition(currentState.ally)) {
                continue;
            }

            SearchState nextState;
            simulateTurn(currentState, entry.action, actions, seed, nextState);

            Candidate candidate;
            candidate.state = nextState;
            candidate.action = entry.action;
            candidate.damage = currentState.enemy.hp - nextState.enemy.hp;
            candidate.kill = nextState.enemy.hp <= 0;
            candidate.alive = nextState.ally.hp > 0;
            candidate.score = computeImmediateScore(currentState, candidate);
            outCandidates[count++] = candidate;
        }

        std::sort(outCandidates, outCandidates + count, [](const Candidate &lhs, const Candidate &rhs) {
            if (lhs.score != rhs.score) {
                return lhs.score > rhs.score;
            }
            if (lhs.damage != rhs.damage) {
                return lhs.damage > rhs.damage;
            }
            if (lhs.state.ally.hp != rhs.state.ally.hp) {
                return lhs.state.ally.hp > rhs.state.ally.hp;
            }
            return actionPriority(lhs.action) < actionPriority(rhs.action);
        });

        return count;
    }

    int greedyUpperBound(const SearchState &startState, const uint64_t seed, int actions[MAX_ACTIONS],
                         int bestActions[MAX_ACTIONS]) {
        clearActionTail(bestActions, startState.processedTurns);

        SearchState currentState = startState;
        for (int extraTurns = 0; extraTurns < MAX_EXTRA_TURNS; ++extraTurns) {
            if (currentState.enemy.hp <= 0) {
                return extraTurns;
            }
            if (currentState.ally.hp <= 0) {
                return NO_SOLUTION;
            }

            Candidate candidates[MAX_BRANCHING];
            const int candidateCount = buildCandidates(currentState, actions, seed, candidates);
            if (candidateCount == 0) {
                return NO_SOLUTION;
            }

            const Candidate *selected = nullptr;
            for (int i = 0; i < candidateCount; ++i) {
                if (candidates[i].alive) {
                    selected = &candidates[i];
                    break;
                }
            }

            if (selected == nullptr) {
                return NO_SOLUTION;
            }

            const int actionIndex = currentState.processedTurns;
            bestActions[actionIndex] = selected->action;
            currentState = selected->state;
            clearActionTail(bestActions, currentState.processedTurns);
        }

        return currentState.enemy.hp <= 0 ? currentState.processedTurns - startState.processedTurns : NO_SOLUTION;
    }

    bool depthLimitedSearch(const SearchState &currentState, const int remainingTurns, const uint64_t seed,
                            int actions[MAX_ACTIONS], int bestActions[MAX_ACTIONS]) {
        if (optimisticLowerBoundTurns(currentState) > remainingTurns) {
            return false;
        }
        ++Node_Used;

        if (currentState.enemy.hp <= 0) {
            return true;
        }
        if (currentState.ally.hp <= 0 || remainingTurns <= 0) {
            return false;
        }
        if (optimisticDamageUpperBound(currentState, remainingTurns) < currentState.enemy.hp) {
            return false;
        }

        Candidate candidates[MAX_BRANCHING];
        const int candidateCount = buildCandidates(currentState, actions, seed, candidates);
        for (int i = 0; i < candidateCount; ++i) {
            const Candidate &candidate = candidates[i];
            if (!candidate.alive) {
                continue;
            }

            const int actionIndex = currentState.processedTurns;
            actions[actionIndex] = candidate.action;
            clearActionTail(actions, actionIndex + 1);

            if (candidate.kill) {
                std::memcpy(bestActions, actions, sizeof(int) * MAX_ACTIONS);
                return true;
            }

            if (depthLimitedSearch(candidate.state, remainingTurns - 1, seed, actions, bestActions)) {
                return true;
            }

            actions[actionIndex] = -1;
        }

        return false;
    }

    Genome buildGenomeFromState(const SearchState &state, const int actions[MAX_ACTIONS]) {
        Genome genome{};
        genome.AllyPlayer = state.ally;
        genome.EnemyPlayer = state.enemy;
        genome.state = state.nowState;
        genome.position = state.position;
        genome.processed = state.processedTurns;
        genome.turn = state.processedTurns + 1;
        genome.fitness = std::max(0, state.enemy.maxHp - state.enemy.hp);
        genome.EActions[0] = -1;
        genome.EActions[1] = -1;
        genome.Aactions = -1;
        genome.Initialized = true;
        genome.Visited = 0;
        for (int i = 0; i < MAX_ACTIONS; ++i) {
            genome.actions[i] = actions[i];
        }
        return genome;
    }

    SearchState makeInitialState(const Player players[2], const uint64_t seed, const int turns, const int actions[MAX_ACTIONS],
                                 int initialActions[MAX_ACTIONS]) {
        for (int i = 0; i < MAX_ACTIONS; ++i) {
            initialActions[i] = -1;
        }

        for (int i = 0; i < MAX_ACTIONS; ++i) {
            if (actions[i] == -1 || actions[i] == 0) {
                initialActions[i] = -1;
                break;
            }
            initialActions[i] = actions[i];
        }

        SearchState state;
        state.ally = players[0];
        state.enemy = players[1];
        state.position = 1;
        state.nowState = 0;
        state.processedTurns = 0;

        lcg::init(seed, true);

        if (turns > 0) {
            Player copiedPlayers[2] = {players[0], players[1]};
            int position = 1;
            uint64_t nowState = 0;
            BattleEmulator::Main(&position, turns, initialActions, copiedPlayers, nullptr, seed, nullptr, nullptr, -2,
                                 &nowState);
            state.ally = copiedPlayers[0];
            state.enemy = copiedPlayers[1];
            state.position = position;
            state.nowState = nowState;
            state.processedTurns = computeProcessedTurns(nowState);
        }

        clearActionTail(initialActions, state.processedTurns);
        return state;
    }
}

uint32_t ActionOptimizer::getNodesUsed() {
    return Node_Used;
}

Genome ActionOptimizer::RunAlgorithm(const Player players[2], const uint64_t seed, const int turns, int maxGenerations,
                                     int actions[MAX_ACTIONS], int seedOffset) {
    (void)maxGenerations;
    (void)seedOffset;

    Node_Used = 0;

    int workingActions[MAX_ACTIONS];
    SearchState initialState = makeInitialState(players, seed, turns, actions, workingActions);
    Genome initialGenome = buildGenomeFromState(initialState, workingActions);

    if (initialState.enemy.hp <= 0 || initialState.ally.hp <= 0) {
        return initialGenome;
    }

    int incumbentActions[MAX_ACTIONS];
    std::memcpy(incumbentActions, workingActions, sizeof(workingActions));
    const int greedyDepth = greedyUpperBound(initialState, seed, workingActions, incumbentActions);

    int bestActions[MAX_ACTIONS];
    std::memcpy(bestActions, incumbentActions, sizeof(incumbentActions));
    int bestDepth = greedyDepth;

    if (bestDepth == NO_SOLUTION) {
        bestDepth = MAX_EXTRA_TURNS;
        for (int limit = optimisticLowerBoundTurns(initialState); limit <= MAX_EXTRA_TURNS; ++limit) {
            std::memcpy(workingActions, incumbentActions, sizeof(workingActions));
            clearActionTail(workingActions, initialState.processedTurns);
            if (depthLimitedSearch(initialState, limit, seed, workingActions, bestActions)) {
                bestDepth = limit;
                break;
            }
        }
    } else {
        const int lowerBound = optimisticLowerBoundTurns(initialState);
        for (int limit = lowerBound; limit < bestDepth; ++limit) {
            std::memcpy(workingActions, incumbentActions, sizeof(workingActions));
            clearActionTail(workingActions, initialState.processedTurns);
            if (depthLimitedSearch(initialState, limit, seed, workingActions, bestActions)) {
                bestDepth = limit;
                break;
            }
        }
    }

    if (bestDepth == NO_SOLUTION || bestDepth > MAX_EXTRA_TURNS) {
        return initialGenome;
    }

    Player replayPlayers[2] = {players[0], players[1]};
    int replayPosition = 1;
    uint64_t replayState = 0;
    BattleEmulator::Main(&replayPosition, turns + bestDepth, bestActions, replayPlayers, nullptr, seed, nullptr,
                         nullptr, -2, &replayState);

    SearchState finalState;
    finalState.ally = replayPlayers[0];
    finalState.enemy = replayPlayers[1];
    finalState.position = replayPosition;
    finalState.nowState = replayState;
    finalState.processedTurns = computeProcessedTurns(replayState);
    return buildGenomeFromState(finalState, bestActions);
}

void ActionOptimizer::updateCompromiseScore(Genome &genome) {
    (void)genome;
}

std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(const Player players[2], const uint64_t seed, const int turns,
                                                          const int maxGenerations, int actions[MAX_ACTIONS],
                                                          int numThreads, bool dropbug) {
    (void)numThreads;
    (void)dropbug;
    auto genome = RunAlgorithm(players, seed, turns, maxGenerations, actions, 0);
    return {0, genome};
}
