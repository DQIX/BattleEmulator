#include <iostream>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>

#include "lcg.h"
#include "BattleEmulator.h"
#include "ActionOptimizer.h"
#include "debug.h"
#ifdef DEBUG

#include <chrono>

#endif

#if defined(OPTIMIZE_MODE)
#include "SimpleParameterOptimizer.h"
#endif

int startturn = -1;

#if defined(no_tate)

const Player copiedPlayers[2] = {
	// プレイヤー1
	{
		37, 37, 29,  21, 19, 0, 6, 6
	},

	// プレイヤー2
	{
		93, 93, 30, 22, 11, 0, 18, 18
	}
};

#elif defined(tate)

const Player copiedPlayers[2] = {
	// プレイヤー1
	{
		37, 37, 29,  24, 19, 0, 6, 6
	},

	// プレイヤー2
	{
		93, 93, 30, 22, 11, 0, 18, 18
	}
};

#endif


// 勝利フラグと確定した敵残HPを返す
struct RunResult {
	bool win;
	int enemyHp; // 使わなくなったが一応残す
	int turn;
	int position;
};

int toint(char *string);

//void processResult(const Player *copiedPlayers, const uint64_t seed, std::string input);

std::string ltrim(const std::string &s);

std::string rtrim(const std::string &s);

std::string trim(const std::string &s);

bool SearchRequest(const Player copiedPlayers2[2], uint64_t seed, const int aActions[350], bool dropbug,
                   int medicinalHerbCount,
                   std::stringstream &ss);

uint64_t BruteForceRequest(const Player copiedPlayers2[2], int hours, int minutes, int seconds, int turns,
                           int eActions[350],
                           int aActions[350], int damages[350]);


void mainLoop(const Player copiedPlayers2[2]);

using namespace std;

int foundSeeds = 0;

uint64_t FoundSeed = 0;
constexpr int kDefaultMedicinalHerbCount = 1;

void printHeader(std::stringstream &ss);

// ヘッダーを出力する関数
void printHeader(std::stringstream &ss) {
	ss << std::left << std::setw(6) << "turn"
			<< std::setw(18) << "sp"
			<< std::setw(18) << "aAct"
			<< std::setw(18) << "eAct"
			<< std::setw(6) << "aD"
			<< std::setw(6) << "eD1"
			<< std::setw(6) << "ahp"
			<< std::setw(6) << "ehp"
			<< std::setw(6) << "amp"
			<< std::setw(6) << "ini"<< "\n";
	ss << std::string(140, '-') << "\n"; // 区切り線を出力
}

std::string dumpTable(const BattleResult &result, const int32_t gene[350], int PastTurns);

