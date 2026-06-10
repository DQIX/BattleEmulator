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

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost, uint64_t NowState,
                                              const int transitionEvents[8], int transitionEventCount) {
    (void) NowState;
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + getActionCost(SimpleParameterOptimizerNode::turnHeignt);

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);
    if (transitionEvents != nullptr) {
        for (int i = 0; i < transitionEventCount; ++i) {
            gCost += getActionCost(transitionEvents[i]);
        }
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
    //if (genome.AllyPlayer.paralysis) statusCost += getActionCost(SimpleParameterOptimizerNode::paralysisWeight);
    if (genome.AllyPlayer.sleeping) statusCost += getActionCost(SimpleParameterOptimizerNode::sleepWeight);
    if (genome.AllyPlayer.PoisonEnable) statusCost += getActionCost(SimpleParameterOptimizerNode::poisonWeight);

    statusCost += getActionCost(SimpleParameterOptimizerNode::speedLevelWeight) * genome.AllyPlayer.speedLevel;
    statusCost += getActionCost(SimpleParameterOptimizerNode::BuffWeight) * genome.AllyPlayer.BuffLevel;

    // Special abilities
    if (genome.AllyPlayer.acrobaticStar) statusCost -= getActionCost(SimpleParameterOptimizerNode::ActHeight);
    if (genome.AllyPlayer.specialCharge) statusCost -= getActionCost(SimpleParameterOptimizerNode::SpHeight);

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * getActionCost(SimpleParameterOptimizerNode::ResourceHPCost); // Penalty for low MP
    }

    resourceCost += (6 - genome.AllyPlayer.SpecialMedicineCount) * getActionCost(SimpleParameterOptimizerNode::SpecialMedicineCost);
    resourceCost += (2 - genome.AllyPlayer.SpecialAntidoteCount) * getActionCost(SimpleParameterOptimizerNode::SpecialAntiCost);

    return resourceCost;
}

#else
static constexpr std::array<double, 201> GENOME_A = {
    0.0,
        /* 1 */ 1.77919,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 23 */ 7.1119,
        0.0,
        /* 25 */ -0.600363,
        /* 26 */ 5.20363,
        /* 27 */ 8.2138,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 53 */ 6.91054,
        0.0,0.0,
        /* 56 */ 1.05663,
        0.0,0.0,
        /* 59 */ 1.38015,
        0.0,
        /* 61 */ 3.29117,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 69 */ 0.498307,
        /* 70 */ -5.33702,
        0.0,0.0,
        /* 73 */ -7.13347,
        /* 74 */ 2.0297,
        /* 75 */ 1.21955,
        /* 76 */ 8.91256,
        /* 77 */ -0.0847585,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ -0.178075,
        /* 151 */ 8.25281,
        /* 152 */ -3.81255,
        /* 153 */ 3.51966,
        /* 154 */ 2.56217,
        /* 155 */ 0.61116,
        /* 156 */ 6.21784,
        /* 157 */ -0.830143,
        /* 158 */ 9.43772,
        /* 159 */ 1.87433,
        /* 160 */ 1.48388,
        /* 161 */ 2.40294,
        /* 162 */ 5.7291,
        /* 163 */ -2.79765,
        0.0,0.0,0.0,0.0,0.0,
        /* 169 */ 2.56159,
        0.0,0.0,
        /* 172 */ 1.83742,
        /* 173 */ 0.842993,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };
static constexpr std::array<double, 201> GENOME_B = {
        0.0,
            /* 1 */ -0.920533,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 23 */ 4.83219,
            0.0,
            /* 25 */ -0.499387,
            /* 26 */ 2.5963,
            /* 27 */ 4.79708,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 53 */ 2.33004,
            0.0,0.0,
            /* 56 */ -1.85735,
            0.0,0.0,
            /* 59 */ 1.84811,
            0.0,
            /* 61 */ -0.692163,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 69 */ 1.47169,
            /* 70 */ -4.28693,
            0.0,0.0,
            /* 73 */ -0.429238,
            /* 74 */ 3.03127,
            /* 75 */ 1.27803,
            /* 76 */ 0.823324,
            /* 77 */ 0.363049,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 150 */ 1.03595,
            /* 151 */ 3.5537,
            /* 152 */ 0.694187,
            /* 153 */ -0.795435,
            /* 154 */ 1.78586,
            /* 155 */ -4.4749,
            /* 156 */ 1.2063,
            /* 157 */ 1.24704,
            /* 158 */ 0.742546,
            /* 159 */ 2.50921,
            /* 160 */ 0.580161,
            /* 161 */ -1.48419,
            /* 162 */ 4.09379,
            /* 163 */ 0.623297,
            0.0,0.0,0.0,0.0,0.0,
            /* 169 */ 2.77116,
            0.0,0.0,
            /* 172 */ -2.5996,
            /* 173 */ 2.80514,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
        };

