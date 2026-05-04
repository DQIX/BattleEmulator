#include <iostream>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>

#include "lcg.h"
#include "BattleEmulator.h"
#include "debug.h"
#include "ActionOptimizer.h"
#include "EnhancedCostCalculator.h"

#ifdef DEBUG

#include <chrono>

#endif

#if defined(OPTIMIZE_MODE)
#include "SimpleParameterOptimizer.h"
#endif

const Player copiedPlayers[2] = {
	// プレイヤー1
	{
		309, 309.0, 312, 312, 298, 298, 193, 234, 165, // 最初のメンバー
		165, false, false, 0, false, 0, -1,
		// specialCharge, dirtySpecialCharge, specialChargeTurn, inactive, paralysis, paralysisLevel, paralysisTurns
		3, 1.0, false, -1, 0, -1, // SpecialMedicineCount, defence, sleeping, sleepingTurn, BuffLevel, BuffTurns
		false, -1, 0, -1, 0, false, 1, 1, 1
	}, // hasMagicMirror, MagicMirrorTurn, AtkBuffLevel, AtkBuffTurn, TensionLevel

	// プレイヤー2
	{
		4800, 4800.0, 248, 248, 278, 278, 157, 0, 255, // 最初のメンバー
		255, false, false, 0, false, 0, -1,
		// specialCharge, dirtySpecialCharge, specialChargeTurn, inactive, paralysis, paralysisLevel, paralysisTurns
		8, 1.0, false, -1, 0, -1, // SpecialMedicineCount, defence, sleeping, sleepingTurn, BuffLevel, BuffTurns
		false, -1, 0, -1, 0, false, 0, 0, 0
	} // hasMagicMirror, MagicMirrorTurn, AtkBuffLevel, AtkBuffTurn, TensionLevel
};

// 勝利フラグと確定した敵残HPを返す
struct RunResult {
	bool win;
	int enemyHp;   // 使わなくなったが一応残す
	int turn;
	int position;
};

int toint(char* string);

//void processResult(const Player *copiedPlayers, const uint64_t seed, std::string input);

std::string ltrim(const std::string& s);

std::string rtrim(const std::string& s);

std::string trim(const std::string& s);

bool SearchRequest(const Player copiedPlayers[2], uint64_t seed, const int aActions[350], bool dropbug, std::stringstream& ss);

uint64_t BruteForceRequest(const Player copiedPlayers[2], int hours, int minutes, int seconds, int turns,
                           int eActions[350],
                           int aActions[350], int damages[350]);


void mainLoop(const Player copiedPlayers[2]);

using namespace std;

int foundSeeds = 0;

uint64_t FoundSeed = 0;

void printHeader(std::stringstream& ss);

// ヘッダーを出力する関数
void printHeader(std::stringstream& ss){
	ss << std::left << std::setw(6) << "turn"
		<< std::setw(18) << "sp"
		<< std::setw(18) << "aAct"
		<< std::setw(18) << "eAct1"
		<< std::setw(18) << "eAct2"
		<< std::setw(6) << "aD"
		<< std::setw(6) << "eD1"
		<< std::setw(6) << "eD2"
		<< std::setw(6) << "ahp"
		<< std::setw(6) << "ehp"
		<< std::setw(6) << "amp"

		<< std::setw(6) << "ini"
		<< std::setw(6) << "Para"
		<< std::setw(6) << "Sle"
		<< std::setw(6) << "ATT"
		<< std::setw(6) << "DET"
		<< std::setw(6) << "MMT"
		<< std::setw(6) << "Tab"
		<< std::setw(6) << "Sct" << "\n";
	ss << std::string(140, '-') << "\n"; // 区切り線を出力
}

std::string dumpTable(const BattleResult& result,const int32_t gene[350], int PastTurns);

