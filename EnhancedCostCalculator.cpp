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
double EnhancedCostCalculator::getActionCost(int action) {
   return SimpleParameterOptimizer::getActionCost(action);
}
#else
constexpr std::array<double, 201> GENOME_A = {
    /* 0 */ -1.01287,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 27 */ 2.86852,
    0.0,0.0,
    /* 30 */ -0.387348,
    /* 31 */ -2.75651,
    /* 32 */ 2.36487,
    /* 33 */ 1.76129,
    /* 34 */ -2.2479,
    0.0,
    /* 36 */ 1.48351,
    /* 37 */ -1.08621,
    /* 38 */ 2.73616,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 49 */ 1.91957,
    /* 50 */ -0.424166,
    0.0,
    /* 52 */ 0.38546,
    /* 53 */ 0.418896,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 62 */ 2.01644,
    /* 63 */ -1.2238,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 150 */ 0.554627,
    /* 151 */ 0.469539,
    /* 152 */ 2.73929,
    /* 153 */ 2.17895,
    /* 154 */ 1.5636,
    0.0,0.0,0.0,0.0,
    /* 159 */ 0.899292,
    /* 160 */ 4.14048,
    /* 161 */ 2.74627,
    /* 162 */ -0.0905484,
    /* 163 */ -2.37039,
    /* 164 */ 0.145178,
    /* 165 */ -0.371991,
    0.0,
    /* 167 */ -0.463776,
    0.0,
    /* 169 */ -1.95986,
    /* 170 */ -0.0827653,
    /* 171 */ 2.22466,
    /* 172 */ 3.86618,
    /* 173 */ -1.71927,
    /* 174 */ -0.995901,
    0.0,0.0,0.0,
    /* 178 */ 5.81339,
    /* 179 */ 0.552908,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
};

constexpr std::array<double, 201> GENOME_B = {
    /* 0 */ 0.144745,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 27 */ 4.09679,
    0.0,0.0,
    /* 30 */ -0.64395,
    /* 31 */ 0.0284431,
    /* 32 */ 1.20496,
    /* 33 */ -0.889254,
    /* 34 */ -3.50757,
    0.0,
    /* 36 */ 2.389,
    /* 37 */ 1.56603,
    /* 38 */ 3.94626,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 48 */ 1.25606,
    0.0,
    /* 50 */ 2.19545,
    0.0,
    /* 52 */ 2.88147,
    /* 53 */ 2.07547,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 62 */ -3.31974,
    /* 63 */ -2.11009,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 150 */ 3.36576,
    /* 151 */ 3.90971,
    /* 152 */ 0.0104026,
    /* 153 */ 2.18273,
    /* 154 */ 0.115592,
    /* 155 */ 3.47843,
    /* 156 */ 2.20653,
    /* 157 */ 7.17149,
    /* 158 */ -0.881866,
    /* 159 */ 3.23757,
    /* 160 */ 3.99468,
    /* 161 */ 1.15513,
    /* 162 */ 0.249456,
    /* 163 */ -0.669774,
    /* 164 */ 1.38472,
    /* 165 */ 1.18339,
    /* 166 */ 3.9215,
    /* 167 */ 1.15014,
    0.0,
    /* 169 */ 0.901951,
    /* 170 */ 2.67189,
    /* 171 */ 2.69712,
    /* 172 */ -0.535811,
    /* 173 */ -0.192197,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
};

