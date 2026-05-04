//
// Enhanced Cost Calculator Implementation
//

#include "EnhancedCostCalculator.h"
#include <array>

#include "SimpleParameterOptimizer.h"

#if defined(OPTIMIZE_MODE)

#include "SimpleParameterOptimizer.h"

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost, uint64_t NowState) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + getActionCost(SimpleParameterOptimizerNode::turnHeignt);

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);

    uint8_t state = NowState & 0xf;
    if(state == BattleEmulator::TYPE_2A){
        gCost += getActionCost(SimpleParameterOptimizerNode::TYPE_2AWeight);
    }else if(state == BattleEmulator::TYPE_2B){
        gCost += getActionCost(SimpleParameterOptimizerNode::TYPE_2BWeight);
    }else if(state == BattleEmulator::TYPE_2C){
        gCost += getActionCost(SimpleParameterOptimizerNode::TYPE_2CWeight);
    }else if(state == BattleEmulator::TYPE_2D){
        gCost += getActionCost(SimpleParameterOptimizerNode::TYPE_2DWeight);
    }else if(state == BattleEmulator::TYPE_2E){
        gCost += getActionCost(SimpleParameterOptimizerNode::TYPE_2EWeight);
    }


    return gCost;
}

double EnhancedCostCalculator::calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp, uint64_t NowState) {
    if (genome.EnemyPlayer.hp <= 0) {
        return 0.0; // Goal reached
    }

    double hCost = 0.0;

    // Primary heuristic: enemy HP ratio (scaled down for better granularity)
    hCost = (genome.EnemyPlayer.hp / enemyMaxHp) * getActionCost(SimpleParameterOptimizerNode::enemyHpWeight);

    // Player HP consideration (more granular than original)
    double playerHpRatio = genome.AllyPlayer.hp / playerMaxHp;
    hCost += (1.0 - playerHpRatio) * getActionCost(SimpleParameterOptimizerNode::playerHpWeight);

    // MP consideration (resource management)
    hCost += calculateResourceCost(genome) * getActionCost(SimpleParameterOptimizerNode::resourceWeight);

    // Status effect penalties/bonuses
    hCost += calculateStatusEffectCost(genome) * getActionCost(SimpleParameterOptimizerNode::StatusEffectWeight);

    return hCost;
}

double EnhancedCostCalculator::getActionCost(int action) {
   return SimpleParameterOptimizer::getActionCost(action);
}

double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // Negative status effects (penalties)
    if (genome.AllyPlayer.paralysis) statusCost += getActionCost(SimpleParameterOptimizerNode::paralysisWeight);
    if (genome.AllyPlayer.sleeping) statusCost += getActionCost(SimpleParameterOptimizerNode::sleepWeight);

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * getActionCost(SimpleParameterOptimizerNode::BuffWeight);
    statusCost -= genome.AllyPlayer.AtkBuffLevel * getActionCost(SimpleParameterOptimizerNode::AtkBuffWeight);
    statusCost -= genome.AllyPlayer.TensionLevel * getActionCost(SimpleParameterOptimizerNode::TensionWeight);

    // Special abilities
    //if (genome.AllyPlayer.acrobaticStar) statusCost -= getActionCost(SimpleParameterOptimizerNode::ActHeight);
    if (genome.AllyPlayer.specialCharge) statusCost -= getActionCost(SimpleParameterOptimizerNode::SpHeight);
    if (genome.AllyPlayer.hasMagicMirror) statusCost -= getActionCost(SimpleParameterOptimizerNode::hasMagicMirrorHeight);

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * getActionCost(SimpleParameterOptimizerNode::ResourceHPCost); // Penalty for low MP
    }

    resourceCost += (3 - genome.AllyPlayer.SpecialMedicineCount) * getActionCost(SimpleParameterOptimizerNode::SpecialMedicineCost);
    resourceCost += (2 - genome.AllyPlayer.ElfinElixirCount) * getActionCost(SimpleParameterOptimizerNode::ElfinElixirCost);

    return resourceCost;
}

#else
constexpr std::array<double, 201> GENOME = {
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 5.84353,
        0.0,0.0,
        /* 30 */ 0.568706,
        /* 31 */ -0.892922,
        /* 32 */ 3.42785,
        /* 33 */ -0.192975,
        /* 34 */ -2.45272,
        0.0,
        /* 36 */ 2.37771,
        /* 37 */ 0.377641,
        /* 38 */ 4.21265,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 48 */ -2.44981,
        0.0,
        /* 50 */ 3.97609,
        0.0,
        /* 52 */ 3.22608,
        /* 53 */ 3.82255,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ 0.158835,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 1.25687,
        /* 151 */ 0.226153,
        /* 152 */ 1.6254,
        /* 153 */ 6.29028,
        /* 154 */ 1.53847,
        /* 155 */ 3.87233,
        /* 156 */ 2.55513,
        /* 157 */ -0.229176,
        /* 158 */ 1.77744,
        /* 159 */ -5.75963,
        /* 160 */ 1.81324,
        /* 161 */ 2.44924,
        /* 162 */ 2.82166,
        /* 163 */ 1.27783,
        /* 164 */ -0.40471,
        /* 165 */ 1.97589,
        /* 166 */ 2.78148,
        /* 167 */ -1.04918,
        0.0,
        /* 169 */ 2.63692,
        /* 170 */ 5.04647,
        /* 171 */ 1.05955,
        /* 172 */ 5.37454,
        /* 173 */ 4.2043,
        /* 174 */ -0.475724,
        /* 175 */ 0.857534,
        /* 176 */ 1.94189,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };
