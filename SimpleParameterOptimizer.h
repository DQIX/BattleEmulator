//
// Simple Parameter Optimizer - シンプルで高速なパラメータ自動調整
//

#if defined(OPTIMIZE_MODE)

#ifndef SIMPLE_PARAMETER_OPTIMIZER_H
#define SIMPLE_PARAMETER_OPTIMIZER_H

#include "Player.h"
#include "Genome.h"
#include <vector>

// グローバルパラメータ（シンプルに直接アクセス）
struct CostParams {
    // 基本重み
    static double enemyHpWeight;        // 敵HP重み
    static double playerHpWeight;       // プレイヤーHP重み
    static double resourceWeight;       // リソース重み
    static double StatusEffectWeight;       // リソース重み
    static double turnHeignt;       // リソース重み
    static double SpHeight;       // リソース重み
    static double ActHeight;       // リソース重み

    // アクションペナルティ
    static double AttackPenalty;
    static double healPenalty;
    static double dragonSlashPenalty;
    static double itemHealPenalty;
    static double defensePenalty;
    static double fleePenalty;
    static double buffPenalty;
    static double specialPenalty;
    static double CRACKLEPenalty;
    static double antidotePenalty;
    static double wooshAllyPenalty;

    // ステータス効果
    static double paralysisWeight;
    static double sleepWeight;
    static double poisonWeight;
    static double buffBonus;
    static double atkBuffBonus;

    static std::string toText() ;

    // デフォルト値設定
    static void setDefaults();
    static void copyFrom(const CostParams& other);
};

// 最適化結果
struct OptimResult {
    CostParams bestParams;
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

private:
    // パラメータセットをテスト
    static int testParameters(const CostParams& params, const Player players[2],
                             uint64_t seed, const int actions[350], int turns);

    // ランダムパラメータ生成
    static CostParams generateRandom();

    // パラメータを少し変更
    static CostParams mutate(const CostParams& base, double strength = 0.2);

    // パラメータの妥当性チェック
    static void clampParams(CostParams& params);
};

#endif // SIMPLE_PARAMETER_OPTIMIZER_H

#endif
