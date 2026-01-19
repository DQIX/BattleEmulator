//
// Simple Parameter Optimizer - シンプルで高速なパラメータ自動調整
//

#ifndef SIMPLE_PARAMETER_OPTIMIZER_H
#define SIMPLE_PARAMETER_OPTIMIZER_H

#if defined(OPTIMIZE_MODE)

#include <array>
#include <limits>

#include "Player.h"
#include "Genome.h"
#include <vector>

#include "BattleEmulator.h"

static constexpr int MAX_ACTION_ID = 512;
static constexpr int ids = 26;
static constexpr double DEFAULT_ACTION_COST = 1.0;
static constexpr double DEFAULT_STEP = 0.5; // 変異の基本スケール

// GA パラメータ（必要なら調整）
static constexpr int GA_POPULATION = 50; // 1世代あたり生成する子の数
static constexpr double GA_MUTATION_PROB = 0.15; // 各遺伝子が変異する確率
static constexpr double GA_CROSSOVER_PROB = 0.9; // 親から交叉する確率
static constexpr int GA_EVAL_SEEDS = 10;
static constexpr uint64_t kNumThreads = 8; // ★固定スレッド数（好きに調整）

// --- Stability tuning parameters (内部定義・調整可能) ---
constexpr uint64_t STABILITY_CHECKS = 30;           // 世代ごとに最良個体を何回別 seed で再評価するか
constexpr uint64_t GA_INSTABILITY_WEIGHT = 10.0; // instability を fitness に掛ける重み（経験則で調整）

// ---- 安定性チェック用: ランダム追加 actions（compile 時に決める） ----
// ここを編集するだけで「追加しうる行動」を切り替え可能
static constexpr std::array<int, 3> STABILITY_RANDOM_ACTION_POOL = {
    BattleEmulator::ATTACK_ALLY,
    BattleEmulator::MEDICINAL_HERBS,
    BattleEmulator::DEFENCE, // ※ もし敵の PSYCHE_UP を混ぜたいなら BattleEmulator::PSYCHE_UP を入れる
};
// 1回の stability check で最大いくつ挿入するか（0なら無効）
static constexpr double STABILITY_EXTRA_ACTION_INSERT_PROB = 0.60;
// 1回の stability check で最大いくつ挿入するか（0なら無効）
static constexpr int STABILITY_EXTRA_ACTIONS_MAX = 2;


// 最適化結果
struct OptimResult {
    uint64_t bestTurn = 999;
    uint64_t testCount = 0;
    bool found = false;
};


// --- 追加: クッション関数（範囲を評価して結果だけ返す） ---
struct EvalResult {
    int index = -1;
    uint64_t fitness = std::numeric_limits<uint64_t>::infinity();
    uint64_t measuredTurns = 0;
    double measuredMs = 0.0;
};

// --- 遺伝的アルゴリズム実装 ---
struct GAGenome {
    std::vector<double> genes; // size = TUNE_IDS.size()
    uint64_t fitness; // 小さいほど良い（ターン優先）
    uint64_t measuredTurns; // 実測ターン
    double measuredMs; // 実測時間（ms）
};

// --- 追加: stability check のクッション関数（範囲を評価して合計だけ返す） ---
struct StabilityChunkResult {
    uint64_t instabilitySum = 0;
    int performed = 0;
    uint64_t turns = 0;
};


class SimpleParameterOptimizer {


    public:
    // メイン最適化実行（シンプルなランダムサーチ）
    static OptimResult optimize(const Player players[2], uint64_t seed,
                               const int actions[350], int maxTests = 50, int turns = 0);

    static double getActionCost(int action);

    // パラメータセットをテスト
    static uint64_t testParameters(const Player players[2],
                             uint64_t seed, const int actions[350], int turns);
private:
    static std::vector<EvalResult> evaluateGenomeRange(
        std::vector<GAGenome> *population,
        const std::vector<int> *pendingIndices,
        int start,
        int end,
        const Player players[2],
        const std::array<uint64_t, GA_EVAL_SEEDS> &evalSeeds,
        const int actions[350],
        int turnsLimit,
        uint64_t seedForThread
    );

    static StabilityChunkResult stabilityCheckRange(
        const GAGenome *bestGenomeCopy,
        int baselineTurn,
        int beginIdx,
        int endIdx,
        uint64_t baseSeed,
        const Player players[2],
        const int actions[350],
        int turnsLimit
    );

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

