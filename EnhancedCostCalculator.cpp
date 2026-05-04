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
static constexpr std::array<double, 201> GENOME_A = {
    0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 27 */ 2.59815,
        0.0,0.0,
        /* 30 */ 0.991185,
        /* 31 */ 0.877488,
        /* 32 */ 1.47669,
        /* 33 */ -0.467152,
        /* 34 */ -0.560149,
        0.0,
        /* 36 */ 3.82519,
        /* 37 */ 3.11601,
        /* 38 */ 3.48204,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 48 */ 0.225563,
        0.0,
        /* 50 */ 2.42553,
        0.0,
        /* 52 */ 2.55465,
        /* 53 */ 3.02135,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 62 */ -0.685323,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,
        /* 150 */ 1.25479,
        /* 151 */ 0.943889,
        /* 152 */ 1.12736,
        /* 153 */ 2.52265,
        /* 154 */ 0.871853,
        /* 155 */ -1.38709,
        /* 156 */ 2.27589,
        /* 157 */ -0.53187,
        /* 158 */ 2.16594,
        /* 159 */ -0.206523,
        /* 160 */ -1.6591,
        /* 161 */ 2.01669,
        /* 162 */ 2.68847,
        /* 163 */ 1.43298,
        /* 164 */ 1.34218,
        /* 165 */ 0.985288,
        /* 166 */ -0.464629,
        /* 167 */ 0.311034,
        0.0,
        /* 169 */ 1.87985,
        /* 170 */ 4.16472,
        /* 171 */ 0.326611,
        /* 172 */ 0.0543113,
        /* 173 */ 0.267389,
        /* 174 */ -0.604447,
        /* 175 */ -0.428284,
        /* 176 */ 1.09807,
        0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0
    };

static constexpr std::array<double, 201> GENOME_B = {
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


// ---- staticメンバの実体 ----
const double* EnhancedCostCalculator::s_genome = GENOME_A.data();

double EnhancedCostCalculator::calculateGCost(const Genome &genome, int action, double preGCost, uint64_t NowState) {
    // Base cost is turn number (maintains depth-first preference)
    double gCost = preGCost + s_genome[SimpleParameterOptimizerNode::turnHeignt];

    // Add fine-grained action costs to break ties
    gCost += getActionCost(action);

    uint8_t state = NowState & 0xf;
    if(state == BattleEmulator::TYPE_2A){
        gCost += s_genome[SimpleParameterOptimizerNode::TYPE_2AWeight];
    }else if(state == BattleEmulator::TYPE_2B){
        gCost += s_genome[SimpleParameterOptimizerNode::TYPE_2BWeight];
    }else if(state == BattleEmulator::TYPE_2C){
        gCost += s_genome[SimpleParameterOptimizerNode::TYPE_2CWeight];
    }else if(state == BattleEmulator::TYPE_2D){
        gCost += s_genome[SimpleParameterOptimizerNode::TYPE_2DWeight];
    }else if(state == BattleEmulator::TYPE_2E){
        gCost += s_genome[SimpleParameterOptimizerNode::TYPE_2EWeight];
    }

    return gCost;
}

double EnhancedCostCalculator::calculateHCost(const Genome &genome, double enemyMaxHp, double playerMaxHp, uint64_t NowState) {
    if (genome.EnemyPlayer.hp <= 0) {
        return 0.0; // Goal reached
    }

    double hCost = 0.0;

    // Primary heuristic: enemy HP ratio (scaled down for better granularity)
    hCost = (genome.EnemyPlayer.hp / enemyMaxHp) * s_genome[SimpleParameterOptimizerNode::enemyHpWeight];

    // Player HP consideration (more granular than original)
    double playerHpRatio = genome.AllyPlayer.hp / playerMaxHp;
    hCost += (1.0 - playerHpRatio) * s_genome[SimpleParameterOptimizerNode::playerHpWeight];

    // MP consideration (resource management)
    hCost += calculateResourceCost(genome) * s_genome[SimpleParameterOptimizerNode::resourceWeight];

    // Status effect penalties/bonuses
    hCost += calculateStatusEffectCost(genome) * s_genome[SimpleParameterOptimizerNode::StatusEffectWeight];


    uint8_t state = NowState & 0xf;
    if(state == BattleEmulator::TYPE_2A){
        hCost += s_genome[SimpleParameterOptimizerNode::TYPE_2AWeight];
    }else if(state == BattleEmulator::TYPE_2B){
        hCost += s_genome[SimpleParameterOptimizerNode::TYPE_2BWeight];
    }else if(state == BattleEmulator::TYPE_2C){
        hCost += s_genome[SimpleParameterOptimizerNode::TYPE_2CWeight];
    }else if(state == BattleEmulator::TYPE_2D){
        hCost += s_genome[SimpleParameterOptimizerNode::TYPE_2DWeight];
    }else if(state == BattleEmulator::TYPE_2E){
        hCost += s_genome[SimpleParameterOptimizerNode::TYPE_2EWeight];
    }

    return hCost;
}

void EnhancedCostCalculator::setCostTable(CostTable table) {
    s_genome = (table == CostTable::TableA) ? GENOME_A.data() : GENOME_B.data();
}

double EnhancedCostCalculator::calculateStatusEffectCost(const Genome &genome) {
    double statusCost = 0.0;

    // Negative status effects (penalties)
    if (genome.AllyPlayer.paralysis) statusCost += s_genome[SimpleParameterOptimizerNode::paralysisWeight];
    if (genome.AllyPlayer.sleeping) statusCost += s_genome[SimpleParameterOptimizerNode::sleepWeight];

    // Positive status effects (bonuses - negative cost)
    statusCost -= genome.AllyPlayer.BuffLevel * s_genome[SimpleParameterOptimizerNode::BuffWeight];
    statusCost -= genome.AllyPlayer.AtkBuffLevel * s_genome[SimpleParameterOptimizerNode::AtkBuffWeight];
    statusCost -= genome.AllyPlayer.TensionLevel * s_genome[SimpleParameterOptimizerNode::TensionWeight];

    // Special abilities
   // if (genome.AllyPlayer.acrobaticStar) statusCost -= s_genome[SimpleParameterOptimizerNode::SpHeight];
    if (genome.AllyPlayer.specialCharge) statusCost -= s_genome[SimpleParameterOptimizerNode::SpHeight];
    if (genome.AllyPlayer.hasMagicMirror) statusCost -= s_genome[SimpleParameterOptimizerNode::hasMagicMirrorHeight];

    return statusCost;
}

double EnhancedCostCalculator::calculateResourceCost(const Genome &genome) {
    double resourceCost = 0.0;

    // MP consideration
    if (genome.AllyPlayer.maxMp > 0) {
        double mpRatio = static_cast<double>(genome.AllyPlayer.mp) / genome.AllyPlayer.maxMp;
        resourceCost += (1.0 - mpRatio) * s_genome[SimpleParameterOptimizerNode::ResourceHPCost]; // Penalty for low MP
    }

    resourceCost += (3 - genome.AllyPlayer.SpecialMedicineCount) * s_genome[SimpleParameterOptimizerNode::SpecialMedicineCost];
    resourceCost += (2 - genome.AllyPlayer.ElfinElixirCount) * s_genome[SimpleParameterOptimizerNode::ElfinElixirCost];

    return resourceCost;
}

#endif