//
// Simple Parameter Optimizer - シンプルで高速なパラメータ自動調整
//

#ifndef SIMPLE_PARAMETER_OPTIMIZER_H
#define SIMPLE_PARAMETER_OPTIMIZER_H

#include "BattleEmulator.h"

#include <array>
#include <limits>
#include <ostream>

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
    static constexpr int SpecialMedicineCount = 167;

    static constexpr int ids = 24;

    // この配列に最適化対象の aABILITY_EXTRA_ACTIONS_MAX = 3;
    //// 各挿入を行う確率（1.0=必ずction id を並べるだけで追加完了
    static constexpr std::array<int, ids> TUNE_IDS = {
        BattleEmulator::ATTACK_ALLY,
        BattleEmulator::DRAGON_SLASH,
        BattleEmulator::DEFENCE,
        BattleEmulator::FLEE_ALLY,
        BattleEmulator::MEDICINAL_HERBS,
        BattleEmulator::HEAL,
        BattleEmulator::CRACK_ALLY,
        SimpleParameterOptimizerNode::turnHeignt,
        SimpleParameterOptimizerNode::enemyHpWeight,
        SimpleParameterOptimizerNode::playerHpWeight,
        SimpleParameterOptimizerNode::resourceWeight,
        SimpleParameterOptimizerNode::StatusEffectWeight,
        SimpleParameterOptimizerNode::paralysisWeight,
        SimpleParameterOptimizerNode::sleepWeight,
        SimpleParameterOptimizerNode::poisonWeight,
        SimpleParameterOptimizerNode::SpHeight,
        SimpleParameterOptimizerNode::ActHeight,
        SimpleParameterOptimizerNode::ResourceHPCost,
        SimpleParameterOptimizerNode::NoResourceCost,
        SimpleParameterOptimizerNode::BuffWeight,
        SimpleParameterOptimizerNode::AtkBuffWeight,
        SimpleParameterOptimizerNode::TensionWeight,
        SimpleParameterOptimizerNode::AntidoteWeight,
        SimpleParameterOptimizerNode::SpecialMedicineCount,
    };

    static constexpr int lastid = TUNE_IDS[ids - 1];

    static_assert(TUNE_IDS[ids - 1] != 0, "TUNE_IDS mismatch");
};

#if defined(OPTIMIZE_MODE)

#include "Player.h"
#include "Genome.h"
#include <vector>

using fitness_t = __int128;

static constexpr fitness_t kUnevaluatedFitness = std::numeric_limits<__int128>::max();


constexpr uint64_t FAULT_WEIGHT = 50ull;
constexpr uint64_t TURN_WEIGHT  = 32ull;
constexpr uint64_t HP_WEIGHT    = 16ull;
//constexpr uint64_t MS_WEIGHT    = -24ULL;
//constexpr uint64_t NODES_WEIGHT = 10ull;
constexpr uint64_t HERB_WEIGHT = 0ull;

static_assert(FAULT_WEIGHT > TURN_WEIGHT);
static_assert(TURN_WEIGHT  > HP_WEIGHT);
// static_assert(HP_WEIGHT    > MS_WEIGHT);
//static_assert(MS_WEIGHT    > NODES_WEIGHT);
static_assert(HP_WEIGHT > HERB_WEIGHT);

static constexpr int MAX_ACTION_ID = 512;
static constexpr double DEFAULT_ACTION_COST = 0.0;
static constexpr double DEFAULT_STEP = 0.5; // 変異の基本スケール

// GA パラメータ（必要なら調整）
static constexpr int GA_POPULATION = 500; // 1世代あたり生成する子の数
static constexpr double GA_MUTATION_PROB = 0.15; // 各遺伝子が変異する確率
static constexpr double GA_CROSSOVER_PROB = 0.9; // 親から交叉する確率
static constexpr int GA_EVAL_SEEDS = 7;
static constexpr uint64_t kNumThreads = 8; // ★固定スレッド数（好きに調整）

// --- Stability tuning parameters (内部定義・調整可能) ---
constexpr uint64_t STABILITY_CHECKS = 0;           // 世代ごとに最良個体を何回別 seed で再評価するか
constexpr uint64_t GA_INSTABILITY_WEIGHT = 10.0; // instability を fitness に掛ける重み（経験則で調整）

// ---- 安定性チェック用: ランダム追加 actions（compile 時に決める） ----
// ここを編集するだけで「追加しうる行動」を切り替え可能
static constexpr std::array<int, 3> STABILITY_RANDOM_ACTION_POOL = {
    BattleEmulator::ATTACK_ALLY,
    BattleEmulator::MEDICINAL_HERBS,
    BattleEmulator::DEFENCE,
};
// 1回の stability check で最大いくつ挿入するか（0なら無効）
static constexpr double STABILITY_EXTRA_ACTION_INSERT_PROB = 0.60;
// 1回の stability check で最大いくつ挿入するか（0なら無効）
static constexpr int STABILITY_EXTRA_ACTIONS_MAX = 0;


// 最適化結果
struct OptimResult {
    uint64_t bestTurn = 999;
    uint64_t bestStableGap = 0;
    uint64_t bestStableDeviation = 0;
    uint64_t testCount = 0;
    bool found = false;
};


// --- 追加: idx（範囲を評価して結果だけ返す） ---
struct EvalResult {
    int index = -1;
    fitness_t fitness = std::numeric_limits<fitness_t>::max();
    uint64_t measuredTurns = 0;
    uint64_t totalHP{}; // 実測ターン
    uint64_t faultCount{}; // 実測ターン
    double measuredMs = 0.0;
    uint64_t stabilityGap{};
    uint64_t maxDeviation{};
    uint64_t seed[GA_EVAL_SEEDS]{};
    int actions[350]{};
};

// --- 遺伝的アルゴリズム実装 ---
struct GAGenome {
    std::vector<double> genes; // size = TUNE_IDS.size()
    fitness_t fitness = std::numeric_limits<fitness_t>::max(); // 小さいほど良い（ターン優先）
    uint64_t measuredTurns; // 実測ターン
    uint64_t totalHP; // 実測ターン
    uint64_t faultCount; // 実測ターン
    double measuredMs; // 実測時間（ms）
    uint64_t stabilityGap;
    uint64_t maxDeviation;
};

// --- 追加: stability check のクッション関数（範囲を評価して合計だけ返す） ---
struct StabilityChunkResult {
    uint64_t instabilitySum = 0;
    int performed = 0;
    uint64_t turns = 0;
};

inline std::string to_string(__int128 value) {
    if (value == 0) return "0";

    bool negative = value < 0;
    if (negative) value = -value;

    std::string result;
    while (value > 0) {
        result.push_back('0' + value % 10);
        value /= 10;
    }

    if (negative) result.push_back('-');
    std::reverse(result.begin(), result.end());
    return result;
}


inline std::ostream& operator<<(std::ostream& os, __int128 value) {
    return os << to_string(value);
}

class SimpleParameterOptimizer {
public:
    // メイン最適化実行（シンプルなランダムサーチ）
    static OptimResult optimize(const Player players[2], uint64_t seed,
                               const int actions[350], int maxTests = 50, int turns = 0);

    static double getActionCost(int action);

    // ★追加: パラメータセットをテスト（turn と enemyHp を参照で返す）
    static void testParameters(
    const Player players[2],
    uint64_t seed,
    const int actions[350],
    int turns,
    uint64_t &outTurn,
    int &outEnemyHp,
    int &outherb
);

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


#endif // SIMPLE_PARAMETER_OPTIMIZER_H

