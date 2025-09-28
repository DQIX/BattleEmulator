//
// Simple Parameter Optimizer Implementation
//

#if defined(OPTIMIZE_MODE)

#include "SimpleParameterOptimizer.h"
#include "ActionOptimizer.h"
#include "BattleEmulator.h"
#include <random>
#include <iostream>
#include <algorithm>
#include <sstream>

// グローバルパラメータの初期化
double CostParams::enemyHpWeight = 30.0;
double CostParams::playerHpWeight = 2.0;
double CostParams::turnHeignt = 2.0;
double CostParams::resourceWeight = 0.5;
double CostParams::StatusEffectWeight = 0.5;
double CostParams::healPenalty = 0.01;
double CostParams::AttackPenalty = 0.01;
double CostParams::dragonSlashPenalty = 0.01;
double CostParams::itemHealPenalty = 0.015;
double CostParams::defensePenalty = 0.05;
double CostParams::fleePenalty = 0.05;
double CostParams::buffPenalty = 0.02;
double CostParams::specialPenalty = 0.001;
double CostParams::paralysisWeight = 1.0;
double CostParams::sleepWeight = 1.5;
double CostParams::poisonWeight = 0.5;
double CostParams::buffBonus = 0.1;
double CostParams::atkBuffBonus = 0.1;

std::string CostParams::toText() {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(6);
    oss << "enemyHpWeight=" << enemyHpWeight << "\n"
        << "playerHpWeight=" << playerHpWeight << "\n"
        << "resourceWeight=" << resourceWeight << "\n"
        << "turnHeignt=" << turnHeignt << "\n"
        << "StatusEffectWeight=" << StatusEffectWeight << "\n"
        << "healPenalty=" << healPenalty << "\n"
        << "AttackPenalty=" << AttackPenalty << "\n"
        << "dragonSlashPenalty=" << dragonSlashPenalty << "\n"
        << "itemHealPenalty=" << itemHealPenalty << "\n"
        << "defensePenalty=" << defensePenalty << "\n"
        << "fleePenalty=" << fleePenalty << "\n"
        << "buffPenalty=" << buffPenalty << "\n"
        << "specialPenalty=" << specialPenalty << "\n"
        << "paralysisWeight=" << paralysisWeight << "\n"
        << "sleepWeight=" << sleepWeight << "\n"
        << "poisonWeight=" << poisonWeight << "\n"
        << "buffBonus=" << buffBonus << "\n"
        << "atkBuffBonus=" << atkBuffBonus;
   std::cout << oss.str() << std::endl;
    return oss.str();
}


void CostParams::setDefaults() {
    enemyHpWeight = 35.0;
    playerHpWeight = 2.0;
    resourceWeight = 2.0;
    turnHeignt = 1.0;
    StatusEffectWeight = 3.0;
    AttackPenalty = 0.00;
    dragonSlashPenalty = 0.01;
    healPenalty = 0.01;
    itemHealPenalty = 0.015;
    defensePenalty = 0.05;
    fleePenalty = 0.05;
    buffPenalty = 0.02;
    specialPenalty = 0.001;
    paralysisWeight = 1.0;
    sleepWeight = 1.5;
    poisonWeight = 0.5;
    buffBonus = 0.1;
    atkBuffBonus = 0.1;
}

void CostParams::copyFrom(const CostParams& other) {
    enemyHpWeight = other.enemyHpWeight;
    playerHpWeight = other.playerHpWeight;
    resourceWeight = other.resourceWeight;
    turnHeignt = other.turnHeignt;
    StatusEffectWeight = other.StatusEffectWeight;
    healPenalty = other.healPenalty;
    AttackPenalty = other.AttackPenalty;
    dragonSlashPenalty = other.dragonSlashPenalty;
    itemHealPenalty = other.itemHealPenalty;
    defensePenalty = other.defensePenalty;
    fleePenalty = other.fleePenalty;
    buffPenalty = other.buffPenalty;
    specialPenalty = other.specialPenalty;
    paralysisWeight = other.paralysisWeight;
    sleepWeight = other.sleepWeight;
    poisonWeight = other.poisonWeight;
    buffBonus = other.buffBonus;
    atkBuffBonus = other.atkBuffBonus;
}

