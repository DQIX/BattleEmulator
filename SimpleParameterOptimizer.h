//
// Simple Parameter Optimizer - シンプルで高速なパラメータ自動調整
//

#ifndef SIMPLE_PARAMETER_OPTIMIZER_H
#define SIMPLE_PARAMETER_OPTIMIZER_H

#if defined(OPTIMIZE_MODE)

#include "Player.h"
#include "Genome.h"
#include <vector>

// 最適化結果
struct OptimResult {
    int bestTurn = 999;
    int testCount = 0;
    bool found = false;
};

class SimpleParameterOptimizer {
public:
    // メイン最適化実行（シンプルなランダムサーチ）
    static OptimResult optimize(const Player players[2], uint64_t seed,
                               const int actions[350], int maxTests = 50, int turns = 0);

    // グリッドサーチ（確実だが遅い）
    static OptimResult gridSearch(const Player players[2], uint64_t seed,
                                 const int actions[350], int resolution = 3);

    // 山登り法（高速で実用的）
    static OptimResult hillClimbing(const Player players[2], uint64_t seed,
                                   const int actions[350], int maxSteps = 20);

    static double getActionCost(int action);

    // パラメータセットをテスト
    static int testParameters(const Player players[2],
                             uint64_t seed, const int actions[350], int turns);
};

#endif
class SimpleParameterOptimizerNode {
public:
    static constexpr int turnHeignt = 150;
    static constexpr int enemyHpWeight = 151;
    static constexpr int playerHpWeight = 152;
    static constexpr int resourceWeight = 153;
    static constexpr int StatusEffectWeight = 154;
    static constexpr int paralysisWeight = 155;
    static constexpr int sleepWeight = 156;
    static constexpr int poisonWeight = 157;
    static constexpr int inactiveWeight = 158;
    static constexpr int SpHeight = 159;
    static constexpr int ActHeight = 160;
    static constexpr int ResourceHPCost = 161;
    static constexpr int NoResourceCost = 162;
    static constexpr int BuffWeight = 163;
    static constexpr int AtkBuffWeight = 164;
    static constexpr int TensionWeight = 165;
    static constexpr int AntidoteWeight = 166;
};

#endif // SIMPLE_PARAMETER_OPTIMIZER_H

