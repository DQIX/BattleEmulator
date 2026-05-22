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
    if (genome.AllyPlayer.inactive) statusCost += getActionCost(SimpleParameterOptimizerNode::inactiveWeight);

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
        /* 27 */ 1.44634,
        0.0,0.0,
        /* 30 */ 2.19858,
        /* 31 */ 1.97243,
        /* 32 */ 1.83296,
        /* 33 */ 1.30137,
        /* 34 */ -2.04183,
        0.0,
        /* 36 */ 3.26881,
        /* 37 */ 2.5158,
        /* 38 */ 1.58599,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 48 */ 0.723857,
        0.0,
        /* 50 */ 0.506933,
        0.0,
        /* 52 */ -0.93145,
        /* 53 */ 0.438609,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -2.90599,
        /* 63 */ 1.31459,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 1.48479,
        /* 151 */ 4.42511,
        /* 152 */ 1.22207,
        /* 153 */ -0.101174,
        /* 154 */ 0.55695,
        /* 155 */ -0.921954,
        /* 156 */ -0.377077,
        /* 157 */ 3.15785,
        /* 158 */ 0.975878,
        /* 159 */ 2.00677,
        /* 160 */ -1.62438,
        /* 161 */ 2.03183,
        /* 162 */ 2.24301,
        /* 163 */ 0.0401756,
        /* 164 */ 2.47505,
        /* 165 */ 0.230408,
        /* 166 */ 1.6285,
        /* 167 */ 0.76883,
        0.0,
        /* 169 */ 1.48993,
        /* 170 */ -1.04217,
        /* 171 */ 1.0795,
        /* 172 */ -0.443277,
        /* 173 */ 0.422813,
        /* 174 */ -0.682929,
        /* 175 */ 2.67399,
        /* 176 */ 1.15515,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };

static constexpr std::array<double, 201> GENOME_B = {
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 27 */ 2.35514,
            0.0,0.0,
            /* 30 */ -2.96917,
            /* 31 */ 0.276771,
            /* 32 */ 3.71712,
            /* 33 */ 0.0936267,
            /* 34 */ 0.59174,
            0.0,
            /* 36 */ 2.88146,
            /* 37 */ 2.53392,
            /* 38 */ -0.387997,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 48 */ 2.51291,
            0.0,
            /* 50 */ 1.46386,
            0.0,
            /* 52 */ -2.70143,
            /* 53 */ 4.27297,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 62 */ 0.0140746,
            /* 63 */ 5.26118,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 150 */ 3.3873,
            /* 151 */ 2.14367,
            /* 152 */ 0.669748,
            /* 153 */ -1.1524,
            /* 154 */ -0.90958,
            /* 155 */ -0.577229,
            /* 156 */ 0.283205,
            /* 157 */ 4.96972,
            /* 158 */ 0.762833,
            /* 159 */ 1.22619,
            /* 160 */ -3.80104,
            /* 161 */ 3.7937,
            /* 162 */ -1.80275,
            /* 163 */ 0.112025,
            /* 164 */ 0.470727,
            /* 165 */ -1.77066,
            /* 166 */ 2.87882,
            /* 167 */ -1.81858,
            0.0,
            /* 169 */ 2.46854,
            /* 170 */ 3.57045,
            /* 171 */ 1.55484,
            /* 172 */ -3.06078,
            /* 173 */ -0.646282,
            /* 174 */ -0.202353,
            /* 175 */ 0.783356,
            /* 176 */ 0.375141,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
        };
static constexpr std::array<double, 201> GENOME_C = {
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 27 */ 2.18718,
            0.0,0.0,
            /* 30 */ 2.60716,
            /* 31 */ -0.414853,
            /* 32 */ 1.96083,
            /* 33 */ 0.0385558,
            /* 34 */ 0.0933649,
            0.0,
            /* 36 */ 0.0860173,
            /* 37 */ -0.770425,
            /* 38 */ 0.0865761,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 48 */ 1.4036,
            0.0,
            /* 50 */ 4.21218,
            0.0,
            /* 52 */ 2.45364,
            /* 53 */ 4.07274,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 62 */ -1.1737,
            /* 63 */ 1.32921,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 150 */ 0.979352,
            /* 151 */ -0.7567,
            /* 152 */ 0.0127842,
            /* 153 */ -2.00274,
            /* 154 */ 0.114903,
            /* 155 */ -0.0388035,
            /* 156 */ 0.0064223,
            /* 157 */ 1.34596,
            /* 158 */ -1.6786,
            /* 159 */ 4.24646,
            /* 160 */ 1.36985,
            /* 161 */ -0.115597,
            /* 162 */ 0.348578,
            /* 163 */ 1.56108,
            /* 164 */ -0.10539,
            /* 165 */ 0.0702611,
            /* 166 */ 1.9766,
            /* 167 */ -0.142493,
            0.0,
            /* 169 */ 0.7709,
            /* 170 */ 0.810069,
            /* 171 */ -0.405277,
            /* 172 */ 1.40336,
            /* 173 */ 3.50454,
            /* 174 */ 0.436189,
            /* 175 */ 1.30815,
            /* 176 */ -5.77906,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
        };

