//
// Enhanced Cost Calculator with Simple Global Parameters
//

#include "EnhancedCostCalculator.h"
#include "SimpleParameterOptimizer.h"  // CostParamsにアクセス
#include <algorithm>


#if defined(OPTIMIZE_MODE)

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost) {
    double gCost = preGCost + CostParams::turnHeignt;
    gCost += getActionCost(action);
    return gCost;
}

double EnhancedCostCalculator::calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp) {
    if (genome.EnemyPlayer.hp <= 0) {
        return 0.0;
    }

    double hCost = 0.0;

    // 敵HP（グローバルパラメータ使用）
    double enemyHpRatio = genome.EnemyPlayer.hp / enemyMaxHp;
    hCost = enemyHpRatio * CostParams::enemyHpWeight;

    // プレイヤーHP（グローバルパラメータ使用）
    double playerHpRatio = genome.AllyPlayer.hp / playerMaxHp;
    hCost += (1.0 - playerHpRatio) * CostParams::playerHpWeight;

    // リソース（グローバルパラメータ使用）
    hCost += calculateResourceCost(genome) * CostParams::resourceWeight;

    // ステータス効果
    hCost += calculateStatusEffectCost(genome) * CostParams::StatusEffectWeight;

    return hCost;
}

double EnhancedCostCalculator::getActionCost(int action) {
    switch(action) {
        case BattleEmulator::ATTACK_ALLY:
            return CostParams::AttackPenalty;
        case BattleEmulator::DRAGON_SLASH:
            return CostParams::dragonSlashPenalty;
        case BattleEmulator::HEAL:
            return CostParams::healPenalty;
        case BattleEmulator::MEDICINAL_HERBS:
            return CostParams::itemHealPenalty;
        case BattleEmulator::DEFENCE:
            return CostParams::defensePenalty;
        case BattleEmulator::FLEE_ALLY:
            return CostParams::fleePenalty;
        case BattleEmulator::CRACK_ALLY:
            return CostParams::buffPenalty;
        case BattleEmulator::ACROBATIC_STAR:
            return CostParams::specialPenalty;
        default:
            return 0.1;
    }
}

double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // デバフペナルティ（グローバルパラメータ使用）
    if (genome.AllyPlayer.paralysis) statusCost += CostParams::paralysisWeight;
    if (genome.AllyPlayer.sleeping) statusCost += CostParams::sleepWeight;
    if (genome.AllyPlayer.PoisonEnable) statusCost += CostParams::poisonWeight;

    // バフボーナス（グローバルパラメータ使用）
    statusCost -= genome.AllyPlayer.BuffLevel * CostParams::buffBonus;
    statusCost -= genome.AllyPlayer.AtkBuffLevel * CostParams::atkBuffBonus;
    statusCost -= genome.AllyPlayer.TensionLevel * 0.05;

    // 特殊能力
    if (genome.AllyPlayer.acrobaticStar) statusCost -= CostParams::SpHeight;
    if (genome.AllyPlayer.specialCharge) statusCost -= CostParams::ActHeight;

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * 0.5;
    }

    // アイテム
    if (genome.AllyPlayer.medicinal_herbs_count <= 1) {
        resourceCost += 0.2;
    }
    
    return resourceCost;
}

#else
//
// Enhanced Cost Calculator Implementation
//

#include "EnhancedCostCalculator.h"
#include <algorithm>

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + 0.820127;

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);

    return gCost;
}

double EnhancedCostCalculator::calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp) {
    if (genome.EnemyPlayer.hp <= 0) {
        return 0.0; // Goal reached
    }

    double hCost = 0.0;

    // Primary heuristic: enemy HP ratio (scaled down for better granularity)
    hCost = (genome.EnemyPlayer.hp / enemyMaxHp) * 36.280982;

    // Player HP consideration (more granular than original)
    double playerHpRatio = genome.AllyPlayer.hp / playerMaxHp;
    hCost += (1.0 - playerHpRatio) * 0.902848;

    // MP consideration (resource management)
    hCost += calculateResourceCost(genome) * 9.912960;

    // Status effect penalties/bonuses
    hCost += calculateStatusEffectCost(genome) * 4.767679;

    return hCost;
}

double EnhancedCostCalculator::getActionCost(int action) {
    switch(action) {
        case BattleEmulator::ATTACK_ALLY:
            return 1.218873; // No penalty for attacking
        case BattleEmulator::DRAGON_SLASH:
            return 0.487316; // Offensive actions have no penalty

        case BattleEmulator::HEAL:
            return 1.005633; // Slight penalty for healing

        case BattleEmulator::MEDICINAL_HERBS:
            return 0.807670; // Less penalty for item healing

        case BattleEmulator::DEFENCE:
            return 1.819962; // Higher penalty for defensive actions

        case BattleEmulator::FLEE_ALLY:
            return 0.446136; // High penalty for fleeing

        case BattleEmulator::CRACK_ALLY:
            return 0.027378; // Small penalty for buff spells

        case BattleEmulator::ACROBATIC_STAR:
            return 0.016894; // Small penalty for special abilities

        default:
            return 0.1; // Default moderate penalty
    }
}

double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // Negative status effects (penalties)
    if (genome.AllyPlayer.paralysis) statusCost += 0.1;
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
    if (genome.AllyPlayer.medicinal_herbs_count <= 1) {
        resourceCost += 0.2; // Penalty for low healing items
    }

    return resourceCost;
}
#endif