static constexpr std::array<double, 201> GENOME_C = {
    0.0,
        /* 1 */ -0.199243,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 23 */ 7.13278,
        0.0,
        /* 25 */ -1.47138,
        /* 26 */ 3.4773,
        /* 27 */ 7.40181,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 53 */ 3.45499,
        0.0,0.0,
        /* 56 */ -1.63137,
        0.0,0.0,
        /* 59 */ 1.62175,
        0.0,
        /* 61 */ -1.27423,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 69 */ 1.34451,
        /* 70 */ -5.99235,
        0.0,0.0,
        /* 73 */ 11.1061,
        /* 74 */ 4.00036,
        /* 75 */ 2.4836,
        /* 76 */ 1.4873,
        /* 77 */ 1.11393,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 1.39759,
        /* 151 */ 9.07343,
        /* 152 */ 2.19577,
        /* 153 */ -1.8636,
        /* 154 */ 1.77267,
        /* 155 */ -1.28969,
        /* 156 */ 1.45554,
        /* 157 */ -0.314135,
        /* 158 */ -0.421166,
        /* 159 */ 3.79117,
        /* 160 */ 1.63471,
        /* 161 */ -2.1406,
        /* 162 */ 0.272961,
        /* 163 */ 1.91749,
        0.0,0.0,0.0,0.0,0.0,
        /* 169 */ 2.98098,
        0.0,0.0,
        /* 172 */ -4.60907,
        /* 173 */ 0.914692,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };
constexpr std::array<double, 201> GENOME_D = {
        0.0,
            /* 1 */ 0.878605,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 25 */ -0.871383,
            /* 26 */ 2.34553,
            /* 27 */ 2.35019,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 50 */ 1.75942,
            0.0,0.0,
            /* 53 */ -0.00639339,
            0.0,
            /* 55 */ 1.49051,
            /* 56 */ -2.17355,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 69 */ 0.286679,
            /* 70 */ -0.0169863,
            /* 71 */ 1.85946,
            /* 72 */ 0.0748831,
            /* 73 */ -1.11219,
            0.0,
            /* 75 */ -0.512978,
            /* 76 */ 2.45887,
            /* 77 */ -2.20108,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 85 */ 1.13831,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 150 */ 0.327276,
            /* 151 */ 2.11955,
            /* 152 */ 1.84897,
            /* 153 */ 1.26943,
            /* 154 */ 1.51128,
            /* 155 */ -0.676526,
            /* 156 */ 2.23354,
            /* 157 */ 0.0318492,
            /* 158 */ 3.26234,
            /* 159 */ 2.64259,
            /* 160 */ 4.42783,
            /* 161 */ 0.0635954,
            /* 162 */ -0.180735,
            0.0,0.0,0.0,0.0,0.0,0.0,
            /* 169 */ 2.02726,
            0.0,0.0,
            /* 172 */ 0.83003,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
        };