std::string dumpTable(const BattleResult& result, const int32_t gene[350], int PastTurns){
	stringstream ss6;
	printHeader(ss6);
	int currentTurn = -1;
	int eDamage[2] = {-1, -1}, aDamage = -1;
	bool initiative_tmp = false;
	std::string eAction[2], aAction, sp, tmpState, ATKTurn1, DEFTurn1, magicMirrorTurn1, specialChargeTurn1, amp1, ahp2,
	            ehp2, amp2;
	auto counter = 0;
	// データのループ
	for(int i = 0; i < result.position; ++i){
		auto action = result.actions[i];
		auto damage = result.damages[i];
		auto ATKTurn = result.AtkBuffTurns[i];
		auto DEFTurn = result.BuffTurnss[i];
		auto magicMirrorTurn = result.MagicMirrorTurns[i];
		auto turn = result.turns[i];
		auto initiative = result.initiative[i];
		auto ehp1 = result.ehp[i];
		auto ahp1 = result.ahp[i];
		auto isEnemy = result.isEnemy[i];
		auto state = result.state[i] & 0xf;
		auto specialChargeTurn = result.scTurn[i];
		int amp = -1;
		if(i >= 1){
			amp = result.amp[i - 1];
		}


		if(state == BattleEmulator::TYPE_2A){
			tmpState = "A";
		}
		else if(state == BattleEmulator::TYPE_2B){
			tmpState = "B";
		}
		else if(state == BattleEmulator::TYPE_2C){
			tmpState = "C";
		}
		else if(state == BattleEmulator::TYPE_2D){
			tmpState = "D";
		}
		if(state == BattleEmulator::TYPE_2E){
			tmpState = "E";
		}

		auto special = gene[turn];

		std::string specialAction;
		if(special != 0 && special != -1){
			specialAction = BattleEmulator::getActionName(special & 0x3ff);
		}

		// ターンが変わったら、前のターンのデータを出力
		if(turn != currentTurn){
			if(currentTurn != -1){
				// 前のターンの出力
				if(turn > PastTurns){
					ss6
						<< std::left << std::setw(6) << (currentTurn + 1)
						<< std::setw(18) << sp
						<< std::setw(18) << aAction
						<< std::setw(18) << eAction[0]
						<< std::setw(18) << eAction[1]
						<< std::setw(6) << aDamage
						<< std::setw(6) << eDamage[0]
						<< std::setw(6) << eDamage[1]
						<< std::setw(6) << ahp2
						<< std::setw(6) << ehp2
						<< std::setw(6) << amp2
						<< std::setw(6) << (initiative_tmp ? "yes" : "")
						<< std::setw(6) << ((aAction == "Paralysis" || aAction == "Cure Paralysis") ? "yes" : "")
						<< std::setw(6) << ((aAction == "Sleeping" || aAction == "Cure Sleeping") ? "yes" : "")
						<< std::setw(6) << ATKTurn1
						<< std::setw(6) << DEFTurn1
						<< std::setw(6) << magicMirrorTurn1
						<< std::setw(6) << tmpState
						<< std::setw(6) << specialChargeTurn1
						<< std::setw(11) << "" << "\n";
				}
			}
			// ターンの初期化
			currentTurn = turn;
			eAction[0] = "";
			eAction[1] = "";
			aAction = "";
			eDamage[0] = 0;
			eDamage[1] = 0;
			aDamage = 0;
			sp = "";
			initiative_tmp = false;
			counter = 0;
			ATKTurn1 = "";
			DEFTurn1 = "";
			magicMirrorTurn1 = "";
			specialChargeTurn1 = "";
		}

		// 敵か味方の行動を適切な変数に格納
		if(isEnemy){
			eAction[counter] = BattleEmulator::getActionName(action);
			eDamage[counter] = damage;
			counter++;
			ahp2 = std::to_string(ahp1);
		}
		else{
			ehp2 = std::to_string(ehp1);
			amp2 = std::to_string(amp);
			aAction = BattleEmulator::getActionName(action);
			aDamage = damage;
			if(ATKTurn >= 0){
				ATKTurn1 = std::to_string(ATKTurn);
			}
			if(DEFTurn >= 0){
				DEFTurn1 = std::to_string(DEFTurn);
			}
			if(magicMirrorTurn >= 0){
				magicMirrorTurn1 = std::to_string(magicMirrorTurn);
			}
			if(specialChargeTurn > 0){
				specialChargeTurn1 = std::to_string(specialChargeTurn);
			}

			amp1 = std::to_string(amp);

			initiative_tmp = initiative;
			sp = specialAction;

			if(eAction[0] != "magic Burst" && eAction[1] != "magic Burst"){
				if(!initiative && action == BattleEmulator::TURN_SKIPPED || action == BattleEmulator::PARALYSIS ||
					action == BattleEmulator::SLEEPING){
					sp = "---------------";
				}
				if((action == BattleEmulator::CURE_SLEEPING || action == BattleEmulator::CURE_PARALYSIS)){
					sp = "---------------";
				}
			}
		}
	}

	// 最後のターンのデータを出力
	if(currentTurn != -1){
		ss6
			<< std::left << std::setw(6) << (currentTurn + 1)
			<< std::setw(18) << sp
			<< std::setw(18) << aAction
			<< std::setw(18) << eAction[0]
			<< std::setw(18) << eAction[1]
			<< std::setw(6) << aDamage
			<< std::setw(6) << eDamage[0]
			<< std::setw(6) << eDamage[1]
			<< std::setw(6) << ahp2
			<< std::setw(6) << ehp2
			<< std::setw(6) << amp2
			<< std::setw(6) << (initiative_tmp ? "yes" : "")
			<< std::setw(6) << ((aAction == "Paralysis" || aAction == "Cure Paralysis") ? "yes" : "")
			<< std::setw(6) << ((aAction == "Sleeping") ? "yes" : "")
			<< std::setw(6) << ATKTurn1
			<< std::setw(6) << DEFTurn1
			<< std::setw(6) << magicMirrorTurn1
			<< std::setw(6) << tmpState
			<< std::setw(6) << specialChargeTurn1
			<< std::setw(11) << "" << "\n";
	}

	return ss6.str();
}

