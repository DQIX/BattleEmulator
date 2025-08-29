//
// Flexible Constraint Manager for A* Algorithm
// Allows deviation from predefined sequences when they lead to suboptimal states
//

#ifndef FLEXIBLE_CONSTRAINT_MANAGER_H
#define FLEXIBLE_CONSTRAINT_MANAGER_H

#include <vector>
#include <unordered_map>
#include <cstdint>
#include "Genome.h"

struct ConstraintState {
    int sequenceLength;           // Length of predefined sequence
    int currentPosition;          // Current position in sequence
    double deviationPenalty;      // Penalty for deviating from sequence
    bool allowDeviation;          // Whether deviation is currently allowed
    int stuckCounter;             // Counter for detecting stuck states
    double lastBestFCost;         // Track progress to detect stagnation
};

class FlexibleConstraintManager {
public:
    // Initialize constraint manager with predefined sequence
    static void initialize(const int actions[350]);
    
    // Check if an action is allowed given current constraints
    static bool isActionAllowed(const Genome& genome, int proposedAction);
    
    // Get penalty for deviating from predefined sequence
    static double getDeviationPenalty(const Genome& genome, int proposedAction);
    
    // Update constraint state based on search progress
    static void updateConstraintState(const Genome& genome, double currentBestFCost, int searchCounter);
    
    // Check if we should allow more flexibility due to stagnation
    static bool shouldIncreaseFlexibility(int searchCounter);
    
    // Reset constraint state for new search
    static void reset();
    
private:
    static std::vector<int> predefinedSequence;
    static ConstraintState constraintState;
    static std::unordered_map<int, int> stagnationHistory;
    
    // Helper functions
    static int getPredefinedActionAt(int turn);
    static bool isSequenceComplete(const Genome& genome);
    static double calculateProgressScore(const Genome& genome);
};

#endif // FLEXIBLE_CONSTRAINT_MANAGER_H