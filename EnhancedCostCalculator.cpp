//
// Enhanced Cost Calculator Implementation
//

#include "EnhancedCostCalculator.h"
#include <array>
#include <cassert>

#include "SimpleParameterOptimizer.h"


thread_local EnhancedCostCalculator::CostTable g_costTable =
    EnhancedCostCalculator::CostTable::TableA;

EnhancedCostCalculator::CostTable
EnhancedCostCalculator::getCostTable() noexcept {
    return g_costTable;
}


#if defined(OPTIMIZE_MODE)

#include "SimpleParameterOptimizer.h"

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost, uint64_t NowState) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + getActionCost(SimpleParameterOptimizerNode::turnHeignt);

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);

    uint8_t state = NowState & 0xf;
    if(state == BattleEmulator::TYPE_2A){
        gCost += getActionCost(SimpleParameterOptimizerNode::TYPE_2AWeight);
    }else if(state == BattleEmulator::TYPE_2B){
        gCost += getActionCost(SimpleParameterOptimizerNode::TYPE_2BWeight);
    }else if(state == BattleEmulator::TYPE_2C){
        gCost += getActionCost(SimpleParameterOptimizerNode::TYPE_2CWeight);
    }else if(state == BattleEmulator::TYPE_2D){
        gCost += getActionCost(SimpleParameterOptimizerNode::TYPE_2DWeight);
    }else if(state == BattleEmulator::TYPE_2E){
        gCost += getActionCost(SimpleParameterOptimizerNode::TYPE_2EWeight);
    }


    return gCost;
}

double EnhancedCostCalculator::calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp, uint64_t NowState) {
    if (genome.EnemyPlayer.hp <= 0) {
        return 0.0; // Goal reached
    }

    double hCost = 0.0;

    // Primary heuristic: enemy HP ratio (scaled down for better granularity)
    hCost = (genome.EnemyPlayer.hp / enemyMaxHp) * getActionCost(SimpleParameterOptimizerNode::enemyHpWeight);

    // Player HP consideration (more granular than original)
    double playerHpRatio = genome.AllyPlayer.hp / playerMaxHp;
    hCost += (1.0 - playerHpRatio) * getActionCost(SimpleParameterOptimizerNode::playerHpWeight);

    // MP consideration (resource management)
    hCost += calculateResourceCost(genome) * getActionCost(SimpleParameterOptimizerNode::resourceWeight);

    // Status effect penalties/bonuses
    hCost += calculateStatusEffectCost(genome) * getActionCost(SimpleParameterOptimizerNode::StatusEffectWeight);

    return hCost;
}

double EnhancedCostCalculator::getActionCost(int action) {
   return SimpleParameterOptimizer::getActionCost(action);
}

double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // Negative status effects (penalties)
    if (genome.AllyPlayer.paralysis) statusCost += getActionCost(SimpleParameterOptimizerNode::paralysisWeight);
    if (genome.AllyPlayer.sleeping) statusCost += getActionCost(SimpleParameterOptimizerNode::sleepWeight);

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * getActionCost(SimpleParameterOptimizerNode::BuffWeight);
    statusCost -= genome.AllyPlayer.AtkBuffLevel * getActionCost(SimpleParameterOptimizerNode::AtkBuffWeight);
    statusCost -= genome.AllyPlayer.TensionLevel * getActionCost(SimpleParameterOptimizerNode::TensionWeight);

    // Special abilities
    //if (genome.AllyPlayer.acrobaticStar) statusCost -= getActionCost(SimpleParameterOptimizerNode::ActHeight);
    if (genome.AllyPlayer.specialCharge) statusCost -= getActionCost(SimpleParameterOptimizerNode::SpHeight);
    if (genome.AllyPlayer.hasMagicMirror) statusCost -= getActionCost(SimpleParameterOptimizerNode::hasMagicMirrorHeight);

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * getActionCost(SimpleParameterOptimizerNode::ResourceHPCost); // Penalty for low MP
    }

    resourceCost += (3 - genome.AllyPlayer.SpecialMedicineCount) * getActionCost(SimpleParameterOptimizerNode::SpecialMedicineCost);
    resourceCost += (2 - genome.AllyPlayer.ElfinElixirCount) * getActionCost(SimpleParameterOptimizerNode::ElfinElixirCost);

    return resourceCost;
}