const std::string version = "v1.0.17e";

void showHeader(){
#ifdef BUILD_DATE
	const std::string buildDate = BUILD_DATE;
#else
	const std::string buildDate = "Unknown";
#endif

#ifdef BUILD_TIME
	const std::string buildTime = BUILD_TIME;
#else
	const std::string buildTime = "Unknown";
#endif

	auto compiler = "Unknown";
#if defined(MINGW_BUILD)
	compiler = "mingw";
#elif defined(MSVC_BUILD)
	compiler = "msBuild";
#endif


#if defined(OPTIMIZATION_O3_ENABLED)
	std::cout << "dq9 Corvus battle emulator " << version << " (Optimized for O3), Build date: " << buildDate << ", " <<
		buildTime << " UTC/GMT, Compiler: " << compiler << std::endl;
#elif defined(OPTIMIZATION_O2_ENABLED)
	std::cout << "dq9 Corvus battle emulator " << version << " (Optimized for O2), Build date: " << buildDate << ", " << buildTime << " UTC/GMT, Compiler: " << compiler << std::endl;
#elif defined(NO_OPTIMIZATION)
	std::cout << "dq9 Corvus battle emulator " << version << " (No optimization), Build date: " << buildDate << ", " << buildTime << " UTC/GMT, Compiler: " << compiler << std::endl;
#else
#endif
	std::cout << "Waiting for input[q/b]: " << std::endl;
}