OptimResult SimpleParameterOptimizer::optimize(const Player players[2], uint64_t seed,
                                              const int actions[350], int maxTests, int turns) {
    std::cout << "パラメータ最適化開始 (最大テスト数: " << maxTests << ")" << std::endl;

    OptimResult result;
    CostParams originalParams;
    originalParams.copyFrom(CostParams()); // 現在の値を保存

    // 初期値でテスト
    result.bestTurn = testParameters(CostParams(), players, seed, actions, turns);
    result.bestParams.copyFrom(CostParams());
    result.testCount = 1;
    result.found = (result.bestTurn < 999);

    std::cout << "初期パラメータ: " << result.bestTurn << "ターン" << std::endl;

    std::random_device rd;
    std::mt19937 gen(rd());

    for (int test = 1; test < maxTests; ++test) {
        CostParams testParams = generateRandom();
        CostParams::copyFrom(testParams); // グローバルに適用

        int turn = testParameters(testParams, players, seed, actions, turns);

        if (turn < result.bestTurn) {
            result.bestTurn = turn;
            result.bestParams.copyFrom(testParams);
            result.found = true;

            std::cout << "改善! Test " << test << ": " << turn << "ターン"
                      << " (EHP:" << testParams.enemyHpWeight
                      << " PHP:" << testParams.playerHpWeight << ")" << std::endl;

            CostParams::toText();
        }

        if (test % 5 == 0) {
            std::cout << "Progress: " << test << "/" << maxTests << std::endl;
        }

        // 早期終了
        if (result.bestTurn <= 5) break;
    }

    result.testCount = maxTests;

    // 最適パラメータを適用
    CostParams::copyFrom(result.bestParams);

    std::cout << "最適化完了! 最短: " << result.bestTurn << "ターン" << std::endl;
    return result;
}

int SimpleParameterOptimizer::testParameters(const CostParams& params, const Player players[2],
                                            uint64_t seed, const int actions[350], int turns) {
    // パラメータを一時的に適用
    CostParams::copyFrom(params);

    Player copiedPlayers[2] = {players[0], players[1]};

    int gene[350] = {0};
    for (int i = 0; i < 350; ++i) {
        gene[i] = actions[i];
        if (actions[i] == -1) break;
    }

    // ActionOptimizerを実行（少ない世代数で高速化）
    auto genome = ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 150000, gene, 0);

    if (genome.EnemyPlayer.hp <= 0) {
        return genome.turn - 1;
    } else {
        return 999; // 失敗
    }
}

CostParams SimpleParameterOptimizer::generateRandom() {
    static std::random_device rd;
    static std::mt19937 gen(rd());

    CostParams params;

    // 合理的な範囲でランダム生成（各項目ごとに個別の分布）
    std::uniform_real_distribution<> enemyHpDist(10.0, 50.0);
    std::uniform_real_distribution<> playerHpDist(0.5, 10.0);
    std::uniform_real_distribution<> resourceDist(0.1, 10.0);
    std::uniform_real_distribution<> StatusEffectWeight(0.1, 10.0);
    std::uniform_real_distribution<> turnHeignt(0.1, 1.5);

    std::uniform_real_distribution<> AttackPenalty(0.01, 2.0);
    std::uniform_real_distribution<> healPenaltyDist(0.01, 2.0);
    std::uniform_real_distribution<> dragonSlashPenaltyDist(0.01, 2.0);
    std::uniform_real_distribution<> itemHealPenaltyDist(0.01, 2.0);
    std::uniform_real_distribution<> defensePenaltyDist(0.01, 2.0);
    std::uniform_real_distribution<> fleePenaltyDist(0.01, 2.0);
    std::uniform_real_distribution<> buffPenaltyDist(0.01, 2.0);
    std::uniform_real_distribution<> specialPenaltyDist(0.01, 2.0);

    params.enemyHpWeight = enemyHpDist(gen);
    params.playerHpWeight = playerHpDist(gen);
    params.resourceWeight = resourceDist(gen);
    params.turnHeignt = turnHeignt(gen);
    params.StatusEffectWeight = StatusEffectWeight(gen);

    params.healPenalty = healPenaltyDist(gen);
    params.AttackPenalty = AttackPenalty(gen);
    params.dragonSlashPenalty = dragonSlashPenaltyDist(gen);
    params.itemHealPenalty = itemHealPenaltyDist(gen);
    params.defensePenalty = defensePenaltyDist(gen);
    params.fleePenalty = fleePenaltyDist(gen);
    params.buffPenalty = buffPenaltyDist(gen);
    params.specialPenalty = specialPenaltyDist(gen);

    return params;
}

#endif