constexpr std::array<double, 201> GENOME_D = {
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 27 */ 0.646242,
            0.0,0.0,
            /* 30 */ -1.47359,
            /* 31 */ 1.15914,
            /* 32 */ -0.125389,
            /* 33 */ -1.88987,
            /* 34 */ 2.6463,
            0.0,
            /* 36 */ 2.93816,
            /* 37 */ 4.10472,
            /* 38 */ 1.06228,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 48 */ 1.83698,
            0.0,
            /* 50 */ -0.623756,
            0.0,
            /* 52 */ 0.453719,
            /* 53 */ -0.306311,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 62 */ -1.62099,
            /* 63 */ 4.43458,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 150 */ 4.59221,
            /* 151 */ -0.0170246,
            /* 152 */ -0.90862,
            /* 153 */ 2.57082,
            /* 154 */ 1.2477,
            /* 155 */ 0.904464,
            /* 156 */ 0.288216,
            /* 157 */ -1.86496,
            /* 158 */ 3.08558,
            /* 159 */ -1.37448,
            /* 160 */ 1.78965,
            /* 161 */ 1.32922,
            /* 162 */ 3.43129,
            /* 163 */ -1.57872,
            /* 164 */ 4.72102,
            /* 165 */ 2.2594,
            /* 166 */ 4.62065,
            /* 167 */ 3.74816,
            0.0,
            /* 169 */ -1.06573,
            /* 170 */ 3.31385,
            /* 171 */ 2.71389,
            /* 172 */ 0.561154,
            /* 173 */ 2.43046,
            /* 174 */ 1.87059,
            /* 175 */ -1.03536,
            /* 176 */ -0.90144,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
        };
constexpr std::array<double, 201> GENOME_F = {
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 27 */ 1.23846,
            0.0,0.0,
            /* 30 */ 0.988338,
            /* 31 */ 1.48165,
            /* 32 */ 1.17361,
            /* 33 */ -2.40899,
            /* 34 */ -0.158864,
            0.0,
            /* 36 */ -0.142818,
            /* 37 */ -0.445003,
            /* 38 */ -0.304991,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 48 */ -2.41239,
            0.0,
            /* 50 */ 0.853239,
            0.0,
            /* 52 */ -0.593377,
            /* 53 */ -0.54212,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 62 */ -1.18056,
            /* 63 */ 2.13409,
            0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
            /* 150 */ 1.78866,
            /* 151 */ 4.04682,
            /* 152 */ 2.70386,
            /* 153 */ 2.00308,
            /* 154 */ 0.886118,
            /* 155 */ 0.435275,
            /* 156 */ 4.8988,
            /* 157 */ 1.16678,
            /* 158 */ 2.23444,
            /* 159 */ 2.03592,
            /* 160 */ 1.47556,
            /* 161 */ 0.859254,
            /* 162 */ 1.01547,
            /* 163 */ 1.52536,
            /* 164 */ 0.661138,
            /* 165 */ 0.321179,
            /* 166 */ 2.23956,
            /* 167 */ -1.83272,
            0.0,
            /* 169 */ -0.0422385,
            /* 170 */ 0.329458,
            /* 171 */ 1.90618,
            /* 172 */ 0.862319,
            /* 173 */ 1.05443,
            /* 174 */ 0.336213,
            /* 175 */ -2.69694,
            /* 176 */ -0.0764819,
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
    if (genome.AllyPlayer.paralysis) statusCost += s_genome[SimpleParameterOptimizerNode::paralysisWeight];
    if (genome.AllyPlayer.sleeping) statusCost += s_genome[SimpleParameterOptimizerNode::sleepWeight];

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * s_genome[SimpleParameterOptimizerNode::BuffWeight];
    statusCost -= genome.AllyPlayer.AtkBuffLevel * s_genome[SimpleParameterOptimizerNode::AtkBuffWeight];
    statusCost -= genome.AllyPlayer.TensionLevel * s_genome[SimpleParameterOptimizerNode::TensionWeight];

    // Special abilities
   // if (genome.AllyPlayer.acrobaticStar) statusCost -= s_genome[SimpleParameterOptimizerNode::SpHeight];
    if (genome.AllyPlayer.specialCharge) statusCost -= s_genome[SimpleParameterOptimizerNode::SpHeight];
    if (genome.AllyPlayer.inactive) statusCost += s_genome[SimpleParameterOptimizerNode::inactiveWeight];

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