//int main(int argc, char *argv[]) {
int main(){
	showHeader();

	//https://zenn.dev/reputeless/books/standard-cpp-for-competitive-programming/viewer/library-ios-iomanip#3.1-c-%E8%A8%80%E8%AA%9E%E3%81%AE%E5%85%A5%E5%87%BA%E5%8A%9B%E3%82%B9%E3%83%88%E3%83%AA%E3%83%BC%E3%83%A0%E3%81%A8%E3%81%AE%E5%90%8C%E6%9C%9F%E3%82%92%E7%84%A1%E5%8A%B9%E3%81%AB%E3%81%99%E3%82%8B
	//std::cin.tie(0)->sync_with_stdio(0);



#if defined(OPTIMIZE_MODE)
	int actions1[350] = {};
	auto counter1 = 0;
	actions1[counter1++] = BattleEmulator::BUFF;
	actions1[counter1++] = BattleEmulator::MAGIC_MIRROR;
	actions1[counter1++] = BattleEmulator::PSYCHE_UP_ALLY;
	actions1[counter1] = -1;
	SimpleParameterOptimizer::optimize(copiedPlayers, 0x112345, actions1, 100000, counter1);
	return 0;
#endif


#ifdef DEBUG2
	//THIS DEBUG CODE!
	//THIS DEBUG CODE
	uint64_t time1 = 0x932ca66;

	int dummy[100];
	lcg::init(time1);
	int* position1 = new int(1);
	/*
	    *NowStateの各ビットの使用状況は下記の通りである。
	    +-+-+-+-+-+-+-+-+- (* NowState) -+-+-+-+-+-+-+-+-+
	       |            Name            |     size      |
	    0  | Current Rotation Table     |     4bit      |
	    4  | Rotation Internal State    |     4bit      |
	    8  | Free Camera State          |     4bit      |
	    12 | Turn Count Processed       |     20bit     |
	    32 | Combo Previous Attack Id   |     2byte     |
	    40 | Combo Counter              |     1byte     |
	    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	                                 合計 6Byte
	*/
	auto* NowState = new uint64_t(0); //エミュレーターの内部ステートを表すint

	Player players1[2];
	//int32_t gene1[350] = {0};
	//THIS DEBUG CODE!
	int32_t gene1[350] = {30, 31, 62, 62, 50, 53, 62, 30, 31, 34, 53, 33, 31, 34, 34, 34, 34, 53,};
	//gene1[19-1] = BattleEmulator::DEFENCE;
	int counter = 0;

	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::MAGIC_MIRROR;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::DOUBLE_UP;
	// gene1[counter++] = BattleEmulator::DOUBLE_UP;
	// gene1[counter++] = BattleEmulator::DOUBLE_UP;
	// gene1[counter++] = BattleEmulator::DEFENDING_CHAMPION;
	// gene1[counter++] = BattleEmulator::DEFENDING_CHAMPION;
	// gene1[counter++] = BattleEmulator::MAGIC_MIRROR;
	// gene1[counter++] = BattleEmulator::MULTITHRUST;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::MULTITHRUST;
	// gene1[counter++] = BattleEmulator::MULTITHRUST;
	// gene1[counter++] = BattleEmulator::MULTITHRUST;
	// gene1[counter++] = BattleEmulator::MORE_HEAL;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::MAGIC_MIRROR;
	// gene1[counter++] = BattleEmulator::BUFF;
	// gene1[counter++] = BattleEmulator::DOUBLE_UP;
	// gene1[counter++] = BattleEmulator::MULTITHRUST;
	// gene1[counter++] = BattleEmulator::MULTITHRUST;
	// gene1[counter++] = BattleEmulator::MULTITHRUST;

	//for (int i = 0; i < 10; ++i) {
	(*NowState) = BattleEmulator::TYPE_2A;
	(*position1) = 1;
	BattleResult dummy1;
	std::memcpy(players1, copiedPlayers, sizeof(players1));
	BattleEmulator::Main(position1, 30, gene1, players1, &dummy1, time1, dummy, dummy, -1, NowState);

	std::stringstream ss1;
	ss1 << time1 << " ";
	std::cout << dumpTable(dummy1, gene1, -1) << std::endl;
	//}
	delete position1;
	delete NowState;

	return 0;
#endif

#ifdef DEBUG3
	uint64_t time1 = 0x11029ull;

	auto counter = 0;
	int actions[350] = {0};
	actions[counter++] = BattleEmulator::BUFF;
	actions[counter++] = BattleEmulator::MAGIC_MIRROR;
	actions[counter++] = BattleEmulator::PSYCHE_UP_ALLY;
	actions[counter] = -1;

	std::stringstream ss;
	SearchRequest(copiedPlayers, time1, actions, false, ss);
	std::cout << ss.str();
	return 0;
#endif

	mainLoop(copiedPlayers);
	return 0;
}