#else
static constexpr std::array<double, 201> GENOME_A = {
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 2.59815,
        0.0,0.0,
        /* 30 */ 0.991185,
        /* 31 */ 0.877488,
        /* 32 */ 1.47669,
        /* 33 */ -0.467152,
        /* 34 */ -0.560149,
        0.0,
        /* 36 */ 3.82519,
        /* 37 */ 3.11601,
        /* 38 */ 3.48204,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 48 */ 0.225563,
        0.0,
        /* 50 */ 2.42553,
        0.0,
        /* 52 */ 2.55465,
        /* 53 */ 3.02135,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -0.685323,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 1.25479,
        /* 151 */ 0.943889,
        /* 152 */ 1.12736,
        /* 153 */ 2.52265,
        /* 154 */ 0.871853,
        /* 155 */ -1.38709,
        /* 156 */ 2.27589,
        /* 157 */ -0.53187,
        /* 158 */ 2.16594,
        /* 159 */ -0.206523,
        /* 160 */ -1.6591,
        /* 161 */ 2.01669,
        /* 162 */ 2.68847,
        /* 163 */ 1.43298,
        /* 164 */ 1.34218,
        /* 165 */ 0.985288,
        /* 166 */ -0.464629,
        /* 167 */ 0.311034,
        0.0,
        /* 169 */ 1.87985,
        /* 170 */ 4.16472,
        /* 171 */ 0.326611,
        /* 172 */ 0.0543113,
        /* 173 */ 0.267389,
        /* 174 */ -0.604447,
        /* 175 */ -0.428284,
        /* 176 */ 1.09807,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };

static constexpr std::array<double, 201> GENOME_B = {
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 5.84353,
        0.0,0.0,
        /* 30 */ 0.568706,
        /* 31 */ -0.892922,
        /* 32 */ 3.42785,
        /* 33 */ -0.192975,
        /* 34 */ -2.45272,
        0.0,
        /* 36 */ 2.37771,
        /* 37 */ 0.377641,
        /* 38 */ 4.21265,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 48 */ -2.44981,
        0.0,
        /* 50 */ 3.97609,
        0.0,
        /* 52 */ 3.22608,
        /* 53 */ 3.82255,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ 0.158835,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 1.25687,
        /* 151 */ 0.226153,
        /* 152 */ 1.6254,
        /* 153 */ 6.29028,
        /* 154 */ 1.53847,
        /* 155 */ 3.87233,
        /* 156 */ 2.55513,
        /* 157 */ -0.229176,
        /* 158 */ 1.77744,
        /* 159 */ -5.75963,
        /* 160 */ 1.81324,
        /* 161 */ 2.44924,
        /* 162 */ 2.82166,
        /* 163 */ 1.27783,
        /* 164 */ -0.40471,
        /* 165 */ 1.97589,
        /* 166 */ 2.78148,
        /* 167 */ -1.04918,
        0.0,
        /* 169 */ 2.63692,
        /* 170 */ 5.04647,
        /* 171 */ 1.05955,
        /* 172 */ 5.37454,
        /* 173 */ 4.2043,
        /* 174 */ -0.475724,
        /* 175 */ 0.857534,
        /* 176 */ 1.94189,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };
static constexpr std::array<double, 201> GENOME_C = {
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 3.36654,
        0.0,0.0,
        /* 30 */ 1.26092,
        /* 31 */ -0.883415,
        /* 32 */ 0.604913,
        /* 33 */ 1.56912,
        /* 34 */ -1.7205,
        0.0,
        /* 36 */ 4.16967,
        /* 37 */ -0.496227,
        /* 38 */ 5.97312,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 48 */ 2.12757,
        0.0,
        /* 50 */ 1.03431,
        0.0,
        /* 52 */ 0.159572,
        /* 53 */ 2.84813,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ 0.57405,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 2.43414,
        /* 151 */ -1.36289,
        /* 152 */ -1.20132,
        /* 153 */ 5.55961,
        /* 154 */ -0.575067,
        /* 155 */ 2.43473,
        /* 156 */ -3.26634,
        /* 157 */ 1.14277,
        /* 158 */ 0.0358613,
        /* 159 */ 2.47734,
        /* 160 */ 0.583487,
        /* 161 */ 2.92376,
        /* 162 */ 3.34124,
        /* 163 */ 2.97214,
        /* 164 */ -2.22052,
        /* 165 */ -1.41307,
        /* 166 */ -1.28615,
        /* 167 */ 3.2219,
        0.0,
        /* 169 */ 1.02857,
        /* 170 */ 3.23729,
        /* 171 */ 0.751912,
        /* 172 */ 5.02166,
        /* 173 */ 2.13598,
        /* 174 */ -2.08484,
        /* 175 */ 0.704511,
        /* 176 */ 0.985215,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };

