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
        TableA, // 元のGENOME_A
        TableB, // GENOME_B
        TableC,  // 新しいGENOME_C
        TableD,  // 新しいGENOME_C
        TableF,  // 新しいGENOME_C
        TableG,  // 新しいGENOME_C
    };
    static CostTable getCostTable() noexcept;
#if !defined(OPTIMIZE_MODE)
    // コストテーブルを切り替える（探索開始前に1回だけ呼ぶ）
    static void setCostTable(CostTable table);
#endif

    // Calculate enhanced g-cost with action-specific costs
    static double calculateGCost(const Genome &genome, int action, double preGCost, uint64_t NoState,
                                 const int transitionEvents[8] = nullptr, int transitionEventCount = 0);

    // Calculate enhanced h-cost with multiple factors
    static double calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp, uint64_t NoStat);

private:
#if !defined(OPTIMIZE_MODE)
    // アクティブなテーブルへのポインタ（切り替えの実体）
    static const double* s_genome;
#endif
    static double getActionCost(int action);

    // Calculate status effect penalties/bonuses
    static double calculateStatusEffectCost(const Genome &genome);

    // Calculate resource-based cost (MP, items)
    static double calculateResourceCost(const Genome &genome);
};

#endif // ENHANCED_COST_CALCULATOR_H
