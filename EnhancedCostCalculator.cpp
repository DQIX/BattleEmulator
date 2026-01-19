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
    if (genome.AllyPlayer.PoisonEnable) statusCost += getActionCost(SimpleParameterOptimizerNode::poisonWeight);

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * getActionCost(SimpleParameterOptimizerNode::BuffWeight);
    statusCost -= genome.AllyPlayer.AtkBuffLevel * getActionCost(SimpleParameterOptimizerNode::AtkBuffWeight);
    statusCost -= genome.AllyPlayer.TensionLevel * getActionCost(SimpleParameterOptimizerNode::TensionWeight);

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

    // Item count considerations (rough estimates)
    if (genome.AllyPlayer.SpecialMedicineCount <= 1 && genome.AllyPlayer.SpecialAntidoteCount <= 1) {
        resourceCost += getActionCost(SimpleParameterOptimizerNode::NoResourceCost); // Penalty for low healing items
    }

    return resourceCost;
}


#else

constexpr std::array<double, 201> GENOME = {
    /* 0 */ -0.0109228,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 25 */ -1.38373,
    /* 26 */ 0.59014,
    /* 27 */ 3.2997,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 53 */ 3.2988,
    0.0,0.0,0.0,0.0,0.0,
    /* 59 */ 0.808701,
    0.0,
    /* 61 */ 5.2108,
    /* 62 */ -0.102978,    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
    /* 150 */ -0.346932,
    /* 151 */ 2.20266,
    /* 152 */ 2.05549,
    /* 153 */ 1.35308,
    /* 154 */ -0.608405,
    /* 155 */ -0.312065,
    /* 156 */ -1.02201,
    /* 157 */ 0.93424,
    /* 158 */ 1.26295,
    /* 159 */ 5.39034,
    /* 160 */ -0.557405,
    /* 161 */ 2.62132,
    /* 162 */ -0.737328,
    /* 163 */ 0.542754,
    /* 164 */ 1.49704,
    /* 165 */ -1.93283,
    /* 166 */ 3.56998,
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
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
    if (genome.AllyPlayer.PoisonEnable) statusCost += GENOME[SimpleParameterOptimizerNode::poisonWeight];

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

    // Item count considerations (rough estimates)
    if (genome.AllyPlayer.SpecialMedicineCount <= 1 && genome.AllyPlayer.SpecialAntidoteCount <= 1) {
        resourceCost += GENOME[SimpleParameterOptimizerNode::NoResourceCost]; // Penalty for low healing items
    }

    return resourceCost;
}

#endif