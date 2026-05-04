#include <iostream>
#include <cstring>
#include <cmath>
#include <vector>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <memory>

#include "lcg.h"
#include "BattleEmulator.h"
#include "debug.h"
#include "ActionOptimizer.h"
#include "InputBuilder.h"

#ifdef DEBUG

#include <chrono>

#endif
#if defined(OPTIMIZE_MODE)
#include "SimpleParameterOptimizer.h"
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

    bool SearchRequest(const Player copiedPlayers[2], uint64_t seed, const int aActions[350], int numThreads, bool Dropbug);

    uint64_t BruteForceRequest(const Player copiedPlayers[2], int hours, int minutes, int seconds, int turns,
                               int aActions[350], int damages[350]);

    void dumpTableMain(BattleResult &result1, Genome &genome, uint64_t seed, int turns);

    void printHeader(std::stringstream &ss);

    std::pair<char, int> toABCint(const char *str);

    int foundSeeds = 0;

    uint64_t FoundSeed = 0;

    int foundTurn = 0;
    int foundTurnOffset = 0;

    const char *version = "v11.0.0_vA_v2";

    std::stringstream performanceLogger = std::stringstream();

#if defined(BattleEmulatorLV19)
    constexpr int THREAD_COUNT = 4;
#elif defined(BattleEmulatorLV13)
    constexpr int THREAD_COUNT = 5;
#elif defined(erusionn_lv21)
    constexpr int THREAD_COUNT = 5;
#endif
    // `InputBuilder` インスタンス作成
    InputBuilder builder;

#if defined(erusionn_lv21)
    constexpr Player BasePlayers[2] = {
        // プレイヤー1
        {
            131, 131.0, 205, 205, 146, 146, 105, 105, 120, 70, // 最初のメンバー
            70, false, false, 0, false, 0, -1,
            // specialCharge, dirtySpecialCharge, specialChargeTurn, inactive, paralysis, paralysisLevel, paralysisTurns
            5, 1.0, false, -1, 0, -1, // SpecialMedicineCount, defence, sleeping, sleepingTurn, BuffLevel, BuffTurns
            false, -1, 0, -1, 0, false, 1, 1, 1, -1, 0, -1, false, 2, false, -1, false
        }, // hasMagicMirror, MagicMirrorTurn, AtkBuffLevel, AtkBuffTurn, TensionLevel

        // プレイヤー2
        {
            1276, 1276.0, 200, 200, 158, 158, 85, 85, 0, 40, // 最初のメンバー
            40, false, false, 0, false, 0, -1,
            // specialCharge, dirtySpecialCharge, specialChargeTurn, inactive, paralysis, paralysisLevel, paralysisTurns
            0, 1.0, false, -1, 0, -1, // SpecialMedicineCount, defence, sleeping, sleepingTurn, BuffLevel, BuffTurns
            false, -1, 0, -1, 0, false, 0, 0, 0, -1, 0, -1, false, 2, false, -1, false
        } // hasMagicMirror, MagicMirrorTurn, AtkBuffLevel, AtkBuffTurn, TensionLevel
    };
