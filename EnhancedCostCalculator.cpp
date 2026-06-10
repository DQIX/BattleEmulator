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
        /* 0 */ 1.1475,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 25 */ -0.0370009,
        /* 26 */ 1.08667,
        /* 27 */ 2.89689,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 50 */ -0.756621,
        0.0,0.0,
        /* 53 */ 3.73399,
        0.0,
        /* 55 */ 0.57513,
        /* 56 */ -1.05402,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 73 */ 0.373826,
        0.0,
        /* 75 */ -3.17316,
        /* 76 */ 0.220407,
        /* 77 */ -3.15261,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 85 */ 4.64605,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 1.54709,
        /* 151 */ -0.570389,
        /* 152 */ -3.23956,
        /* 153 */ 1.42151,
        /* 154 */ 0.724899,
        /* 155 */ 2.99589,
        /* 156 */ 5.4153,
        /* 157 */ 1.82886,
        /* 158 */ 4.79071,
        /* 159 */ 2.20497,
        /* 160 */ 0.0235097,
        /* 161 */ -0.539499,
        /* 162 */ 1.27263,
        0.0,0.0,0.0,0.0,0.0,0.0,
        /* 169 */ 4.16655,
        0.0,0.0,
        /* 172 */ 2.37851,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };
static constexpr std::array<double, 201> GENOME_B = {
    /* 0 */ 0.846385,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 25 */ 0.892488,
    /* 26 */ 3.04582,
    /* 27 */ 2.72497,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 50 */ 0.947566,
    0.0,0.0,
    /* 53 */ 1.51467,
    0.0,
    /* 55 */ -2.86533,
    /* 56 */ -0.676758,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 73 */ -1.01944,
    0.0,
    /* 75 */ -2.15242,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 85 */ 0.367564,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 150 */ 0.457679,
    /* 151 */ 2.40745,
    /* 152 */ -0.953929,
    /* 153 */ 2.29855,
    /* 154 */ 2.42855,
    /* 155 */ 3.87207,
    /* 156 */ 0.856065,
    /* 157 */ 9.17487,
    /* 158 */ 2.18095,
    /* 159 */ 1.05838,
    /* 160 */ 1.15279,
    /* 161 */ 3.90232,
    /* 162 */ 1.52802,
    0.0,0.0,0.0,0.0,0.0,0.0,
    /* 169 */ 2.48268,
    0.0,0.0,
    /* 172 */ -0.702352,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
};
static constexpr std::array<double, 201> GENOME_C = {
    /* 0 */ 2.47243,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 25 */ 0.969943,
    /* 26 */ 2.67324,
    /* 27 */ 4.38242,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 50 */ 2.38193,
    0.0,0.0,
    /* 53 */ 2.22519,
    0.0,
    /* 55 */ 1.32809,
    /* 56 */ -0.156309,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 73 */ 1.25027,
    0.0,
    /* 75 */ -1.03118,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 85 */ 1.30034,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 150 */ -0.694337,
    /* 151 */ 1.46452,
    /* 152 */ 0.203147,
    /* 153 */ 2.50742,
    /* 154 */ 1.08968,
    /* 155 */ 1.8319,
    /* 156 */ 3.81305,
    /* 157 */ 2.38744,
    /* 158 */ 1.21589,
    /* 159 */ 0.119457,
    /* 160 */ 0.593983,
    /* 161 */ 1.15822,
    /* 162 */ -0.184273,
    0.0,0.0,0.0,0.0,0.0,0.0,
    /* 169 */ 2.20231,
    0.0,0.0,
    /* 172 */ 1.41587,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
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
