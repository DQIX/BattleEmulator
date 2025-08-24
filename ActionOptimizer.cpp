//
// Created by Owner on 2024/11/21.
//

#include "ActionOptimizer.h"
#include <vector>
#include <random>
//#include <functional>
#if defined(MULTITHREADING)
#include <future>
#include "lcg.h"
#endif
//#include <queue>
#include "BattleEmulator.h"
#include "Genome.h"
#include "ActionBanManager.h"
#include "HeapQueue.h"

void ActionOptimizer::updateCompromiseScore(Genome &genome) {
    //敵の行動に応じた減点
    for (int i = 0; i < 2; ++i) {
        if (genome.EActions[i] == BattleEmulator::KASAP) {
            genome.compromiseScore -= 2;
        }
    }

    // 味方の状態に応じた減点
    if (genome.Aactions == BattleEmulator::SLEEPING || genome.Aactions == BattleEmulator::CURE_SLEEPING || genome.
        Aactions == BattleEmulator::TURN_SKIPPED) {
        genome.compromiseScore -= 1; // 睡眠
    } else {
        genome.compromiseScore = std::min(0, genome.compromiseScore + 1);
    }
}

#if defined(MULTITHREADING)

// 並列処理のための関数
std::pair<int, Genome> ActionOptimizer::RunAlgorithmSingleThread(const Player players[2], uint64_t seed, int turns,
                                                                 int maxGenerations, int actions[], int start,
                                                                 int end) {
    BattleEmulator::ResetTurnProcessed();
    auto seed1 = seed;
    auto turns1 = turns;
    auto maxGenerations1 = maxGenerations;

    Genome bestGenome = {};
    bestGenome.turn = INT_MAX;

    std::unique_ptr<int> position = std::make_unique<int>(1);
    std::unique_ptr<uint64_t> nowState = std::make_unique<uint64_t>(0);

    std::optional<BattleResult> result1;
    result1 = BattleResult();

    for (int i = start; i < end; ++i) {
        Genome candidate = RunAlgorithm(players, seed1, turns1, maxGenerations1, actions, i * 2);
        Player localPlayers1[2] = {players[0], players[1]};

        *position = 1;
        *nowState = 0;

        result1->clear();
        BattleEmulator::Main(position.get(), 100, candidate.actions, localPlayers1, result1, seed, nullptr, nullptr, -1,
                             nowState.get());

        if (localPlayers1[0].hp >= 0 && localPlayers1[1].hp == 0) {
            candidate.turn = result1->turn;
            if (candidate.turn < bestGenome.turn) {
                candidate.AllyPlayer.hp = localPlayers1[0].hp;
                candidate.EnemyPlayer.hp = localPlayers1[1].hp;
                bestGenome = candidate;
            }
        }
    }
    // TurnProcessedを取得
    int turnProcessed = BattleEmulator::getTurnProcessed();

    // turnProcessedとbestGenomeをペアで返す
    return std::make_pair(turnProcessed, bestGenome);
}


// メインの並列処理関数
std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(const Player players[2], uint64_t seed, int turns,
                                                          int totalIterations, int actions[350], int numThreads) {
    lcg::init(seed, true);
    int chunkSize = totalIterations / numThreads;

    std::vector<std::future<std::pair<int, Genome> > > futures;
    futures.reserve(numThreads); // ✅ メモリ確保でスコープ外の問題を防ぐ

    for (int i = 0; i < numThreads; ++i) {
        int start = i * chunkSize;
        int end = (i == numThreads - 1) ? totalIterations : start + chunkSize;

        futures.push_back(std::async(std::launch::async, RunAlgorithmSingleThread,
                                     std::cref(players), seed, turns, 3000, actions, start, end));
    }

    Genome bestGenome = {};
    bestGenome.turn = INT_MAX;

    int totalTurnProcessed = 0;

    for (auto &future: futures) {
        auto [turnProcessed, candidate] = future.get(); // ペアを取得
        totalTurnProcessed += turnProcessed;
        if (candidate.turn < bestGenome.turn) {
            bestGenome = candidate;
        }
    }

    return std::make_pair(totalTurnProcessed, bestGenome);
}

#endif

