//
// Adaptive Action Generator
// Generates possible actions with flexible constraint consideration
//

#ifndef ADAPTIVE_ACTION_GENERATOR_H
#define ADAPTIVE_ACTION_GENERATOR_H

#include <vector>
#include "Genome.h"
#include "Player.h"

struct ActionCandidate {
    int action;
    double priority;          // Priority score for this action
    bool isFromSequence;      // Whether this action is from predefined sequence
    double constraintCost;    // Additional cost due to constraints
};

class AdaptiveActionGenerator {
public:
    // Generate possible actions with constraint awareness
    static std::vector<ActionCandidate> generateActions(
        const Genome& genome, 
        const Player players[2], 
        int searchCounter
    );
    
    // Get base actions that are always available
    static std::vector<int> getBaseActions();
    
    // Get conditional actions based on game state
    static std::vector<int> getConditionalActions(const Genome& genome, const Player players[2]);
    
    // Calculate action priority based on current situation
    static double calculateActionPriority(const Genome& genome, int action);
    
private:
    // Helper functions
    static bool isActionPhysicallyPossible(const Genome& genome, const Player players[2], int action);
    static double getActionEffectivenessScore(const Genome& genome, int action);
    static double getActionRiskScore(const Genome& genome, int action);
};

#endif // ADAPTIVE_ACTION_GENERATOR_H