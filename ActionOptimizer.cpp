//
// Compatibility entry points. The final search lives in ErusionnSearch.cpp.
//

#include "ActionOptimizer.h"
#include "ErusionnSearch.h"
#include <random>
#include <unordered_set>
#include <memory>

#include "BattleEmulator.h"
#include "LinearIdPool.h"
#include "Genome.h"
#include "EnhancedHashCalculator.h"
#include "EnhancedCostCalculator.h"
#include "EnhancedHeapQueue.h"
#include "lcg.h"

struct ActionEntry{
	int action;
	bool (*condition)(const Genome&);
	// 実行後、効果があったか（事後）
	bool (*isEffective)(
		const Genome& before,
		const Genome& after
	);
};

// 1要素の検証
constexpr bool isValid(const ActionEntry& e){
	return e.condition != nullptr
		&& e.isEffective != nullptr;
}

// テーブル全体の検証
template <size_t N>
constexpr bool validateActionTable(const ActionEntry (&table)[N]){
	for(size_t i = 0; i < N; ++i){
		if(!isValid(table[i])){
			return false;
		}
	}
	return true;
}

// Legacy candidate reference; not executed by RunAlgorithm.
constexpr ActionEntry ACTION_TABLE[] = {
	{
		BattleEmulator::MIDHEAL, [](const Genome& g){
			return (g.AllyPlayer.hp / g.AllyPlayer.maxHp) < 0.7 && g.AllyPlayer.hp >= 4;
		},
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::SPECIAL_MEDICINE, [](const Genome& g){
			return g.AllyPlayer.SpecialMedicineCount > 0;
		},
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::FLEE_ALLY, [](const Genome&){ return true; },
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::DOUBLE_UP,
		[](const Genome& g){
			return g.AllyPlayer.AtkBuffLevel == 0;
		},
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::PSYCHE_UP_ALLY,
		[](const Genome& g){
			return g.EnemyPlayer.hp > 180 && g.AllyPlayer.TensionLevel <= 3;
		},
		[](const Genome& before, const Genome& after){
			return after.AllyPlayer.TensionLevel > before.AllyPlayer.TensionLevel;
		}
		// 事後: テンションが上がったか

	},
	{
		BattleEmulator::BUFF,
		[](const Genome& g){ return g.AllyPlayer.mp >= 10 && g.AllyPlayer.BuffLevel <= 1; },
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::MULTITHRUST,
		[](const Genome& g){ return g.AllyPlayer.mp >= 10 && g.AllyPlayer.AtkBuffLevel >= 2; },
		[](const Genome&, const Genome&){ return true; }
	},
	{
		BattleEmulator::DEFENCE,
		[](const Genome&){ return true; },
		[](const Genome&, const Genome&){ return true; }
	},
};

// ★ ここが本体 ★
static_assert(
	validateActionTable(ACTION_TABLE),
	"ACTION_TABLE contains null function pointer"
);

static uint32_t Node_Used;

uint32_t ActionOptimizer::getNodesUsed(){
	return Node_Used;
}

// Preserve callers of the previous optimizer API.
Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                                     int actions[350], int seedOffset){
    (void)turns; (void)maxGenerations; (void)seedOffset;
    auto result = ErusionnSearch::Run(players, seed, actions);
    Node_Used = static_cast<uint32_t>(result.expanded);
    return result.genome;
}

void ActionOptimizer::updateCompromiseScore(Genome& genome){
	// Enemy action penalty processing (unchanged)
}

std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(const Player players[2], uint64_t seed, int turns,
                                                          int maxGenerations, int actions[350], int numThreads,
                                                          bool dropbug){
	(void)numThreads;
	(void)dropbug;
	auto genome = RunAlgorithm(players, seed, turns, maxGenerations, actions, 0);
	return {0, genome};
}
