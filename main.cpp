#include <iostream>
#include <cstring>
#include <cmath>
#include <vector>
#include <iomanip>
#include <sstream>
#include <fstream>

#include "lcg.h"
#include "BattleEmulator.h"
#include "debug.h"
#include "ActionOptimizer.h"
#include "InputBuilder.h"

#ifdef DEBUG

#include <chrono>

#endif


namespace {
    // MinGW/GCC用のnoinline属性
#ifndef NOINLINE
#ifdef _MSC_VER
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE
#endif
#endif

    int toint(char *string);

    //void processResult(const Player *copiedPlayers, const uint64_t seed, std::string input);

    std::string ltrim(const std::string &s);

    std::string rtrim(const std::string &s);

    std::string trim(const char *s);

    std::string trim(const std::string &s);

    bool isMatchStrWithTrim(const char *s1, const char *s2);

    void help(const char *program_name);

    void SearchRequest(Player copiedPlayers[2], uint64_t seed, const int aActions[350], int numThreads);

    uint64_t BruteForceRequest(Player copiedPlayers[2], int hours, int minutes, int seconds, int turns,
                               int damages[350], int aActions[350]);

    void dumpTableMain(BattleResult &result1, Genome &genome, uint64_t seed, int turns);

    void printHeader(std::stringstream &ss);

    std::pair<char, int> toABCint(const char *str);

    int foundSeeds = 0;

    uint64_t FoundSeed = 0;

    int foundTurn = 0;
    int foundTurnOffset = 0;

    const char *version = "v4.0.4_vE_v5";

    std::stringstream performanceLogger = std::stringstream();

    constexpr int THREAD_COUNT = 1;
    // `InputBuilder` インスタンス作成
    InputBuilder builder;

    // ヘッダーを出力する関数
    void printHeader(std::stringstream &ss) {
        ss << std::left << std::setw(6) << "turn"
                << std::setw(18) << "sp"
                << std::setw(18) << "aAct"
                << std::setw(18) << "eAct1"
                << std::setw(6) << "aD"
                << std::setw(6) << "eD1"
                << std::setw(6) << "ahp"
                << std::setw(6) << "ehp"
                << std::setw(6) << "amp"

                << std::setw(6) << "ini"
                << std::setw(6) << "Para"
                << std::setw(6) << "Sct" << "\n";
        ss << std::string(99, '-') << "\n"; // 区切り線を出力
    }

    std::string dumpTable(BattleResult &result, int32_t gene[350], int PastTurns);

