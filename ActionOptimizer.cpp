//
// Created by Owner on 2024/11/21.
// Modified for A* Algorithm
//

#include "ActionOptimizer.h"
#include <vector>
#include <random>
#include <unordered_set>
#if defined(MULTITHREADING)
#include <future>
#include "lcg.h"
#endif
#include <memory>
#include <queue>

#include "BattleEmulator.h"
#include "Genome.h"
#include "ActionBanManager.h"
#include "HeapQueue.h"



// 状態ハッシュ計算（アクション配列ベース）
uint64_t computeStateHash(const Genome& genome) {
    uint64_t hash = 0;
    // Hash actions content
    // 各 int を 64bit に拡張してミックス（順序も反映）
    for (int i = 0; i < 350; ++i) {
        if (genome.actions[i] == -1 || genome.actions[i] == 0) {
            return hash;
        }
        auto a = static_cast<uint64_t>(static_cast<uint32_t>(genome.actions[i]));
        // 64bit 版 hash_combine に近いミキシング
        hash ^= a + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
        // index も軽く混ぜて順序性を強化
        hash ^= (static_cast<uint64_t>(i) * 0x9e3779b97f4a7c15ULL);
    }
    return hash;
}

// A*アルゴリズム実装
Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                                     int actions[350], int seedOffset) {
    std::mt19937 rng(seed + seedOffset);

    // 敵のmaxHpをキャッシュ（不変値）
    const int enemyMaxHp = static_cast<int>(players[1].maxHp);

    std::unique_ptr<int> position = std::make_unique<int>(1);
    std::unique_ptr<uint64_t> nowState = std::make_unique<uint64_t>(0);

    // A*用の優先度キューと訪問済みセット
    HeapQueue openSet(1000000);//578000
    std::unordered_set<uint64_t> closedSet;

    // 初期ノードの設定
    Genome initialGenome = {};
    initialGenome.EnemyPlayer = players[1];
    initialGenome.AllyPlayer = players[0];
    initialGenome.EActions[0] = -1;
    initialGenome.EActions[1] = -1;
    initialGenome.Aactions = -1;
    initialGenome.fitness = 0;
    initialGenome.turn = turns; // 過去のターンを保持
    initialGenome.processed = 0;
    initialGenome.Initialized = false;
    initialGenome.compromiseScore = 0;
    initialGenome.isEliminated = false;
    initialGenome.processed = turns - 1; // processed = turn - 1 で次のターンを実行
    initialGenome.Visited = 0;
    initialGenome.position = 1;
    initialGenome.state = BattleEmulator::TYPE_2A;
    initialGenome.canMove = false;

    // 初期アクション配列の設定
    for (int i = 0; i < 350; ++i) {
        if (actions[i] == -1 || actions[i] == 0) {
            initialGenome.actions[i] = -1;
            break;
        } else {
            initialGenome.actions[i] = actions[i];
        }
    }

    // 初期ノードをopenSetに追加
    AStarNode initialNode;
    initialNode.genome = initialGenome;
    initialNode.gCost = 0; // 初期コストは0
    initialNode.hCost = (initialGenome.EnemyPlayer.hp > 0) ?
                         initialGenome.EnemyPlayer.hp / static_cast<double>(enemyMaxHp): 0;
    initialNode.fCost = initialNode.gCost + initialNode.hCost;
    initialNode.stateHash = computeStateHash(initialGenome);

    openSet.push(initialNode);

    Genome bestSolution = {};
    bestSolution.turn = INT32_MAX;
    bool solutionFound = false;

    int counter = 0;

    double startT = turns + 40;

    while (!openSet.empty() && (maxGenerations == -1 || counter < maxGenerations)) {
        // 最小fCostのノードを取得
        AStarNode currentNode = openSet.top();
        openSet.pop();

        if (counter % 1000000 == 0) {
            std::cout << counter << "," << initialGenome.turn << "," << currentNode.hCost << ", "<< currentNode.gCost << "," << currentNode.genome.EnemyPlayer.hp << std::endl;
        }

        // 既に探索済みの状態はスキップ
        if (closedSet.count(currentNode.stateHash)) {
            continue;
        }
        closedSet.insert(currentNode.stateHash);

        Genome currentGenome = currentNode.genome;

        // ターン数制限チェック
        if (currentGenome.turn > startT) {
            continue;
        }

        // 勝利条件チェック
        if (currentGenome.EnemyPlayer.hp <= 0) {
            if (!solutionFound || currentGenome.turn < bestSolution.turn) {
                bestSolution = currentGenome;
                solutionFound = true;
            }
            //break;
            continue;
        }

        // 敗北条件チェック
        if (currentGenome.AllyPlayer.hp <= 0) {
            continue;
        }

        // 既存の最適解より悪い場合はスキップ
        if (solutionFound && currentGenome.turn >= bestSolution.turn) {
            continue;
        }

        // 各アクションを試行（ban機能は削除）
        std::vector<int> possibleActions;

        // 基本アクション（常に可能）
        possibleActions.push_back(BattleEmulator::ATTACK_ALLY);
        possibleActions.push_back(BattleEmulator::DRAGON_SLASH);
        possibleActions.push_back(BattleEmulator::DEFENCE);
        possibleActions.push_back(BattleEmulator::FLEE_ALLY);

        // 薬草
        if (players[0].medicinal_herbs_count >= 1) {
            possibleActions.push_back(BattleEmulator::MEDICINAL_HERBS);
        }
        // 回復魔法
        if (currentGenome.AllyPlayer.mp >= 2) {
            possibleActions.push_back(BattleEmulator::HEAL);
        }

        // バイキルト
        if (currentGenome.AllyPlayer.mp >= 3) {
            possibleActions.push_back(BattleEmulator::CRACK_ALLY);
        }
        // アクロバットスター
        if (currentGenome.AllyPlayer.specialCharge == true && currentGenome.AllyPlayer.specialChargeTurn != 0 &&
            currentGenome.AllyPlayer.acrobaticStar == false) {
            possibleActions.push_back(BattleEmulator::ACROBATIC_STAR);
        }

        // 各アクションを実行して新しいノードを生成
        for (int action : possibleActions) {
            Genome newGenome = currentGenome;
            newGenome.actions[currentGenome.turn] = action; // 現在ターンにアクションを設定
            newGenome.Initialized = true;

            // バトルエミュレータ実行用のコピー
            Player CopedPlayers[2] = {currentGenome.AllyPlayer, currentGenome.EnemyPlayer};
            *position = currentGenome.position;
            *nowState = currentGenome.state;

            if ( newGenome.turn - newGenome.processed != 1) {
                std::cout << newGenome.turn - newGenome.processed << std::endl;
            }
            // 1ターンだけ部分実行（currentGenome.turn - newGenome.processed = 1ターン分）
            BattleEmulator::Main(position.get(), newGenome.turn - newGenome.processed, newGenome.actions, CopedPlayers,
                                 (std::optional<BattleResult>&)std::nullopt, seed,
                                 nullptr, nullptr, -2, nowState.get());

            // 結果を新しいGenomeに反映
            newGenome.position = *position;
            newGenome.state = *nowState;
            newGenome.turn = currentGenome.turn + 1;
            newGenome.processed = currentGenome.turn; // 現在のターンまで処理済み
            newGenome.AllyPlayer = CopedPlayers[0];
            newGenome.EnemyPlayer = CopedPlayers[1];

            // 状態ハッシュ計算
            uint64_t newStateHash = computeStateHash(newGenome);

            // 既に探索済みの状態はスキップ
            if (closedSet.count(newStateHash)) {
                continue;
            }

            // A*コスト計算
            AStarNode newNode;
            newNode.genome = newGenome;
            newNode.gCost = (newGenome.turn - 1) / 50.0; // 実際のターン数コスト

            // ヒューリスティック: enemyMaxHp / currentEnemyHp
            if (newGenome.EnemyPlayer.hp > 0) {
                newNode.hCost =  newGenome.EnemyPlayer.hp / static_cast<double>(enemyMaxHp);
            } else {
                newNode.hCost = 0; // 敵が倒れた場合
            }

            newNode.fCost = newNode.gCost + newNode.hCost;
            newNode.stateHash = newStateHash;

            // openSetに追加
            openSet.push(newNode);
        }

        counter++;
    }

    if (solutionFound) {
        return bestSolution;
    }

    // 解が見つからなかった場合、最後に処理したノードを返す
    if (!openSet.empty()) {
        return openSet.top().genome;
    }

    return initialGenome;
}

