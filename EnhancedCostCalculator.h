//
// Enhanced Cost Calculator for A* Algorithm
// Provides better granularity in f-cost calculation to prevent identical costs
//

#ifndef ENHANCED_COST_CALCULATOR_H
#define ENHANCED_COST_CALCULATOR_H

#include "Genome.h"
#include "BattleEmulator.h"

class EnhancedCostCalculator {
public:
    // Calculate enhanced g-cost with action-specific costs
    static double calculateGCost(const Genome &genome, int action, int preGCost);
    
    // Calculate enhanced h-cost with multiple factors
    static double calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp);
    
private:
    // Get action-specific cost modifier
    static double getActionCost(int action);
    
    // Calculate status effect penalties/bonuses
    static double calculateStatusEffectCost(const Genome &genome);
    
    // Calculate resource-based cost (MP, items)
    static double calculateResourceCost(const Genome &genome);
};

#endif // ENHANCED_COST_CALCULATOR_H