bool SearchRequest(const Player copiedPlayers[2], uint64_t seed, const int aActions[350], bool dropbug, std::stringstream &ss){
	int32_t gene[350] = {0};
	auto turns = 0;
	for(int i = 0; i < 350; ++i){
		gene[i] = aActions[i];
		if(aActions[i] == -1){
			gene[i] = -1;
			gene[i + 1] = -1;
			break;
		}
		turns++;
	}

	lcg::init(seed);

	Genome genomeA, genomeB;

#if !defined(OPTIMIZE_MODE)

    // --- TableA で探索 ---
    EnhancedCostCalculator::setCostTable(EnhancedCostCalculator::CostTable::TableA);
    genomeA = ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 5000, gene, 0);

    // --- TableB で探索 ---
    EnhancedCostCalculator::setCostTable(EnhancedCostCalculator::CostTable::TableB);
    genomeB = ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 5000, gene, 0);

    // --- TableC で探索 ---
    EnhancedCostCalculator::setCostTable(EnhancedCostCalculator::CostTable::TableC);
    Genome genomeC = ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 5000, gene, 0);

	// --- TableC で探索 ---
	EnhancedCostCalculator::setCostTable(EnhancedCostCalculator::CostTable::TableD);
	Genome genomeD = ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 5000, gene, 0);


    BattleResult resultA, resultB, resultC, resultD;

	auto runMain = [&](const Genome& g, BattleResult& res) -> RunResult {
		Player players[2] = {copiedPlayers[0], copiedPlayers[1]};
		int position = 1;
		uint64_t nowState = 0;
		BattleEmulator::Main(&position, 100, g.actions, players, &res, seed, nullptr, nullptr, -1, &nowState);
		bool win = players[1].hp <= 0;
		return { win, players[1].hp, res.turn, res.position };
	};

    auto rrA = runMain(genomeA, resultA);
    auto rrB = runMain(genomeB, resultB);
    auto rrC = runMain(genomeC, resultC);
    auto rrD = runMain(genomeD, resultD);

    if (!rrA.win && !rrB.win && !rrC.win && !rrD.win) {
        return false;
    }

    // 勝利したもの同士でターン数→敵残HP（メモ化済み）で比較
    // 負けたものは無条件で除外
	auto isBetter = [](const RunResult& a, const RunResult& b) -> bool {
		if (a.turn != b.turn) return a.turn < b.turn;
		return a.position < b.position;  // 同ターンなら行動数が少ない方
    };

    const Genome* chosenGenome = nullptr;
    const BattleResult* chosenResult = nullptr;
    const RunResult* chosenRR = nullptr;

    auto tryUpdate = [&](const RunResult& rr, const Genome& g, const BattleResult& r) {
        if (!rr.win) return;  // 負けは無価値
        if (chosenRR == nullptr || isBetter(rr, *chosenRR)) {
            chosenGenome = &g;
            chosenResult = &r;
            chosenRR = &rr;
        }
    };

	//A（ケース1）: ためる・すてみ → Multithrust のテンション蓄積戦法
	//B（ケース3）: メラゾーマ反射しながら長期消耗戦
	//C（ケース2）: 最短ルートでメラゾーマ反射 → 最速決着
    tryUpdate(rrA, genomeA, resultA);
    tryUpdate(rrB, genomeB, resultB);
    tryUpdate(rrC, genomeC, resultC);
    tryUpdate(rrD, genomeD, resultD);

    ss << dumpTable(*chosenResult, chosenGenome->actions, 0) << std::endl;

    ss << "0x" << std::hex << seed << std::dec << ": ";

	for (auto i = 0; i < 100; ++i) {
		if (chosenGenome->actions[i] == 0 || chosenGenome->actions[i] == -1) {
			break;
		}
		ss << chosenGenome->actions[i] << ", ";
	}
	ss << std::endl;

	// --- 各テーブルの結果をログ出力 ---
	auto printRunResult = [&](const char* label, const RunResult& rr) {
		ss << "[" << label << "] ";
		if (rr.win) {
			ss << "Win  turn=" << (rr.turn + 1) << " position=" << rr.position;
		} else {
			ss << "Lose";
		}
		ss << std::endl;
	};
	printRunResult("TableA", rrA);
	printRunResult("TableB", rrB);
	printRunResult("TableC", rrC);
	printRunResult("TableD", rrD);


#endif

	//探索成功
	return true;
}