    std::string dumpTable(BattleResult &result, int32_t gene[350], int PastTurns) {
        std::stringstream ss6;
        printHeader(ss6);
        int currentTurn = -1;
        int eDamage[2] = {-1, -1}, aDamage = -1;
        bool initiative_tmp = false;
        std::string eAction[2], aAction, sp, tmpState, ATKTurn1, specialChargeTurn1, amp1,
                ahp2,
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
            auto state = result.state[i] & 0xf;
            auto specialChargeTurn = result.scTurn[i];
            int amp = -1;
            if (i >= 1) {
                amp = result.amp[i - 1];
            }


            if (state == BattleEmulator::TYPE_2A) {
                tmpState = "A";
            } else if (state == BattleEmulator::TYPE_2B) {
                tmpState = "B";
            } else if (state == BattleEmulator::TYPE_2C) {
                tmpState = "C";
            } else if (state == BattleEmulator::TYPE_2D) {
                tmpState = "D";
            }
            if (state == BattleEmulator::TYPE_2E) {
                tmpState = "E";
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
                                << std::setw(6) << amp2
                                << std::setw(6) << (initiative_tmp ? "yes" : "")
                                << std::setw(6) << ((aAction == "Paralysis" || aAction == "Cure Paralysis")
                                                        ? "yes"
                                                        : "")
                                << std::setw(6) << ((aAction == "Sleeping" || aAction == "Cure Sleeping") ? "yes" : "")
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
                if (specialChargeTurn > 0) {
                    specialChargeTurn1 = std::to_string(specialChargeTurn);
                }

                amp1 = std::to_string(amp);

                initiative_tmp = initiative;
                sp = specialAction;

                if (eAction[0] != "magic Burst" && eAction[1] != "magic Burst") {
                    //動けない場合、テーブルにアクションを表示しない
                    if ((action == BattleEmulator::INACTIVE_ALLY || action == BattleEmulator::PARALYSIS || action == BattleEmulator::CURE_SLEEPING || action == BattleEmulator::CURE_PARALYSIS)) {
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
                    << std::setw(6) << (initiative_tmp ? "yes" : "")
                    << std::setw(6) << ((aAction == "Paralysis" || aAction == "Cure Paralysis") ? "yes" : "")
                    << std::setw(6) << ((aAction == "Sleeping") ? "yes" : "")
                    << std::setw(6) << specialChargeTurn1
                    << std::setw(11) << "" << "\n";
        }

        return ss6.str();
    }

    void showHeader() {
#ifdef BUILD_DATE
        const auto buildDate = BUILD_DATE;
#else
        const std::string buildDate = "Unknown";
#endif

#ifdef BUILD_TIME
        const auto buildTime = BUILD_TIME;
#else
        const std::string buildTime = "Unknown";
#endif

        auto compiler = "Unknown";
#if defined(MINGW_BUILD)
        compiler = "mingw";
#elif defined(MSVC_BUILD)
        compiler = "msBuild";
#endif

#if defined(MULTITHREADING)
        std::string multiThreading = ", multithreading is not supported, -j " + std::to_string(THREAD_COUNT);
#elif defined(NO_MULTITHREADING)
        std::string multiThreading = ", multithreading is disabled";
#endif
#if defined(OPTIMIZATION_O3_ENABLED)
        std::cout << "dq9 Morag battle emulator " << version << " (Optimized for O3), Build date: " <<
                buildDate
                << ", " <<
                buildTime << " UTC/GMT, Compiler: " << compiler << multiThreading << std::endl;
#elif defined(OPTIMIZATION_O2_ENABLED)
        std::cout << "dq9 Morag battle emulator " << version << " (Optimized for O2), Build date: " << buildDate << ", " << buildTime  << " UTC/GMT, Compiler: " << compiler << multiThreading << std::endl;
#elif defined(NO_OPTIMIZATION)
        std::cout << "dq9 Morag battle emulator " << version << " (No optimization), Build date: " <<
                buildDate << ", " << buildTime << " UTC/GMT, Compiler: " << compiler << multiThreading << std::endl;
#else
        std::cout << "dq9 Corvus battle emulator" << version << " (Unknown build configuration), Build date: " << buildDate << ", " << buildTime   << " UTC, Compiler: " << compiler << std::endl;
        << ", " << buildTime << std::endl;
#endif
    }

    void help(const char *program_name) {
        std::cout << "Usage: " << program_name << " h m s [actions...]" << std::endl;;
        std::cerr << "error: Not enough argc!!" << std::endl;
    }

    void pla(int &enemyConsecutive, int *aActions, int &valuesIndex, bool EnemyPresent) {
        // 連続敵行動が3件以上の場合、眠り判定を行う
        if (enemyConsecutive >= 3) {
            while (enemyConsecutive >= 1) {
                aActions[valuesIndex++] = BattleEmulator::PARALYSIS;
                enemyConsecutive -= 1;
            }
        }
        enemyConsecutive = 0;
    }

    NOINLINE bool ProcessInputBuilder(const int argc, char *argv[], int *aActions, int *values, int &valuesIndex) {
        // 最初の3件は時間情報のため、最低でも4件必要
        if (argc < 4) {
            return false;
        }

        int counter2 = 0;

        // 行動引数は argv[4] 以降
        int totalActions = argc - 4;
        // 1ターンあたりの上限行動数（3件）
        constexpr int actionsPerTurn = 2;

        // ターン数は、totalActions を actionsPerTurn で割った商＋余りがあれば1ターンとして計上
        int turns = totalActions / actionsPerTurn;
        int remainder = totalActions % actionsPerTurn;
        if (remainder > 0) {
            turns++;
        }

        int enemyConsecutive = 0; // 複数ターンにわたる敵連続行動数
        int tokenIndex = 4; // 最初の行動引数の位置
        int enemyActions = 0; // このターン内の敵の行動数
        int offset = 0;

        for (int turn = 0; turn < turns; turn++) {
            if (argc <= tokenIndex) {
                break;
            }
            bool allyPresent = false; // このターンに味方行動があるかのフラグ
            bool EnemyPresent = false; // このターンに味方行動があるかのフラグ
            int turnCouner = 0;


            // 現ターンの行動数（最後のターンは3未満の場合があるので調整）
            int actionsThisTurn = actionsPerTurn;
            if ((turn == turns - 1) && (remainder > 0)) {
                actionsThisTurn = remainder;
            }

            // 現ターン分のトークンを処理
            for (int j = 0; j < actionsThisTurn; j++) {
                if (argc <= tokenIndex) {
                    break;
                }
                const char *token = argv[tokenIndex++];
                if (isMatchStrWithTrim(token, "h") || isMatchStrWithTrim(token, "ah")) {
                    // 回復は明示的な味方行動
                    enemyConsecutive += enemyActions;
                    pla(enemyConsecutive, aActions, valuesIndex, EnemyPresent);
                    enemyActions = 0;
                    aActions[valuesIndex++] = BattleEmulator::HEAL;
                    values[counter2++] = -2;
                    values[counter2++] = -2;
                    allyPresent = true;
                    foundTurnOffset = 0;
                } else if (isMatchStrWithTrim(token, "y") || isMatchStrWithTrim(token, "i")) {
                    aActions[valuesIndex++] = BattleEmulator::INACTIVE_ALLY;
                    allyPresent = true;
                    offset++;
                    if (remainder > 0) {
                        remainder--;
                    } else {
                        turns++;
                    }
                    foundTurnOffset++;
                    break;
                } else if (isMatchStrWithTrim(token, "r")) {
                    enemyConsecutive += enemyActions;
                    pla(enemyConsecutive, aActions, valuesIndex, EnemyPresent);
                    enemyActions = 0;
                } else if (isMatchStrWithTrim(token, "p")) {
                    enemyActions++;
                    foundTurnOffset++;
                } else {
                    // 上記以外は toABCint による分解処理
                    auto [prefix, tmp] = toABCint(token);

                    foundTurnOffset = 0;

                    // 味方行動の条件（今回は prefix == 'a' が味方とする）
                    if (prefix == 'a') {
                        enemyConsecutive += enemyActions;
                        pla(enemyConsecutive, aActions, valuesIndex, EnemyPresent);
                        enemyActions = 0;
                        aActions[valuesIndex++] = BattleEmulator::ATTACK_ALLY;
                        values[counter2++] = -3;
                        values[counter2++] = tmp;
                        allyPresent = true;
                    } else if (prefix == 'h') {
                        enemyConsecutive += enemyActions;
                        pla(enemyConsecutive, aActions, valuesIndex, EnemyPresent);
                        enemyActions = 0;
                        aActions[valuesIndex++] = BattleEmulator::HEAL;
                        values[counter2++] = -2;
                        values[counter2++] = tmp;
                        allyPresent = true;
                    } else {
                        values[counter2++] = tmp;
                        enemyActions++;
                        EnemyPresent = true;
                    }
                }
            } // 1ターン分の処理終了
            //0 2 21 a19 9 11 16 13 13 r
            //0 2 18 15 a16 y 14 11 9 11
            //0 2 42 a17 11 10 a18 12 10 9 13 r
            if ((enemyActions == 2 || enemyActions == 3) && ((tokenIndex - 3 - enemyActions) % 2 == offset % 2)) {
                enemyActions--;
            }
        }

        enemyConsecutive += enemyActions;
        pla(enemyConsecutive, aActions, valuesIndex, false);

        aActions[valuesIndex] = -1;
        values[counter2] = -1;


        return true;
    }


    //入力パーサー
    NOINLINE int ProgramMain(Player players[2], int hours, int minutes, int seconds, int argc, char *argv[]) {
        const int MAX = 350;
        // values[] はダメージやホイミ/味方行動マーカー、麻痺マーカー (-10) を格納する
        int values[MAX] = {0};
        // aActions[] は味方行動（ホイミ、味方攻撃、麻痺の場合は PARALYSIS）を格納する
        int aActions[MAX] = {0};

        int valuesIndex = 0; // values[] の書き込み位置

        ProcessInputBuilder(argc, argv, aActions, values, valuesIndex);

        FoundSeed = 0;
        foundSeeds = 0;
        auto seed = BruteForceRequest(players, hours, minutes, seconds, valuesIndex, values, aActions);
        std::cout << "foundTurn: " << (foundTurn + foundTurnOffset) << ", " << valuesIndex << std::endl;
        if (foundSeeds == 1) {
            SearchRequest(players, seed, aActions, THREAD_COUNT);
        }
        return 0;
    }

    /**
     * 戦闘結果とゲノム情報を基に、テーブルデータを出力し、指定されたシード値と行動データをコンソールに表示します。
     * 結果には攻撃力、防御力、バージョン情報が含まれます。
     *
     * @param result1 戦闘結果オブジェクト。テーブル生成に必要なデータを提供します。
     * @param genome ゲノム情報オブジェクト。行動データを含みます。
     * @param seed テーブル生成と表示に使用されるランダムシード値。
     * @param turns テーブル表示を省略するターン数(リリースバイナリでのみ使用)
     */
    void dumpTableMain(BattleResult &result1, Genome &genome, uint64_t seed, int turns) {
        std::cout << dumpTable(result1, genome.actions, turns) << std::endl;

        std::cout << "ver: " << version << ", seed: ";
        std::cout << "0x" << std::hex << seed << std::dec << ", actions: ";

        for (auto i = 0; i < 100; ++i) {
            if (genome.actions[i] == 0 || genome.actions[i] == -1) {
                break;
            }
            std::cout << genome.actions[i] << ", ";
        }
        std::cout << std::endl;
    }

    void PerformanceDebug(const char *name, int turnProcessed, double elapsed_time1, uint64_t seeds) {
        // 正しい計算：1秒あたりの探索回数 (万回/秒)
        double performance = (static_cast<double>(turnProcessed) * 100.0) /
                             static_cast<double>(elapsed_time1);
        performanceLogger << name << ": Turn Consumed: " << turnProcessed << " (" << (
            static_cast<double>(turnProcessed) / 10000) << " mann), ";

        if (seeds != 0) {
            performanceLogger << "Seed Processd: " << (seeds) << "  (" << (
                static_cast<double>(seeds) / 10000) << " mann), ";
        }
        performanceLogger << "elapsed time: " << double(elapsed_time1) / 1000 << " ms, " <<
                "Performance: " << std::fixed << std::setprecision(2) << performance << " mann turns/s" <<
                std::endl;
    }

#ifdef MULTITHREADING
    void SearchRequest(Player copiedPlayers[2], uint64_t seed, const int aActions[350], int numThreads) {
#ifdef DEBUG
        auto t0 = std::chrono::high_resolution_clock::now();
        BattleEmulator::ResetTurnProcessed();
#endif

        int32_t gene[350] = {0};
        auto turns = 0;
        for (int i = 0; i < 350; ++i) {
            gene[i] = aActions[i];
            if (aActions[i] == -1) {
                gene[i] = -1;
                gene[i + 1] = -1;
                break;
            }
            turns++;
        }
        if (foundTurn != 0) {
            turns = foundTurn + foundTurnOffset;
        }

        auto genome =
                ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 100000, gene, numThreads);

        auto turnProcessed = BattleEmulator::getTurnProcessed();
        std::optional<BattleResult> result1;
        result1 = BattleResult();
        Player players[2] = {copiedPlayers[0], copiedPlayers[1]};

        lcg::init(seed);

        auto *position = new int(1);
        auto *nowState = new uint64_t(0);

        BattleEmulator::Main(position, 100, genome.actions, players, result1, seed, nullptr, nullptr, -1,
                             nowState);

        delete position;
        delete nowState;

#if defined(MINGW_BUILD)
        dumpTableMain(result1.value(), genome, seed, 0);
#else
        dumpTableMain(result1.value(), genome, seed, turns);
#endif

#ifdef DEBUG
        auto t3 = std::chrono::high_resolution_clock::now();
        auto elapsed_time1 =
                std::chrono::duration_cast<std::chrono::microseconds>(t3 - t0).count();
        PerformanceDebug("Searcher multi", turnProcessed, static_cast<double>(elapsed_time1), 0);
#endif
    }
#elif NO_MULTITHREADING

    void SearchRequest(const Player copiedPlayers[2], uint64_t seed, const int aActions[350], int numThreads = 1) {
#ifdef DEBUG
        auto t0 = std::chrono::high_resolution_clock::now();
        BattleEmulator::ResetTurnProcessed();
#endif

        int32_t gene[350] = {0};
        auto turns = 0;
        for (int i = 0; i < 350; ++i) {
            gene[i] = aActions[i];
            if (aActions[i] == -1) {
                gene[i] = -1;
                gene[i + 1] = -1;
                break;
            }
            turns++;
        }

        lcg::init(seed);

        BattleResult bestResult;
        Genome bestGenome;
        int maxTurns = INT_MAX - 1;

        std::optional<BattleResult> result1;
        result1 = BattleResult();
        Player players[2];

        auto *position = new int(1);
        auto *nowState = new uint64_t(0);

        for (int i = 0; i < 1000; ++i) {
            auto genome = ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 1500, gene, i * 2);

            players[0] = copiedPlayers[0];
            players[1] = copiedPlayers[1];

            (*position) = 1;
            (*nowState) = 0;

            result1->clear();
            BattleEmulator::Main(position, turns + 100, genome.actions, players, result1, seed, nullptr, nullptr, -1,
                                 nowState);

            if (players[0].hp >= 0 && players[1].hp == 0) {
                if (result1->turn < maxTurns) {
                    maxTurns = result1->turn;
                    bestResult = result1.value();
                    bestGenome = genome;
                }
            }
        }


        delete position;
        delete nowState;

        dumpTableMain(bestResult, bestGenome, seed, turns);

#ifdef DEBUG
        auto turnProcessed = BattleEmulator::getTurnProcessed();
        auto t3 = std::chrono::high_resolution_clock::now();
        auto elapsed_time1 =
                std::chrono::duration_cast<std::chrono::microseconds>(t3 - t0).count();
        PerformanceDebug("Searcher single", turnProcessed, static_cast<double>(elapsed_time1), 0);
#endif
    }

#endif