void ActionOptimizer::updateCompromiseScore(Genome &genome) {
    // 敵の行動に応じた減点処理
}

#if defined(MULTITHREADING)

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

    int turnProcessed = BattleEmulator::getTurnProcessed();
    return std::make_pair(turnProcessed, bestGenome);
}

std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(const Player players[2], uint64_t seed, int turns,
                                                          int totalIterations, int actions[350], int numThreads) {
    lcg::init(seed, true);
    int chunkSize = totalIterations / numThreads;

    std::vector<std::future<std::pair<int, Genome>>> futures;
    futures.reserve(numThreads);

    for (int i = 0; i < numThreads; ++i) {
        int start = i * chunkSize;
        int end = (i == numThreads - 1) ? totalIterations : start + chunkSize;

        futures.push_back(std::async(std::launch::async, RunAlgorithmSingleThread,
                                     std::cref(players), seed, turns, 2000000, actions, start, end));
    }

    Genome bestGenome = {};
    bestGenome.turn = INT_MAX;

    int totalTurnProcessed = 0;

    for (auto &future: futures) {
        auto [turnProcessed, candidate] = future.get();
        totalTurnProcessed += turnProcessed;
        if (candidate.turn < bestGenome.turn) {
            bestGenome = candidate;
        }
    }

    return std::make_pair(totalTurnProcessed, bestGenome);
}

#endif