constexpr std::array<double, 201> GENOME_D = {
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 0.93932,
        0.0,0.0,
        /* 30 */ 2.434,
        /* 31 */ -1.91128,
        /* 32 */ 4.47898,
        /* 33 */ -2.57949,
        /* 34 */ 0.743278,
        0.0,
        /* 36 */ 0.977298,
        /* 37 */ 1.65166,
        /* 38 */ 4.12193,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 48 */ 2.33098,
        0.0,
        /* 50 */ -1.18916,
        0.0,
        /* 52 */ 0.841975,
        /* 53 */ 2.31697,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -0.246927,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 0.848033,
        /* 151 */ 1.06417,
        /* 152 */ 3.90284,
        /* 153 */ -1.1343,
        /* 154 */ 0.438199,
        /* 155 */ 1.86857,
        /* 156 */ -1.32214,
        /* 157 */ -1.52786,
        /* 158 */ 0.68191,
        /* 159 */ 3.21787,
        /* 160 */ -0.441893,
        /* 161 */ -1.76501,
        /* 162 */ -1.48211,
        /* 163 */ -0.254381,
        /* 164 */ -0.498795,
        /* 165 */ 3.56129,
        /* 166 */ 1.02365,
        /* 167 */ 2.02183,
        0.0,
        /* 169 */ -3.16709,
        /* 170 */ -1.73203,
        /* 171 */ 3.57121,
        /* 172 */ 6.00524,
        /* 173 */ 0.777189,
        /* 174 */ -0.650682,
        /* 175 */ 2.35656,
        /* 176 */ 4.48436,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };

constexpr std::array<double, 201> GENOME_F = {
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 3.76715,
        0.0,0.0,
        /* 30 */ 1.66387,
        /* 31 */ -1.14351,
        /* 32 */ 4.07865,
        /* 33 */ -0.733694,
        /* 34 */ -0.407631,
        0.0,
        /* 36 */ 3.22924,
        /* 37 */ 3.59789,
        /* 38 */ 5.60411,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 48 */ -2.42505,
        0.0,
        /* 50 */ -1.84608,
        0.0,
        /* 52 */ 3.29182,
        /* 53 */ 3.93574,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -0.572629,
        /* 63 */ 0.27563,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 0.716256,
        /* 151 */ 1.43863,
        /* 152 */ 5.64488,
        /* 153 */ 5.09196,
        /* 154 */ 0.364928,
        /* 155 */ -0.332921,
        /* 156 */ 2.41571,
        /* 157 */ -0.89724,
        /* 158 */ 0.938336,
        /* 159 */ 2.05657,
        /* 160 */ 0.792228,
        /* 161 */ 2.22774,
        /* 162 */ 1.98082,
        /* 163 */ 1.88262,
        /* 164 */ 0.158019,
        /* 165 */ 2.52299,
        /* 166 */ -1.68663,
        /* 167 */ 2.59963,
        0.0,
        /* 169 */ 3.38596,
        /* 170 */ 4.30025,
        /* 171 */ 1.87538,
        /* 172 */ 1.75347,
        /* 173 */ 3.29224,
        /* 174 */ 0.285585,
        /* 175 */ -0.29682,
        /* 176 */ 1.73841,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };

constexpr std::array<double, 201> GENOME_G = {
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 27 */ 1.95919,
            0.0,0.0,
            /* 30 */ 1.04172,
            /* 31 */ -0.854512,
            /* 32 */ 3.14438,
            /* 33 */ -1.10988,
            /* 34 */ -1.7599,
            0.0,
            /* 36 */ 0.987072,
            /* 37 */ 2.2417,
            /* 38 */ 2.91844,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 48 */ 1.41844,
            0.0,
            /* 50 */ 1.76047,
            0.0,
            /* 52 */ 1.03486,
            /* 53 */ 0.774439,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 62 */ -2.88417,
            /* 63 */ 1.47202,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 150 */ 1.66783,
            /* 151 */ 3.95068,
            /* 152 */ 1.76343,
            /* 153 */ 1.21578,
            /* 154 */ 0.318604,
            /* 155 */ -0.1495,
            /* 156 */ 2.35108,
            /* 157 */ 1.26936,
            /* 158 */ 1.98461,
            /* 159 */ 2.58618,
            /* 160 */ -1.81943,
            /* 161 */ 1.77889,
            /* 162 */ 1.56903,
            /* 163 */ 3.11588,
            /* 164 */ 0.75967,
            /* 165 */ 2.93885,
            /* 166 */ -1.07542,
            /* 167 */ -0.642895,
            0.0,
            /* 169 */ 0.59579,
            /* 170 */ 2.36429,
            /* 171 */ -0.858895,
            /* 172 */ 2.78332,
            /* 173 */ 0.462108,
            /* 174 */ -0.185992,
            /* 175 */ 1.30107,
            /* 176 */ 2.30069,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
        };