static constexpr std::array<double, 201> GENOME_C = {
    /* 0 */ 0.883435,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 27 */ 1.90032,
    0.0,0.0,
    /* 30 */ 3.63373,
    /* 31 */ 2.43815,
    /* 32 */ 2.62736,
    /* 33 */ -2.18701,
    /* 34 */ -3.35431,
    0.0,
    /* 36 */ 1.24389,
    /* 37 */ -0.608143,
    /* 38 */ 2.87326,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 49 */ 3.34441,
    /* 50 */ 4.62613,
    0.0,
    /* 52 */ 4.63183,
    /* 53 */ 1.41338,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 62 */ -3.53327,
    /* 63 */ -0.82691,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 150 */ 2.7509,
    /* 151 */ 2.54694,
    /* 152 */ -2.27277,
    /* 153 */ 7.18377,
    /* 154 */ 0.747387,
    /* 155 */ 0.462895,
    /* 156 */ 0.67231,
    /* 157 */ 1.98169,
    /* 158 */ 3.57228,
    /* 159 */ 0.516535,
    /* 160 */ -2.67877,
    /* 161 */ 2.34843,
    /* 162 */ 1.8637,
    /* 163 */ -0.950248,
    /* 164 */ -1.32889,
    /* 165 */ 1.05928,
    /* 166 */ 3.90033,
    /* 167 */ 3.85291,
    0.0,
    /* 169 */ 3.08366,
    /* 170 */ 6.33003,
    /* 171 */ 3.60103,
    /* 172 */ -1.44897,
    /* 173 */ -1.52911,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
};