// ブルートフォースリクエスト関数
[[nodiscard]] uint64_t BruteForceRequest(const Player copiedPlayers[2], int hours, int minutes, int seconds, int turns,
                                         int eActions[350],
                                         int aActions[350], int damages[350]){
	std::cout << "BruteForceRequest executed with time " << hours << ":" << minutes << ":" << seconds << std::endl;
	std::cout << "eActions: ";
	for(int i = 0; i < 350 && eActions[i] != -1; ++i) std::cout << eActions[i] << " ";
	std::cout << "\naActions: ";
	for(int i = 0; i < 350 && aActions[i] != -1; ++i) std::cout << aActions[i] << " ";
	std::cout << "\ndamages: ";
	for(int i = 0; i < 350 && damages[i] != -1; ++i) std::cout << damages[i] << " ";
	std::cout << std::endl;

	foundSeeds = 0;
	FoundSeed = 0;

	int totalSeconds = hours * 3600 + minutes * 60 + seconds;
	totalSeconds = totalSeconds - 17;
	//数字は探索範囲(秒)
	auto time1 = static_cast<uint64_t>(floor((totalSeconds - 30) * (1 / 0.12515)));
	time1 = time1 << 16;
	std::cout << time1 << std::endl;

	//数字は探索範囲(秒)
	auto time2 = static_cast<uint64_t>(floor((totalSeconds + 30) * (1 / 0.125155)));
	time2 = time2 << 16;
	std::cout << time2 << std::endl;
	int32_t gene[350] = {0};
	for(int i = 0; i < 350; ++i){
		gene[i] = aActions[i];
		if(aActions[i] == -1){
			gene[i] = -1;
			gene[i + 1] = -1;
			break;
		}
	}

	/*
	*NowStateの各ビットの使用状況は下記の通りである。
	+-+-+-+-+-+-+-+-+- (* NowState) -+-+-+-+-+-+-+-+-+
	   |            Name            |     size      |
	0  | Current Rotation Table     |     4bit      |
	4  | Rotation Internal State    |     4bit      |
	8  | Free Camera State          |     4bit      |
	12 | Turn Count Processed       |     20bit     |
	32 | Combo Previous Attack Id   |     2byte     |
	40 | Combo Counter              |     1byte     |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	                             合計 6Byte
	*/
	int* position = new int(1);
	auto* nowState = new uint64_t(0);
	int maxElement = 350;
	Player players[2];
	for(uint64_t seed = time1; seed < time2; ++seed){
		//        if (seed % 1000000000 == 0) {
		//            std::cout << seed << std::endl;
		//        }
		lcg::init(seed);
		// for (int st = BattleEmulator::TYPE_2A; st < BattleEmulator::TYPE_2D; ++st) {
		(*nowState) = BattleEmulator::TYPE_2A;
		(*position) = 1;
		//std::memcpy(players, copiedPlayers, sizeof(players));
		players[0] = copiedPlayers[0];
		players[1] = copiedPlayers[1];


		bool resultBool = BattleEmulator::Main(position, turns, gene, players,
		                                       nullptr, seed, eActions, damages,
		                                       maxElement,
		                                       nowState);
		if(resultBool){
			//std::cout << seed << ", " << st << std::endl;
			std::cout << std::hex << seed << std::dec << std::endl;
			FoundSeed = seed;
			foundSeeds++;
		}
		//}
	}
	delete position;
	delete nowState;

	std::cout << std::endl << "found: " << foundSeeds << std::endl;

	if(foundSeeds == 1){
		return FoundSeed;
	}
	if(foundSeeds == 0){
		std::cout << "not found!!!" << std::endl;
		return 0;
	}
	FoundSeed = 0;
	foundSeeds = 0;
	return 0;
}


void BruteForceMainLoop(const Player copiedPlayers[2], uint64_t start, uint64_t end, int gene[350],
						int damages[350], int eaction1[350]) {
	int *position = new int(1);
	auto *nowState = new uint64_t(0);
	int maxElement = 350;
	for (uint64_t seed = start; seed < end; ++seed) {
		BattleEmulator::resetStartTurn();
		lcg::init(seed);
		(*nowState) = 0;
		(*position) = 1;
		Player players[2] = {copiedPlayers[0], copiedPlayers[1]};


		bool resultBool = BattleEmulator::Main(position, 100, gene, players,
											  nullptr, seed, eaction1,
											   damages,
											   maxElement,
											   nowState);
		if (resultBool) {
			std::cout << seed << std::endl;
			FoundSeed = seed;
			foundSeeds++;
		}
	}
	delete position;
	delete nowState;
}

