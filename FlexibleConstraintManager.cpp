//
// Flexible Constraint Manager Implementation
//

#include "FlexibleConstraintManager.h"
#include <algorithm>
#include <cmath>

// Static member definitions
std::vector<int> FlexibleConstraintManager::predefinedSequence;
ConstraintState FlexibleConstraintManager::constraintState;
std::unordered_map<int, int> FlexibleConstraintManager::stagnationHistory;

void FlexibleConstraintManager::initialize(const int actions[350]) {
    predefinedSequence.clear();
    
    // Store predefined sequence
    for (int i = 0; i < 350; ++i) {
        if (actions[i] == -1 || actions[i] == 0) {
            break;
        }
        predefinedSequence.push_back(actions[i]);
    }
    
    // Initialize constraint state
    constraintState.sequenceLength = static_cast<int>(predefinedSequence.size());
    constraintState.currentPosition = 0;
    constraintState.deviationPenalty = 1.0; // Start with moderate penalty
    constraintState.allowDeviation = false; // Start strict
    constraintState.stuckCounter = 0;
    constraintState.lastBestFCost = 1000000.0; // High initial value
    
    stagnationHistory.clear();
}

bool FlexibleConstraintManager::isActionAllowed(const Genome& genome, int proposedAction) {
    // If we're past the predefined sequence, allow any action
    if (isSequenceComplete(genome)) {
        return true;
    }
    
    int expectedAction = getPredefinedActionAt(genome.turn);
    
    // If action matches predefined sequence, always allow
    if (proposedAction == expectedAction) {
        return true;
    }
    
    // Check if deviation is allowed based on current constraint state
    if (constraintState.allowDeviation) {
        return true;
    }
    
    // For longer sequences (6+ actions), be more flexible by default
    if (constraintState.sequenceLength >= 6) {
        // Allow deviation after being stuck for a while
        return constraintState.stuckCounter > 50000;
    }
    
    // For shorter sequences, be more strict initially
    return constraintState.stuckCounter > 100000;
}

double FlexibleConstraintManager::getDeviationPenalty(const Genome& genome, int proposedAction) {
    if (isSequenceComplete(genome)) {
        return 0.0; // No penalty after sequence completion
    }
    
    int expectedAction = getPredefinedActionAt(genome.turn);
    
    if (proposedAction == expectedAction) {
        return 0.0; // No penalty for following sequence
    }
    
    // Calculate penalty based on how far we are in the sequence
    double positionRatio = static_cast<double>(genome.turn) / constraintState.sequenceLength;
    
    // Penalty decreases as we get further in sequence (more flexibility later)
    double basePenalty = constraintState.deviationPenalty * (1.0 - positionRatio * 0.5);
    
    // Reduce penalty if we've been stuck for a while
    double stuckReduction = std::min(0.8, constraintState.stuckCounter / 100000.0);
    
    return basePenalty * (1.0 - stuckReduction);
}

void FlexibleConstraintManager::updateConstraintState(const Genome& genome, double currentBestFCost, int searchCounter) {
    // Check for progress
    if (currentBestFCost < constraintState.lastBestFCost - 0.1) {
        // Good progress, reset stuck counter
        constraintState.stuckCounter = 0;
        constraintState.lastBestFCost = currentBestFCost;
    } else {
        // No significant progress, increment stuck counter
        constraintState.stuckCounter++;
    }
    
    // Increase flexibility if stuck for too long
    if (constraintState.stuckCounter > 50000) {
        constraintState.allowDeviation = true;
        constraintState.deviationPenalty *= 0.95; // Gradually reduce penalty
    }
    
    // For very long sequences, be more aggressive about allowing deviation
    if (constraintState.sequenceLength >= 8 && constraintState.stuckCounter > 25000) {
        constraintState.allowDeviation = true;
        constraintState.deviationPenalty *= 0.9;
    }
    
    // Track stagnation patterns
    int bucketKey = searchCounter / 10000;
    stagnationHistory[bucketKey]++;
    
    // If we see repeated stagnation, permanently increase flexibility
    if (stagnationHistory[bucketKey] > 5) {
        constraintState.deviationPenalty = std::max(0.1, constraintState.deviationPenalty * 0.8);
        constraintState.allowDeviation = true;
    }
}

bool FlexibleConstraintManager::shouldIncreaseFlexibility(int searchCounter) {
    // Increase flexibility based on search progress and sequence length
    if (constraintState.sequenceLength >= 6) {
        // For longer sequences, increase flexibility earlier
        return searchCounter > 25000 || constraintState.stuckCounter > 30000;
    } else {
        // For shorter sequences, be more patient
        return searchCounter > 50000 || constraintState.stuckCounter > 75000;
    }
}

void FlexibleConstraintManager::reset() {
    constraintState.currentPosition = 0;
    constraintState.deviationPenalty = 1.0;
    constraintState.allowDeviation = false;
    constraintState.stuckCounter = 0;
    constraintState.lastBestFCost = 1000000.0;
    stagnationHistory.clear();
}

int FlexibleConstraintManager::getPredefinedActionAt(int turn) {
    if (turn < 0 || turn >= static_cast<int>(predefinedSequence.size())) {
        return -1; // No predefined action
    }
    return predefinedSequence[turn];
}

bool FlexibleConstraintManager::isSequenceComplete(const Genome& genome) {
    return genome.turn >= constraintState.sequenceLength;
}

double FlexibleConstraintManager::calculateProgressScore(const Genome& genome) {
    // Calculate how well the current state is progressing
    double enemyHpRatio = static_cast<double>(genome.EnemyPlayer.hp) / genome.EnemyPlayer.maxHp;
    double playerHpRatio = static_cast<double>(genome.AllyPlayer.hp) / genome.AllyPlayer.maxHp;
    
    // Progress is good if enemy HP is low and player HP is reasonable
    return (1.0 - enemyHpRatio) + (playerHpRatio * 0.1);
}