double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost, uint64_t NowState) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + GENOME[SimpleParameterOptimizerNode::turnHeignt];

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);

    uint8_t state = NowState & 0xf;
    if(state == BattleEmulator::TYPE_2A){
        gCost += GENOME[SimpleParameterOptimizerNode::TYPE_2AWeight];
    }else if(state == BattleEmulator::TYPE_2B){
        gCost += GENOME[SimpleParameterOptimizerNode::TYPE_2BWeight];
    }else if(state == BattleEmulator::TYPE_2C){
        gCost += GENOME[SimpleParameterOptimizerNode::TYPE_2CWeight];
    }else if(state == BattleEmulator::TYPE_2D){
        gCost += GENOME[SimpleParameterOptimizerNode::TYPE_2DWeight];
    }else if(state == BattleEmulator::TYPE_2E){
        gCost += GENOME[SimpleParameterOptimizerNode::TYPE_2EWeight];
    }

    return gCost;
}

double EnhancedCostCalculator::calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp, uint64_t NowState) {
    if (genome.EnemyPlayer.hp <= 0) {
        return 0.0; // Goal reached
    }

    double hCost = 0.0;

    // Primary heuristic: enemy HP ratio (scaled down for better granularity)
    hCost = (genome.EnemyPlayer.hp / enemyMaxHp) * GENOME[SimpleParameterOptimizerNode::enemyHpWeight];

    // Player HP consideration (more granular than original)
    double playerHpRatio = genome.AllyPlayer.hp / playerMaxHp;
    hCost += (1.0 - playerHpRatio) * GENOME[SimpleParameterOptimizerNode::playerHpWeight];

    // MP consideration (resource management)
    hCost += calculateResourceCost(genome) * GENOME[SimpleParameterOptimizerNode::resourceWeight];

    // Status effect penalties/bonuses
    hCost += calculateStatusEffectCost(genome) * GENOME[SimpleParameterOptimizerNode::StatusEffectWeight];


    uint8_t state = NowState & 0xf;
    if(state == BattleEmulator::TYPE_2A){
        hCost += GENOME[SimpleParameterOptimizerNode::TYPE_2AWeight];
    }else if(state == BattleEmulator::TYPE_2B){
        hCost += GENOME[SimpleParameterOptimizerNode::TYPE_2BWeight];
    }else if(state == BattleEmulator::TYPE_2C){
        hCost += GENOME[SimpleParameterOptimizerNode::TYPE_2CWeight];
    }else if(state == BattleEmulator::TYPE_2D){
        hCost += GENOME[SimpleParameterOptimizerNode::TYPE_2DWeight];
    }else if(state == BattleEmulator::TYPE_2E){
        hCost += GENOME[SimpleParameterOptimizerNode::TYPE_2EWeight];
    }

    return hCost;
}

double EnhancedCostCalculator::getActionCost(int action) {
    return (action >= 0 && action < GENOME.size()) ? GENOME[action] : 0.0;
}

double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // Negative status effects (penalties)
    if (genome.AllyPlayer.paralysis) statusCost += GENOME[SimpleParameterOptimizerNode::paralysisWeight];
    if (genome.AllyPlayer.sleeping) statusCost += GENOME[SimpleParameterOptimizerNode::sleepWeight];

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * GENOME[SimpleParameterOptimizerNode::BuffWeight];
    statusCost -= genome.AllyPlayer.AtkBuffLevel * GENOME[SimpleParameterOptimizerNode::AtkBuffWeight];
    statusCost -= genome.AllyPlayer.TensionLevel * GENOME[SimpleParameterOptimizerNode::TensionWeight];

    // Special abilities
   // if (genome.AllyPlayer.acrobaticStar) statusCost -= GENOME[SimpleParameterOptimizerNode::SpHeight];
    if (genome.AllyPlayer.specialCharge) statusCost -= GENOME[SimpleParameterOptimizerNode::SpHeight];
    if (genome.AllyPlayer.hasMagicMirror) statusCost -= GENOME[SimpleParameterOptimizerNode::hasMagicMirrorHeight];

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * GENOME[SimpleParameterOptimizerNode::ResourceHPCost]; // Penalty for low MP
    }

    resourceCost += (3 - genome.AllyPlayer.SpecialMedicineCount) * GENOME[SimpleParameterOptimizerNode::SpecialMedicineCost];
    resourceCost += (2 - genome.AllyPlayer.ElfinElixirCount) * GENOME[SimpleParameterOptimizerNode::ElfinElixirCost];

    return resourceCost;
}

#endif