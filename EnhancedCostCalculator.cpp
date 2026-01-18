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
    if (genome.AllyPlayer.inactive) statusCost += getActionCost(SimpleParameterOptimizerNode::inactiveWeight);

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
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 2.13904,
        0.0,0.0,
        /* 30 */ 0.905441,
        0.0,0.0,
        /* 33 */ 0.428556,
        /* 34 */ 0.0626551,
        0.0,
        /* 36 */ 3.07984,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 50 */ 0.583279,
        0.0,0.0,
        /* 53 */ 1.59131,
        0.0,
        /* 55 */ 3.19911,
        0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -5.50962,    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 3.09578,
        /* 151 */ 5.3499,
        /* 152 */ 2.8811,
        /* 153 */ 0.560242,
        /* 154 */ 0.355399,
        /* 155 */ 1.18556,
        /* 156 */ 2.02524,
        /* 157 */ 0.964527,
        /* 158 */ 3.63417,
        /* 159 */ 1.30542,
        /* 160 */ 0.281252,
        /* 161 */ -3.06435,
        /* 162 */ -0.772676,
        /* 163 */ 1.09424,
        /* 164 */ 3.62059,
        /* 165 */ 1.58102,
        /* 166 */ 2.10819,
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
    if (genome.AllyPlayer.inactive) statusCost += GENOME[SimpleParameterOptimizerNode::inactiveWeight];

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