// 入力文字列を配列に分割するヘルパー関数
void parseActions(const std::string& str, int actions[350]){
	std::istringstream iss(str);
	int value, index = 0;
	while(iss >> value && index < 350){
		actions[index++] = value;
	}
	actions[index++] = -1;
	actions[index++] = -1;
	actions[index++] = -1;
}


// メインループ
void mainLoop(const Player copiedPlayers[2]){
	int eActions[350] = {0};
	int aActions[350] = {0};
	int damages[350] = {0};

	std::string input;
	while(std::getline(std::cin, input)){
		//意図せずcinが閉じられると無限ループするので対策
		if(input.empty()) continue;

		char command = input[0];
		if(command == 'q'){
			std::cout << "Exiting loop." << std::endl;
			return;
		}
		if(command == 'b'){
			// Check if there is enough input (e.g., at least "b " and some parameters)
			if(input.size() < 3){
				std::cerr << "Error: insufficient input for command 'b'." << std::endl;
				continue;
			}

			// Extract the substring after the command character and a space
			std::string params = input.substr(2);
			if(params.empty()){
				std::cerr << "Error: no parameters provided for command 'b'." << std::endl;
				continue;
			}

			std::istringstream ss(params);

			int hours, minutes, seconds, turns;
			if(!(ss >> hours >> minutes >> seconds >> turns)){
				std::cerr << "Error: failed to parse time parameters." << std::endl;
				continue;
			}

			// Read the three action strings separated by '-' delimiters
			std::string eActionsStr, aActionsStr, damagesStr;
			if(!std::getline(ss, eActionsStr, '-')){
				std::cerr << "Error: failed to read eActions." << std::endl;
				continue;
			}
			if(!std::getline(ss, aActionsStr, '-')){
				std::cerr << "Error: failed to read aActions." << std::endl;
				continue;
			}
			if(!std::getline(ss, damagesStr, '-')){
				std::cerr << "Error: failed to read damages." << std::endl;
				continue;
			}

			// 各アクション配列に値を代入
			parseActions(eActionsStr, eActions);
			parseActions(aActionsStr, aActions);
			parseActions(damagesStr, damages);

			auto seed = BruteForceRequest(copiedPlayers, hours, minutes, seconds, turns, eActions, aActions, damages);
			if(foundSeeds == 1){
				std::stringstream ss2;
				if(!SearchRequest(copiedPlayers, seed, aActions, true, ss2)){
					std::cout << std::endl;
					std::cout << "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=" << std::endl;
					std::cout << "      **YOU WILL NOW LOSE!**       " << std::endl;
					std::cout << "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=" << std::endl;
					std::cout << std::endl;
				}
				std::cout << ss2.str();
			}
			continue;
		}
		if(command == 'h'){
			showHeader();
			continue;
		}
		std::cerr << "Unknown command." << std::endl;
	}
	if(std::cerr.good()){
		std::cerr <<
			"Unrecoverable Error: An anomaly occurred in the main loop of the C++ process, forcing the battle emulator process to terminate. To recover, please restart the integrated system"
			<< std::endl;
	}
}

int toint(char* str){
	try{
		int number = std::stoi(str);
		return number;
	}
	catch(const std::invalid_argument& e){
		std::cerr << "Invalid argument: " << e.what() << std::endl;
		return -1;
	}
	catch(const std::out_of_range& e){
		std::cerr << "Out of range: " << e.what() << std::endl;
		return -1;
	}
}


// 左側の空白をトリム
std::string ltrim(const std::string& s){
	size_t start = s.find_first_not_of(" \t\n\r\f\v");
	return (start == std::string::npos) ? "" : s.substr(start);
}

