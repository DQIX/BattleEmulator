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
        /* 0 */ 0.0887744,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 2.48109,
        0.0,0.0,
        /* 30 */ 2.18664,
        /* 31 */ 1.4672,
        /* 32 */ 0.509408,
        /* 33 */ -0.69497,
        /* 34 */ -3.64925,
        0.0,
        /* 36 */ 0.870768,
        /* 37 */ 0.716908,
        /* 38 */ 3.47621,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 47 */ -0.802511,
        0.0,0.0,
        /* 50 */ 2.77027,
        0.0,
        /* 52 */ -2.56872,
        /* 53 */ 4.68222,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -2.0377,
        /* 63 */ 0.890292,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 0.0597808,
        /* 151 */ -0.23479,
        /* 152 */ 0.890964,
        /* 153 */ -0.343675,
        /* 154 */ -0.392222,
        /* 155 */ 2.98359,
        /* 156 */ 2.67928,
        /* 157 */ 8.20009,
        /* 158 */ -4.44,
        /* 159 */ 0.126003,
        /* 160 */ -0.647784,
        /* 161 */ 1.31771,
        /* 162 */ 3.17387,
        /* 163 */ 2.35417,
        /* 164 */ 0.829201,
        /* 165 */ -2.81477,
        /* 166 */ 4.14051,
        /* 167 */ 0.478161,
        0.0,
        /* 169 */ 1.11618,
        /* 170 */ 2.97878,
        /* 171 */ 1.98366,
        /* 172 */ 2.61829,
        /* 173 */ 1.87293,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };
static constexpr std::array<double, 201> GENOME_C = {
        /* 0 */ -0.586022,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 5.83654,
        0.0,0.0,
        /* 30 */ -0.272854,
        /* 31 */ 2.25365,
        /* 32 */ 4.30703,
        /* 33 */ 1.14131,
        /* 34 */ -7.60274,
        0.0,
        /* 36 */ 1.16213,
        /* 37 */ 1.2663,
        /* 38 */ 3.07915,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 47 */ 3.68778,
        0.0,0.0,
        /* 50 */ 3.41397,
        0.0,
        /* 52 */ 1.0765,
        /* 53 */ 5.40339,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -1.46245,
        /* 63 */ -0.615482,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ -0.482678,
        /* 151 */ 1.53124,
        /* 152 */ 0.860153,
        /* 153 */ 3.37893,
        /* 154 */ 0.641802,
        /* 155 */ 4.40426,
        /* 156 */ -0.727428,
        /* 157 */ 1.15756,
        /* 158 */ 2.5095,
        /* 159 */ 1.05501,
        /* 160 */ 0.750061,
        /* 161 */ 3.21828,
        /* 162 */ 4.10497,
        /* 163 */ 1.43505,
        /* 164 */ 0.101614,
        /* 165 */ 2.49169,
        /* 166 */ 3.1773,
        /* 167 */ 4.14396,
        0.0,
        /* 169 */ -0.0121145,
        /* 170 */ 2.42907,
        /* 171 */ 2.85665,
        /* 172 */ 0.854489,
        /* 173 */ 1.1084,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };

constexpr std::array<double, 201> GENOME_D = {
    /* 0 */ 1.56616,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 27 */ 1.62231,
    0.0,0.0,
    /* 30 */ 0.158156,
    /* 31 */ 3.20031,
    /* 32 */ 2.1851,
    /* 33 */ 0.490947,
    /* 34 */ -5.37126,
    0.0,
    /* 36 */ 5.0921,
    /* 37 */ 2.71796,
    /* 38 */ 3.75022,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 47 */ 0.274177,
    0.0,0.0,
    /* 50 */ 1.04318,
    0.0,
    /* 52 */ -0.1546,
    /* 53 */ 1.49662,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 62 */ -2.70544,
    /* 63 */ 2.42846,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 150 */ -0.386836,
    /* 151 */ 1.39254,
    /* 152 */ 2.0495,
    /* 153 */ 2.49409,
    /* 154 */ 2.29616,
    /* 155 */ 1.12356,
    /* 156 */ 0.483191,
    /* 157 */ 0.479318,
    /* 158 */ -0.141516,
    /* 159 */ -1.68619,
    /* 160 */ -0.0469153,
    /* 161 */ 0.76666,
    /* 162 */ 1.94445,
    /* 163 */ -0.253795,
    /* 164 */ -0.120695,
    /* 165 */ 0.782352,
    /* 166 */ 0.0285343,
    /* 167 */ 1.21867,
    0.0,
    /* 169 */ -0.151908,
    /* 170 */ 4.84855,
    /* 171 */ 1.55315,
    /* 172 */ 0.318493,
    /* 173 */ 1.57935,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
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
        /* 0 */ 1.10435,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 1.76484,
        0.0,0.0,
        /* 30 */ 1.41287,
        /* 31 */ 2.49591,
        /* 32 */ 2.91811,
        /* 33 */ 0.410521,
        /* 34 */ -0.686313,
        0.0,
        /* 36 */ 1.86461,
        /* 37 */ 0.269488,
        /* 38 */ 0.524432,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 47 */ -1.53019,
        0.0,0.0,
        /* 50 */ 2.25692,
        0.0,
        /* 52 */ 2.31676,
        /* 53 */ 2.78689,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -5.28552,
        /* 63 */ 0.616884,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 1.10821,
        /* 151 */ 3.41719,
        /* 152 */ 3.46181,
        /* 153 */ 2.76767,
        /* 154 */ 1.68777,
        /* 155 */ -0.0928972,
        /* 156 */ -0.828177,
        /* 157 */ 0.348558,
        /* 158 */ 0.28434,
        /* 159 */ 1.52723,
        /* 160 */ -0.470353,
        /* 161 */ 0.935076,
        /* 162 */ 2.49843,
        /* 163 */ -0.862709,
        /* 164 */ 0.410378,
        /* 165 */ 0.0749485,
        /* 166 */ 1.36556,
        /* 167 */ -0.72223,
        0.0,
        /* 169 */ 0.141144,
        /* 170 */ 3.12832,
        /* 171 */ 2.95669,
        /* 172 */ 1.63452,
        /* 173 */ 2.20708,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
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