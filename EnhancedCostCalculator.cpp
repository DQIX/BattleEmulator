//
// Enhanced Cost Calculator Implementation
//

#include "EnhancedCostCalculator.h"
#include <algorithm>

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, int preGCost) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + 1.0;

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
    //hCost += genome.EnemyPlayer.BuffLevel * 1;

    // Player HP consideration (more granular than original)
    double playerHpRatio = genome.AllyPlayer.hp / playerMaxHp;
    hCost += (1.0 - playerHpRatio) * 2.0;

    // MP consideration (resource management)
    hCost += calculateResourceCost(genome);

    // Status effect penalties/bonuses
    hCost += calculateStatusEffectCost(genome);

    return hCost;
}

double EnhancedCostCalculator::getActionCost(int action) {
    switch (action) {
        case BattleEmulator::ATTACK_ALLY:
        case BattleEmulator::DRAGON_SLASH:
            return 0.0; // Offensive actions have no penalty

        case BattleEmulator::HEAL:
            return 0.05; // Slight penalty for healing

        case BattleEmulator::MEDICINAL_HERBS:
            return 0.03; // Less penalty for item healing

        case BattleEmulator::DEFENCE:
            return 0.15; // Higher penalty for defensive actions

        case BattleEmulator::FLEE_ALLY:
            return 0.1; // High penalty for fleeing

        case BattleEmulator::CRACK_ALLY:
            return 0.02; // Small penalty for buff spells

        case BattleEmulator::ACROBATIC_STAR:
            return 0.0001; // Small penalty for special abilities

        case BattleEmulator::SPECIAL_ANTIDOTE:
            return 0.02;
        case BattleEmulator::SPECIAL_MEDICINE:
            return 0.5;
        case BattleEmulator::WOOSH_ALLY:
            return 0.3;
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
    if (genome.AllyPlayer.specialCharge) statusCost -= 0.1;

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