    void BruteForceMainLoop(const Player copiedPlayers[2], uint64_t start, uint64_t end, int turns, int gene[350],
                            int damages[350]) {
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
                                                   (std::optional<BattleResult> &) std::nullopt, seed, nullptr,
                                                   damages,
                                                   maxElement,
                                                   nowState);
            if (resultBool) {
                std::cout << seed << std::endl;
                FoundSeed = seed;
                foundSeeds++;
                foundTurn = BattleEmulator::getStartTurn();
            }
        }
        delete position;
        delete nowState;
    }

    // ブルートフォースリクエスト関数
    [[nodiscard]] uint64_t BruteForceRequest(Player copiedPlayers[2], int hours, int minutes, int seconds,
                                             int turns, int damages[350], int aActions[350]) {
#ifdef DEBUG
        auto t0 = std::chrono::high_resolution_clock::now();
#endif

        std::cout << "BruteForceRequest executed with time " << hours << ":" << minutes << ":" << seconds <<
                std::endl;
        std::cout << "\naActions: ";
        for (int i = 0; i < 350 && aActions[i] != -1; ++i) std::cout << aActions[i] << " ";
        std::cout << "\ndamages: ";
        for (int i = 0; i < 350 && damages[i] != -1; ++i) std::cout << damages[i] << " ";
        std::cout << std::endl;
        BattleEmulator::ResetTurnProcessed();

        foundSeeds = 0;
        FoundSeed = 0;

        int totalSeconds = hours * 3600 + minutes * 60 + seconds;
        totalSeconds = totalSeconds - 15;
        auto time1 = static_cast<uint64_t>(floor((totalSeconds - 3.5) * (1 / 0.12515)));
        time1 = (time1 & 0xffff) << 16;

        auto time2 = static_cast<uint64_t>(floor((totalSeconds + 3.5) * (1 / 0.125155)));
        time2 = (time2 & 0xffff) << 16;

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
        BruteForceMainLoop(copiedPlayers, time1, time2, turns, aActions, damages);

        std::cout << std::endl << "found: " << foundSeeds << std::endl;

        if (foundSeeds == 1) {
#ifdef DEBUG
            auto turnProcessed = BattleEmulator::getTurnProcessed();
            BattleEmulator::ResetTurnProcessed();
            auto t3 = std::chrono::high_resolution_clock::now();
            auto elapsed_time1 =
                    std::chrono::duration_cast<std::chrono::microseconds>(t3 - t0).count();
            PerformanceDebug("BruteForcer", turnProcessed, static_cast<int>(elapsed_time1), time2 - time1);

#endif

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

    NOINLINE std::pair<char, int> toABCint(const char *str) {
        if (str == nullptr) throw std::invalid_argument("Input is null");

        size_t len = std::strlen(str);
        if (len > 4) throw std::length_error("Input exceeds maximum allowed length (4)");

        // 先頭がアルファベットの場合
        if (len >= 2 && std::isalpha(static_cast<unsigned char>(str[0]))) {
            char prefix = static_cast<char>(std::tolower(static_cast<unsigned char>(str[0])));
            int value = 0;

            for (size_t i = 1; i < len; ++i) {
                if (!std::isdigit(static_cast<unsigned char>(str[i]))) {
                    throw std::invalid_argument("Invalid character in numeric portion");
                }
                value = value * 10 + (str[i] - '0');
            }

            return std::make_pair(prefix, value);
        }

        // 通常の整数として扱う（先頭が数字の場合）
        try {
            int number = std::stoi(str);
            return std::make_pair('\0', number);
        } catch (const std::invalid_argument &e) {
            std::cerr << "Invalid argument: " << e.what() << std::endl;
            return std::make_pair('\0', -1);
        } catch (const std::out_of_range &e) {
            std::cerr << "Out of range: " << e.what() << std::endl;
            return std::make_pair('\0', -1);
        }
    }


    // 左側の空白をトリム
    NOINLINE std::string ltrim(const std::string &s) {
        size_t start = s.find_first_not_of(" \t\n\r\f\v");
        return (start == std::string::npos) ? "" : s.substr(start);
    }

    // 右側の空白をトリム
    NOINLINE std::string rtrim(const std::string &s) {
        size_t end = s.find_last_not_of(" \t\n\r\f\v");
        return (end == std::string::npos) ? "" : s.substr(0, end + 1);
    }


    // char* を受け取るバージョン（std::stringに変換せず処理）
    NOINLINE std::string trim(const char *s) {
        if (s == nullptr) return "";
        std::string str(s);
        return trim(str);
    }

    // 両側の空白をトリム
    NOINLINE std::string trim(const std::string &s) {
        return rtrim(ltrim(s));
    }

    NOINLINE bool isMatchStrWithTrim(const char *s1, const char *s2) {
        return trim(s1) == trim(s2);
    }
}


constexpr Player BasePlayers[2] = {
    // プレイヤー1
    {
        65, 65.0, 61, 61, 66, 66, 40, 40, 29, 22, // 最初のメンバー
        22, false, false, 0, false, 0, -1,
        // specialCharge, dirtySpecialCharge, specialChargeTurn, inactive, paralysis, paralysisLevel, paralysisTurns
        8, 1.0, false, -1, 0, -1, // SpecialMedicineCount, defence, sleeping, sleepingTurn, BuffLevel, BuffTurns
        false, -1, 0, -1, 0, false, 1, 1, 1, -1, 0, -1, false, 2, false, -1, -1, 7, false
    }, // hasMagicMirror, MagicMirrorTurn, AtkBuffLevel, AtkBuffTurn, TensionLevel

    // プレイヤー2
    {
        456, 456.0, 56, 56, 58, 58, 54, 54, 0, 255, // 最初のメンバー
        255, false, false, 0, false, 0, -1,
        // specialCharge, dirtySpecialCharge, specialChargeTurn, inactive, paralysis, paralysisLevel, paralysisTurns
        0, 1.0, false, -1, 0, -1, // SpecialMedicineCount, defence, sleeping, sleepingTurn, BuffLevel, BuffTurns
        false, -1, 0, -1, 0, false, 0, 0, 0, -1, 0, -1, false, 2, false, -1, -1, 7, false
    } // hasMagicMirror, MagicMirrorTurn, AtkBuffLevel, AtkBuffTurn, TensionLevel
};


int main(int argc, char *argv[]) {
    showHeader();
#ifdef DEBUG
    auto t0 = std::chrono::high_resolution_clock::now();
#endif


    //https://zenn.dev/reputeless/books/standard-cpp-for-competitive-programming/viewer/library-ios-iomanip#3.1-c-%E8%A8%80%E8%AA%9E%E3%81%AE%E5%85%A5%E5%87%BA%E5%8A%9B%E3%82%B9%E3%83%88%E3%83%AA%E3%83%BC%E3%83%A0%E3%81%A8%E3%81%AE%E5%90%8C%E6%9C%9F%E3%82%92%E7%84%A1%E5%8A%B9%E3%81%AB%E3%81%99%E3%82%8B
    //std::cin.tie(0)->sync_with_stdio(0);


#ifdef DEBUG2
    uint64_t time1 = 0x3cc2e2c;

    int dummy[100];
    lcg::init(time1, false);
    int *position1 = new int(1);

    //ver: v4.0.3_vW_aa, seed: 0x3cc2e2c, actions: 25, 25, 25, 25, 26, 25, 59, 25, 27, 27, 59, 59, 23, 61, 27, 23, 59, 59, 61, 59, 23, 25, 25, 23, 25, 59, 25, 25, 61,

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

    int32_t gene1[350] = {
        25, 25, 25, 25, 26, 25, 59, 25, 27, 27, 59, 59, 23, 61, 27, 23, 59, 59, 61, 59, 23, 25, 25, 23, 25, 59, 25, 25, 61,
        BattleEmulator::ATTACK_ALLY};
    //0x22e2dbaf:
    //0x44dbafa: 25, 25, 25, 50, 54, 25, 50, 54, 56, 54, 25, 54, 53, 53, 25, 50, 25, 56, 54, 25, 54,
    //ver: v4.0.3_vS_aa, seed: 0x3e5f51b, actions: 25, 22, 22, 25, 25, 26, 25, 25, 59, 61, 25, 59, 23, 56, 25, 61, 61, 23, 61, 25, 56, 61, 59, 25,
    // int32_t gene1[350] = {
    //     25, 25, 25, 26, 25, 24, 25, 61, 23, 59, 61, 61, 23, 25, 23, 25, 59, 61, 61, 23, 25, 56, 25, 59, 59, 59, 25,
    //     BattleEmulator::ATTACK_ALLY};
    //gene1[19-1] = BattleEmulator::DEFENCE;
    int counter = 0;
    // int32_t gene1[350] = {0};
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // //


    //
    // gene1[counter++] = BattleEmulator::MIRACLE_SLASH;
    // gene1[counter++] = BattleEmulator::MIRACLE_SLASH;
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // gene1[counter++] = BattleEmulator::CRACKLE;
    // gene1[counter++] = BattleEmulator::CRACKLE;
    // gene1[counter++] = BattleEmulator::CRACKLE;
    // gene1[counter++] = BattleEmulator::CRACKLE;
    // gene1[counter++] = BattleEmulator::ATTACK_ALLY;
    // gene1[counter++] = BattleEmulator::MIRACLE_SLASH;
    // gene1[counter++] = BattleEmulator::MIRACLE_SLASH;
    // gene1[counter++] = BattleEmulator::MIRACLE_SLASH;
    // gene1[counter++] = BattleEmulator::MIRACLE_SLASH;
    // gene1[counter++] = BattleEmulator::MIRACLE_SLASH;
    // gene1[counter++] = BattleEmulator::MIRACLE_SLASH;

    //for (int i = 0; i < 10; ++i) {
    (*NowState) = 0;
    (*position1) = 1;
    std::optional<BattleResult> dummy1;
    dummy1 = BattleResult();
    std::memcpy(players1, BasePlayers, sizeof(players1));
    BattleEmulator::Main(position1, (counter == 0 ? 1000 : counter), gene1, players1, dummy1, time1, dummy, dummy, -1,
                         NowState);

    std::stringstream ss1;
    ss1 << time1 << " ";

    if (dummy1.has_value()) {
        std::cout << dumpTable(dummy1.value(), gene1, -1) << std::endl;
    }
    //}
    delete position1;
    delete NowState;

    return 0;
#endif

#ifdef DEBUG3
    uint64_t seed = 139924924;

    int actions[350] = {
        25, -1,
        //25, 25, 26, 25, 22, 25, 25, -1
        //        25, 25, 26, 25, 22, 25, 25, -1
    };
    Player Player5[2] = {BasePlayers[0], BasePlayers[1]};
    SearchRequest(Player5, seed, actions, 1);

    std::cout << performanceLogger.rdbuf() << std::endl;

    return 0;
#endif

    if (argc < 5) {
        help(argv[0]);
        return 1;
    }

    // 戦闘発生時間の取得
    const int hours = toint(argv[1]);
    const int minutes = toint(argv[2]);
    const int seconds = toint(argv[3]);

    if (hours < 0 || minutes < 0 || seconds < 0) {
        std::cerr << "Invalid time parameters" << std::endl;
        return 1;
    }

    Player players2[2] = {BasePlayers[0], BasePlayers[1]};

    auto exitCode = ProgramMain(players2, hours, minutes, seconds, argc, argv);
    std::cout << performanceLogger.rdbuf();
#ifdef DEBUG
    auto t1 = std::chrono::high_resolution_clock::now();
    auto elapsed_time =
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    std::cout << "elapsed time: " << double(elapsed_time) / 1000 << " ms" << std::endl;
#endif

    return exitCode;
}
