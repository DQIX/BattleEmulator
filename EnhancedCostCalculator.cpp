//
// Enhanced Cost Calculator Implementation
//

#include "EnhancedCostCalculator.h"
#include <algorithm>

#if defined(OPTIMIZE_MODE)

#include "SimpleParameterOptimizer.h"

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + getActionCost(SimpleParameterOptimizer::turnHeignt);

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);

    if (genome.AllyPlayer.PoisonEnable == true && action == BattleEmulator::SPECIAL_ANTIDOTE) {
        gCost -= 0.1;
    }

    return gCost;
}

double EnhancedCostCalculator::calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp) {
    if (genome.EnemyPlayer.hp <= 0) {
        return 0.0; // Goal reached
    }

    double hCost = 0.0;

    // Primary heuristic: enemy HP ratio (scaled down for better granularity)
    hCost = (genome.EnemyPlayer.hp / enemyMaxHp) * getActionCost(SimpleParameterOptimizer::enemyHpWeight);

    // Player HP consideration (more granular than original)
    double playerHpRatio = genome.AllyPlayer.hp / playerMaxHp;
    hCost += (1.0 - playerHpRatio) * getActionCost(SimpleParameterOptimizer::playerHpWeight);

    // MP consideration (resource management)
    hCost += calculateResourceCost(genome) * getActionCost(SimpleParameterOptimizer::resourceWeight);

    // Status effect penalties/bonuses
    hCost += calculateStatusEffectCost(genome) * getActionCost(SimpleParameterOptimizer::StatusEffectWeight);

    return hCost;
}

double EnhancedCostCalculator::getActionCost(int action) {
   return SimpleParameterOptimizer::getActionCost(action);
}


double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // Negative status effects (penalties)
    if (genome.AllyPlayer.paralysis) statusCost += getActionCost(SimpleParameterOptimizer::paralysisWeight);
    if (genome.AllyPlayer.sleeping) statusCost += getActionCost(SimpleParameterOptimizer::sleepWeight);
    if (genome.AllyPlayer.PoisonEnable) statusCost += getActionCost(SimpleParameterOptimizer::poisonWeight);
    if (genome.AllyPlayer.inactive) statusCost += getActionCost(SimpleParameterOptimizer::inactiveWeight);

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * 0.1;
    statusCost -= genome.AllyPlayer.AtkBuffLevel * 0.1;
    statusCost -= genome.AllyPlayer.TensionLevel * 0.05;

    // Special abilities
    if (genome.AllyPlayer.acrobaticStar) statusCost -= getActionCost(SimpleParameterOptimizer::SpHeight);
    if (genome.AllyPlayer.specialCharge) statusCost -= getActionCost(SimpleParameterOptimizer::ActHeight);

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * getActionCost(SimpleParameterOptimizer::ResourceHPCost); // Penalty for low MP
    }

    // Item count considerations (rough estimates)
    if (genome.AllyPlayer.SpecialMedicineCount <= 1 && genome.AllyPlayer.SpecialAntidoteCount <= 1) {
        resourceCost += getActionCost(SimpleParameterOptimizer::NoResourceCost); // Penalty for low healing items
    }

    return resourceCost;
}


#else

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + 2.0;

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);

    if (genome.AllyPlayer.PoisonEnable == true && action == BattleEmulator::SPECIAL_ANTIDOTE) {
        gCost -= 0.1;
    }

    return gCost;
}

double EnhancedCostCalculator::calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp) {
    if (genome.EnemyPlayer.hp <= 0) {
        return 0.0; // Goal reached
    }

    double hCost = 0.0;

    // Primary heuristic: enemy HP ratio (scaled down for better granularity)
    hCost = (genome.EnemyPlayer.hp / enemyMaxHp) * 30.0;

    // Player HP consideration (more granular than original)
    double playerHpRatio = genome.AllyPlayer.hp / playerMaxHp;
    hCost += (1.0 - playerHpRatio) * 2.0;

    // MP consideration (resource management)
    hCost += calculateResourceCost(genome) * 0.5;

    // Status effect penalties/bonuses
    hCost += calculateStatusEffectCost(genome) * 0.5;

    return hCost;
}

double EnhancedCostCalculator::getActionCost(int action) {
    switch (action) {
        case BattleEmulator::MIDHEAL:
            return 0.01;
        case BattleEmulator::SPECIAL_ANTIDOTE:
            return 0.01; // Offensive actions have no penalty

        case BattleEmulator::SPECIAL_MEDICINE:
            return 0.01; // Slight penalty for healing

        case BattleEmulator::DOUBLE_UP:
            return 0.001; // Less penalty for item healing

        case BattleEmulator::PSYCHE_UP_ALLY:
            return 0.05; // Higher penalty for defensive actions

        case BattleEmulator::FLEE_ALLY:
            return 0.1; // High penalty for fleeing

        case BattleEmulator::BUFF:
            return 0.05; // Small penalty for buff spells

        case BattleEmulator::MULTITHRUST:
            return 0.1; // Small penalty for special abilities
        default:
            return 0.1; // Default moderate penalty
    }
}

double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // Negative status effects (penalties)
    if (genome.AllyPlayer.paralysis) statusCost += 1.0;
    if (genome.AllyPlayer.sleeping) statusCost += 1.5;
    if (genome.AllyPlayer.PoisonEnable) statusCost += 0.5;

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * 0.1;
    statusCost -= genome.AllyPlayer.AtkBuffLevel * 0.1;
    statusCost -= genome.AllyPlayer.TensionLevel * 0.05;

    // Special abilities
    if (genome.AllyPlayer.acrobaticStar) statusCost -= 0.2;
    if (genome.AllyPlayer.specialCharge) statusCost -= 0.2;

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * 0.5; // Penalty for low MP
    }

    // Item count considerations (rough estimates)
    if (genome.AllyPlayer.SpecialMedicineCount <= 1 && genome.AllyPlayer.SpecialAntidoteCount <= 1) {
        resourceCost += 0.2; // Penalty for low healing items
    }

    return resourceCost;
}

#endif