constexpr std::array<double, 201> GENOME_D = {
    /* 0 */ 0.701874,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 27 */ 3.85068,
    0.0,0.0,
    /* 30 */ 3.63216,
    /* 31 */ 1.25096,
    /* 32 */ 1.71544,
    /* 33 */ -1.11638,
    /* 34 */ -2.8716,
    0.0,
    /* 36 */ 4.79603,
    /* 37 */ 6.38621,
    /* 38 */ 3.50795,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 48 */ 2.31427,
    0.0,
    /* 50 */ 3.68427,
    0.0,
    /* 52 */ 5.60245,
    /* 53 */ 1.36914,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 62 */ -4.96643,
    /* 63 */ -1.09075,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 150 */ 4.19404,
    /* 151 */ 1.18002,
    /* 152 */ -2.17712,
    /* 153 */ 2.27636,
    /* 154 */ 0.0199251,
    /* 155 */ -0.856267,
    /* 156 */ 0.526915,
    /* 157 */ -0.269101,
    /* 158 */ -2.72068,
    /* 159 */ -4.15459,
    /* 160 */ 2.11703,
    /* 161 */ 3.75437,
    /* 162 */ 2.06633,
    /* 163 */ 3.77555,
    /* 164 */ -0.900498,
    /* 165 */ -1.70778,
    /* 166 */ 10.6864,
    /* 167 */ 0.730768,
    0.0,
    /* 169 */ 2.28044,
    /* 170 */ 1.33339,
    /* 171 */ -0.438291,
    /* 172 */ -1.07553,
    /* 173 */ -0.624129,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
};
constexpr std::array<double, 201> GENOME_F = {
    /* 0 */ 1.73859,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 27 */ 2.73595,
    0.0,0.0,
    /* 30 */ 4.32682,
    /* 31 */ -1.51662,
    /* 32 */ 1.66687,
    /* 33 */ -3.61032,
    /* 34 */ -3.40824,
    0.0,
    /* 36 */ 1.96138,
    /* 37 */ 0.448671,
    /* 38 */ 3.38219,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 48 */ -3.2837,
    0.0,
    /* 50 */ 1.76383,
    0.0,
    /* 52 */ 5.07954,
    /* 53 */ 3.55673,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 62 */ -6.79686,
    /* 63 */ 3.29165,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 150 */ 3.30788,
    /* 151 */ 2.15647,
    /* 152 */ -7.35622,
    /* 153 */ 2.88193,
    /* 154 */ 0.588713,
    /* 155 */ -0.309771,
    /* 156 */ 2.20964,
    /* 157 */ -0.227928,
    /* 158 */ 2.61822,
    /* 159 */ -3.26067,
    /* 160 */ 2.31486,
    /* 161 */ 6.77255,
    /* 162 */ -0.511489,
    /* 163 */ 4.49404,
    /* 164 */ 0.696592,
    /* 165 */ 2.44394,
    /* 166 */ 1.22484,
    /* 167 */ 0.952727,
    0.0,
    /* 169 */ 5.21268,
    /* 170 */ 1.83525,
    /* 171 */ -1.0995,
    /* 172 */ 1.58805,
    /* 173 */ 0.694581,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
};
constexpr std::array<double, 201> GENOME_G = {
        /* 0 */ -0.315732,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 1.35492,
        0.0,0.0,
        /* 30 */ 4.33191,
        /* 31 */ -1.34831,
        /* 32 */ 4.5635,
        /* 33 */ -2.72391,
        /* 34 */ -4.47188,
        0.0,
        /* 36 */ 7.17104,
        /* 37 */ 5.88128,
        /* 38 */ 0.81413,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 48 */ 2.2989,
        0.0,
        /* 50 */ -1.79337,
        0.0,
        /* 52 */ 2.82377,
        /* 53 */ 1.7751,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -6.34129,
        /* 63 */ 1.22501,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 3.21018,
        /* 151 */ 1.35744,
        /* 152 */ -5.79485,
        /* 153 */ -1.82552,
        /* 154 */ -0.331819,
        /* 155 */ -0.438349,
        /* 156 */ -0.27816,
        /* 157 */ 3.81926,
        /* 158 */ -0.0839182,
        /* 159 */ 1.07364,
        /* 160 */ 2.1004,
        /* 161 */ 0.805033,
        /* 162 */ -1.25089,
        /* 163 */ -0.163345,
        /* 164 */ -0.451641,
        /* 165 */ 2.13839,
        /* 166 */ -2.37315,
        /* 167 */ 1.89711,
        0.0,
        /* 169 */ -2.70672,
        /* 170 */ -2.61203,
        /* 171 */ 4.00116,
        /* 172 */ 2.5123,
        /* 173 */ 2.56805,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
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
#endif

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

double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // Negative status effects (penalties)
    //if (genome.AllyPlayer.paralysis) statusCost += getActionCost(SimpleParameterOptimizerNode::paralysisWeight);
    //if (genome.AllyPlayer.sleeping) statusCost += getActionCost(SimpleParameterOptimizerNode::sleepWeight);

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * getActionCost(SimpleParameterOptimizerNode::BuffWeight);
    statusCost -= genome.AllyPlayer.AtkBuffLevel * getActionCost(SimpleParameterOptimizerNode::AtkBuffWeight);
    statusCost -= genome.AllyPlayer.TensionLevel * getActionCost(SimpleParameterOptimizerNode::TensionWeight);
    statusCost += genome.EnemyPlayer.BuffLevel * getActionCost(SimpleParameterOptimizerNode::KABUFFWeight);
    statusCost += genome.EnemyPlayer.BarrierLevel * getActionCost(SimpleParameterOptimizerNode::BarrierWeight);
    //statusCost += genome.AllyPlayer.SpeedLevel * getActionCost(SimpleParameterOptimizerNode::SpeedLevelWeight);

    // Special abilities
   // if (genome.AllyPlayer.acrobaticStar) statusCost -= getActionCost(SimpleParameterOptimizerNode::SpHeight);
    if (genome.AllyPlayer.specialCharge) statusCost -= getActionCost(SimpleParameterOptimizerNode::SpHeight);
    if (genome.AllyPlayer.hasMagicMirror) statusCost -= getActionCost(SimpleParameterOptimizerNode::hasMagicMirrorHeight);
    //if (genome.AllyPlayer.inactive) statusCost += getActionCost(SimpleParameterOptimizerNode::inactiveWeight);

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * getActionCost(SimpleParameterOptimizerNode::ResourceHPCost); // Penalty for low MP
    }

    //resourceCost += (3 - genome.AllyPlayer.SpecialMedicineCount) * getActionCost(SimpleParameterOptimizerNode::SpecialMedicineCost);
    //resourceCost += (1 - genome.AllyPlayer.ElfinElixirCount) * getActionCost(SimpleParameterOptimizerNode::ElfinElixirCost);

    return resourceCost;
}
