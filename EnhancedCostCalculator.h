//
// Enhanced Cost Calculator for A* Algorithm
// Provides better granularity in f-cost calculation to prevent identical costs
//

#ifndef ENHANCED_COST_CALCULATOR_H
#define ENHANCED_COST_CALCULATOR_H

#include "Genome.h"
#include "BattleEmulator.h"
#include <array>
#include <cstddef>

class EnhancedCostCalculator {
public:
    enum class CostTable {
        TableA, // 元のGENOME
        TableB  // 新しいGENOME
    };

    // コストテーブルを切り替える（探索開始前に1回だけ呼ぶ）
    static void setCostTable(CostTable table);

    // Calculate enhanced g-cost with action-specific costs
    static double calculateGCost(const Genome &genome, int action, double preGCost, uint64_t NoState);

    // Calculate enhanced h-cost with multiple factors
    static double calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp, uint64_t NoStat);

private:
    // アクティブなテーブルへのポインタ（切り替えの実体）
    static const double* s_genome;

    // Get action-specific cost modifier（インライン化でホットパスを最速に）
    static inline double getActionCost(int action) {
        return (action >= 0 && action < 201) ? s_genome[action] : 0.0;
    }
    
    // Calculate status effect penalties/bonuses
    static double calculateStatusEffectCost(const Genome &genome);
    
    // Calculate resource-based cost (MP, items)
    static double calculateResourceCost(const Genome &genome);
};

#endif // ENHANCED_COST_CALCULATOR_H