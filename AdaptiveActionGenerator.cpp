//
// Adaptive Action Generator Implementation
//

#include "AdaptiveActionGenerator.h"
#include "FlexibleConstraintManager.h"
#include "BattleEmulator.h"
#include <algorithm>

std::vector<ActionCandidate> AdaptiveActionGenerator::generateActions(
    const Genome& genome, 
    const Player players[2], 
    int searchCounter) {
    
    std::vector<ActionCandidate> candidates;
    
    // Get all physically possible actions
    std::vector<int> baseActions = getBaseActions();
    std::vector<int> conditionalActions = getConditionalActions(genome, players);
    
    // Combine all possible actions
    std::vector<int> allActions = baseActions;
    allActions.insert(allActions.end(), conditionalActions.begin(), conditionalActions.end());
    
    // Evaluate each action
    for (int action : allActions) {
        if (!isActionPhysicallyPossible(genome, players, action)) {
            continue;
        }
        
        ActionCandidate candidate;
        candidate.action = action;
        candidate.isFromSequence = false;
        candidate.constraintCost = 0.0;
        
        // Check if this action is from predefined sequence
        bool isAllowed = FlexibleConstraintManager::isActionAllowed(genome, action);
        if (isAllowed) {
            // Check if it matches the expected sequence action
            // This is a simplified check - in practice you'd compare with expected action
            candidate.isFromSequence = true;
        }
        
        // Calculate constraint cost
        candidate.constraintCost = FlexibleConstraintManager::getDeviationPenalty(genome, action);
        
        // Calculate priority
        candidate.priority = calculateActionPriority(genome, action);
        
        // Adjust priority based on constraints
        if (!isAllowed && !FlexibleConstraintManager::shouldIncreaseFlexibility(searchCounter)) {
            candidate.priority *= 0.1; // Heavily penalize disallowed actions
        }
        
        candidates.push_back(candidate);
    }
    
    // Sort by priority (higher is better)
    std::sort(candidates.begin(), candidates.end(), 
              [](const ActionCandidate& a, const ActionCandidate& b) {
                  return a.priority > b.priority;
              });
    
    return candidates;
}

std::vector<int> AdaptiveActionGenerator::getBaseActions() {
    return {
        BattleEmulator::ATTACK_ALLY,
        BattleEmulator::DRAGON_SLASH,
        BattleEmulator::DEFENCE,
        BattleEmulator::FLEE_ALLY,
        BattleEmulator::ACROBATIC_STAR,
        BattleEmulator::CRACK_ALLY
    };
}

std::vector<int> AdaptiveActionGenerator::getConditionalActions(const Genome& genome, const Player players[2]) {
    std::vector<int> actions;
    
    // Medicinal herbs
    if (players[0].medicinal_herbs_count >= 1) {
        actions.push_back(BattleEmulator::MEDICINAL_HERBS);
    }
    
    // Heal spell
    if (genome.AllyPlayer.mp >= 2) {
        actions.push_back(BattleEmulator::HEAL);
    }
    
    // Crack/Buff spell
    if (genome.AllyPlayer.mp >= 3) {
        actions.push_back(BattleEmulator::CRACK_ALLY);
    }
    
    // Acrobatic Star
    if (genome.AllyPlayer.specialCharge && 
        genome.AllyPlayer.specialChargeTurn != 0 &&
        !genome.AllyPlayer.acrobaticStar) {
        actions.push_back(BattleEmulator::ACROBATIC_STAR);
    }
    
    return actions;
}

double AdaptiveActionGenerator::calculateActionPriority(const Genome& genome, int action) {
    double priority = 1.0;
    
    // Base priority based on action type
    switch (action) {
        case BattleEmulator::ATTACK_ALLY:
        case BattleEmulator::DRAGON_SLASH:
            priority = 2.0; // High priority for offensive actions
            break;
            
        case BattleEmulator::HEAL:
        case BattleEmulator::MEDICINAL_HERBS: {
            // Priority based on player HP
            double hpRatio = static_cast<double>(genome.AllyPlayer.hp) / genome.AllyPlayer.maxHp;
            priority = (1.0 - hpRatio) * 3.0; // Higher priority when HP is low
        } break;
            
        case BattleEmulator::CRACK_ALLY:
            // Buff priority based on current buff level
            priority = (genome.AllyPlayer.AtkBuffLevel == 0) ? 1.5 : 0.5;
            break;
            
        case BattleEmulator::ACROBATIC_STAR:
            priority = 2.5; // High priority for special abilities
            break;
            
        case BattleEmulator::DEFENCE:
            priority = 0.8; // Lower priority for defensive actions
            break;
            
        case BattleEmulator::FLEE_ALLY:
            priority = 0.1; // Very low priority for fleeing
            break;
            
        default:
            priority = 1.0;
            break;
    }
    
    // Adjust based on effectiveness and risk
    priority *= getActionEffectivenessScore(genome, action);
    priority *= (2.0 - getActionRiskScore(genome, action)); // Reduce priority for risky actions
    
    return priority;
}

bool AdaptiveActionGenerator::isActionPhysicallyPossible(const Genome& genome, const Player players[2], int action) {
    switch (action) {
        case BattleEmulator::MEDICINAL_HERBS:
            return players[0].medicinal_herbs_count >= 1;
            
        case BattleEmulator::HEAL:
            return genome.AllyPlayer.mp >= 2;
            
        case BattleEmulator::CRACK_ALLY:
            return genome.AllyPlayer.mp >= 3;
            
        case BattleEmulator::ACROBATIC_STAR:
            return genome.AllyPlayer.specialCharge && 
                   genome.AllyPlayer.specialChargeTurn != 0 &&
                   !genome.AllyPlayer.acrobaticStar;
            
        default:
            return true; // Most actions are always possible
    }
}

double AdaptiveActionGenerator::getActionEffectivenessScore(const Genome& genome, int action) {
    // Simplified effectiveness calculation
    switch (action) {
        case BattleEmulator::ATTACK_ALLY:
        case BattleEmulator::DRAGON_SLASH:
            // More effective when enemy HP is low
            return 1.0 + (1.0 - static_cast<double>(genome.EnemyPlayer.hp) / genome.EnemyPlayer.maxHp);
            
        case BattleEmulator::HEAL:
        case BattleEmulator::MEDICINAL_HERBS:
            // More effective when player HP is low
            return 2.0 - (static_cast<double>(genome.AllyPlayer.hp) / genome.AllyPlayer.maxHp);
            
        default:
            return 1.0;
    }
}

double AdaptiveActionGenerator::getActionRiskScore(const Genome& genome, int action) {
    // Simplified risk calculation (1.0 = no risk, 2.0 = high risk)
    switch (action) {
        case BattleEmulator::DEFENCE:
            return 0.5; // Low risk
            
        case BattleEmulator::FLEE_ALLY:
            return 2.0; // High risk (might fail)
            
        default:
            return 1.0; // Normal risk
    }
}