// オレオレアルゴリズム実行
Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                                     int actions[350], int seedOffset) {
    std::mt19937 rng(seed + seedOffset);
    auto heal = (rng() % 8);
    auto noHEAL = (rng() % 7) + turns;
    //auto heal = 0;
    std::unique_ptr<int> position = std::make_unique<int>(1);
    std::unique_ptr<uint64_t> nowState = std::make_unique<uint64_t>(0);
    auto counter = 0;
    Genome genome;
    ActionBanManager Bans;
    for (int i = 0; i < 350; ++i) {
        if (actions[i] == 0 || actions[i] == -1) {
            break;
        }
        genome.actions[i] = actions[i];
    }
    auto action = -1;
    auto Aactions = -1;
    // auto Adamage = -1;
    // int Eactions[2] = {-1, -1};
    // int Edamage[2] = {-1, -1};
    auto counter1 = 0;
    //std::priority_queue<Genome, std::vector<Genome>, std::greater<> > que;
    //std::priority_queue<Genome> que;
    HeapQueue que(300);

    genome = {};

    auto startTurn = turns;

    genome.EnemyPlayer = players[1];
    genome.AllyPlayer = players[0];
    genome.EActions[0] = -1;
    genome.EActions[1] = -1;
    genome.Aactions = -1;
    genome.fitness = 0;
    genome.turn = turns + 1;
    genome.Initialized = false;
    genome.compromiseScore = 0;
    genome.isEliminated = false;
    genome.processed = 0;
    genome.Visited = 0;
    genome.position = 1;
    genome.state = BattleEmulator::TYPE_2A;
    genome.canMove = false;

    for (int i = 0; i < 350; ++i) {
        if (actions[i] == -1 || actions[i] == 0) {
            genome.actions[i] = BattleEmulator::ATTACK_ALLY;
            break;
        } else {
            genome.actions[i] = actions[i];
        }
    }

    que.push(genome);
    std::optional<BattleResult> result;
    result = BattleResult();

    Genome BaseGenome = {};
    BaseGenome.turn = INT32_MAX - 1;
    bool found = false;
    Genome currentGenome;
    while (!que.empty() && (maxGenerations == -1 || maxGenerations > counter)) {
        currentGenome = que.top();
        que.pop();

        turns = currentGenome.turn;

        if (turns > (startTurn+45)) {
            continue;
        }

        if (currentGenome.Initialized && turns > 1 && currentGenome.actions[turns - 1] > 0) {
            Bans.ban_action(turns - 1, currentGenome.actions[turns - 1]);
        }


        Player CopedPlayers[2] = {currentGenome.AllyPlayer, currentGenome.EnemyPlayer};
        *position = currentGenome.position;
        *nowState = currentGenome.state;

        const auto tmpgenomu = currentGenome;
        result->clear(); //メモリ新規確保よりこっちのほうが早い

        BattleEmulator::Main(position.get(), currentGenome.turn - currentGenome.processed, currentGenome.actions,
                             CopedPlayers,
                             result, seed,
                             nullptr, nullptr, -1, nowState.get());

        if (currentGenome.Initialized && CopedPlayers[0].hp <= 0) {
            continue;
        }

        if (CopedPlayers[1].hp <= 0) {
            currentGenome.fitness += 100;
            //que.push(currentGenome);
            if (BaseGenome.turn > currentGenome.turn) {
                BaseGenome = currentGenome; //最適解を更新
                found = true;
            }
            continue;
        }

        if (BaseGenome.turn < currentGenome.turn) {
            continue; //最適解より長い回答は求めてない
        }

        counter1 = 0;
        currentGenome.EActions[0] = 0;
        currentGenome.EActions[1] = 0;
        auto actioncount = ((result->turn + 1) & 1) + 2;
        for (int i = std::max(0, result->position - actioncount); i < result->position; ++i) {
            if (result->isEnemy[i]) {
                //Eactions[counter1] = result->actions[i];
                //Edamage[counter1] = result->damages[i];
                currentGenome.EActions[counter1++] = result->actions[i];
            } else {
                Aactions = result->actions[i];
                //Adamage = result->damages[i];
                currentGenome.Aactions = result->actions[i];
            }
        }


        if (currentGenome.Visited == 0) {
            updateCompromiseScore(currentGenome);
        }
        //この時点で副作用が分かる。

        auto backToPast = false;

        if (currentGenome.EActions[0] == BattleEmulator::HEAL_ENEMY) {
            backToPast = true;
        }

        // if (currentGenome.Visited == 1) {
        //     std::cout << std::endl;
        // }

        if (currentGenome.Initialized && backToPast && currentGenome.Visited == 0) {
            // &&
            currentGenome.fitness -= 10;
            currentGenome.Visited++;
            que.push(currentGenome);
            counter++;
            continue;
        }
        bool skip = false;

        if (Aactions == BattleEmulator::SLEEPING || Aactions == BattleEmulator::PARALYSIS || Aactions ==
            BattleEmulator::TURN_SKIPPED) {
            //canmove1 = false;
            skip = true;
        }

        auto baseFitness = tmpgenomu.fitness;

        if (currentGenome.EActions[0] == BattleEmulator::BUFF_ENEMY || currentGenome.EActions[1] ==
            BattleEmulator::BUFF_ENEMY) {
            baseFitness -= 2;
        }

        if (skip) {
            if (!currentGenome.Initialized) {
                //todo
                currentGenome.actions[turns] = BattleEmulator::SPECIAL_MEDICINE; // -1消しとかないとなぜかズレる
                currentGenome.fitness += 10;
                currentGenome.position = *(position);
                currentGenome.state = *(nowState);
                currentGenome.turn = turns + 1;
                currentGenome.processed = turns;
                currentGenome.AllyPlayer = CopedPlayers[0];
                currentGenome.EnemyPlayer = CopedPlayers[1];
                que.push(currentGenome);
                counter++;
                continue;
            }
            if (currentGenome.Visited == 0) {
                currentGenome.fitness -= 2;
                currentGenome.Visited++;
                que.push(currentGenome);
                counter++;
                continue;
            }
        }
        const auto AllyPlayer = CopedPlayers[0];
        //auto EnemyPlayer = CopedPlayers[1];
        auto preHP = tmpgenomu.EnemyPlayer.hp;

        auto AllyPlayerPre = tmpgenomu.AllyPlayer;
        //auto EnemyPlayerPre = tmpgenomu.EnemyPlayer;

        currentGenome.Initialized = true;
        //if (!skip) {
        if (!Bans.is_action_banned(BattleEmulator::MEDICINAL_HERBS, turns) && AllyPlayerPre.medicinal_herbs_count
            > 0) {
            action = BattleEmulator::MEDICINAL_HERBS;
            if (tmpgenomu.Visited >= 1) {
                currentGenome.fitness = baseFitness; // 固定値に
                currentGenome.Visited = 0;
            } else {
                currentGenome.fitness = baseFitness + 7 + static_cast<int>(rng() % 13);
            }
            currentGenome.actions[turns - 1] = action;

            CopedPlayers[0] = tmpgenomu.AllyPlayer;
            CopedPlayers[1] = tmpgenomu.EnemyPlayer;

            (*position) = tmpgenomu.position;
            (*nowState) = tmpgenomu.state;

            BattleEmulator::Main(position.get(), tmpgenomu.turn - tmpgenomu.processed, currentGenome.actions,
                                 CopedPlayers,
                                 (std::optional<BattleResult> &) std::nullopt, seed,
                                 nullptr, nullptr, -2, nowState.get());
            currentGenome.position = (*position);
            currentGenome.state = (*nowState);
            currentGenome.turn = turns + 1;
            currentGenome.processed = turns;
            currentGenome.AllyPlayer = CopedPlayers[0];
            currentGenome.EnemyPlayer = CopedPlayers[1];

            if (!(turns > noHEAL && CopedPlayers[1].hp > preHP)) {
                que.push(currentGenome);
            }

        }

        if (AllyPlayerPre.mp >= 2 && !Bans.is_action_banned(BattleEmulator::HEAL, turns)) {
            action = BattleEmulator::HEAL;
            if (tmpgenomu.Visited >= 1) {
                currentGenome.fitness = baseFitness; // 固定値に
                currentGenome.Visited = 0;
            } else {
                currentGenome.fitness = baseFitness + 5 + static_cast<int>(rng() % 6);
            }
            currentGenome.actions[turns - 1] = action;

            CopedPlayers[0] = tmpgenomu.AllyPlayer;
            CopedPlayers[1] = tmpgenomu.EnemyPlayer;

            (*position) = tmpgenomu.position;
            (*nowState) = tmpgenomu.state;

            BattleEmulator::Main(position.get(), tmpgenomu.turn - tmpgenomu.processed, currentGenome.actions,
                                 CopedPlayers,
                                 (std::optional<BattleResult> &) std::nullopt, seed,
                                 nullptr, nullptr, -2, nowState.get());
            currentGenome.position = (*position);
            currentGenome.state = (*nowState);
            currentGenome.turn = turns + 1;
            currentGenome.processed = turns;
            currentGenome.AllyPlayer = CopedPlayers[0];
            currentGenome.EnemyPlayer = CopedPlayers[1];

            if (!(turns > noHEAL && CopedPlayers[1].hp > preHP)) {
                que.push(currentGenome);
            }
        }

        if (AllyPlayerPre.mp >= heal && AllyPlayerPre.mp >= 3 && !Bans.is_action_banned(BattleEmulator::CRACK_ALLY, turns)) {
            action = BattleEmulator::CRACK_ALLY;
            if (tmpgenomu.Visited >= 1) {
                currentGenome.fitness = baseFitness; // 固定値に
                currentGenome.Visited = 0;
            } else {
                currentGenome.fitness = baseFitness + 12 + static_cast<int>(rng() % 13);
            }
            currentGenome.actions[turns - 1] = action;

            CopedPlayers[0] = tmpgenomu.AllyPlayer;
            CopedPlayers[1] = tmpgenomu.EnemyPlayer;

            (*position) = tmpgenomu.position;
            (*nowState) = tmpgenomu.state;

            BattleEmulator::Main(position.get(), tmpgenomu.turn - tmpgenomu.processed, currentGenome.actions,
                                 CopedPlayers,
                                 (std::optional<BattleResult> &) std::nullopt, seed,
                                 nullptr, nullptr, -2, nowState.get());
            currentGenome.position = (*position);
            currentGenome.state = (*nowState);
            currentGenome.turn = turns + 1;
            currentGenome.processed = turns;
            currentGenome.AllyPlayer = CopedPlayers[0];
            currentGenome.EnemyPlayer = CopedPlayers[1];

            if (!(turns > noHEAL && CopedPlayers[1].hp > preHP)) {
                que.push(currentGenome);
            }
        }

        if (!Bans.is_action_banned(BattleEmulator::ATTACK_ALLY, turns)) {
            action = BattleEmulator::ATTACK_ALLY;
            auto visited = tmpgenomu.Visited;
            if (tmpgenomu.Visited >= 1) {
                currentGenome.fitness = baseFitness; // 固定値に
                currentGenome.Visited = 0;
            } else {
                currentGenome.fitness = baseFitness + 10 + static_cast<int>(rng() % 13);
            }
            currentGenome.actions[turns - 1] = action;

            CopedPlayers[0] = tmpgenomu.AllyPlayer;
            CopedPlayers[1] = tmpgenomu.EnemyPlayer;

            (*position) = tmpgenomu.position;
            (*nowState) = tmpgenomu.state;

            auto ehp1 = tmpgenomu.EnemyPlayer.hp;

            BattleEmulator::Main(position.get(), tmpgenomu.turn - tmpgenomu.processed, currentGenome.actions,
                                 CopedPlayers,
                                 (std::optional<BattleResult> &) std::nullopt, seed,
                                 nullptr, nullptr, -2, nowState.get());
            currentGenome.position = (*position);
            currentGenome.state = (*nowState);
            currentGenome.turn = turns + 1;
            currentGenome.processed = turns;
            currentGenome.AllyPlayer = CopedPlayers[0];
            currentGenome.EnemyPlayer = CopedPlayers[1];

            if (visited == 0 && ehp1 - currentGenome.EnemyPlayer.hp > 30) {
                currentGenome.fitness += 15;
            }

            if (!(turns > noHEAL && CopedPlayers[1].hp > preHP)) {
                que.push(currentGenome);
            }
        }


        if (!Bans.is_action_banned(BattleEmulator::DEFENCE, turns)) {
            action = BattleEmulator::DEFENCE;
            if (tmpgenomu.Visited >= 1) {
                currentGenome.fitness = baseFitness; // 固定値に
                currentGenome.Visited = 0;
            } else {
                currentGenome.fitness = baseFitness + 6 + static_cast<int>(rng() % 13);
            }
            currentGenome.actions[turns - 1] = action;

            CopedPlayers[0] = tmpgenomu.AllyPlayer;
            CopedPlayers[1] = tmpgenomu.EnemyPlayer;

            (*position) = tmpgenomu.position;
            (*nowState) = tmpgenomu.state;

            BattleEmulator::Main(position.get(), tmpgenomu.turn - tmpgenomu.processed, currentGenome.actions,
                                 CopedPlayers,
                                 (std::optional<BattleResult> &) std::nullopt, seed,
                                 nullptr, nullptr, -2, nowState.get());
            currentGenome.position = (*position);
            currentGenome.state = (*nowState);
            currentGenome.turn = turns + 1;
            currentGenome.processed = turns;
            currentGenome.AllyPlayer = CopedPlayers[0];
            currentGenome.EnemyPlayer = CopedPlayers[1];

            if (!(turns > noHEAL && CopedPlayers[1].hp > preHP)) {
                que.push(currentGenome);
            }
        }

        if (!Bans.is_action_banned(BattleEmulator::FLEE_ALLY, turns)) {
            action = BattleEmulator::FLEE_ALLY;
            if (tmpgenomu.Visited >= 1) {
                currentGenome.fitness = baseFitness; // 固定値に
                currentGenome.Visited = 0;
            } else {
                currentGenome.fitness = baseFitness + 5 + static_cast<int>(rng() % 6);
            }
            currentGenome.actions[turns - 1] = action;

            CopedPlayers[0] = tmpgenomu.AllyPlayer;
            CopedPlayers[1] = tmpgenomu.EnemyPlayer;

            (*position) = tmpgenomu.position;
            (*nowState) = tmpgenomu.state;

            BattleEmulator::Main(position.get(), tmpgenomu.turn - tmpgenomu.processed, currentGenome.actions,
                                 CopedPlayers,
                                 (std::optional<BattleResult> &) std::nullopt, seed,
                                 nullptr, nullptr, -2, nowState.get());
            currentGenome.position = (*position);
            currentGenome.state = (*nowState);
            currentGenome.turn = turns + 1;
            currentGenome.processed = turns;
            currentGenome.AllyPlayer = CopedPlayers[0];
            currentGenome.EnemyPlayer = CopedPlayers[1];

            if (!(turns > noHEAL && CopedPlayers[1].hp > preHP)) {
                que.push(currentGenome);
            }
        }

        if (AllyPlayerPre.mp >= 8 && !Bans.is_action_banned(BattleEmulator::DRAGON_SLASH, turns)) {
            action = BattleEmulator::DRAGON_SLASH;
            if (tmpgenomu.Visited >= 1) {
                currentGenome.fitness = baseFitness; // 固定値に
                currentGenome.Visited = 0;
            } else {
                currentGenome.fitness = baseFitness + 10 + static_cast<int>(rng() % 13);
            }
            currentGenome.actions[turns - 1] = action;

            CopedPlayers[0] = tmpgenomu.AllyPlayer;
            CopedPlayers[1] = tmpgenomu.EnemyPlayer;

            (*position) = tmpgenomu.position;
            (*nowState) = tmpgenomu.state;

            BattleEmulator::Main(position.get(), tmpgenomu.turn - tmpgenomu.processed, currentGenome.actions,
                                 CopedPlayers,
                                 (std::optional<BattleResult> &) std::nullopt, seed,
                                 nullptr, nullptr, -2, nowState.get());
            currentGenome.position = (*position);
            currentGenome.state = (*nowState);
            currentGenome.turn = turns + 1;
            currentGenome.processed = turns;
            currentGenome.AllyPlayer = CopedPlayers[0];
            currentGenome.EnemyPlayer = CopedPlayers[1];

            if (!(turns > noHEAL && CopedPlayers[1].hp > preHP)) {
                que.push(currentGenome);
            }
        }
        /*if (!Bans.is_action_banned(BattleEmulator::ITEM_USE, turns)) {
            action = BattleEmulator::ITEM_USE;
            if (tmpgenomu.Visited >= 1) {
                currentGenome.fitness = baseFitness; // 固定値に
                currentGenome.Visited = 0;
            } else {
                currentGenome.fitness = baseFitness + 2 + static_cast<int>(rng() % 6);
            }
            currentGenome.actions[turns - 1] = action;

            CopedPlayers[0] = tmpgenomu.AllyPlayer;
            CopedPlayers[1] = tmpgenomu.EnemyPlayer;

            (*position) = tmpgenomu.position;
            (*nowState) = tmpgenomu.state;

            BattleEmulator::Main(position.get(), tmpgenomu.turn - tmpgenomu.processed, currentGenome.actions,
                                 CopedPlayers,
                                 (std::optional<BattleResult> &) std::nullopt, seed,
                                 nullptr, nullptr, -2, nowState.get());
            currentGenome.position = (*position);
            currentGenome.state = (*nowState);
            currentGenome.turn = turns + 1;
            currentGenome.processed = turns;
            currentGenome.AllyPlayer = CopedPlayers[0];
            currentGenome.EnemyPlayer = CopedPlayers[1];

            que.push(currentGenome);
        }*/


        counter++;
    }

    if (found) {
        return BaseGenome;
    }
    Genome bestGenome = que.empty() ? currentGenome : static_cast<Genome>(que.top());
    return bestGenome;
}
