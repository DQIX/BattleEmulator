#include <iostream>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <fstream>
#include <vector>

#include "lcg.h"
#include "BattleEmulator.h"
#include "camera.h"
#include "camera/freecam_action_mapper.hpp"
#include "debug.h"
#include "ActionOptimizer.h"
#include "EnhancedCostCalculator.h"

#ifdef DEBUG

#include <chrono>

#endif

#if defined(OPTIMIZE_MODE)
#include "SimpleParameterOptimizer.h"
#endif

int startturn = -1;

#if defined(gerunikku)

constexpr Player copiedPlayers[4] = {
	// プレイヤー1
	{
	301, 301, 320, 320, 289, 289, 187, 234, 161, // 最初のメンバー
		161, false, false, 0, false, 0, -1,
		// specialCharge, dirtySpecialCharge, specialChargeTurn, inactive, paralysis, paralysisLevel, paralysisTurns
		8, 1.0, false, -1, 0, -1, // SpecialMedicineCount, defence, sleeping, sleepingTurn, BuffLevel, BuffTurns
		false, -1, 0, -1, 0, false, 1, 1, 1 , false
	}, // hasMagicMirror, MagicMirrorTurn, AtkBuffLevel, AtkBuffTurn, TensionLevel
	{
		402, 402, 161, 161, 256, 256, 98, 0, 10,255,
	},
	// プレイヤー2
	{
		1854, 1854, 125, 125, 238, 238, 148, 0, 255,255,
	},
{
		402, 402, 161, 161, 256, 256, 98, 0, 10,255,
	},

};

#endif



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

bool SearchRequest(const Player copiedPlayers2[4], uint64_t seed, const int aActions[350], bool dropbug, std::stringstream& ss);

uint64_t BruteForceRequest(const Player copiedPlayers2[4], int hours, int minutes, int seconds, int turns,
                           int eActions[350],
                           int aActions[350], int damages[350]);


void mainLoop(const Player copiedPlayers2[4]);

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
		<< std::setw(18) << "eAct3"
		<< std::setw(18) << "eAct4"
		<< std::setw(6) << "aD"
		<< std::setw(6) << "eD1"
		<< std::setw(6) << "eD2"
		<< std::setw(6) << "eD3"
		<< std::setw(6) << "eD4"
		<< std::setw(6) << "ahp"
		<< std::setw(6) << "ehp"
		<< std::setw(6) << "amp"

		<< std::setw(6) << "ini"
		//<< std::setw(6) << "Para"
		//<< std::setw(6) << "Sle"
		<< std::setw(6) << "ATT"
		<< std::setw(6) << "DET"
		//<< std::setw(6) << "MMT"
		<< std::setw(6) << "Tab"
		<< std::setw(6) << "Sct" << "\n";
	ss << std::string(188, '-') << "\n"; // 区切り線を出力
}

std::string dumpTable(const BattleResult& result,const int32_t gene[350], int PastTurns);