constexpr std::array<double, 201> GENOME_F = {
    0.0,
        /* 1 */ 0.396438,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 25 */ 0.559341,
        /* 26 */ 1.93204,
        /* 27 */ 2.67097,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 50 */ -0.153957,
        0.0,0.0,
        /* 53 */ 2.1306,
        0.0,
        /* 55 */ 1.44269,
        /* 56 */ -1.14623,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 69 */ 0.152017,
        /* 70 */ 1.27819,
        /* 71 */ 1.79423,
        /* 72 */ 2.19729,
        /* 73 */ 0.282103,
        0.0,
        /* 75 */ -0.932203,
        /* 76 */ 0.863046,
        /* 77 */ -2.45765,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 85 */ 7.07589,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ -0.31367,
        /* 151 */ 1.73954,
        /* 152 */ 1.24165,
        /* 153 */ 3.45572,
        /* 154 */ 1.31676,
        /* 155 */ -0.582028,
        /* 156 */ 2.9597,
        /* 157 */ 2.53464,
        /* 158 */ 0.0448177,
        /* 159 */ 0.276366,
        /* 160 */ 1.56664,
        /* 161 */ 0.714052,
        /* 162 */ 2.03655,
        0.0,0.0,0.0,0.0,0.0,0.0,
        /* 169 */ 1.01307,
        0.0,0.0,
        /* 172 */ 0.0114852,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };


constexpr std::array<double, 201> GENOME_G = {
    0.0,
        /* 1 */ 1.33233,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 25 */ -1.75638,
        /* 26 */ 3.34697,
        /* 27 */ 4.57948,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 50 */ 0.0479571,
        0.0,0.0,
        /* 53 */ 5.08153,
        0.0,
        /* 55 */ 3.56534,
        /* 56 */ -2.55736,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 69 */ 0.345593,
        /* 70 */ -0.937567,
        /* 71 */ 0.502517,
        /* 72 */ 3.27024,
        /* 73 */ 2.32732,
        0.0,
        /* 75 */ -2.02021,
        /* 76 */ 2.12069,
        /* 77 */ -5.40617,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 85 */ -0.157942,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 2.4123,
        /* 151 */ 2.85766,
        /* 152 */ 1.01206,
        /* 153 */ 1.09309,
        /* 154 */ 4.25519,
        /* 155 */ 3.4852,
        /* 156 */ -2.60095,
        /* 157 */ 2.71671,
        /* 158 */ -1.05661,
        /* 159 */ 1.90525,
        /* 160 */ 0.664278,
        /* 161 */ 0.0367449,
        /* 162 */ 0.201945,
        0.0,0.0,0.0,0.0,0.0,0.0,
        /* 169 */ -0.189723,
        0.0,0.0,
        /* 172 */ -1.10709,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };




// ---- staticメンバの実体 ----
const double* EnhancedCostCalculator::s_genome = GENOME_A.data();

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost, uint64_t NowState,
                                              const int transitionEvents[8], int transitionEventCount) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + s_genome[SimpleParameterOptimizerNode::turnHeignt];

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);
    if (transitionEvents != nullptr) {
        for (int i = 0; i < transitionEventCount; ++i) {
            gCost += getActionCost(transitionEvents[i]);
        }
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
    //if (genome.AllyPlayer.paralysis) statusCost += s_genome[SimpleParameterOptimizerNode::paralysisWeight];
    if (genome.AllyPlayer.sleeping) statusCost += s_genome[SimpleParameterOptimizerNode::sleepWeight];
    if (genome.AllyPlayer.PoisonEnable) statusCost += s_genome[SimpleParameterOptimizerNode::poisonWeight];

    statusCost += s_genome[SimpleParameterOptimizerNode::speedLevelWeight] * genome.AllyPlayer.speedLevel;
    statusCost += s_genome[SimpleParameterOptimizerNode::BuffWeight] * genome.AllyPlayer.BuffLevel;

    // Special abilities
    if (genome.AllyPlayer.acrobaticStar) statusCost -= s_genome[SimpleParameterOptimizerNode::ActHeight];
    if (genome.AllyPlayer.specialCharge) statusCost -= s_genome[SimpleParameterOptimizerNode::SpHeight];

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * s_genome[SimpleParameterOptimizerNode::ResourceHPCost]; // Penalty for low MP
    }

    resourceCost += (6 - genome.AllyPlayer.SpecialMedicineCount) * s_genome[SimpleParameterOptimizerNode::SpecialMedicineCost];
    resourceCost += (2 - genome.AllyPlayer.SpecialAntidoteCount) * s_genome[SimpleParameterOptimizerNode::SpecialAntiCost];

    return resourceCost;
}

#endif