// 右側の空白をトリム
std::string rtrim(const std::string& s){
	size_t end = s.find_last_not_of(" \t\n\r\f\v");
	return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

// 両側の空白をトリム
std::string trim(const std::string& s){
	return rtrim(ltrim(s));
}


#if defined(MINGW_BUILD)
#define __EMSCRIPTEN__
#define EMSCRIPTEN_KEEPALIVE
#endif

#ifdef __EMSCRIPTEN__
#if !defined(MINGW_BUILD) && !defined(MSVC_BUILD)
#include <emscripten/emscripten.h>
#endif
namespace {
    const int MAX = 350;
    int aActions[MAX] = {0};
    int damages[MAX] = {0};
    // aActions[] は味方行動（ホイミ、味方攻撃、麻痺の場合は PARALYSIS）を格納する
    int eActions[MAX] = {0};

    std::string wasmLastDump;
    std::string wasmLastError;
    uint64_t wasmLastTurnProcessed = 0;

    bool buildResultsFromInput(const char *input) {
        wasmLastError.clear();
        if (input == nullptr) {
            wasmLastError = "input is null";
            return false;
        }

    	std::stringstream ss2(input);

    	// Read the three action strings separated by '-' delimiters
    	std::string eActionsStr, aActionsStr, damagesStr;
    	if(!std::getline(ss2, eActionsStr, '-')){
    		std::cerr << "Error: failed to read eActions." << std::endl;
    		return false;
    	}
    	if(!std::getline(ss2, aActionsStr, '-')){
    		std::cerr << "Error: failed to read aActions." << std::endl;
    		return false;
    	}
    	if(!std::getline(ss2, damagesStr, '-')){
    		std::cerr << "Error: failed to read damages." << std::endl;
    		return false;
    	}

    	// 各アクション配列に値を代入
    	parseActions(eActionsStr, eActions);
    	parseActions(aActionsStr, aActions);
    	parseActions(damagesStr, damages);

        return true;
    }

    std::string buildDumpOutput(const Player copiedPlayers[2], uint64_t seed, int numThreads, bool dropbug) {
        lcg::init(seed, true);

        BattleEmulator::ResetTurnProcessed();

    	std::stringstream ss;
    	if(!SearchRequest(copiedPlayers, seed, aActions, true, ss)){
    		ss << std::endl;
    		ss << "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=" << std::endl;
    		ss << "      **YOU WILL NOW LOSE!**       " << std::endl;
    		ss << "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=" << std::endl;
    		ss << std::endl;

    		auto turns = 0;
		    for(int a_action : aActions){
			    if(a_action == -1){
				    break;
			    }
		    	turns++;
		    }

    		BattleResult res;
    		Player players[2] = {copiedPlayers[0], copiedPlayers[1]};
    		lcg::init(seed);
    		int position = 1;
    		uint64_t nowState = 0;
    		BattleEmulator::Main(&position, 100, aActions, players, &res, seed, nullptr, nullptr, -1, &nowState);
    		ss << dumpTable(res, aActions, turns);
    		return ss.str();
    	}
    	std::cout << ss.str();
        wasmLastTurnProcessed = BattleEmulator::getTurnProcessed();
        return ss.str();
    }
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int wasm_prepare_input(const char *input) {;
    if (!buildResultsFromInput(input)) {
        return 0;
    }

    return 1;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_get_last_error() {
    return wasmLastError.c_str();
}

EMSCRIPTEN_KEEPALIVE uint64_t wasm_bruteforce_range(int resultIndex, uint64_t startSeed, uint64_t endSeed) {
    BattleEmulator::ResetTurnProcessed();
    foundSeeds = 0;
    FoundSeed = 0;

    BruteForceMainLoop(copiedPlayers, startSeed, endSeed, aActions, damages, eActions);
    wasmLastTurnProcessed = BattleEmulator::getTurnProcessed();

    if (foundSeeds == 1) {
        return FoundSeed;
    }
    return 0;
}

EMSCRIPTEN_KEEPALIVE uint64_t wasm_get_turn_processed() {
    return wasmLastTurnProcessed;
}

EMSCRIPTEN_KEEPALIVE int wasm_get_found_seeds() {
    return foundSeeds;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_search_dump(int resultIndex, uint64_t seed, int numThreads, int dropbug) {
    BattleEmulator::ResetTurnProcessed();
    if (resultIndex < 0) {
        wasmLastError = "invalid result index";
        wasmLastDump.clear();
        return wasmLastDump.c_str();
    }

    wasmLastDump = buildDumpOutput(copiedPlayers, seed, numThreads,
                                   dropbug != 0);
    wasmLastTurnProcessed = BattleEmulator::getTurnProcessed();
    return wasmLastDump.c_str();
}
}
#endif