std::string dumpTable(const BattleResult& result, const int32_t gene[350], int PastTurns){
	stringstream ss6;
	printHeader(ss6);
	int currentTurn = -1;
	int eDamage[4] = {-1, -1, -1, -1}, aDamage = -1;
	bool initiative_tmp = false;
	std::string eAction[4], aAction, sp, tmpState, ATKTurn1, DEFTurn1, magicMirrorTurn1, specialChargeTurn1, amp1, ahp2,
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
		auto defenseFlag = result.defenseFlag[i];
		int amp = -1;
		if(i >= 1){
			amp = result.amp[i - 1];
		}

		auto special = gene[turn];

		std::string specialAction;
		if(special != 0 && special != -1){
			specialAction = BattleEmulator::getActionName(special & 0x3ff);
		}

		// ターンが変わったら、前のターンのデータを出力
		if(turn != currentTurn) {
			if(currentTurn != -1){
				// 前のターンの出力
				if(turn > PastTurns){
					ss6
						<< std::left << std::setw(6) << (currentTurn + 1)
						<< std::setw(18) << sp
						<< std::setw(18) << aAction
						<< std::setw(18) << eAction[0]
						<< std::setw(18) << eAction[1]
						<< std::setw(18) << eAction[2]
						<< std::setw(18) << eAction[3]
						<< std::setw(6) << aDamage
						<< std::setw(6) << eDamage[0]
						<< std::setw(6) << eDamage[1]
						<< std::setw(6) << eDamage[2]
						<< std::setw(6) << eDamage[3]
						<< std::setw(6) << ahp2
						<< std::setw(6) << ehp2
						<< std::setw(6) << amp2
						<< std::setw(6) << (initiative_tmp ? "yes" : "")
						//<< std::setw(6) << ((aAction == "Paralysis" || aAction == "Cure Paralysis") ? "yes" : "")
						//<< std::setw(6) << ((aAction == "Sleeping" || aAction == "Cure Sleeping") ? "yes" : "")
						<< std::setw(6) << ATKTurn1
						<< std::setw(6) << DEFTurn1
						//<< std::setw(6) << magicMirrorTurn1
						<< std::setw(6) << tmpState
						<< std::setw(6) << specialChargeTurn1
						<< std::setw(11) << "" << "\n";
				}
			}
			// ターンの初期化
			currentTurn = turn;
			eAction[0] = "";
			eAction[1] = "";
			eAction[2] = "";
			eAction[3] = "";
			aAction = "";
			eDamage[0] = 0;
			eDamage[1] = 0;
			eDamage[2] = 0;
			eDamage[3] = 0;
			aDamage = 0;
			sp = "";
			initiative_tmp = false;
			counter = 0;
			ATKTurn1 = "";
			DEFTurn1 = "";
			magicMirrorTurn1 = "";
			specialChargeTurn1 = "";
			tmpState = (state == 0) ? "A" : "B";
		}

		// 敵か味方の行動を適切な変数に格納
		if(isEnemy){
			eAction[counter] = BattleEmulator::getActionName(action);
			eDamage[counter] = damage;
			counter++;
			ahp2 = std::to_string(ahp1);
		}else{
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

			if(eAction[0] != "magic Burst" && eAction[1] != "magic Burst" &&
			   eAction[2] != "magic Burst" && eAction[3] != "magic Burst"){
				if(!initiative && action == BattleEmulator::TURN_SKIPPED || action == BattleEmulator::PARALYSIS ||
					action == BattleEmulator::SLEEPING){
					sp = "---------------";
				}
				if((action == BattleEmulator::CURE_SLEEPING || action == BattleEmulator::CURE_PARALYSIS)){
					sp = "---------------";
				}
				if((action == BattleEmulator::INACTIVE_ALLY) && !defenseFlag){
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
			<< std::setw(18) << eAction[2]
			<< std::setw(18) << eAction[3]
			<< std::setw(6) << aDamage
			<< std::setw(6) << eDamage[0]
			<< std::setw(6) << eDamage[1]
			<< std::setw(6) << eDamage[2]
			<< std::setw(6) << eDamage[3]
			<< std::setw(6) << ahp2
			<< std::setw(6) << ehp2
			<< std::setw(6) << amp2
			<< std::setw(6) << (initiative_tmp ? "yes" : "")
			//<< std::setw(6) << ((aAction == "Paralysis" || aAction == "Cure Paralysis") ? "yes" : "")
			//<< std::setw(6) << ((aAction == "Sleeping") ? "yes" : "")
			<< std::setw(6) << ATKTurn1
			<< std::setw(6) << DEFTurn1
			//<< std::setw(6) << magicMirrorTurn1
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

bool SearchRequest(const Player copiedPlayers2[4], uint64_t seed, const int aActions[350], bool dropbug, std::stringstream &ss){
	int32_t gene[350] = {0};
	auto turns = 0;
	for(int i = 0; i < 349; ++i){
		gene[i] = aActions[i];
		if(aActions[i] == -1){
			gene[i] = -1;
			break;
		}
		turns++;
	}

	lcg::init(seed);

#if !defined(OPTIMIZE_MODE)
	Genome genome = ActionOptimizer::RunAlgorithm(copiedPlayers2, seed, turns, -1, gene, 0);
    BattleResult result;

	auto runMain = [&](const Genome& g, BattleResult& res) -> RunResult {
		Player players[4] = {copiedPlayers2[0], copiedPlayers2[1], copiedPlayers2[2], copiedPlayers2[3]};
		int position = 1;
		uint64_t nowState = 0;
		BattleEmulator::Main(&position, 100, g.actions, players, &res, seed, nullptr, nullptr, -1, &nowState);
		#if defined(gerunikku)
		bool win = players[1].hp <= 0 && players[2].hp <= 0 && players[3].hp <= 0;
		return { win, players[2].hp, res.turn, res.position };
		#else
		bool win = players[1].hp <= 0;
		return { win, players[1].hp, res.turn, res.position };
		#endif
	};

    const auto rr = runMain(genome, result);
    if (!rr.win) return false;

    ss << dumpTable(result, genome.actions, startturn) << std::endl;

    ss << "0x" << std::hex << seed << std::dec << ": ";

	for (auto i = 0; i < 100; ++i) {
		if (genome.actions[i] == 0 || genome.actions[i] == -1) {
			break;
		}
		ss << genome.actions[i] << ", ";
	}
	ss << std::endl;
    ss << "[IDDFS] Win turn=" << (rr.turn + 1)
       << " position=" << rr.position
       << " nodes=" << ActionOptimizer::getNodesUsed() << std::endl;
#endif

	//探索成功
	return true;
}

// ブルートフォースリクエスト関数
[[nodiscard]] uint64_t BruteForceRequest(const Player copiedPlayers2[4], int hours, int minutes, int seconds, int turns,
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

	uint64_t totalSeconds = hours * 3600 + minutes * 60 + seconds;
	totalSeconds = totalSeconds;
	//数字は探索範囲(秒)
	auto time1 = static_cast<uint64_t>(floor((totalSeconds - 30) * (1 / 0.12515)));
	time1 = time1 << 16;
	std::cout << time1 << std::endl;



	//数字は探索範囲(秒)
	auto time2 = static_cast<uint64_t>(floor((totalSeconds + 30) * (1 / 0.125155)));
	time2 = time2 << 16;
	std::cout << time2 << std::endl;
	int32_t gene[350] = {0};

	for(int i = 0; i < 349; ++i){
		gene[i] = aActions[i];
		if(aActions[i] == -1){
			gene[i] = -1;
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
	Player players[4];
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
		players[2] = copiedPlayers[2];
		players[3] = copiedPlayers[3];


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


void BruteForceMainLoop(const Player copiedPlayers[4], uint64_t start, uint64_t end, int gene[350],
						int damages[350], int eaction1[350]) {
	int maxElement = 350;
	for (uint64_t seed = start; seed < end; ++seed) {
		BattleEmulator::resetStartTurn();
		lcg::init(seed);
		int position = 1;
		uint64_t nowState = 0;
		Player players[4] = {copiedPlayers[0], copiedPlayers[1], copiedPlayers[2], copiedPlayers[3]};


		bool resultBool = BattleEmulator::Main(&position, 100, gene, players,
											  nullptr, seed, eaction1,
											   damages,
											   maxElement,
											   &nowState);
		if (resultBool) {
			std::cout << seed << std::endl;
			FoundSeed = seed;
			foundSeeds++;
			startturn = BattleEmulator::getStartTurn();
		}
	}
}

// 入力文字列を配列に分割するヘルパー関数
void parseActions(const std::string& str, int actions[350]){
	std::istringstream iss(str);
	int value, index = 0;
	while(iss >> value && index < 349){
		actions[index++] = value;
	}
	actions[index++] = -1;
}


// メインループ
void mainLoop(const Player copiedPlayers[4]){
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
    int aActions5[MAX] = {0};
    int damages5[MAX] = {0};
    // aActions[] は味方行動（ホイミ、味方攻撃、麻痺の場合は PARALYSIS）を格納する
    int eActions5[MAX] = {0};

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
    	parseActions(eActionsStr, eActions5);
    	parseActions(aActionsStr, aActions5);
    	parseActions(damagesStr, damages5);

        return true;
    }

    std::string buildDumpOutput(const Player copiedPlayers[4], uint64_t seed, int numThreads, bool dropbug) {
        lcg::init(seed, true);

        BattleEmulator::ResetTurnProcessed();

    	std::stringstream ss;
    	if(!SearchRequest(copiedPlayers, seed, aActions5, true, ss)){
    		ss << std::endl;
    		ss << "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=" << std::endl;
    		ss << "      **YOU WILL NOW LOSE!**       " << std::endl;
    		ss << "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=" << std::endl;
    		ss << std::endl;

    		auto turns = 0;
		    for(int a_action : aActions5){
			    if(a_action == -1){
				    break;
			    }
		    	turns++;
		    }

    		BattleResult res;
    		Player players[4] = {copiedPlayers[0], copiedPlayers[1], copiedPlayers[2], copiedPlayers[3]};
    		lcg::init(seed);
    		int position = 1;
    		uint64_t nowState = 0;
    		BattleEmulator::Main(&position, 100, aActions5, players, &res, seed, nullptr, nullptr, -1, &nowState);
    		ss << dumpTable(res, aActions5, startturn);
    		ss << "startturn=" << startturn << std::endl;
    		return ss.str();
    	}
    	std::cout << ss.str();
    	ss << "startturn=" << startturn << std::endl;
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

    BruteForceMainLoop(copiedPlayers, startSeed, endSeed, aActions5, damages5, eActions5);
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

int main(int argc, char* argv[]){
	showHeader();

	//https://zenn.dev/reputeless/books/standard-cpp-for-competitive-programming/viewer/library-ios-iomanip#3.1-c-%E8%A8%80%E8%AA%9E%E3%81%AE%E5%85%A5%E5%87%BA%E5%8A%9B%E3%82%B9%E3%83%88%E3%83%AA%E3%83%BC%E3%83%A0%E3%81%A8%E3%81%AE%E5%90%8C%E6%9C%9F%E3%82%92%E7%84%A1%E5%8A%B9%E3%81%AB%E3%81%99%E3%82%8B
	//std::cin.tie(0)->sync_with_stdio(0);

#if defined(gerunikku)
	auto makeDebugGene = [](int32_t (&gene)[350], const int turns, const int action) {
		const int boundedTurns = std::clamp(turns, 1, 349);
		for (int i = 0; i < boundedTurns; ++i) gene[i] = action;
		gene[boundedTurns] = -1;
	};

	auto printTrace = [](const uint64_t traceSeed, const int tracePosition,
	                     const Player (&tracePlayers)[4], const BattleResult& traceResult) {
		std::cout << "TRACE seed=0x" << std::hex << traceSeed << std::dec
		          << " position=" << tracePosition
		          << " hp=" << tracePlayers[0].hp << ',' << tracePlayers[1].hp << ','
		          << tracePlayers[2].hp << ',' << tracePlayers[3].hp << '\n';
		for (int i = 0; i < traceResult.position; ++i) {
			std::cout << "TRACE record[" << i << "] turn=" << traceResult.turns[i]
			          << " action=" << traceResult.actions[i]
			          << " damage=" << traceResult.damages[i]
			          << " enemy=" << traceResult.isEnemy[i] << '\n';
		}
	};

	if (argc >= 3 && std::string_view(argv[1]) == "--trace-turn") {
		const uint64_t traceSeed = std::stoull(argv[2], nullptr, 0);
		const int traceAction = argc >= 4 ? std::stoi(argv[3], nullptr, 0) : BattleEmulator::DEFENCE;
		const int traceTarget = argc >= 5 ? std::stoi(argv[4], nullptr, 0) : -1;
		const int currentSeedPosition = argc >= 6 ? std::stoi(argv[5], nullptr, 0) : 0;
		int32_t traceGene[350] = {};
		makeDebugGene(traceGene, 1, traceAction);
		Player tracePlayers[4] = {copiedPlayers[0], copiedPlayers[1], copiedPlayers[2], copiedPlayers[3]};
		BattleResult traceResult;
		int tracePosition = currentSeedPosition + 1;
		uint64_t traceState = 0;
		lcg::init(traceSeed);
		BattleEmulator::Main(&tracePosition, 1, traceGene, tracePlayers, &traceResult,
		                     traceSeed, nullptr, nullptr, -1, &traceState, traceTarget, true);
		printTrace(traceSeed, tracePosition, tracePlayers, traceResult);
		return 0;
	}

	if (argc >= 3 && std::string_view(argv[1]) == "--trace-battle") {
		const uint64_t traceSeed = std::stoull(argv[2], nullptr, 0);
		const int traceTurns = argc >= 4 ? std::stoi(argv[3], nullptr, 0) : 10;
		const int traceAction = argc >= 5 ? std::stoi(argv[4], nullptr, 0) : BattleEmulator::DEFENCE;
		const int traceTarget = argc >= 6 ? std::stoi(argv[5], nullptr, 0) : -1;
		const int currentSeedPosition = argc >= 7 ? std::stoi(argv[6], nullptr, 0) : 0;
		if (traceTurns < 1 || traceTurns > 349) throw std::invalid_argument("trace turns must be 1..349");
		int32_t traceGene[350] = {};
		makeDebugGene(traceGene, traceTurns, traceAction);
		Player tracePlayers[4] = {copiedPlayers[0], copiedPlayers[1], copiedPlayers[2], copiedPlayers[3]};
		BattleResult traceResult;
		int tracePosition = currentSeedPosition + 1;
		uint64_t traceState = 0;
		lcg::init(traceSeed);
		BattleEmulator::Main(&tracePosition, traceTurns, traceGene, tracePlayers, &traceResult,
		                     traceSeed, nullptr, nullptr, -1, &traceState, traceTarget, true);
		printTrace(traceSeed, tracePosition, tracePlayers, traceResult);
		return 0;
	}

	if (argc >= 5 && std::string_view(argv[1]) == "--trace-sequence") {
		const uint64_t traceSeed = std::stoull(argv[2], nullptr, 0);
		const int currentSeedPosition = std::stoi(argv[3], nullptr, 0);
		lcg::init(traceSeed, true);
		BattleEmulator::SearchState traceState{};
		if (!BattleEmulator::InitializeSearchState(&traceState, copiedPlayers,
		                                          currentSeedPosition + 1)) {
			throw std::runtime_error("failed to initialize trace search state");
		}

		for (int step = 0; step < argc - 4; ++step) {
			const std::string_view token(argv[step + 4]);
			const std::size_t separator = token.find(':');
			const int action = std::stoi(std::string(token.substr(0, separator)), nullptr, 0);
			const int target = separator == std::string_view::npos
				? -1
				: std::stoi(std::string(token.substr(separator + 1)), nullptr, 0);
			BattleResult traceResult;

			std::cout << "TRACE sequence-step=" << step
			          << " action=" << action
			          << " target=" << target
			          << " startPosition=" << traceState.position << '\n';
			BattleEmulator::SearchState nextState{};
			if (!BattleEmulator::StepSearchState(traceState, {action, target}, &nextState,
			                                     &traceResult, true)) {
				throw std::runtime_error("failed to execute trace search step");
			}
			traceState = nextState;
			std::cout << "TRACE sequence-state step=" << step
			          << " position=" << traceState.position
			          << " hp=" << traceState.players[0].hp << ',' << traceState.players[1].hp << ','
			          << traceState.players[2].hp << ',' << traceState.players[3].hp
			          << " heroMp=" << traceState.players[0].mp
			          << " mirror=" << traceState.players[0].hasMagicMirror
			          << " mirrorTurn=" << traceState.players[0].MagicMirrorTurn
			          << " buffLevel=" << traceState.players[0].BuffLevel
			          << " buffTurns=" << traceState.players[0].BuffTurns
			          << " tension=" << traceState.players[0].TensionLevel << '\n';
			for (int i = 0; i < traceResult.position; ++i) {
				std::cout << "TRACE sequence-record step=" << step
				          << " record=" << i
				          << " turn=" << traceResult.turns[i]
				          << " action=" << traceResult.actions[i]
				          << " damage=" << traceResult.damages[i]
				          << " enemy=" << traceResult.isEnemy[i] << '\n';
			}
			if (traceState.players[0].hp <= 0 ||
			    (traceState.players[1].hp <= 0 && traceState.players[2].hp <= 0 && traceState.players[3].hp <= 0)) {
				break;
			}
		}
		return 0;
	}

	if (argc >= 7 && std::string_view(argv[1]) == "--scan-camera") {
		const int traceAction = std::stoi(argv[2], nullptr, 0);
		const int traceTarget = std::stoi(argv[3], nullptr, 0);
		const int traceTurns = std::stoi(argv[4], nullptr, 0);
		const uint64_t startSeed = std::stoull(argv[5], nullptr, 0);
		const uint64_t count = std::stoull(argv[6], nullptr, 0);
		const int currentSeedPosition = argc >= 8 ? std::stoi(argv[7], nullptr, 0) : 1;
		if (traceTurns < 1 || traceTurns > 349) throw std::invalid_argument("scan turns must be 1..349");
		int32_t traceGene[350] = {};
		makeDebugGene(traceGene, traceTurns, traceAction);
		std::array<int, 10> categoryCounts{};
		auto emitCandidate = [&](const char* category, const int categoryIndex, const uint64_t seed,
		                         const CameraDebugEvent& event) {
			if (categoryCounts[categoryIndex] >= 12) return;
			++categoryCounts[categoryIndex];
			std::cout << "CAMERA_CANDIDATE category=" << category
			          << " seed=0x" << std::hex << seed << std::dec
			          << " turn=" << event.turnSerial + 1
			          << " actionIndex=" << event.actionIndex
			          << " action=" << event.commonActionId
			          << " actor=0x" << std::hex << event.actorId
			          << " target=0x" << event.targetId << std::dec
			          << " route=" << static_cast<unsigned>(event.actorRouteCount)
			          << " maxRoute=" << static_cast<unsigned>(event.maxRouteCount)
			          << " source=" << static_cast<unsigned>(event.triggerSource)
			          << " call=" << event.runtimeCallFreeCamera
			          << " param5=" << event.runtimeParam5
			          << " reset=" << event.runtimeResetOnly
			          << " manual=" << event.manualRuleWouldCall
			          << " production=" << event.productionCalledFreeCamera
			          << " nodesBefore=";
			for (std::size_t actorIndex = 0; actorIndex < event.presentationActorCount; ++actorIndex) {
				if (actorIndex != 0) std::cout << ',';
				std::cout << static_cast<unsigned>(event.startNodesBefore[actorIndex]);
			}
			std::cout << " nodesAfter=";
			for (std::size_t actorIndex = 0; actorIndex < event.presentationActorCount; ++actorIndex) {
				if (actorIndex != 0) std::cout << ',';
				std::cout << static_cast<unsigned>(event.startNodesAfter[actorIndex]);
			}
			std::cout << '\n';
		};

		camera::SetDebugCapture(true);
		for (uint64_t offset = 0; offset < count; ++offset) {
			const uint64_t seed = startSeed + offset;
			if (seed == 0) continue;
			Player tracePlayers[4] = {copiedPlayers[0], copiedPlayers[1], copiedPlayers[2], copiedPlayers[3]};
			BattleResult traceResult;
			int tracePosition = currentSeedPosition + 1;
			uint64_t traceState = 0;
			lcg::init(seed);
			camera::ClearDebugEvents();
			BattleEmulator::Main(&tracePosition, traceTurns, traceGene, tracePlayers, &traceResult,
			                     seed, nullptr, nullptr, -1, &traceState, traceTarget);
			CameraDebugEvent previousAttackEvent{};
			bool havePreviousAttackEvent = false;
			for (std::size_t eventIndex = 0; eventIndex < camera::DebugEventCount(); ++eventIndex) {
				const CameraDebugEvent event = camera::DebugEventAt(eventIndex);
				if (!event.runtimeDecisionAvailable) continue;
				const bool attackAction = event.commonActionId == BattleEmulator::ATTACK_ENEMY
				    || event.commonActionId == BattleEmulator::ATTACK_ALLY;
				if (attackAction && havePreviousAttackEvent && event.actionIndex > 0
				    && event.actorId == previousAttackEvent.actorId
				    && event.targetId == previousAttackEvent.targetId) {
					emitCandidate("potential-consecutive-attack-reset", 8, seed, event);
				}
				if (event.runtimeResetOnly) emitCandidate("reset-only", 0, seed, event);
				if (event.manualRuleWouldCall && (!event.runtimeCallFreeCamera || event.runtimeResetOnly)) {
					emitCandidate("manual-runtime-mismatch", 1, seed, event);
				}
				if (event.commonActionId == BattleEmulator::ZAKI && !event.runtimeCallFreeCamera) {
					emitCandidate("zaki-suppressed", 2, seed, event);
				}
				if (event.commonActionId == BattleEmulator::ZAKI && event.runtimeCallFreeCamera && !event.runtimeParam5) {
					emitCandidate("zaki-call-param5-0", 3, seed, event);
				}
				if (event.commonActionId == BattleEmulator::ZAKI && event.runtimeCallFreeCamera && event.runtimeParam5) {
					emitCandidate("zaki-call-param5-1", 4, seed, event);
				}
				if (event.maxRouteCount > 4) emitCandidate("route-over-4", 5, seed, event);
				if (event.triggerSource == 1) emitCandidate("actor-membership", 6, seed, event);
				if (event.triggerSource == 3) emitCandidate("fallback-membership", 7, seed, event);
				if (event.mapped && event.runtimeDecisionAvailable) {
					emitCandidate("mapped-presentation", 9, seed, event);
				}
				if (attackAction) {
					previousAttackEvent = event;
					havePreviousAttackEvent = true;
				}
			}
		}
		camera::SetDebugCapture(false);
		std::cout << "CAMERA_SCAN_DONE seeds=" << count
		          << " currentSeedPosition=" << currentSeedPosition
		          << " firstConsumedPosition=" << (currentSeedPosition + 1);
		for (std::size_t i = 0; i < categoryCounts.size(); ++i) std::cout << " c" << i << '=' << categoryCounts[i];
		std::cout << '\n';
		return 0;
	}

	if (argc >= 4 && std::string_view(argv[1]) == "--scan-action-seeds") {
		const uint64_t startSeed = std::stoull(argv[2], nullptr, 0);
		const uint64_t count = std::stoull(argv[3], nullptr, 0);
		const int searchTurns = argc >= 5 ? std::stoi(argv[4], nullptr, 0) : 1;
		const int perAction = argc >= 6 ? std::stoi(argv[5], nullptr, 0) : 4;
		const int heroAction = argc >= 7 ? std::stoi(argv[6], nullptr, 0) : BattleEmulator::DEFENCE;
		const int heroTarget = argc >= 8 ? std::stoi(argv[7], nullptr, 0) : -1;
		const int wantedPresentationType = argc >= 9 ? std::stoi(argv[8], nullptr, 0) : -1;
		const int currentSeedPosition = argc >= 10 ? std::stoi(argv[9], nullptr, 0) : 0;
		const int wantedCommonAction = argc >= 11 ? std::stoi(argv[10], nullptr, 0) : -1;
		const int maxRecord = argc >= 12 ? std::stoi(argv[11], nullptr, 0) : std::numeric_limits<int>::max();
		if (searchTurns < 1 || searchTurns > 349) throw std::invalid_argument("scan-action-seeds turns must be 1..349");
		if (perAction < 0) throw std::invalid_argument("scan-action-seeds perAction must be >= 0 (0 = emit all matches)");

		using dq9::freecam::actions::Find;
		using namespace dq9::freecam::fast;
		if (wantedPresentationType >= 0) {
			std::cout << "ROM_PRESENTATION_TYPE type=" << wantedPresentationType << '\n';
			for (std::uint16_t actionId = 0; actionId < metadata::kActionCount; ++actionId) {
				if (metadata::PresentationType(actionId) != wantedPresentationType) continue;
				std::cout << "ROM_ACTION dq9=" << actionId
				          << " formation=" << static_cast<unsigned>(metadata::AttackFormationMode(actionId))
				          << " selector=0x" << std::hex << metadata::SelectorProjection(actionId) << std::dec
				          << " fallback=" << metadata::FallbackLookupActionId(actionId)
				          << " bact=" << metadata::HasBact(actionId) << '\n';
			}
		}

		constexpr std::size_t kCommonActionCapacity = BattleEmulator::CURE_CONFUSION + 1;
		std::array<std::uint64_t, kCommonActionCapacity> occurrenceCounts{};
		std::array<int, kCommonActionCapacity> emittedCounts{};
		std::array<int, kCommonActionCapacity> bestRecord{};
		std::array<std::uint64_t, kCommonActionCapacity> bestSeed{};
		bestRecord.fill(std::numeric_limits<int>::max());
		int32_t searchGene[350] = {};
		makeDebugGene(searchGene, searchTurns, heroAction);

		for (uint64_t offset = 0; offset < count; ++offset) {
			const uint64_t seed = startSeed + offset;
			if (seed == 0) continue;
			Player searchPlayers[4] = {copiedPlayers[0], copiedPlayers[1], copiedPlayers[2], copiedPlayers[3]};
			BattleResult searchResult;
			int searchPosition = currentSeedPosition + 1;
			uint64_t searchState = 0;
			lcg::init(seed);
			BattleEmulator::Main(&searchPosition, searchTurns, searchGene, searchPlayers, &searchResult,
			                     seed, nullptr, nullptr, -1, &searchState, heroTarget);

			for (int record = 0; record < searchResult.position; ++record) {
				if (!searchResult.isEnemy[record]) continue;
				const int commonAction = searchResult.actions[record];
				if (commonAction < 0 || commonAction >= static_cast<int>(kCommonActionCapacity)) continue;
				if (wantedCommonAction >= 0 && commonAction != wantedCommonAction) continue;
				if (record > maxRecord) continue;
				const auto* binding = Find(commonAction);
				const bool mapped = binding != nullptr && binding->mapped();
				if (wantedPresentationType >= 0
				    && (!mapped || binding->presentationType != wantedPresentationType)) continue;
				++occurrenceCounts[static_cast<std::size_t>(commonAction)];
				if (record < bestRecord[static_cast<std::size_t>(commonAction)]) {
					bestRecord[static_cast<std::size_t>(commonAction)] = record;
					bestSeed[static_cast<std::size_t>(commonAction)] = seed;
				}
				if (perAction != 0 && emittedCounts[static_cast<std::size_t>(commonAction)] >= perAction) continue;
				++emittedCounts[static_cast<std::size_t>(commonAction)];
				std::cout << "ACTION_SEED common=" << commonAction
				          << " name=\"" << BattleEmulator::getActionName(commonAction) << "\""
				          << " seed=0x" << std::hex << seed << std::dec
				          << " turn=" << searchResult.turns[record]
				          << " record=" << record
				          << " finalPosition=" << searchPosition;
				if (mapped) {
					std::cout << " dq9=" << binding->dq9ActionId
					          << " type=" << static_cast<unsigned>(binding->presentationType)
					          << " formation=" << static_cast<unsigned>(binding->attackFormationMode)
					          << " selector=0x" << std::hex
					          << metadata::SelectorProjection(binding->dq9ActionId) << std::dec;
				} else {
					std::cout << " dq9=unmapped type=unknown";
				}
				std::cout << '\n';
			}
		}

		std::cout << "ACTION_SEED_SCAN_DONE startSeed=0x" << std::hex << startSeed << std::dec
		          << " seeds=" << count
		          << " turns=" << searchTurns
		          << " heroAction=" << heroAction
		          << " heroTarget=" << heroTarget
		          << " wantedType=" << wantedPresentationType
		          << " wantedCommon=" << wantedCommonAction
		          << " maxRecord=" << maxRecord
		          << " currentSeedPosition=" << currentSeedPosition << '\n';
		for (std::size_t commonAction = 0; commonAction < occurrenceCounts.size(); ++commonAction) {
			if (occurrenceCounts[commonAction] == 0) continue;
			const auto* binding = Find(static_cast<int>(commonAction));
			const bool mapped = binding != nullptr && binding->mapped();
			std::cout << "ACTION_SUMMARY common=" << commonAction
			          << " name=\"" << BattleEmulator::getActionName(static_cast<int>(commonAction)) << "\""
			          << " occurrences=" << occurrenceCounts[commonAction]
			          << " emitted=" << emittedCounts[commonAction]
			          << " bestRecord=" << bestRecord[commonAction]
			          << " bestSeed=0x" << std::hex << bestSeed[commonAction] << std::dec;
			if (mapped) {
				std::cout << " dq9=" << binding->dq9ActionId
				          << " type=" << static_cast<unsigned>(binding->presentationType);
			} else {
				std::cout << " dq9=unmapped type=unknown";
			}
			std::cout << '\n';
		}
		return 0;
	}

	if (argc >= 4 && std::string_view(argv[1]) == "--find-hero-zero") {
		const int traceAction = std::stoi(argv[2], nullptr, 0);
		const int traceTarget = std::stoi(argv[3], nullptr, 0);
		const uint64_t startSeed = argc >= 5 ? std::stoull(argv[4], nullptr, 0) : 1;
		const uint64_t count = argc >= 6 ? std::stoull(argv[5], nullptr, 0) : 10000;
		const bool requireNoGuard = argc >= 7 && std::stoi(argv[6], nullptr, 0) != 0;
		int32_t traceGene[350] = {};
		makeDebugGene(traceGene, 1, traceAction);
		for (uint64_t offset = 0; offset < count; ++offset) {
			const uint64_t seed = startSeed + offset;
			if (seed == 0) continue;
			Player tracePlayers[4] = {copiedPlayers[0], copiedPlayers[1], copiedPlayers[2], copiedPlayers[3]};
			BattleResult traceResult;
			int tracePosition = 1;
			uint64_t traceState = 0;
			lcg::init(seed);
			BattleEmulator::Main(&tracePosition, 1, traceGene, tracePlayers, &traceResult,
			                     seed, nullptr, nullptr, -1, &traceState, traceTarget);
			bool guardWasPlanned = false;
			if (requireNoGuard) {
				for (int record = 0; record < traceResult.position; ++record) {
					if (traceResult.isEnemy[record] &&
					    traceResult.actions[record] == BattleEmulator::WHIPPING_BOY) {
						guardWasPlanned = true;
						break;
					}
				}
			}
			if (guardWasPlanned) continue;
			for (int record = 0; record < traceResult.position; ++record) {
				if (!traceResult.isEnemy[record] && traceResult.actions[record] == traceAction &&
				    traceResult.damages[record] == 0) {
					std::cout << "FOUND_HERO_ZERO action=" << traceAction
					          << " target=" << traceTarget
					          << " seed=0x" << std::hex << seed << std::dec
					          << " position=" << tracePosition << '\n';
					printTrace(seed, tracePosition, tracePlayers, traceResult);
					return 0;
				}
			}
		}
		std::cout << "NOT_FOUND_HERO_ZERO action=" << traceAction
		          << " target=" << traceTarget
		          << " startSeed=0x" << std::hex << startSeed << std::dec
		          << " count=" << count << '\n';
		return 0;
	}

	if (argc >= 3 && std::string_view(argv[1]) == "--find-enemy-action") {
		const int wantedAction = std::stoi(argv[2], nullptr, 0);
		const uint64_t startSeed = argc >= 4 ? std::stoull(argv[3], nullptr, 0) : 1;
		const uint64_t count = argc >= 5 ? std::stoull(argv[4], nullptr, 0) : 1000000;
		const int searchTurns = argc >= 6 ? std::stoi(argv[5], nullptr, 0) : 1;
		if (searchTurns < 1 || searchTurns > 349) throw std::invalid_argument("search turns must be 1..349");
		int32_t searchGene[350] = {};
		makeDebugGene(searchGene, searchTurns, BattleEmulator::DEFENCE);

		for (uint64_t offset = 0; offset < count; ++offset) {
			const uint64_t seed = startSeed + offset;
			if (seed == 0) continue;
			Player searchPlayers[4] = {copiedPlayers[0], copiedPlayers[1], copiedPlayers[2], copiedPlayers[3]};
			BattleResult searchResult;
			int searchPosition = 1;
			uint64_t searchState = 0;
			lcg::init(seed);
			BattleEmulator::Main(&searchPosition, searchTurns, searchGene, searchPlayers, &searchResult,
			                     seed, nullptr, nullptr, -1, &searchState);
			for (int record = 0; record < searchResult.position; ++record) {
				if (searchResult.isEnemy[record] && searchResult.actions[record] == wantedAction) {
					std::cout << "FOUND action=" << wantedAction
					          << " seed=0x" << std::hex << seed << std::dec
					          << " turn=" << searchResult.turns[record]
					          << " record=" << record
					          << " position=" << searchPosition << '\n';
					printTrace(seed, searchPosition, searchPlayers, searchResult);
					return 0;
				}
			}
		}

		std::cout << "NOT_FOUND action=" << wantedAction
		          << " startSeed=0x" << std::hex << startSeed << std::dec
		          << " count=" << count << " turns=" << searchTurns << '\n';
		return 0;
	}

	if (argc >= 2 && std::string_view(argv[1]) == "--find-confusion-seed") {
		const uint64_t startSeed = argc >= 3 ? std::stoull(argv[2], nullptr, 0) : 1;
		const uint64_t count = argc >= 4 ? std::stoull(argv[3], nullptr, 0) : 1000000;
		const int searchTurns = argc >= 5 ? std::stoi(argv[4], nullptr, 0) : 1;
		if (searchTurns < 1 || searchTurns > 349) throw std::invalid_argument("search turns must be 1..349");
		int32_t searchGene[350] = {};
		makeDebugGene(searchGene, searchTurns, BattleEmulator::DEFENCE);

		for (uint64_t offset = 0; offset < count; ++offset) {
			const uint64_t seed = startSeed + offset;
			if (seed == 0) continue;
			Player searchPlayers[4] = {copiedPlayers[0], copiedPlayers[1], copiedPlayers[2], copiedPlayers[3]};
			BattleResult searchResult;
			int searchPosition = 1;
			uint64_t searchState = 0;
			lcg::init(seed);
			BattleEmulator::Main(&searchPosition, searchTurns, searchGene, searchPlayers, &searchResult,
			                     seed, nullptr, nullptr, -1, &searchState);
			if (searchPlayers[0].confused) {
				std::cout << "FOUND_CONFUSION seed=0x" << std::hex << seed << std::dec
				          << " turns=" << searchTurns << " position=" << searchPosition << '\n';
				printTrace(seed, searchPosition, searchPlayers, searchResult);
				return 0;
			}
		}

		std::cout << "NOT_FOUND_CONFUSION startSeed=0x" << std::hex << startSeed << std::dec
		          << " count=" << count << " turns=" << searchTurns << '\n';
		return 0;
	}
#endif

#if defined(OPTIMIZE_MODE)
	int actions1[350] = {};
	auto counter1 = 0;
	actions1[counter1++] = BattleEmulator::BUFF;
	actions1[counter1] = -1;
	SimpleParameterOptimizer::optimize(copiedPlayers, 0x12398731ull, actions1, 100000, counter1);
	return 0;
#endif


#ifdef DEBUG2
	//THIS DEBUG CODE!
	//THIS DEBUG CODE
	//0x3f1b3c6c: 30, 62, 33, 37, 49, 62, 62, 62, 37, 33, 34,
	//0x3c98d058: 30, 62, 62, 62, 37, 62, 37, 33, 34,
	uint64_t time1 = 0x3c98d058;

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
	int32_t gene1[350] = { 30, 62, 62, 62, 37, 62, 37, 33, 34,     };
	//gene1[19-1] = BattleEmulator::DEFENCE;
	int counter = 0;

	//gene1[counter++] = BattleEmulator::BUFF;

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
	uint64_t time1 = 0x450ff41f;

	auto counter = 0;
	int actions[350] = {0};
	actions[counter++] = BattleEmulator::BUFF;
	actions[counter++] = BattleEmulator::FLEE_ALLY;
	//actions[counter++] = BattleEmulator::PSYCHE_UP_ALLY;
	actions[counter] = -1;

	std::stringstream ss;
	SearchRequest(copiedPlayers, time1, actions, false, ss);
	ss << std::endl;

	if(false){
		SearchRequest(copiedPlayers, time1+1, actions, false, ss);
		ss << std::endl;

		SearchRequest(copiedPlayers, time1+2, actions, false, ss);
		ss << std::endl;

		SearchRequest(copiedPlayers, time1+6, actions, false, ss);
		ss << std::endl;

		SearchRequest(copiedPlayers, time1+10, actions, false, ss);
		ss << std::endl;


		SearchRequest(copiedPlayers, time1+40, actions, false, ss);
		ss << std::endl;

		SearchRequest(copiedPlayers, time1+70, actions, false, ss);
		ss << std::endl;
	}

	std::cout << ss.str();
	return 0;
#endif

	mainLoop(copiedPlayers);
	return 0;
}
