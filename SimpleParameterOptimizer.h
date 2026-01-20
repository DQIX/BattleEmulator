#ifndef SIMPLE_PARAMETER_OPTIMIZER_H
#define SIMPLE_PARAMETER_OPTIMIZER_H

#include "BattleEmulator.h"
#include <array>
#include <vector>
#include <functional>
#include <random>
#include <future>

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

class SimpleParameterOptimizer {
public:
    static constexpr int ids = 24;

    struct Result {
        int index = 0;
        double score = 0;
        std::vector<double> genes;
    };

    using Evaluator = std::function<double(const std::vector<double>& genes, uint64_t seed)>;

    SimpleParameterOptimizer(
        Evaluator evaluator,
        uint64_t seed,
        int lambda = 32,
        int mu = 8,
        int threads = 8
    );

    Result run(int generations);


    static void printGenome(const std::vector<double>& genes);

private:
    Evaluator evaluator_;
    std::mt19937_64 rng_;
    int lambda_;
    int mu_;
    int threads_;

    std::vector<double> mean_;
    std::vector<double> sigma_;

    double sampleNormal(double mean, double sigma);
};

#endif
#endif