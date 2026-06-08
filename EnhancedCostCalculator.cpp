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

//#if defined(erugi1)
constexpr std::array<double, 201> GENOME_A = {
        /* 0 */ 0.433938,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 2.14188,
        0.0,0.0,
        /* 30 */ 1.27713,
        /* 31 */ 0.676042,
        /* 32 */ 1.8058,
        /* 33 */ -0.208296,
        /* 34 */ -1.84862,
        0.0,
        /* 36 */ 4.00539,
        /* 37 */ 0.905904,
        /* 38 */ 0.481592,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 47 */ 0.574827,
        0.0,0.0,
        /* 50 */ 4.77346,
        0.0,
        /* 52 */ 4.03307,
        /* 53 */ 2.48834,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -0.595208,
        /* 63 */ 1.46566,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 1.09829,
        /* 151 */ 2.09721,
        /* 152 */ -1.04741,
        /* 153 */ 1.54733,
        /* 154 */ 0.795068,
        /* 155 */ 2.77423,
        /* 156 */ -0.184946,
        /* 157 */ 0.453805,
        /* 158 */ 3.62471,
        /* 159 */ 1.41191,
        /* 160 */ 1.0964,
        /* 161 */ 0.191182,
        /* 162 */ 0.328019,
        /* 163 */ 1.19814,
        /* 164 */ 1.66605,
        /* 165 */ 0.823746,
        /* 166 */ -1.79176,
        /* 167 */ 1.57527,
        0.0,
        /* 169 */ -1.67289,
        0.0,
        /* 171 */ 0.180499,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };

static constexpr std::array<double, 201> GENOME_B = {
        /* 0 */ -1.03089,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 2.71059,
        0.0,0.0,
        /* 30 */ 1.49915,
        /* 31 */ 2.99199,
        /* 32 */ 1.25708,
        /* 33 */ 0.0718426,
        /* 34 */ -1.46428,
        0.0,
        /* 36 */ 3.83995,
        /* 37 */ 2.46221,
        /* 38 */ 0.250173,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 47 */ 2.49115,
        0.0,0.0,
        /* 50 */ 5.61919,
        0.0,
        /* 52 */ 0.407557,
        /* 53 */ 1.68142,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -1.20722,
        /* 63 */ 4.65717,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 2.31205,
        /* 151 */ 3.31241,
        /* 152 */ 1.49449,
        /* 153 */ -0.846162,
        /* 154 */ 0.991882,
        /* 155 */ 2.27595,
        /* 156 */ 0.894611,
        /* 157 */ 1.26943,
        /* 158 */ -0.469932,
        /* 159 */ 3.6703,
        /* 160 */ -0.256082,
        /* 161 */ 3.58628,
        /* 162 */ 1.58954,
        /* 163 */ 0.179567,
        /* 164 */ 0.74021,
        /* 165 */ 2.42001,
        /* 166 */ -0.658041,
        /* 167 */ -0.654058,
        0.0,
        /* 169 */ 2.69864,
        0.0,
        /* 171 */ 0.268892,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };
static constexpr std::array<double, 201> GENOME_C = {
        /* 0 */ -1.69101,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 9.53338,
        0.0,0.0,
        /* 30 */ 0.779183,
        /* 31 */ 0.683055,
        /* 32 */ 3.92797,
        /* 33 */ 0.883739,
        /* 34 */ -5.85931,
        0.0,
        /* 36 */ 0.346807,
        /* 37 */ -2.79434,
        /* 38 */ 1.44384,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 47 */ -1.4373,
        0.0,0.0,
        /* 50 */ 3.32923,
        0.0,
        /* 52 */ 5.40976,
        /* 53 */ 2.65918,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -2.73756,
        /* 63 */ 0.861946,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 3.97039,
        /* 151 */ 3.66098,
        /* 152 */ -2.20132,
        /* 153 */ 0.687269,
        /* 154 */ 0.63737,
        /* 155 */ 1.94848,
        /* 156 */ 3.3862,
        /* 157 */ 0.453639,
        /* 158 */ 1.95489,
        /* 159 */ -3.50504,
        /* 160 */ 0.375079,
        /* 161 */ 1.18656,
        /* 162 */ -0.769946,
        /* 163 */ 4.27766,
        /* 164 */ 2.13339,
        /* 165 */ 3.57341,
        /* 166 */ 4.86774,
        /* 167 */ -2.10101,
        0.0,
        /* 169 */ -2.53993,
        0.0,
        /* 171 */ 1.78565,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };

constexpr std::array<double, 201> GENOME_D = {
        /* 0 */ 0.278419,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 2.35558,
        0.0,0.0,
        /* 30 */ 5.50841,
        /* 31 */ 2.75971,
        /* 32 */ 3.81068,
        /* 33 */ 2.52439,
        /* 34 */ -0.427802,
        0.0,
        /* 36 */ 0.888635,
        /* 37 */ -0.360737,
        /* 38 */ 1.43777,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 47 */ 1.08003,
        0.0,0.0,
        /* 50 */ 1.2609,
        0.0,
        /* 52 */ 2.09116,
        /* 53 */ 2.34756,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -2.50457,
        /* 63 */ 0.669824,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 2.05258,
        /* 151 */ 5.053,
        /* 152 */ 1.19348,
        /* 153 */ 1.36957,
        /* 154 */ 2.09028,
        /* 155 */ 0.555241,
        /* 156 */ 2.26979,
        /* 157 */ -0.703138,
        /* 158 */ 2.5361,
        /* 159 */ -0.969517,
        /* 160 */ 1.09409,
        /* 161 */ 2.24167,
        /* 162 */ 0.603328,
        /* 163 */ 0.539434,
        /* 164 */ 0.862118,
        /* 165 */ 0.664342,
        /* 166 */ 0.938693,
        /* 167 */ -4.46864,
        0.0,
        /* 169 */ -0.635156,
        0.0,
        /* 171 */ 2.0651,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
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
    /* 0 */ 1.24921,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 27 */ 4.53819,
    0.0,0.0,
    /* 30 */ 2.13653,
    /* 31 */ -0.843908,
    /* 32 */ 1.55073,
    /* 33 */ 0.638951,
    /* 34 */ -4.53676,
    0.0,
    /* 36 */ 4.47791,
    /* 37 */ -0.803887,
    /* 38 */ 3.00978,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 47 */ 1.70487,
    0.0,0.0,
    /* 50 */ 2.96193,
    0.0,
    /* 52 */ 0.900867,
    /* 53 */ 2.61704,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 62 */ -1.22693,
    /* 63 */ 1.48984,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 150 */ 2.8406,
    /* 151 */ 0.684733,
    /* 152 */ 2.9397,
    /* 153 */ 1.62354,
    /* 154 */ 0.698771,
    /* 155 */ -1.73948,
    /* 156 */ 2.10083,
    /* 157 */ -0.850966,
    /* 158 */ 6.24777,
    /* 159 */ 0.875361,
    /* 160 */ -0.41618,
    /* 161 */ -1.58761,
    /* 162 */ 2.88774,
    /* 163 */ 0.425385,
    /* 164 */ 2.86723,
    /* 165 */ 0.254657,
    /* 166 */ 2.30875,
    /* 167 */ 8.00928,
    0.0,
    /* 169 */ -0.612712,
    0.0,
    /* 171 */ 0.451125,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
};


//#endif





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