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


#if !defined(OPTIMIZE_MODE)
static constexpr std::array<double, 201> GENOME_A = {
    0.0,
        /* 1 */ 0.77747,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 23 */ 3.93667,
        0.0,
        /* 25 */ -0.761408,
        /* 26 */ 0.587669,
        /* 27 */ 3.97126,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 53 */ 2.99063,
        0.0,0.0,
        /* 56 */ -0.842511,
        0.0,0.0,
        /* 59 */ 0.458351,
        0.0,
        /* 61 */ -0.297694,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 69 */ 0.72351,
        /* 70 */ -4.21436,
        0.0,0.0,
        /* 73 */ -2.47033,
        /* 74 */ 1.85755,
        /* 75 */ 0.117154,
        /* 76 */ 3.49342,
        /* 77 */ 1.01271,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 0.366135,
        /* 151 */ 1.66591,
        /* 152 */ -1.56922,
        /* 153 */ -3.64048,
        /* 154 */ 0.586476,
        /* 155 */ 2.74124,
        0.0,0.0,
        /* 158 */ 1.38122,
        /* 159 */ 1.70237,
        /* 160 */ 2.87203,
        /* 161 */ -0.710775,
        /* 162 */ -2.47351,
        0.0,0.0,0.0,0.0,0.0,0.0,
        /* 169 */ -2.86228,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };
static constexpr std::array<double, 201> GENOME_B = {
    0.0,
        /* 1 */ 1.69289,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 23 */ 6.91275,
        0.0,
        /* 25 */ -1.18976,
        /* 26 */ 3.07756,
        /* 27 */ 4.99358,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 53 */ 6.31767,
        0.0,0.0,
        /* 56 */ -0.916455,
        0.0,0.0,
        /* 59 */ 0.492813,
        0.0,
        /* 61 */ 0.100632,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 69 */ 0.542394,
        /* 70 */ -4.99902,
        0.0,0.0,
        /* 73 */ 0.235066,
        /* 74 */ 1.39128,
        /* 75 */ 1.68973,
        /* 76 */ 1.97235,
        /* 77 */ 1.5055,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 0.109486,
        /* 151 */ 2.09766,
        /* 152 */ -1.67418,
        /* 153 */ -2.33381,
        /* 154 */ 1.14397,
        /* 155 */ 5.03781,
        0.0,0.0,
        /* 158 */ 0.534486,
        /* 159 */ 1.22647,
        /* 160 */ 4.5911,
        /* 161 */ -0.107089,
        /* 162 */ -1.80126,
        0.0,0.0,0.0,0.0,0.0,0.0,
        /* 169 */ -3.04552,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };

static constexpr std::array<double, 201> GENOME_C = {
        0.0,
            /* 1 */ -0.253984,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 23 */ 4.26005,
            0.0,
            /* 25 */ -2.78256,
            /* 26 */ 4.70359,
            /* 27 */ 4.89656,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 53 */ 2.45029,
            0.0,0.0,
            /* 56 */ -4.0056,
            0.0,0.0,
            /* 59 */ -0.873535,
            0.0,
            /* 61 */ -0.0407614,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 69 */ 1.07232,
            /* 70 */ -1.7872,
            0.0,0.0,
            /* 73 */ 1.18612,
            /* 74 */ 3.09315,
            /* 75 */ -0.397769,
            /* 76 */ 1.616,
            /* 77 */ 1.01712,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 150 */ 1.13876,
            /* 151 */ 0.787916,
            /* 152 */ 2.46691,
            /* 153 */ -2.64515,
            /* 154 */ 1.51308,
            /* 155 */ 1.19558,
            /* 156 */ 2.28977,
            /* 157 */ 2.41499,
            /* 158 */ 1.76048,
            /* 159 */ 0.312878,
            /* 160 */ 1.37537,
            /* 161 */ 0.662856,
            /* 162 */ 1.60807,
            0.0,0.0,0.0,0.0,0.0,0.0,
            /* 169 */ -5.5731,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
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

double EnhancedCostCalculator::getActionCost(int action) {
    return (action >= 0 && action < 201) ? s_genome[action] : 0.0;
}
#else
double EnhancedCostCalculator::getActionCost(int action) {
    return SimpleParameterOptimizer::getActionCost(action);
}
#endif

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
    (void) NowState;
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

double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // Negative status effects (penalties)
    if (genome.AllyPlayer.paralysis) statusCost += getActionCost(SimpleParameterOptimizerNode::paralysisWeight);
    if (genome.AllyPlayer.inactive) statusCost += getActionCost(SimpleParameterOptimizerNode::inactiveWeight);

    // Special abilities
    if (genome.AllyPlayer.acrobaticStar) statusCost -= getActionCost(SimpleParameterOptimizerNode::SpHeight);
    if (genome.AllyPlayer.specialCharge) statusCost -= getActionCost(SimpleParameterOptimizerNode::ActHeight);

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * getActionCost(SimpleParameterOptimizerNode::ResourceHPCost); // Penalty for low MP
    }

    resourceCost += (8 - genome.AllyPlayer.medicinal_herbs_count) * getActionCost(SimpleParameterOptimizerNode::SpecialMedicineCost);

    return resourceCost;
}