// ---- staticメンバの実体 ----
const double* EnhancedCostCalculator::s_genome = GENOME_A.data();

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost, uint64_t NowState) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + s_genome[SimpleParameterOptimizerNode::turnHeignt];

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);

    uint8_t state = NowState & 0xf;
    if(state == BattleEmulator::TYPE_2A){
        gCost += s_genome[SimpleParameterOptimizerNode::TYPE_2AWeight];
    }else if(state == BattleEmulator::TYPE_2B){
        gCost += s_genome[SimpleParameterOptimizerNode::TYPE_2BWeight];
    }else if(state == BattleEmulator::TYPE_2C){
        gCost += s_genome[SimpleParameterOptimizerNode::TYPE_2CWeight];
    }else if(state == BattleEmulator::TYPE_2D){
        gCost += s_genome[SimpleParameterOptimizerNode::TYPE_2DWeight];
    }else if(state == BattleEmulator::TYPE_2E){
        gCost += s_genome[SimpleParameterOptimizerNode::TYPE_2EWeight];
    }

    return gCost;
}

double EnhancedCostCalculator::calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp, uint64_t NowState) {
    if (genome.EnemyPlayer.hp <= 0) {
        return 0.0; // Goal reached
    }

    double hCost = 0.0;

    // Primary heuristic: enemy HP ratio (scaled down for better granularity)
    hCost = (genome.EnemyPlayer.hp / enemyMaxHp) * s_genome[SimpleParameterOptimizerNode::enemyHpWeight];

    // Player HP consideration (more granular than original)
    double playerHpRatio = genome.AllyPlayer.hp / playerMaxHp;
    hCost += (1.0 - playerHpRatio) * s_genome[SimpleParameterOptimizerNode::playerHpWeight];

    // MP consideration (resource management)
    hCost += calculateResourceCost(genome) * s_genome[SimpleParameterOptimizerNode::resourceWeight];

    // Status effect penalties/bonuses
    hCost += calculateStatusEffectCost(genome) * s_genome[SimpleParameterOptimizerNode::StatusEffectWeight];

    return hCost;
}
void EnhancedCostCalculator::setCostTable(CostTable table) {
    g_costTable = table;
    if (table == CostTable::TableA)      s_genome = GENOME_A.data();
    else if (table == CostTable::TableB) s_genome = GENOME_B.data();
    else if (table == CostTable::TableC) s_genome = GENOME_C.data();
    else if (table == CostTable::TableD) s_genome = GENOME_D.data();
    else if (table == CostTable::TableF) s_genome = GENOME_F.data();
    else if (table == CostTable::TableG) s_genome = GENOME_G.data();
    else                                 assert(false);
}
double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // Negative status effects (penalties)
    if (genome.AllyPlayer.paralysis) statusCost += s_genome[SimpleParameterOptimizerNode::paralysisWeight];
    if (genome.AllyPlayer.sleeping) statusCost += s_genome[SimpleParameterOptimizerNode::sleepWeight];

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * s_genome[SimpleParameterOptimizerNode::BuffWeight];
    statusCost -= genome.AllyPlayer.AtkBuffLevel * s_genome[SimpleParameterOptimizerNode::AtkBuffWeight];
    statusCost -= genome.AllyPlayer.TensionLevel * s_genome[SimpleParameterOptimizerNode::TensionWeight];

    // Special abilities
   // if (genome.AllyPlayer.acrobaticStar) statusCost -= s_genome[SimpleParameterOptimizerNode::SpHeight];
    if (genome.AllyPlayer.specialCharge) statusCost -= s_genome[SimpleParameterOptimizerNode::SpHeight];
    if (genome.AllyPlayer.hasMagicMirror) statusCost -= s_genome[SimpleParameterOptimizerNode::hasMagicMirrorHeight];

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * s_genome[SimpleParameterOptimizerNode::ResourceHPCost]; // Penalty for low MP
    }

    resourceCost += (3 - genome.AllyPlayer.SpecialMedicineCount) * s_genome[SimpleParameterOptimizerNode::SpecialMedicineCost];
    resourceCost += (2 - genome.AllyPlayer.ElfinElixirCount) * s_genome[SimpleParameterOptimizerNode::ElfinElixirCost];

    return resourceCost;
}

#endif