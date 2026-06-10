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
    (void) NowState;
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + getActionCost(SimpleParameterOptimizerNode::turnHeignt);

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);
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
        /* 0 */ 1.13705,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 25 */ -0.455175,
        /* 26 */ 2.7775,
        /* 27 */ 4.34221,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 50 */ 1.90127,
        0.0,0.0,
        /* 53 */ -0.0981912,
        0.0,
        /* 55 */ 1.31334,
        /* 56 */ -0.879251,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 73 */ 0.563695,
        0.0,
        /* 75 */ -0.537869,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 85 */ 0.816824,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 0.56253,
        /* 151 */ 3.61521,
        /* 152 */ 1.27676,
        /* 153 */ 1.90016,
        /* 154 */ 1.67882,
        /* 155 */ 0.18871,
        /* 156 */ -0.0876287,
        /* 157 */ 0.826711,
        /* 158 */ 2.10445,
        /* 159 */ 2.04741,
        /* 160 */ 1.76839,
        /* 161 */ 1.29972,
        /* 162 */ 2.37094,
        0.0,0.0,0.0,0.0,0.0,0.0,
        /* 169 */ 2.45706,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
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
        /* 27 */ 4.29349,
        0.0,0.0,
        /* 30 */ 2.30675,
        /* 31 */ -1.20484,
        /* 32 */ 1.61744,
        /* 33 */ -0.79034,
        /* 34 */ -2.07177,
        0.0,
        /* 36 */ 5.01678,
        /* 37 */ -0.128479,
        /* 38 */ 3.64306,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 48 */ -0.218467,
        0.0,
        /* 50 */ 1.09443,
        0.0,
        /* 52 */ -1.15721,
        /* 53 */ 1.25786,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -1.11696,
        /* 63 */ -0.41507,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 3.84854,
        /* 151 */ 2.90669,
        /* 152 */ 3.26863,
        /* 153 */ 2.6008,
        /* 154 */ 1.07879,
        /* 155 */ 1.95719,
        /* 156 */ 3.70074,
        /* 157 */ 0.72681,
        /* 158 */ -0.770388,
        /* 159 */ 0.498919,
        /* 160 */ 0.266122,
        /* 161 */ 3.21057,
        /* 162 */ 1.27974,
        /* 163 */ 0.420194,
        /* 164 */ 0.205753,
        /* 165 */ 0.89696,
        /* 166 */ -0.29768,
        /* 167 */ 2.2747,
        0.0,
        /* 169 */ 2.97905,
        /* 170 */ 3.86221,
        /* 171 */ 1.87479,
        /* 172 */ 2.0201,
        /* 173 */ 0.903868,
        /* 174 */ -2.24872,
        /* 175 */ -0.216961,
        /* 176 */ 1.68551,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };






// ---- staticメンバの実体 ----
const double* EnhancedCostCalculator::s_genome = GENOME_A.data();

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost, uint64_t NowState) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + s_genome[SimpleParameterOptimizerNode::turnHeignt];

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);

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