#endif
    /**
     * バトルログのヘッダー情報を文字列ストリームに書き込みます。
     * ヘッダーはターンや各種パラメータのラベルで構成され、フォーマット済みの表形式で出力されます。
     * また、ヘッダーの下部には区切り線が出力されます。
     *
     * @param ss 出力先の文字列ストリームオブジェクト
     */
    void printHeader(std::stringstream &ss) {
        ss << std::left << std::setw(6) << "turn"
                << std::setw(18) << "sp"
                << std::setw(18) << "aAct"
                << std::setw(18) << "eAct1"
                //<< std::setw(18) << "eAct2"
                << std::setw(6) << "aD"
                << std::setw(6) << "eD1"
                //<< std::setw(6) << "eD2"
                << std::setw(6) << "ahp"
                << std::setw(6) << "ehp"
                << std::setw(6) << "amp"
                << std::setw(6) << "ini"
#if defined(MINGW_BUILD)
                << std::setw(6) << "DET"
                //<< std::setw(6) << "TAB"
                << std::setw(6) << "Sct"
#endif
                << "\n";
#if defined(MINGW_BUILD)
        ss << std::string(110, '-') << "\n"; // 区切り線を出力
#else
        ss << std::string(100, '-') << "\n"; // 区切り線を出力
#endif
    }

    std::string dumpTable(BattleResult &result, int32_t gene[350], int PastTurns);

    /**
     * バトル結果および対応するデータからテーブルを作成し、その内容を文字列として返します。
     * テーブルには各ターンの攻撃や防御、特殊行動などの詳細情報が含まれます。
     *
     * @param result バトル結果を格納したBattleResultオブジェクト
     * @param gene 各ターンの味方の行動を示す遺伝子配列
     * @param PastTurns 結果の出力に含めない過去のターン数
     * @return 整形されたテーブルを表す文字列
     */
    std::string dumpTable(BattleResult &result, int32_t gene[350], int PastTurns) {
        std::stringstream ss6;
        printHeader(ss6);
        int currentTurn = -1;
        int eDamage[2] = {-1, -1}, aDamage = -1;
        bool initiative_tmp, def_f = false;
        std::string eAction[2], aAction, sp, tmpState, DEFTurn1, specialChargeTurn1,
                ahp2,
                ehp2, amp2, poisonTurn1, SpeedTurn1, tmp_state;
        auto counter = 0;
        // データのループ
        for (int i = 0; i < result.position; ++i) {
            auto action = result.actions[i];
            auto damage = result.damages[i];
            auto DEFTurn = result.BuffTurnss[i];
            auto turn = result.turns[i];
            auto initiative = result.initiative[i];
            auto ehp1 = result.ehp[i];
            auto ahp1 = result.ahp[i];
            auto isEnemy = result.isEnemy[i];
            auto state = result.state[i] & 0xf;
            auto specialChargeTurn = result.scTurn[i];
            auto poisonTurn = result.PoisonTurns[i];
            auto SpeedTurn = result.SpeedTurn[i];
            auto defenseFlag = result.defenseFlag[i];
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
                                //<< std::setw(18) << eAction[1]
                                << std::setw(6) << aDamage
                                << std::setw(6) << eDamage[0]
                                //<< std::setw(6) << eDamage[1]
                                << std::setw(6) << ahp2
                                << std::setw(6) << ehp2
                                << std::setw(6) << amp2
                                << std::setw(6) << (initiative_tmp ? "yes" : "")
#if defined(MINGW_BUILD)
                                << std::setw(6) << DEFTurn1
                                //<< std::setw(6) << tmp_state
                                << std::setw(6) << specialChargeTurn1
                                << std::setw(11) << ""
#endif
                                << "\n";
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
                DEFTurn1 = "";
                poisonTurn1 = "";
                SpeedTurn1 = "";
                specialChargeTurn1 = "";
                tmp_state = "";
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
                if (DEFTurn >= 0) {
                    DEFTurn1 = std::to_string(DEFTurn);
                }
                if (poisonTurn >= 0) {
                    poisonTurn1 = std::to_string(poisonTurn);
                }

                if (SpeedTurn >= 0) {
                    SpeedTurn1 = std::to_string(SpeedTurn);
                }
                if (specialChargeTurn > 0) {
                    specialChargeTurn1 = std::to_string(specialChargeTurn);
                }
                initiative_tmp = initiative;
                sp = specialAction;

                if (state == BattleEmulator::TYPE_2A) {
                    tmp_state = "A";
                } else if (state == BattleEmulator::TYPE_2B) {
                    tmp_state = "B";
                }

                if (eAction[0] != "magic Burst" && eAction[1] != "magic Burst") {
                    if (!initiative && (action == BattleEmulator::TURN_SKIPPED || action == BattleEmulator::PARALYSIS ||
                                        action == BattleEmulator::SLEEPING)) {
                        sp = "---------------";
                    }
                    if ((action == BattleEmulator::CURE_SLEEPING || action == BattleEmulator::CURE_PARALYSIS)) {
                        sp = "---------------";
                    }
                    if (!initiative && defenseFlag && action != BattleEmulator::DEFENCE) {
                        sp = "Defense !Sleep";
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
                    //<< std::setw(18) << eAction[1]
                    << std::setw(6) << aDamage
                    << std::setw(6) << eDamage[0]
                    //<< std::setw(6) << eDamage[1]
                    << std::setw(6) << ahp2
                    << std::setw(6) << ehp2
                    << std::setw(6) << amp2
                    << std::setw(6) << (initiative_tmp ? "yes" : "")
#if defined(MINGW_BUILD)
                    << std::setw(6) << DEFTurn1
                    //<< std::setw(6) << tmp_state
                    << std::setw(6) << specialChargeTurn1
                    << std::setw(11) << ""
#endif

                    << "\n";
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
        std::string multiThreading = ", multithreading is supported, -j " + std::to_string(THREAD_COUNT);
#elif defined(NO_MULTITHREADING)
        std::string multiThreading = ", multithreading is disabled";
#endif
#if defined(OPTIMIZATION_O3_ENABLED)
        std::cout << "dq9 Ragin' Contagion battle emulator " << version << " (Optimized for O3), Build date: " <<
                buildDate
                << ", " <<
                buildTime << " UTC/GMT, Compiler: " << compiler << std::endl;
#elif defined(OPTIMIZATION_O2_ENABLED)
        std::cout << "dq9 Ragin' Contagion battle emulator " << version << " (Optimized for O2), Build date: " << buildDate << ", " << buildTime  << " UTC/GMT, Compiler: " << compiler << std::endl;
#elif defined(NO_OPTIMIZATION)
        std::cout << "dq9 Ragin' Contagion battle emulator " << version << " (No optimization), Build date: " << buildDate << ", " << buildTime << " UTC/GMT, Compiler: " << compiler << std::endl;
#else
        std::cout << "dq9 Corvus battle emulator" << version << " (Unknown build configuration), Build date: " << buildDate << ", " << buildTime   << " UTC, Compiler: " << compiler << std::endl;
        << ", " << buildTime << std::endl;
#endif
    }

    // ヘッダー出力関数
    NOINLINE void printShortcutTableHeader() {
        std::cout << std::left << std::setw(5) << "Key"
                << std::left << std::setw(18) << "Action"
                << std::endl;

        std::cout << std::string(3, '-') << " "
                << std::string(18, '-') << std::endl;
    }

    // 各行の出力関数
    NOINLINE void printShortcutEntry(const char *shortcutKey, const char *name) {
        std::cout << std::left << std::setw(5) << shortcutKey
                << std::left << std::setw(18) << name
                << std::endl;
    }

    /**
     * プログラムの使用方法について、ヘルプ情報をコンソールに表示します。
     * 指定されたプログラム名をもとに、コマンドのフォーマットや例を出力します。
     * また、アクションショートカットテーブルも表示されます。
     *
     * @param program_name 実行中のプログラム名
     */
    void help(const char *program_name) {
        std::cout << "Usage: " << program_name << " h m s [actions...]" << std::endl;
        std::cout << "WARNING: Please input 0 damage attacks (such as shield guard) correctly" << std::endl;

        // printShortcutTableHeader();
        // printShortcutEntry("A", "Attack Ally");
        // printShortcutEntry("A", BattleEmulator::getActionName(BattleEmulator::MIRACLE_SLASH));


        std::cerr << "error: Not enough argc!!" << std::endl;
    }

    /**
     * 入力データを解析し、ビルダーに適切なコマンドを追加します。
     * コマンドの形式やルールに基づき、条件処理を行います。
     * 時に、眠りなどに対し適切に処理できるように設計されています。
     *
     * @param argc 入力引数の個数
     * @param argv 入力引数を保持する文字列配列
     * @return 入力が正常に処理され、ビルダーに追加された場合はtrueを返し、
     *         引数の個数が不十分または他のエラーが発生した場合はfalseを返します。
     */
     NOINLINE bool ProcessInputBuilder(const int argc, char *argv[]) {
        // 4番目以降の引数を `push()` に入れる
        for (int i = 4; i < argc; ++i) {
            if (isMatchStrWithTrim(argv[i], "b") || isMatchStrWithTrim(argv[i], "s") || isMatchStrWithTrim(argv[i], "ab") || isMatchStrWithTrim(argv[i], "as")) {
                builder.push(InputBuilder::TYPE_BUFF_ALLY, InputBuilder::PREFIX_BUFF_ALLY);
                continue;
            }
            if (isMatchStrWithTrim(argv[i], "d") || isMatchStrWithTrim(argv[i], "D") ||  isMatchStrWithTrim(argv[i], "ad") || isMatchStrWithTrim(argv[i], "aD") || isMatchStrWithTrim(argv[i], "h") || isMatchStrWithTrim(argv[i], "ah")) {
                builder.push(InputBuilder::TYPE_PRE_SPECIAL_MEDICINE, InputBuilder::PREFIX_SPECIAL_MEDICINE);
                builder.push(InputBuilder::TYPE_SPECIAL_MEDICINE, InputBuilder::PREFIX_SPECIAL_MEDICINE);
                continue;
            }
            if (isMatchStrWithTrim(argv[i], "at") || isMatchStrWithTrim(argv[i], "ap") || isMatchStrWithTrim(argv[i], "t") || isMatchStrWithTrim(argv[i], "p")) {
                builder.push(InputBuilder::TYPE_PSYCHE_UP_ALLY, InputBuilder::PREFIX_PSYCHE_UP_ALLY);
                continue;
            }
            if (isMatchStrWithTrim(argv[i], "y") || isMatchStrWithTrim(argv[i], "Y")) {
                builder.push(InputBuilder::TYPE_INACTIVE, InputBuilder::PREFIX_INACTIVE);
                continue;
            }
            if (isMatchStrWithTrim(argv[i], "r") || isMatchStrWithTrim(argv[i], "k")) {
                builder.push(InputBuilder::TYPE_WAR_CRY, InputBuilder::TYPE_WAR_CRY);
                continue;
            }
            auto [prefix, damage] = toABCint(argv[i]);
            if (prefix == 'a') {
                builder.push(InputBuilder::TYPE_ATTACK, InputBuilder::PREFIX_ATTACK); //攻撃フォローアップ
            }
            if (prefix == InputBuilder::PREFIX_MULTITHRUST) {
                builder.push(InputBuilder::TYPE_MULTITHRUST, InputBuilder::PREFIX_MULTITHRUST); //攻撃フォローアップ
            }
            if (prefix == 'h' || prefix == 'd' ||  prefix == 'D') {
                builder.push(InputBuilder::TYPE_PRE_SPECIAL_MEDICINE, InputBuilder::PREFIX_SPECIAL_MEDICINE); //攻撃フォローアップ
            }
            builder.push(damage, prefix);
        }
        return true;
    }

    /**
     * このexeのメインロジック。
     *
     * @param hours 時間を表す整数値
     * @param minutes 分を表す整数値
     * @param seconds 秒を表す整数値
     * @return 処理が成功した場合は0を返し、エラーが発生した場合は1を返します。
     */
    NOINLINE int ProgramMain(int hours, int minutes, int seconds) {
        // 構造体の組み合わせを作成
        try {
            auto results = builder.makeStructure();
            for (const auto &result: results) {
                result.print(); // 結果の出力

                // スタックを配列に変換
                int aActions[350] = {0};
                int damages[350] = {0};

                // Aactions をスタックにコピー
                for (int i = 0; i < result.AactionsCounter; ++i) {
                    aActions[i] = result.Aactions[i];
                }
                aActions[result.AactionsCounter] = -1;

                // AII_damage をスタックにコピー
                for (int i = 0; i < result.AII_damageCounter; ++i) {
                    damages[i] = result.AII_damage[i];
                }
                damages[result.AII_damageCounter] = -1;


                FoundSeed = 0;
                foundSeeds = 0;
                auto seed = BruteForceRequest(BasePlayers, hours, minutes, seconds, result.AactionsCounter, aActions,
                                              damages);
                if (foundSeeds == 1) {
                    auto test = SearchRequest(BasePlayers, seed, aActions, THREAD_COUNT, true);
                    if (!test) {
                        std::cout << "The first search request failed." << std::endl;
                        if (!SearchRequest(BasePlayers, seed, aActions, THREAD_COUNT, false)) {
                            std::cout << "The second search request failed" << std::endl;
                        }
                    }
                }
            }
        } catch (const std::runtime_error &e) {
            std::cerr << e.what() << std::endl;
            return 1;
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

        std::cout << "ver: " << version << ", atk: " << BasePlayers[0].atk << ", def: " << BasePlayers[0].def <<
                ", seed: ";
        std::cout << "0x" << std::hex << seed << std::dec << std::endl << "actions: ";

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
                "Performance: " << std::fixed << std::setprecision(2) << performance << " mann turns/s" << std::endl;
    }

    bool SearchRequest(const Player copiedPlayers[2], uint64_t seed, const int aActions[350], int numThreads, bool Dropbug) {
#ifdef DEBUG
        auto t0 = std::chrono::high_resolution_clock::now();
        BattleEmulator::ResetTurnProcessed();
#endif

        int32_t gene[350] = {0};
        auto turns = 0;
        for (int i = 0; i < 349; ++i) {
            gene[i] = aActions[i];
            if (aActions[i] == -1) {
                gene[i] = -1;
                gene[i + 1] = -1;
                break;
            }
            turns++;
        }
#if defined(BattleEmulatorLV13)
        auto [turnProcessed,genome] =
                ActionOptimizer::RunAlgorithmAsync(copiedPlayers, seed, turns, 3000, gene, numThreads, Dropbug);
#elif defined(BattleEmulatorLV19)
        auto [turnProcessed,genome] =
        ActionOptimizer::RunAlgorithmAsync(copiedPlayers, seed, turns, 1500, gene, numThreads, Dropbug);
#elif defined(erusionn_lv21)
        auto genome =
                ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 1500, gene, numThreads);
#endif

#ifdef DEBUG
        auto t3 = std::chrono::high_resolution_clock::now();
        auto elapsed_time1 =
                std::chrono::duration_cast<std::chrono::microseconds>(t3 - t0).count();
        PerformanceDebug("Searcher multi", BattleEmulator::getTurnProcessed(), static_cast<double>(elapsed_time1), 0);
#endif

        if (genome.turn >= 100) {
            return false;
        }

        BattleResult result1;
        result1 = BattleResult();
        Player players[2] = {copiedPlayers[0], copiedPlayers[1]};

        auto *position = new int(1);
        auto *nowState = new uint64_t(0);

        BattleEmulator::Main(position, 100, genome.actions, players, &result1, seed, nullptr, nullptr, -1,
                             nowState);

        delete position;
        delete nowState;

        std::cout << "foundTurn: " << foundTurn << ", " << turns << std::endl;
#ifdef MINGW_BUILD
        dumpTableMain(result1, genome, seed, foundTurn);
#else
        dumpTableMain(result1, genome, seed, foundTurn);
#endif

        return true;
    }

    /**
     * 指定された範囲のシードを使って総当たりを行い、一致するシード知を探索します。
     * 各シードごとにバトルシミュレーションを実行し、条件を満たす場合はシードを記録します。
     *
     * @param copiedPlayers バトルに使用するプレイヤーデータ（コピーされた配列）
     * @param start シード探索の開始位置
     * @param end シード探索の終了位置
     * @param gene バトルシミュレーションに使用する遺伝子データの配列
     * @param damages バトル中に発生するダメージを記録する配列
     */
    void BruteForceMainLoop(const Player copiedPlayers[2], uint64_t start, uint64_t end, int gene[350],
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


            bool resultBool = BattleEmulator::Main(position, 20, gene, players,
                                                  nullptr, seed, nullptr, damages,
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

    /**
     * 指定されたプレイヤーペアと時間パラメータ、行動データを使って、総当たりを実行しシード値を探索します。
     * 実行結果として、探索されたシード値を返却します。また、デバッグモードで実行時間およびパフォーマンスを記録します。
     * 複数のシード値が見つかった場合は、0を返します
     *
     * @param copiedPlayers プレイヤーのデータを格納した配列（2人分）
     * @param hours         現在の時間（時）
     * @param minutes       現在の時間（分）
     * @param seconds       現在の時間（秒）
     * @param turns         バトルのターン数
     * @param aActions      各ターンの行動データが格納された配列
     * @param damages       各ターンでのダメージデータが格納された配列
     * @return 見つかったシード値（見つからない場合は0を返す）
     */
    [[nodiscard]] uint64_t BruteForceRequest(const Player copiedPlayers[2], int hours, int minutes, int seconds,
                                             int turns,
                                             int aActions[350], int damages[350]) {
#ifdef DEBUG
        auto t0 = std::chrono::high_resolution_clock::now();
#endif

        std::cout << "BruteForceRequest executed with time " << hours << ":" << minutes << ":" << seconds << std::endl;
        // std::cout << "\naActions: ";
        // for (int i = 0; i < 350 && aActions[i] != -1; ++i) std::cout << aActions[i] << " ";
        // std::cout << "\ndamages: ";
        // for (int i = 0; i < 350 && damages[i] != -1; ++i) std::cout << damages[i] << " ";
        // std::cout << std::endl;
        BattleEmulator::ResetTurnProcessed();

        foundSeeds = 0;
        FoundSeed = 0;

        int totalSeconds = hours * 3600 + minutes * 60 + seconds;
        totalSeconds = totalSeconds - 15;
        auto time1 = static_cast<uint64_t>(floor((totalSeconds - 3.5) * (1 / 0.12515)));
        time1 = time1 << 16;

        auto time2 = static_cast<uint64_t>(floor((totalSeconds + 3.5) * (1 / 0.125155)));
        time2 = time2 << 16;
        int32_t gene[350] = {0};

        for (int i = 0; i < 350; ++i) {
            gene[i] = aActions[i];
            if (aActions[i] == -1) {
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

        BruteForceMainLoop(copiedPlayers, time1, time2, gene, damages);

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

    /**
     * 入力文字列を解析し、先頭のアルファベットが存在する場合はプレフィックスと数値部分を
     * ペアとして返します。文字列が純粋な数値の場合は、プレフィックスなしでその数値を返します。
     *
     * 入力文字列が空、長さ超過、または不正な形式の場合は適切な例外がスローされます。
     * 入力が整数として解析される場合には例外処理でエラーがログとして出力されます。
     *
     * @param str 入力文字列（最大長は4文字）
     * @return アルファベットのプレフィックスと整数部分のペア。プレフィックスが存在しない場合は
     *         '\0' が返されます。
     * @throws std::invalid_argument 引数が nullptr の場合や、不正な形式の文字列が含まれる場合にスローされます。
     * @throws std::length_error 入力文字列の長さが4文字を超える場合にスローされます。
     */
    NOINLINE std::pair<char, int> toABCint(const char *str) {
        if (str == nullptr) throw std::invalid_argument("Input is null");

        size_t len = std::strlen(str);

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


    /**
     * 文字列の先頭から空白文字を削除した新しい文字列を返します。
     * 空白文字として扱われるのは、スペース、タブ、改行、復帰、フォームフィード、および垂直タブです。
     *
     * @param s 対象の文字列
     * @return 先頭の空白文字を削除した新しい文字列
     */
    NOINLINE std::string ltrim(const std::string &s) {
        size_t start = s.find_first_not_of(" \t\n\r\f\v");
        return (start == std::string::npos) ? "" : s.substr(start);
    }

    /**
     * 文字列の末尾から空白文字を除去します。
     * 空白文字として認識されるのは、スペース、タブ、改行、復帰、改ページ、
     * 垂直タブなどのホワイトスペース文字です。
     *
     * @param s 処理対象の文字列
     * @return 空白が除去された新しい文字列
     */
    NOINLINE std::string rtrim(const std::string &s) {
        size_t end = s.find_last_not_of(" \t\n\r\f\v");
        return (end == std::string::npos) ? "" : s.substr(0, end + 1);
    }


    /**
     * 指定された文字列の先頭および末尾に存在する空白文字を削除します。
     * 入力文字列がnullの場合は空の文字列を返します。
     *
     * @param s トリム対象のC文字列
     * @return トリム後の文字列
     */
    NOINLINE std::string trim(const char *s) {
        if (s == nullptr) return "";
        std::string str(s);
        return trim(str);
    }

    /**
     * 文字列の両端から空白を削除し、トリムされた文字列を返します。
     * 入力文字列の先頭と末尾の空白文字（スペース、タブ、改行など）が取り除かれます。
     *
     * @param s トリム対象の文字列
     * @return トリムされた文字列
     */
    NOINLINE std::string trim(const std::string &s) {
        return rtrim(ltrim(s));
    }

    /**
     * 二つの文字列をトリム処理後に比較し、一致するかどうかを判定します。
     * トリム処理は各文字列の先頭と末尾の不要な空白を削除します。
     *
     * @param s1 比較対象の最初の文字列
     * @param s2 比較対象の二番目の文字列
     * @return トリム後の文字列が一致する場合にtrue、一致しない場合にfalseを返します
     */
    NOINLINE bool isMatchStrWithTrim(const char *s1, const char *s2) {
        return trim(s1) == trim(s2);
    }
}

#if defined(DEBUG4)
void testbattleemu() {
    int *position = new int(1);
    auto *nowState = new uint64_t(0);
    int maxElement = 350;
    auto start = 10000;
    auto end = 10000000;
    int gene[350] = {BattleEmulator::BUFF, -1};
    for (uint64_t seed = start; seed < end; ++seed) {
        BattleEmulator::resetStartTurn();
        lcg::init(seed);
        (*nowState) = 0;
        (*position) = 1;
        Player players[2] = {BasePlayers[0], BasePlayers[1]};
        BattleEmulator::Main(position, 1, gene, players,
                                               nullptr, seed, nullptr, nullptr,
                                               -2,
                                               nowState);
    }
    delete position;
    delete nowState;
}
#endif

int main(int argc, char *argv[]) {
    if (argc == 2 && isMatchStrWithTrim(argv[1], "h")) {
        showHeader();
        return 0;
    }

#if defined(OPTIMIZE_MODE)
    int actions1[350] = {};
    auto counter1 = 0;
    actions1[counter1++] = BattleEmulator::BUFF;
    actions1[counter1++] = BattleEmulator::PSYCHE_UP_ALLY;
    actions1[counter1++] = BattleEmulator::PSYCHE_UP_ALLY;
    actions1[counter1] = -1;
    SimpleParameterOptimizer::optimize(BasePlayers, 0x1060, actions1, 100000, counter1);
    return 0;
#endif

#ifdef DEBUG
    auto t0 = std::chrono::high_resolution_clock::now();
#endif


    //https://zenn.dev/reputeless/books/standard-cpp-for-competitive-programming/viewer/library-ios-iomanip#3.1-c-%E8%A8%80%E8%AA%9E%E3%81%AE%E5%85%A5%E5%87%BA%E5%8A%9B%E3%82%B9%E3%83%88%E3%83%AA%E3%83%BC%E3%83%A0%E3%81%A8%E3%81%AE%E5%90%8C%E6%9C%9F%E3%82%92%E7%84%A1%E5%8A%B9%E3%81%AB%E3%81%99%E3%82%8B
    //std::cin.tie(0)->sync_with_stdio(0);


#ifdef DEBUG4
    testbattleemu();
    return 0;
#endif

#ifdef DEBUG2
    //time1 = 0x199114b2;
    //time1 = 0x226d97a6;
    //time1 = 0x1c2a9bda;
    //time1 = 0x1aa6c05d;
    //3838815720
    //3839393442

    /*
    ver: v8.0.6_vG_v2, atk: 220, def: 155, seed: 0x1001
actions: 30, 25, 30, 62, 62, 50, 62, 62, 33, 30, 34,
        */


    //AI Warning: This is code related to debug2
    uint64_t time1 = 0x10001;

    int dummy[100];
    lcg::init(time1, false);
    int *position1 = new int(1);

    /*
    ver: v8.0.6_vG_v2, atk: 220, def: 155, seed: 0x1000
actions: 30, 30, 50, 62, 53, 62, 62, 62, 33, 34,
    */

    //ver: v5.0.6_vE_aa, atk: 82, def: 90, seed: 0x416d71f, actions: 25, 25, 50, 25, 25, 61, 50, 61, 61, 27, 27, 25, 50, 25, 61, 25, 56, 61, 25, 61, 50, 53, 25,

    //ver: v5.0.5_vD_aa, atk: 82, def: 90, seed: 0x416b4f2, actions: 25, 25, 25, 25, 50, 25, 25, 25, 25, 61, 50, 25, 56, 25, 25, 61, 27, 25, 61, 55, 25, 56, 27,
    //ver: v5.0.5_vC_aa, atk: 82, def: 90, seed: 0x427227d, actions: 25, 35, 35, 25, 59, 50, 25, 50, 50, 25, 56, 25, 25, 25, 25, 50, 61, 25, 61, 56, 61, 61, 58,
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

    //0x22e2dbaf:

    //AI Warning: This is code related to debug2
    // int32_t gene1[350] = {
    //     30, 25, 30, 62, 62, 50, 62, 62, 33, 30, 34,
    //     BattleEmulator::ATTACK_ALLY};
    //gene1[19-1] = BattleEmulator::DEFENCE;
    int counter = 0;
    int32_t gene1[350] = {0};
    gene1[counter++] = BattleEmulator::MULTITHRUST;
    gene1[counter++] = BattleEmulator::BUFF;
    gene1[counter++] = BattleEmulator::SPECIAL_MEDICINE;
    gene1[counter++] = BattleEmulator::PSYCHE_UP_ALLY;
    gene1[counter++] = BattleEmulator::PSYCHE_UP_ALLY;
    gene1[counter++] = BattleEmulator::PSYCHE_UP_ALLY;
    gene1[counter++] = BattleEmulator::PSYCHE_UP_ALLY;
    gene1[counter++] = BattleEmulator::SPECIAL_MEDICINE;
    gene1[counter++] = BattleEmulator::BUFF;
    gene1[counter++] = BattleEmulator::BUFF;
    gene1[counter++] = BattleEmulator::SPECIAL_MEDICINE;
    gene1[counter++] = BattleEmulator::PSYCHE_UP_ALLY;
    gene1[counter++] = BattleEmulator::DEFENCE;
    gene1[counter++] = BattleEmulator::DEFENCE;
    gene1[counter++] = BattleEmulator::DEFENCE;
    gene1[counter++] = BattleEmulator::DEFENCE;
    gene1[counter++] = BattleEmulator::DEFENCE;
    // gene1[counter++] = BattleEmulator::DEFENCE;
    // gene1[counter++] = BattleEmulator::DEFENCE;
    // gene1[counter++] = BattleEmulator::DEFENCE;
    // gene1[counter++] = BattleEmulator::DEFENCE;
    // gene1[counter++] = BattleEmulator::DEFENCE;
    // gene1[counter++] = BattleEmulator::DEFENCE;
    // gene1[counter++] = BattleEmulator::DEFENCE;
    // gene1[counter++] = BattleEmulator::DEFENCE;
    // gene1[counter++] = BattleEmulator::PSYCHE_UP_ALLY;
    // gene1[counter++] = BattleEmulator::MERCURIAL_THRUST;
    // gene1[counter++] = BattleEmulator::MERCURIAL_THRUST;

    (*NowState) = 0;
    (*position1) = 1;
    BattleResult dummy1;
    std::memcpy(players1, BasePlayers, sizeof(players1));
    BattleEmulator::Main(position1, (counter == 0 ? 1000 : counter), gene1, players1, &dummy1, time1, dummy, dummy, -1,
                         NowState);

    std::stringstream ss1;
    ss1 << time1 << " ";
    std::cout << (*position1) << std::endl;
    std::cout << dumpTable(dummy1, gene1, -1) << std::endl;


    //}
    delete position1;
    delete NowState;

    return 0;
#endif

#ifdef DEBUG3
    uint64_t seed = 0x0c421b38;

    int actions[350] = {
        BattleEmulator::BUFF,
        -1,
    };
    SearchRequest(BasePlayers, seed, actions, THREAD_COUNT, true);

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


    if (!ProcessInputBuilder(argc, argv)) {
        return 1;
    }
    auto exitCode = ProgramMain(hours, minutes, seconds);
    std::cout << performanceLogger.rdbuf();
#ifdef DEBUG
    auto t1 = std::chrono::high_resolution_clock::now();
    auto elapsed_time =
            std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
    std::cout << "elapsed time: " << double(elapsed_time) / 1000 << " ms" << std::endl;
#endif

    return exitCode;
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
    std::vector<ResultStructure> wasmResults;
    std::string wasmLastDump;
    std::string wasmLastError;
    uint64_t wasmLastTurnProcessed = 0;

    std::vector<std::string> splitTokens(const char *input) {
        std::vector<std::string> tokens;
        if (!input) {
            return tokens;
        }
        std::string current;
        for (const char *p = input; *p != '\0'; ++p) {
            if (std::isspace(static_cast<unsigned char>(*p))) {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
            } else {
                current.push_back(*p);
            }
        }
        if (!current.empty()) {
            tokens.push_back(current);
        }
        return tokens;
    }

    bool buildResultsFromInput(const char *input) {
        wasmLastError.clear();
        if (input == nullptr) {
            wasmLastError = "input is null";
            return false;
        }

        builder.clear();
        auto tokens = splitTokens(input);
        std::vector<std::string> argvStorage;
        argvStorage.reserve(tokens.size() + 4);
        argvStorage.push_back("wasm");
        argvStorage.push_back("0");
        argvStorage.push_back("0");
        argvStorage.push_back("0");
        for (const auto &token : tokens) {
            if (!token.empty()) {
                argvStorage.push_back(token);
            }
        }

        std::vector<char *> argv;
        argv.reserve(argvStorage.size());
        for (auto &arg : argvStorage) {
            argv.push_back(const_cast<char *>(arg.c_str()));
        }

        if (!ProcessInputBuilder(static_cast<int>(argv.size()), argv.data())) {
            wasmLastError = "input parse failed";
            return false;
        }

        try {
            wasmResults = builder.makeStructure();
        } catch (const std::exception &e) {
            wasmLastError = e.what();
            return false;
        }

        builder.clear();
        if (wasmResults.empty()) {
            wasmLastError = "no input combinations";
            return false;
        }
        return true;
    }

    void fillArraysFromResult(const ResultStructure &result, int aActions[350], int damages[350]) {
        for (int i = 0; i < 350; ++i) {
            aActions[i] = 0;
            damages[i] = 0;
        }
        for (int i = 0; i < result.AactionsCounter; ++i) {
            aActions[i] = result.Aactions[i];
        }
        aActions[result.AactionsCounter] = -1;
        for (int i = 0; i < result.AII_damageCounter; ++i) {
            damages[i] = result.AII_damage[i];
        }
        damages[result.AII_damageCounter] = -1;
    }

    std::string buildDumpOutput(const Player copiedPlayers[2], uint64_t seed, const ResultStructure &result,
                                int numThreads, bool dropbug) {
        int32_t gene[350] = {0};
        int turns = 0;
        for (int i = 0; i < 349; ++i) {
            if (i < result.AactionsCounter) {
                gene[i] = result.Aactions[i];
                turns++;
                continue;
            }
            gene[i] = -1;
            gene[i + 1] = -1;
            break;
        }
        if (result.AactionsCounter >= 349) {
            gene[349] = -1;
        }

        auto genome =
                ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 8000, gene, 0);

        if (genome.turn >= 100) {
            return "SearchRequest failed: turn limit reached.";
        }

        BattleResult result1;
        Player players[2] = {copiedPlayers[0], copiedPlayers[1]};

        int position = 1;
        uint64_t nowState = 0;

        BattleEmulator::Main(&position, 100, genome.actions, players, &result1, seed, nullptr, nullptr, -1,
                             &nowState);

        std::stringstream ss;
        ss << dumpTable(result1, genome.actions, foundTurn) << "\n";
        ss << "ver: " << version << ", atk: " << BasePlayers[0].atk << ", def: " << BasePlayers[0].def << ", seed: ";
        ss << "0x" << std::hex << seed << std::dec << "\n" << "actions: ";
        for (auto i = 0; i < 100; ++i) {
            if (genome.actions[i] == 0 || genome.actions[i] == -1) {
                break;
            }
            ss << genome.actions[i] << ", ";
        }
        ss << "\n";
        return ss.str();
    }
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int wasm_prepare_input(const char *input) {
    if (!buildResultsFromInput(input)) {
        return 0;
    }
    return static_cast<int>(wasmResults.size());
}

EMSCRIPTEN_KEEPALIVE const char *wasm_get_last_error() {
    return wasmLastError.c_str();
}

EMSCRIPTEN_KEEPALIVE uint64_t wasm_bruteforce_range(int resultIndex, uint64_t startSeed, uint64_t endSeed) {
    if (resultIndex < 0 || resultIndex >= static_cast<int>(wasmResults.size())) {
        wasmLastError = "invalid result index";
        return 0;
    }

    int aActions[350] = {0};
    int damages[350] = {0};
    const auto &result = wasmResults[static_cast<size_t>(resultIndex)];
    fillArraysFromResult(result, aActions, damages);

    BattleEmulator::ResetTurnProcessed();
    foundSeeds = 0;
    FoundSeed = 0;
    BruteForceMainLoop(BasePlayers, startSeed, endSeed, aActions, damages);
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
    if (resultIndex < 0 || resultIndex >= static_cast<int>(wasmResults.size())) {
        wasmLastError = "invalid result index";
        wasmLastDump.clear();
        return wasmLastDump.c_str();
    }

    wasmLastDump = buildDumpOutput(BasePlayers, seed, wasmResults[static_cast<size_t>(resultIndex)], numThreads,
                                   dropbug != 0);
    wasmLastTurnProcessed = BattleEmulator::getTurnProcessed();
    return wasmLastDump.c_str();
}
}
#endif
