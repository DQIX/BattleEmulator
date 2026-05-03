//
// Enhanced Cost Calculator Implementation
//

#include "EnhancedCostCalculator.h"
#include <array>

#include "SimpleParameterOptimizer.h"

#if defined(OPTIMIZE_MODE)

#include "SimpleParameterOptimizer.h"

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + getActionCost(SimpleParameterOptimizerNode::turnHeignt);

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);

    if (genome.AllyPlayer.PoisonEnable == true && action == BattleEmulator::SPECIAL_ANTIDOTE) {
        gCost -= getActionCost(SimpleParameterOptimizerNode::AntidoteWeight);
    }

    return gCost;
}

double EnhancedCostCalculator::calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp) {
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
    if (genome.AllyPlayer.isStunned) statusCost += getActionCost(SimpleParameterOptimizerNode::inactiveWeight);

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * getActionCost(SimpleParameterOptimizerNode::BuffWeight);
    statusCost -= genome.AllyPlayer.AtkBuffLevel * getActionCost(SimpleParameterOptimizerNode::AtkBuffWeight);
    statusCost -= genome.AllyPlayer.TensionLevel * getActionCost(SimpleParameterOptimizerNode::TensionWeight);

    // Special abilities
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

    resourceCost += (2 - genome.AllyPlayer.MagicWaterCount) * getActionCost(SimpleParameterOptimizerNode::MagicWaterCost);

    // Item count considerations (rough estimates)
    if (genome.AllyPlayer.SpecialMedicineCount <= 1 && genome.AllyPlayer.SpecialAntidoteCount <= 1) {
        resourceCost += getActionCost(SimpleParameterOptimizerNode::NoResourceCost); // Penalty for low healing items
    }

    return resourceCost;
}


#else
constexpr std::array<double, 201> GENOME = {
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 1.62501,
        0.0,0.0,
        /* 30 */ 7.5857,
        0.0,0.0,
        /* 33 */ -1.73149,
        /* 34 */ -0.957736,    0.0,
        /* 36 */ 7.8957,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 49 */ -1.25643,
        /* 50 */ 5.82616,
        0.0,
        /* 52 */ 1.12026,
        /* 53 */ 2.65016,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -1.62081,    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 0.82181,
        /* 151 */ 6.90146,
        /* 152 */ -4.11337,
        /* 153 */ 1.56812,
        /* 154 */ 0.1694,
        /* 155 */ -1.1226,
        /* 156 */ 3.98818,
        /* 157 */ 2.12945,
        /* 158 */ 1.39256,
        /* 159 */ -3.70097,
        /* 160 */ 4.35968,
        /* 161 */ 2.32119,
        /* 162 */ 1.57115,
        /* 163 */ 1.82689,
        /* 164 */ -0.359419,
        /* 165 */ 1.98467,
        /* 166 */ 2.24294,
        /* 167 */ 0.842564,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };
double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + GENOME[SimpleParameterOptimizerNode::turnHeignt];

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);

    if (genome.AllyPlayer.PoisonEnable == true && action == BattleEmulator::SPECIAL_ANTIDOTE) {
        gCost -= GENOME[SimpleParameterOptimizerNode::AntidoteWeight];
    }

    return gCost;
}

double EnhancedCostCalculator::calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp) {
    if (genome.EnemyPlayer.hp <= 0) {
        return 0.0; // Goal reached
    }

    double hCost = 0.0;

    // Primary heuristic: enemy HP ratio (scaled down for better granularity)
    hCost = (genome.EnemyPlayer.hp / enemyMaxHp) * GENOME[SimpleParameterOptimizerNode::enemyHpWeight];

    // Player HP consideration (more granular than original)
    double playerHpRatio = genome.AllyPlayer.hp / playerMaxHp;
    hCost += (1.0 - playerHpRatio) * GENOME[SimpleParameterOptimizerNode::playerHpWeight];

    // MP consideration (resource management)
    hCost += calculateResourceCost(genome) * GENOME[SimpleParameterOptimizerNode::resourceWeight];

    // Status effect penalties/bonuses
    hCost += calculateStatusEffectCost(genome) * GENOME[SimpleParameterOptimizerNode::StatusEffectWeight];

    return hCost;
}

double EnhancedCostCalculator::getActionCost(int action) {
    return (action >= 0 && action < GENOME.size()) ? GENOME[action] : 0.0;
}

double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // Negative status effects (penalties)
    if (genome.AllyPlayer.paralysis) statusCost += GENOME[SimpleParameterOptimizerNode::paralysisWeight];
    if (genome.AllyPlayer.sleeping) statusCost += GENOME[SimpleParameterOptimizerNode::sleepWeight];
    if (genome.AllyPlayer.isStunned) statusCost += GENOME[SimpleParameterOptimizerNode::inactiveWeight];

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * GENOME[SimpleParameterOptimizerNode::BuffWeight];
    statusCost -= genome.AllyPlayer.AtkBuffLevel * GENOME[SimpleParameterOptimizerNode::AtkBuffWeight];
    statusCost -= genome.AllyPlayer.TensionLevel * GENOME[SimpleParameterOptimizerNode::TensionWeight];

    // Special abilities
    if (genome.AllyPlayer.acrobaticStar) statusCost -= GENOME[SimpleParameterOptimizerNode::SpHeight];
    if (genome.AllyPlayer.specialCharge) statusCost -= GENOME[SimpleParameterOptimizerNode::ActHeight];

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * GENOME[SimpleParameterOptimizerNode::ResourceHPCost]; // Penalty for low MP
    }

    resourceCost += (2 - genome.AllyPlayer.MagicWaterCount) * GENOME[SimpleParameterOptimizerNode::MagicWaterCost];

    // Item count considerations (rough estimates)
    if (genome.AllyPlayer.SpecialMedicineCount <= 1 && genome.AllyPlayer.SpecialAntidoteCount <= 1) {
        resourceCost += GENOME[SimpleParameterOptimizerNode::NoResourceCost]; // Penalty for low healing items
    }

    return resourceCost;
}

#endif