std::string dumpTable(const BattleResult &result, const int32_t gene[350], int PastTurns) {
	stringstream ss6;
	printHeader(ss6);
	int currentTurn = -1;
	int eDamage[2] = {-1, -1}, aDamage = -1;
	bool initiative_tmp = false;
	std::string eAction[2], aAction, sp, tmpState, ATKTurn1, DEFTurn1, magicMirrorTurn1, specialChargeTurn1, amp1, ahp2,
			ehp2, amp2;
	auto counter = 0;
	// データのループ
	for (int i = 0; i < result.position; ++i) {
		auto action = result.actions[i];
		auto damage = result.damages[i];
		auto turn = result.turns[i];
		auto initiative = result.initiative[i];
		auto ehp1 = result.ehp[i];
		auto ahp1 = result.ahp[i];
		auto isEnemy = result.isEnemy[i];
		auto specialChargeTurn = result.scTurn[i];
		int amp = -1;
		if (i >= 1) {
			amp = result.amp[i - 1];
		}

		auto special = gene[turn];

		std::string specialAction;
		if (special != 0 && special != -1) {
			specialAction = BattleEmulator::getActionName(special & 0x3ff);
		}

		// ターンが変わったら、前のターンのデータを出力
		if (turn != currentTurn) {
			if (currentTurn != -1) {
				// 前のターンの出力
				if (turn > PastTurns) {
					ss6
							<< std::left << std::setw(6) << (currentTurn + 1)
							<< std::setw(18) << sp
							<< std::setw(18) << aAction
							<< std::setw(18) << eAction[0]
							<< std::setw(6) << aDamage
							<< std::setw(6) << eDamage[0]
							<< std::setw(6) << ahp2
							<< std::setw(6) << ehp2
							<< std::setw(6) << amp1
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
		if (isEnemy) {
			eAction[counter] = BattleEmulator::getActionName(action);
			eDamage[counter] = damage;
			counter++;
			ahp2 = std::to_string(ahp1);
		} else {
			ehp2 = std::to_string(ehp1);
			amp2 = std::to_string(amp);
			aAction = BattleEmulator::getActionName(action);
			aDamage = damage;
			amp1 = std::to_string(amp);

			initiative_tmp = initiative;
			sp = specialAction;

			if (eAction[0] != "magic Burst" && eAction[1] != "magic Burst") {
				if (!initiative && action == BattleEmulator::TURN_SKIPPED || action == BattleEmulator::PARALYSIS ||
				    action == BattleEmulator::SLEEPING) {
					sp = "---------------";
				}
				if ((action == BattleEmulator::CURE_SLEEPING || action == BattleEmulator::CURE_PARALYSIS)) {
					sp = "---------------";
				}
			}
		}
	}

	// 最後のターンのデータを出力
	if (currentTurn != -1) {
		ss6
				<< std::left << std::setw(6) << (currentTurn + 1)
				<< std::setw(18) << sp
				<< std::setw(18) << aAction
				<< std::setw(18) << eAction[0]
				<< std::setw(6) << aDamage
				<< std::setw(6) << eDamage[0]
				<< std::setw(6) << ahp2
				<< std::setw(6) << ehp2
				<< std::setw(6) << amp2
				<< std::setw(11) << "" << "\n";
	}

	return ss6.str();
}

const std::string version = "v1.0.17e";

void showHeader() {
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
	std::cout << "dq9 Corvus battle emulator " << version << " (Optimized for O2), Build date: " << buildDate << ", " <<
			buildTime << " UTC/GMT, Compiler: " << compiler << std::endl;
#elif defined(NO_OPTIMIZATION)
	std::cout << "dq9 Corvus battle emulator " << version << " (No optimization), Build date: " << buildDate << ", " <<
			buildTime << " UTC/GMT, Compiler: " << compiler << std::endl;
#else
#endif
	std::cout << "Waiting for input[q/b]: " << std::endl;
}


//int main(int argc, char *argv[]) {

bool SearchRequest(const Player copiedPlayers2[2], uint64_t seed, const int aActions[350], bool dropbug,
                   int medicinalHerbCount,
                   std::stringstream &ss) {
	(void) dropbug;

	auto currentTurn = 0;
	int remainingMedicinalHerbCount = medicinalHerbCount > 0 ? medicinalHerbCount : 0;
	int32_t pastActions[350] = {0};
	for (int i = 0; i < 349; ++i) {
		if (aActions[i] == -1) {
			break;
		}
		if (aActions[i] == BattleEmulator::MEDICINAL_HERBS) {
			--remainingMedicinalHerbCount;
			if (remainingMedicinalHerbCount < 0) {
				ss << "BFS search failed: medicinal herbs exhausted by past actions currentTurn="
						<< currentTurn << std::endl;
				return false;
			}
		}
		pastActions[i] = aActions[i];
		currentTurn++;
	}
	pastActions[currentTurn] = -1;

	Player startPlayers[2] = {copiedPlayers2[0], copiedPlayers2[1]};
	int startPosition = 1;
	uint64_t startNowState = BattleEmulator::TYPE_2A;
	if (currentTurn > 0) {
		lcg::init(seed);
		BattleEmulator::Main(&startPosition, currentTurn, pastActions, startPlayers, nullptr, seed, nullptr,
		                     nullptr, -2, &startNowState);
	}

	if (startPlayers[0].hp == 0) {
		ss << "BFS search failed: player is already dead at turn=" << currentTurn << std::endl;
		return false;
	}
	if (startPlayers[1].hp == 0) {
		BattleResult battleResult;
		Player players[2] = {copiedPlayers2[0], copiedPlayers2[1]};
		int position = 1;
		uint64_t nowState = 0;
		lcg::init(seed);
		BattleEmulator::Main(&position, currentTurn, pastActions, players, &battleResult, seed, nullptr,
		                     nullptr, -1, &nowState);

		ss << dumpTable(battleResult, pastActions, startturn) << std::endl;
		ss << "0x" << std::hex << seed << std::dec << ": ";
		for (auto i = 0; i < 100; ++i) {
			if (pastActions[i] == 0 || pastActions[i] == -1) {
				break;
			}
			ss << pastActions[i] << ", ";
		}
		ss << std::endl;
		ss << "BFS currentTurn=" << currentTurn
				<< " futureTurn=0"
				<< " winTurn=" << currentTurn
				<< " maxDepth=0"
				<< " nodes=0"
				<< " sameTurnWins=1" << std::endl;
		return true;
	}

	int maxDepth = ActionOptimizer::MaxSearchDepth;
	const int maxFutureTurns = 349 - currentTurn;
	if (maxDepth > maxFutureTurns) {
		maxDepth = maxFutureTurns;
	}
	if (maxDepth <= 0) {
		ss << "BFS search failed: no future turn capacity currentTurn=" << currentTurn << std::endl;
		return false;
	}

	const ActionOptimizer::Result searchResult = ActionOptimizer::FindShortestWin(
		startPlayers, seed, startPosition, startNowState, currentTurn, maxDepth, remainingMedicinalHerbCount);
	if (!searchResult.solved) {
		ss << "BFS search failed: maxTurn=" << searchResult.maxDepth
				<< " currentTurn=" << currentTurn
				<< " nodes=" << searchResult.nodesVisited;
		if (searchResult.exhausted) {
			ss << " exhausted";
		}
		ss << std::endl;
		return false;
	}

	int32_t fullActions[350] = {0};
	for (int i = 0; i < currentTurn; ++i) {
		fullActions[i] = pastActions[i];
	}
	for (int i = 0; i < searchResult.turn; ++i) {
		fullActions[currentTurn + i] = searchResult.actions[i];
	}
	fullActions[currentTurn + searchResult.turn] = -1;

	BattleResult battleResult;
	Player players[2] = {copiedPlayers2[0], copiedPlayers2[1]};
	int position = 1;
	uint64_t nowState = 0;
	lcg::init(seed);
	BattleEmulator::Main(&position, currentTurn + searchResult.turn, fullActions, players, &battleResult, seed, nullptr,
	                     nullptr, -1, &nowState);

	ss << dumpTable(battleResult, fullActions, startturn) << std::endl;
	ss << "0x" << std::hex << seed << std::dec << ": ";
	for (auto i = 0; i < 100; ++i) {
		if (fullActions[i] == 0 || fullActions[i] == -1) {
			break;
		}
		ss << fullActions[i] << ", ";
	}
	ss << std::endl;
	ss << "BFS currentTurn=" << currentTurn
			<< " futureTurn=" << searchResult.turn
			<< " winTurn=" << (currentTurn + searchResult.turn)
			<< " maxDepth=" << maxDepth
			<< " nodes=" << searchResult.nodesVisited
			<< " sameTurnWins=" << searchResult.winningNodes << std::endl;

	return true;
}

// ブルートフォースリクエスト関数
[[nodiscard]] uint64_t BruteForceRequest(const Player copiedPlayers2[2], int hours, int minutes, int seconds, int turns,
                                         int eActions[350],
                                         int aActions[350], int damages[350]) {
	std::cout << "BruteForceRequest executed with time " << hours << ":" << minutes << ":" << seconds << std::endl;
	std::cout << "eActions: ";
	for (int i = 0; i < 350 && eActions[i] != -1; ++i) std::cout << eActions[i] << " ";
	std::cout << "\naActions: ";
	for (int i = 0; i < 350 && aActions[i] != -1; ++i) std::cout << aActions[i] << " ";
	std::cout << "\ndamages: ";
	for (int i = 0; i < 350 && damages[i] != -1; ++i) std::cout << damages[i] << " ";
	std::cout << std::endl;

	foundSeeds = 0;
	FoundSeed = 0;

	uint64_t totalSeconds = hours * 3600 + minutes * 60 + seconds;
	totalSeconds = totalSeconds;
	//数字は探索範囲(秒)
	auto time1 = static_cast<uint64_t>(floor((totalSeconds - 5) * (1 / 0.12515)));
	time1 = time1 << 16;
	std::cout << time1 << std::endl;


	//数字は探索範囲(秒)
	auto time2 = static_cast<uint64_t>(floor((totalSeconds + 5) * (1 / 0.125155)));
	time2 = time2 << 16;
	std::cout << time2 << std::endl;
	int32_t gene[350] = {0};

	for (int i = 0; i < 349; ++i) {
		gene[i] = aActions[i];
		if (aActions[i] == -1) {
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
	int *position = new int(1);
	auto *nowState = new uint64_t(0);
	int maxElement = 350;
	Player players[2];
	for (uint64_t seed = time1; seed < time2; ++seed) {
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
		if (resultBool) {
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

	if (foundSeeds == 1) {
		return FoundSeed;
	}
	if (foundSeeds == 0) {
		std::cout << "not found!!!" << std::endl;
		return 0;
	}
	FoundSeed = 0;
	foundSeeds = 0;
	return 0;
}


void BruteForceMainLoop(const Player copiedPlayers[2], uint64_t start, uint64_t end, int gene[350],
                        int damages[350], int eaction1[350]) {
	int maxElement = 350;
	for (uint64_t seed = start; seed < end; ++seed) {
		BattleEmulator::resetStartTurn();
		lcg::init(seed);
		int position = 1;
		uint64_t nowState = 0;
		Player players[2] = {copiedPlayers[0], copiedPlayers[1]};


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
void parseActions(const std::string &str, int actions[350]) {
	std::istringstream iss(str);
	int value, index = 0;
	while (iss >> value && index < 349) {
		actions[index++] = value;
	}
	actions[index++] = -1;
}


// メインループ
void mainLoop(const Player copiedPlayers[2]) {
	int eActions[350] = {0};
	int aActions[350] = {0};
	int damages[350] = {0};

	std::string input;
	while (std::getline(std::cin, input)) {
		//意図せずcinが閉じられると無限ループするので対策
		if (input.empty()) continue;

		char command = input[0];
		if (command == 'q') {
			std::cout << "Exiting loop." << std::endl;
			return;
		}
		if (command == 'b') {
			// Check if there is enough input (e.g., at least "b " and some parameters)
			if (input.size() < 3) {
				std::cerr << "Error: insufficient input for command 'b'." << std::endl;
				continue;
			}

			// Extract the substring after the command character and a space
			std::string params = input.substr(2);
			if (params.empty()) {
				std::cerr << "Error: no parameters provided for command 'b'." << std::endl;
				continue;
			}

			std::istringstream ss(params);

			int hours, minutes, seconds, turns;
			if (!(ss >> hours >> minutes >> seconds >> turns)) {
				std::cerr << "Error: failed to parse time parameters." << std::endl;
				continue;
			}

			// Read the three action strings separated by '-' delimiters
			std::string eActionsStr, aActionsStr, damagesStr;
			if (!std::getline(ss, eActionsStr, '-')) {
				std::cerr << "Error: failed to read eActions." << std::endl;
				continue;
			}
			if (!std::getline(ss, aActionsStr, '-')) {
				std::cerr << "Error: failed to read aActions." << std::endl;
				continue;
			}
			if (!std::getline(ss, damagesStr, '-')) {
				std::cerr << "Error: failed to read damages." << std::endl;
				continue;
			}

			// 各アクション配列に値を代入
			parseActions(eActionsStr, eActions);
			parseActions(aActionsStr, aActions);
			parseActions(damagesStr, damages);

			auto seed = BruteForceRequest(copiedPlayers, hours, minutes, seconds, turns, eActions, aActions, damages);
			if (foundSeeds == 1) {
				std::stringstream ss2;
				if (!SearchRequest(copiedPlayers, seed, aActions, true, kDefaultMedicinalHerbCount, ss2)) {
					// std::cout << std::endl;
					// std::cout << "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=" << std::endl;
					// std::cout << "      **YOU WILL NOW LOSE!**       " << std::endl;
					// std::cout << "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=" << std::endl;
					// std::cout << std::endl;
				}
				std::cout << ss2.str();
			}
			continue;
		}
		if (command == 'h') {
			showHeader();
			continue;
		}
		std::cerr << "Unknown command." << std::endl;
	}
	if (std::cerr.good()) {
		std::cerr <<
				"Unrecoverable Error: An anomaly occurred in the main loop of the C++ process, forcing the battle emulator process to terminate. To recover, please restart the integrated system"
				<< std::endl;
	}
}

int toint(char *str) {
	try {
		int number = std::stoi(str);
		return number;
	} catch (const std::invalid_argument &e) {
		std::cerr << "Invalid argument: " << e.what() << std::endl;
		return -1;
	} catch (const std::out_of_range &e) {
		std::cerr << "Out of range: " << e.what() << std::endl;
		return -1;
	}
}


// 左側の空白をトリム
std::string ltrim(const std::string &s) {
	size_t start = s.find_first_not_of(" \t\n\r\f\v");
	return (start == std::string::npos) ? "" : s.substr(start);
}

// 右側の空白をトリム
std::string rtrim(const std::string &s) {
	size_t end = s.find_last_not_of(" \t\n\r\f\v");
	return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

// 両側の空白をトリム
std::string trim(const std::string &s) {
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
		if (!std::getline(ss2, eActionsStr, '-')) {
			std::cerr << "Error: failed to read eActions." << std::endl;
			return false;
		}
		if (!std::getline(ss2, aActionsStr, '-')) {
			std::cerr << "Error: failed to read aActions." << std::endl;
			return false;
		}
		if (!std::getline(ss2, damagesStr, '-')) {
			std::cerr << "Error: failed to read damages." << std::endl;
			return false;
		}

		// 各アクション配列に値を代入
		parseActions(eActionsStr, eActions5);
		parseActions(aActionsStr, aActions5);
		parseActions(damagesStr, damages5);

		return true;
	}

	std::string buildDumpOutput(const Player copiedPlayers[2], uint64_t seed, int numThreads, bool dropbug) {
		lcg::init(seed, true);

		BattleEmulator::ResetTurnProcessed();

		std::stringstream ss;
		if (!SearchRequest(copiedPlayers, seed, aActions5, true, kDefaultMedicinalHerbCount, ss)) {
			ss << std::endl;
			ss << "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=" << std::endl;
			ss << "      **YOU WILL NOW LOSE!**       " << std::endl;
			ss << "=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=" << std::endl;
			ss << std::endl;

			auto turns = 0;
			for (int a_action: aActions5) {
				if (a_action == -1) {
					break;
				}
				turns++;
			}

			BattleResult res;
			Player players[2] = {copiedPlayers[0], copiedPlayers[1]};
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
EMSCRIPTEN_KEEPALIVE int wasm_prepare_input(const char *input) {
	;
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

#ifdef DEBUG4
static void runDebug4Benchmark(const Player copiedPlayers[2]) {
	constexpr int targetTurns = 10000000;
	constexpr int maxTurnsPerRun = 50;
	constexpr uint64_t baseSeed = 0x03642037ull;

	int32_t gene[350] = {0};
	for (int i = 0; i < maxTurnsPerRun; ++i) {
		gene[i] = BattleEmulator::HEAL;
	}
	gene[maxTurnsPerRun] = -1;

	Player benchPlayers[2] = {copiedPlayers[0], copiedPlayers[1]};
	benchPlayers[0].hp = 100000000;
	benchPlayers[0].maxHp = 100000000;
	benchPlayers[0].mp = 100000000;
	benchPlayers[0].maxMp = 100000000;
	benchPlayers[1].hp = 100000000;
	benchPlayers[1].maxHp = 100000000;

	BattleEmulator::ResetTurnProcessed();
	const auto started = std::chrono::steady_clock::now();

	int seedOffset = 0;
	while (BattleEmulator::getTurnProcessed() < targetTurns) {
		const int remaining = targetTurns - BattleEmulator::getTurnProcessed();
		const int runTurns = std::min(maxTurnsPerRun, remaining);
		const uint64_t seed = baseSeed + static_cast<uint64_t>(seedOffset);
		int position = 1;
		uint64_t nowState = 0;
		Player players[2] = {benchPlayers[0], benchPlayers[1]};

		lcg::init(seed);
		BattleEmulator::Main(&position, runTurns, gene, players, nullptr, seed, nullptr, nullptr, -2, &nowState);
		++seedOffset;
	}

	const auto finished = std::chrono::steady_clock::now();
	const double seconds = std::chrono::duration<double>(finished - started).count();
	const int turns = BattleEmulator::getTurnProcessed();
	std::cout << "DEBUG4 benchmark" << std::endl;
	std::cout << "baseSeed: 0x" << std::hex << baseSeed << std::dec << std::endl;
	std::cout << "seeds: " << seedOffset << std::endl;
	std::cout << "turns: " << turns << std::endl;
	std::cout << std::fixed << std::setprecision(3);
	std::cout << "seconds: " << seconds << std::endl;
	std::cout << "turns/sec: " << static_cast<double>(turns) / seconds << std::endl;
}
#endif

int main() {
	showHeader();

	//https://zenn.dev/reputeless/books/standard-cpp-for-competitive-programming/viewer/library-ios-iomanip#3.1-c-%E8%A8%80%E8%AA%9E%E3%81%AE%E5%85%A5%E5%87%BA%E5%8A%9B%E3%82%B9%E3%83%88%E3%83%AA%E3%83%BC%E3%83%A0%E3%81%A8%E3%81%AE%E5%90%8C%E6%9C%9F%E3%82%92%E7%84%A1%E5%8A%B9%E3%81%AB%E3%81%99%E3%82%8B
	//std::cin.tie(0)->sync_with_stdio(0);

#ifdef DEBUG4
	runDebug4Benchmark(copiedPlayers);
	return 0;
#endif

#if defined(OPTIMIZE_MODE)
	int actions1[350] = {};
	auto counter1 = 0;
	actions1[counter1++] = BattleEmulator::BUFF;
	actions1[counter1++] = BattleEmulator::MAGIC_MIRROR;
	actions1[counter1++] = BattleEmulator::PSYCHE_UP_ALLY;
	actions1[counter1] = -1;
	SimpleParameterOptimizer::optimize(copiedPlayers, 0x11060049ull, actions1, 100000, counter1);
	return 0;
#endif


#ifdef DEBUG2
	//THIS DEBUG CODE!
	//THIS DEBUG CODE
	uint64_t time1 = 0x279dcb3;

	int dummy[100];
	lcg::init(time1);
	int *position1 = new int(1);
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
	auto *NowState = new uint64_t(0); //エミュレーターの内部ステートを表すint

	Player players1[2];
	//int32_t gene1[350] = {0};
	//THIS DEBUG CODE!
	int32_t gene1[350] = {25, 25, 25, 27, 25, 26, 25, 25, 25,  };
	//gene1[19-1] = BattleEmulator::DEFENCE;
	int counter = 0;
	//
	// gene1[counter++] = BattleEmulator::ATTACK_ALLY;
	// gene1[counter++] = BattleEmulator::MEDICINAL_HERBS;
	// gene1[counter++] = BattleEmulator::ATTACK_ALLY;
	// gene1[counter++] = BattleEmulator::ATTACK_ALLY;
	// gene1[counter++] = BattleEmulator::HEAL;
	// gene1[counter++] = BattleEmulator::ATTACK_ALLY;
	// gene1[counter++] = BattleEmulator::ATTACK_ALLY;
	// gene1[counter++] = BattleEmulator::ATTACK_ALLY;
	// gene1[counter++] = BattleEmulator::HEAL;
	// gene1[counter++] = BattleEmulator::ATTACK_ALLY;
	// gene1[counter++] = BattleEmulator::ATTACK_ALLY;
	// gene1[counter++] = BattleEmulator::ATTACK_ALLY;
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
	uint64_t time1 = 0x0279dcb3;

	auto counter = 0;
	int actions[350] = {0};
	actions[counter++] = BattleEmulator::ATTACK_ALLY;
	// actions[counter++] = BattleEmulator::ATTACK_ALLY;
	// actions[counter++] = BattleEmulator::HEAL;
	actions[counter] = -1;

	std::stringstream ss;
	SearchRequest(copiedPlayers, time1, actions, false, kDefaultMedicinalHerbCount, ss);
	ss << std::endl;

	std::cout << ss.str();
	return 0;
#endif

	mainLoop(copiedPlayers);
	return 0;
}
