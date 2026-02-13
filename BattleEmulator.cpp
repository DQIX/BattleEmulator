//
// Created by Owner on 2024/02/05.
//

#include <cstdint>
#include <iostream>
#include <cmath>
#include "BattleEmulator.h"

#include <array>

#include "lcg.h"
#include "Player.h"
#include "camera.h"
#include "debug.h"
#include "BattleResult.h"


thread_local int preHP[3] = {0, 0, 0};

/**
 * @brief 状態データから現在のコンボ情報をリセットします。
 *
 * この関数は64ビット整数で管理されている現在の状態データ (NowState) に対して、
 * コンボに関連するビット領域 (bit 40-52) をクリアします。
 *
 * @param NowState 現在の状態を保持する64ビット整数へのポインタ。
 */
void inline BattleEmulator::resetCombo(uint64_t *NowState) {
    (*NowState) &= ~(0xFFF00000000);
}

#if defined(BattleEmulatorLV19)
constexpr int kaisinnP = 500;
constexpr int baseHP = 103;
#elif defined(BattleEmulatorLV13)
constexpr int kaisinnP = 200;
constexpr int baseHP = 79;
#elif defined(BattleEmulatorLV15)
constexpr int kaisinnP = 500;
constexpr int baseHP = 89;
#endif
constexpr int DragonSlashKaisinnP = kaisinnP / 2;
constexpr int WooshSlashKaisinnP = 100;

/**
 * @brief 味方のhpを基に、hpテーブルをコンパイル時に生成します。
 *
 * この関数は、基準値 (baseHP) を使用して縮小された割合に基づき整数値のテーブルを計算し、
 * それを配列として返します。
 *
 * @return 生成された9要素の整数配列。
 */
constexpr std::array<int, 9> makeProportionTable3() {
    std::array<int, 9> table{};
    // iを9から1までループし、対応する値を生成する
    for (int i = 9; i >= 1; --i) {
        double multiplier = static_cast<double>(i) / 10.0;
        // base * multiplier の結果は正の数なので、static_cast<int>でfloor相当の効果が得られる
        int value = static_cast<int>(baseHP * multiplier);
        table[9 - i] = value + 1;
    }
    return table;
}

/**
 * @brief コンパイル時に生成されたHPの割合テーブル。
 *
 * 基準値 (baseHP) を使用して、割合に基づく9要素の整数値を含む配列。
 * この配列は makeProportionTable3 関数によって計算され、戦闘計算ロジックに利用されます。
 */
constexpr auto proportionTable3 = makeProportionTable3();

/**
 * @brief コンボ攻撃の処理を行い、ダメージ値を計算します。
 *
 * この関数は、現在の攻撃が直前の攻撃と連続しているかを判定し、コンボカウントに応じて
 * ダメージを増幅します。同時に内部状態を更新します。
 *
 * @param Id 攻撃を識別するための整数ID。
 * @param damage 基本となるダメージ値。
 * @param NowState 現在の状態を示す64ビット符号なし整数ポインタ。
 *
 * @return コンボの結果として増幅されたダメージ値。
 */
double BattleEmulator::processCombo(int32_t Id, double damage, uint64_t *NowState) {
    auto previousAttack = ((*NowState) >> 32) & 0xff;
    auto comboCounter = ((*NowState) >> 40) & 0xf;
    if (previousAttack != 0) {
        if (previousAttack == Id) {
            comboCounter++;
            switch (comboCounter) {
                case 2:
                    damage *= 1.2;
                    break;
                case 3:
                    damage *= 1.5;
                    break;
                default:
                    damage *= 2.0;
                    break;
            }
        } else {
            previousAttack = Id;
            comboCounter = 1;
        }
    } else {
        previousAttack = Id;
        comboCounter = 1;
    }
    resetCombo(NowState);
    (*NowState) |= (previousAttack << 32);
    (*NowState) |= (comboCounter << 40);
    return damage;
}


/**
 * @brief 指定されたアクションIDに対応するアクション名を取得します。
 *
 * この関数は、渡されたアクションIDに基づいて適切なアクション名を文字列として返します。
 * 対応するIDが存在しない場合は、"Unknown Action" が返されます。
 *
 * @param actionId アクションを識別するID。
 * @return アクションIDに対応するアクション名の文字列。
 */
const char *BattleEmulator::getActionName(int actionId) {
    switch (actionId) {
        case BattleEmulator::BUFF:
            return "Buff";
        case BattleEmulator::ATTACK_ENEMY:
            return "Attack";
        case BattleEmulator::MEDICINAL_HERBS:
            return "Medicinal Herbs";
        case BattleEmulator::PARALYSIS:
            return "Paralysis";
        case BattleEmulator::CURE_PARALYSIS:
            return "Cure Paralysis";
        case BattleEmulator::ULTRA_HIGH_SPEED_COMBO:
            return "High-Speed Combo";
        case BattleEmulator::SKY_ATTACK:
            return "Sky Attack";
        case BattleEmulator::CRITICAL_ATTACK:
            return "Critical Attack";
        case BattleEmulator::LAUGH:
            return "Laugh";
        case BattleEmulator::DISRUPTIVE_WAVE:
            return "Disruptive Wave";
        case BattleEmulator::BURNING_BREATH:
            return "Burning Breath";
        case BattleEmulator::DARK_BREATH:
            return "Dark Breath";
        case BattleEmulator::MORE_HEAL:
            return "More Heal(hoimu)";
        case BattleEmulator::MIDHEAL:
            return "Mid Heal(behoimi)";
        case BattleEmulator::FREEZING_BLIZZARD:
            return "freezing blizzard";
        case BattleEmulator::MERA_ZOMA:
            return "Mera Zoma";
        case BattleEmulator::DOUBLE_UP:
            return "Double up";
        case BattleEmulator::MULTITHRUST:
            return "Multithrust";

        case BattleEmulator::ATTACK_ALLY:
            return "Attack";
        case BattleEmulator::HEAL:
            return "Heal";
        case BattleEmulator::DEFENCE:
            return "Defence";

        case BattleEmulator::MAGIC_MIRROR:
            return "magic mirror";

        case BattleEmulator::LIGHTNING_STORM:
            return "Lightning Storm";
        case BattleEmulator::LULLAB_EYE:
            return "Lullab Eye";
        case BattleEmulator::SLEEPING:
            return "Sleeping";
        case BattleEmulator::CURE_SLEEPING:
            return "Cure Sleeping";

        case BattleEmulator::FULLHEAL:
            return "Full heal(behoma)";
        case BattleEmulator::DEFENDING_CHAMPION:
            return "Defense Champion"; //defending champion
        case BattleEmulator::PSYCHE_UP:
            return "Psyche up";
        case BattleEmulator::MEDITATION:
            return "Meditation";
        case BattleEmulator::MAGIC_BURST:
            return "magic Burst";
        case BattleEmulator::RESTORE_MP:
            return "Restore MP";
        case BattleEmulator::MERCURIAL_THRUST:
            return "Mercurial Thrust";
        case BattleEmulator::TURN_SKIPPED:
            return "**Turn Skipped**";

        case BattleEmulator::SAGE_ELIXIR:
            return "Sage Elixir";
        case BattleEmulator::ELFIN_ELIXIR:
            return "Elfin Elixir";
        case BattleEmulator::MAGIC_WATER:
            return "Magic Water";
        case BattleEmulator::GOSPEL_SONG:
            return "gospel song";
        case BattleEmulator::FLEE_ALLY:
            return "Flee";
        case BattleEmulator::POISON_ATTACK:
            return "Poison Attack";
        case BattleEmulator::KASAP:
            return "Kasap(rukanann)";
        case BattleEmulator::SPECIAL_MEDICINE:
            return "Special Medicine";
        case BattleEmulator::MIRACLE_SLASH:
            return "Miracle Slash";
        case BattleEmulator::SWEET_BREATH:
            return "Sweet Breath";
        case BattleEmulator::DECELERATLE:
            return "Deceleratle(bomi)";
        case BattleEmulator::SPECIAL_ANTIDOTE:
            return "!Special Antidote";
        case BattleEmulator::ACROBATIC_STAR:
            return "Acrobatic Star";
        case BattleEmulator::CRACKLE:
            return "Crackle";
        case WOOSH_ALLY:
            return "Woosh";
        case DRAGON_SLASH:
            return "Dragon Slash";
        case ITEM_USE:
            return "Item Use";
        case CRACK_ALLY:
            return "Crack";
        default:
            return "Unknown Action";
    }
}


thread_local int threadTurnProcessed = 0;
thread_local int startTurn = 0;

void BattleEmulator::resetStartTurn() {
    startTurn = 0;
}

int BattleEmulator::getStartTurn() {
    return startTurn;
}

void BattleEmulator::ResetTurnProcessed() {
    threadTurnProcessed = 0;
}

int BattleEmulator::getTurnProcessed() {
    return threadTurnProcessed;
}

inline void BattleEmulator::processTurn() {
    // ここでturnProcessedをインクリメントする処理を追加
    threadTurnProcessed++;
}



//#endif


/**
 * @brief バトルシミュレーションのメイン処理。
 *
 * この関数は、プレイヤーと敵の各ターン（forループ1回）ごとに行動を計算し、
 * バトル状態およびダメージ結果を更新します。
 *
 * 各パラメーターの詳細は以下の通りです:
 *
 * @param position
 *   lcg.cppで定義している線形合同法(LCG)の乱数生成に使用する現在の乱数の位置を表すポインタです。
 *   バトルエミュレーション側では直接インクリメントする場合もありますが、各種乱数取得関数に渡すと
 *   自動的にlcg.cpp内でインクリメントされ、乱数生成と連動した値が提供されます。
 *
 * @param RunCount
 *   この関数内のforループの回数として扱われ、1ターンはループ1回分のシミュレーションに相当します。
 *
 * @param Gene
 *   各ターンで味方が実行すべき行動を示す固定長の遺伝子テーブルです。
 *   配列のインデックスはターン数-1に対応しており、固定長(350要素)としてペクターを使わない理由は高速化のためです。
 *
 * @param players
 *   戦闘のhpなどのステータスを保持するプレイヤー（および敵）の配列です。
 *   配列の先頭要素(index=0)は味方、index=1は敵を表し、各ターンで行動・HP更新等が行われます。
 *   これは参照渡しであるため、関数実行後に更新されます。
 *
 * @param result
 *   バトル結果の蓄積先として使用するstd::optional型の参照です。
 *   modeが -1 の場合にBattleResultへ結果を蓄積しますが、空のoptionalを渡すとセグメンテーション違反が発生するため注意が必要です。
 *   また、resultは生成コストがそれなりにかかるため、使用しないシナリオではなるべく避けるよう推奨します。
 *
 * @param seed
 *   ゲームバトルの初期乱数シードで、主にデバッグ時にデバッカーから参照するために使用されています。
 *
 * @param eActions
 *   敵の行動パターンを示す配列ですが、ブランチによっては使用される可能性もあるため、名残的に定義されています。
 *
 * @param damages
 *   mainでchar*[]から適切にパースされ、内部でint[]に変換された中間表現のダメージ配列です。
 *   この配列の各要素により、各ターンの必須の行動が指定されます。
 *   modeが0以上の場合、要求された中間表現と一致しなかった場合には即座にシミュレーションを終了します。
 *
 * @param mode
 *   シミュレーション実行モードを指定する整数値です。詳細は以下の通りです:
 *   - mode >= 0 : 総当たりシミュレーションモード（damagesが使用される）。
 *   - mode == -1: バトル結果がBattleResultに蓄積される記録モード。
 *   - mode == -2: 結果は記録せず、最速実行のみのモード。
 *
 * @param NowState
 *   内部シミュレーション状態をビット単位で管理する64ビット整数へのポインタです。
 *   この状態は内部でのみ管理され、外部から変更されることは想定していません。
 *   各ビットの用途は以下の通りです:
 *      - 0～3   : Current Rotation Table (4bit)
 *      - 4～7   : Rotation Internal State (4bit)
 *      - 8～11  : Free Camera State (4bit)
 *      - 12～31 : Turn Count Processed (20bit)
 *      - 32～39 : Combo Previous Attack Id (2byte)
 *      - 40～47 : Combo Counter (1byte)
 *   合計で6バイト分の情報を管理しています。main.cpp内の説明とも合わせてご参照ください。
 *
 * @return
 *   modeが0以上の場合、実行中の（damagesに沿った）シミュレーションが要求に一致している場合はtrue、
 *   それ以外の場合はfalseを返します。（modeが -1 または -2の場合、常にfalse）
 */
bool BattleEmulator::Main(int *position, int RunCount, const int32_t Gene[350], Player *players,
                          BattleResult* result,
                          uint64_t seed, const int eActions[350], const int damages[350], int mode,
                          uint64_t *NowState, bool logicalTurnStart) {
    resetCombo(NowState);
    bool player0_has_initiative = false;
    int genePosition = 0;
    int exCounter = 0;
    int exCounter1 = 0;
    uint64_t tmpState = -1;
    int defenseFlag = false;

    auto startPos = static_cast<int>(((*NowState) >> 12) & 0xfffff);
    if (startPos != 0) {
        startPos++;
        RunCount += startPos;
    } else {
        startPos = 1;
        RunCount++;
    }


    for (int counterJ = startPos; counterJ < RunCount; ++counterJ) {
        processTurn();
        if (!logicalTurnStart && genePosition != -1) {
            genePosition = counterJ - 1;
        }
        //現在ターンを保存
        (*NowState) &= ~0xFFFFF000;
        (*NowState) |= (counterJ << 12);

        if (players[0].dirtySpecialCharge) {
            players[0].specialCharge = false;
            players[0].dirtySpecialCharge = false;
        }
        players[0].specialChargeTurn--;
        if (players[0].specialChargeTurn == -1) {
            players[0].specialCharge = false;
        }

        resetCombo(NowState);

        tmpState = (*NowState);

#ifdef DEBUG2
        std::cout << "c: " << counterJ << ", " << (*position) << std::endl;
        if ((*position) == 514) {
            std::cout << "!!" << std::endl;
        }
#endif
        int ehp = players[1].hp;
        int ahp = players[0].hp;

        players[0].defence = 1.0;

        int32_t actions[3] = {0, 0, 0};
        int actionsPosition = 0;
        double speed0 = players[0].speed * lcg::floatRand051_1(position);
        double speed1 = players[1].speed * lcg::floatRand051_1(position);

        // 素早さを比較
        if (speed0 > speed1) {
            player0_has_initiative = true;
        } else {
            player0_has_initiative = false;
        }

        auto counter = 0;
        int enemyAction[2] = {0, 0};
        while (counter != 2) {
            int preAction = 0;
            // const int table[6] = {
            //     ATTACK_ENEMY, POISON_ATTACK, ATTACK_ENEMY, DECELERATLE, KASAP,
            //     SWEET_BREATH
            // };
            enemyAction[counter] = ProcessEnemyRandomAction44(position);


            do {
                auto ActionChanged = false;
                if (counter == 1 && enemyAction[0] == enemyAction[1]) {
                    if (enemyAction[1] == POISON_ATTACK) {
                        enemyAction[1] = ATTACK_ENEMY;
                    } else if (enemyAction[1] == KASAP) {
                        enemyAction[1] = DECELERATLE;
                    } else if (enemyAction[1] == DECELERATLE) {
                        enemyAction[1] = ATTACK_ENEMY;
                    } else if (enemyAction[1] == SWEET_BREATH) {
                        enemyAction[1] = KASAP;
                    }
                }

                if (enemyAction[counter] == SWEET_BREATH && players[0].sleeping) {
                    enemyAction[counter] = KASAP;
                    ActionChanged = true;
                }

                if (enemyAction[counter] == KASAP && players[0].BuffLevel == -2) {
                    enemyAction[counter] = DECELERATLE;
                    ActionChanged = true;
                }
                if (enemyAction[counter] == DECELERATLE && players[0].speedLevel == -2) {
                    enemyAction[counter] = ATTACK_ENEMY;
                    ActionChanged = true;
                }
                if (ActionChanged) {
                    continue;
                }
                break;
            } while (true);


            preAction = enemyAction[counter];

            if (preAction == ATTACK_ENEMY) {
                (*position)++; //0x02156874
                (*position) += 2; //0x0216139c && 0x021613b0
            }
            if (preAction == POISON_ATTACK) {
                (*position)++; //0x02156874
            }
            // if (mode != -1 && mode != -2) {
            //     int need = eActions[exCounter1++];
            //     if (need == -1) {
            //         return true;
            //     }
            //     if (need != enemyAction[counter]) {
            //         return false;
            //     }
            // }
            if (counter == 0) {
                (*position)++; //0x02160d64
            }
            counter++;
        }

        int32_t actionTable = -1;

        if (Gene[genePosition] == 0 || Gene[genePosition] == -1) {
            genePosition = -1;
            //throw std::invalid_argument("GenePosition is invalid");
        }
        if (genePosition != -1 && Gene[genePosition] != 0 && Gene[genePosition] != -1) {
            actionTable = Gene[genePosition];
            if (actionTable == TURN_SKIPPED || actionTable == SLEEPING || actionTable == CURE_SLEEPING || actionTable ==
                CURE_PARALYSIS || actionTable == PARALYSIS) {
                actionTable = ATTACK_ALLY;
            }
        } else {
            actionTable = ATTACK_ALLY;
        }

        if (logicalTurnStart && genePosition != -1) {
            genePosition++;
        }

        if (actionTable == DEFENCE) {
            players[0].defence = 0.5;
            defenseFlag = true;
        } else {
            defenseFlag = false;
        }

        //途中で解除してもいいように2回チェックする
        if (players[0].sleeping) {
            actionTable = SLEEPING;
            players[0].defence = 1.0;
            defenseFlag = false;
        }


        // ソートされた結果を出力
        for (int t = 0; t < 2; ++t) {
            if (!Player::isPlayerAlive(players[1])) {
                break;
            }
            if (!Player::isPlayerAlive(players[0])) {
                break;
            }

            int basedamage = 0;
            if ((t == 0 && !player0_has_initiative) || (t == 1 && player0_has_initiative)) {
                for (const int c: enemyAction) {
                    //--------start_FUN_02158dfc-------
                    (*position)++;
                    //--------end_FUN_02158dfc-------
                    basedamage = callAttackFun(c, position, players, 1, 0, NowState);
                    actions[actionsPosition++] = c;

                    if (players[0].sleeping) {
                        actionTable = SLEEPING;
                        players[0].defence = 1.0;
                    }

                    if (mode == -1) {
                        auto def1 = -1;
                        if (players[0].BuffTurns > 0) {
                            def1 = players[0].BuffTurns;
                        } else if (players[0].BuffLevel != 0) {
                            def1 = 0;
                        }

                        auto poi = -1;
                        if (players[0].PoisonTurn > 0) {
                            poi = players[0].PoisonTurn;
                        } else if (players[0].PoisonEnable == true) {
                            poi = 0;
                        }

                        auto agl = -1;
                        if (players[0].speedTurn > 0) {
                            agl = players[0].speedTurn;
                        } else if (players[0].speedLevel != 0) {
                            agl = 0;
                        } else {
                            agl = -1;
                        }

                        BattleResult::add(result, c, basedamage, true,
                                          def1, poi, agl, counterJ - 1,
                                          player0_has_initiative, ehp,
                                          ahp, tmpState, players[0].specialChargeTurn, players[0].mp, defenseFlag, players[0].sleeping);
                    } else if (mode != -1 && mode != -2) {
                        if (
                            c == ATTACK_ENEMY ||
                            c == POISON_ATTACK ||
                            c == KASAP ||
                            c == DECELERATLE ||
                            c == SWEET_BREATH
                        ) {
                            if (damages[exCounter] == -1) {
                                startTurn = counterJ;
                                return true;
                            }

                            if (damages[exCounter] == -3) {
                                if (c != KASAP) {
                                    return false;
                                }
                                exCounter++;
                            } else if (damages[exCounter] == -2) {
                                if (c != DECELERATLE) {
                                    return false;
                                }
                                exCounter++;
                            } else if (damages[exCounter] == -4) {
                                if (c != SWEET_BREATH) {
                                    return false;
                                }
                                exCounter++;
                            } else if (damages[exCounter++] != basedamage) {
                                return false;
                            }
                            if (damages[exCounter] == -1) {
                                startTurn = counterJ;
                                return true;
                            }
                        }
                    }

                    Player::reduceHp(players[0], basedamage);

                    //--------start_FUN_021594bc-------
                    if (Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])) {
                        (*position) += 1;
                    } else {
                        break;
                    }
                    //--------end_FUN_021594bc-------
                }
            } else {
                int32_t action = actionTable & 0xffff;
                auto skipTurn = false;
                if (action == SLEEPING && !player0_has_initiative && !players[0].sleeping) {
                    skipTurn = true;
                }
                if (action == BattleEmulator::FLEE_ALLY) {
                    skipTurn = true;
                }
                if (!skipTurn) {
                    //--------start_FUN_02158dfc-------
                    if (!players[0].paralysis && !players[0].sleeping) {
                        (*position) += 1;
                    } else if (players[0].sleeping) {
                        action = SLEEPING;
                        players[0].defence = 1.0;

                        players[0].sleepingTurn--;
                        if (players[0].sleepingTurn <= 0) {
                            const int sleepTable[4] = {37, 62, 87, 100};
                            auto probability1 = sleepTable[std::abs(players[0].sleepingTurn)];
                            auto probability2 = lcg::getPercent(position, 100);
                            if (probability1 >= probability2) {
                                players[0].sleeping = false;
                                players[0].sleepingTurn = 0;
                                action = CURE_SLEEPING;
                            }
                        } else {
                            (*position)++;
                        }
                    }


                    //--------end_FUN_02158dfc-------
                    basedamage = callAttackFun(action, position, players, 0, 1, NowState);
                    actions[actionsPosition++] = action;

                    if (mode == -1) {
                        auto def1 = -1;
                        if (players[0].BuffTurns > 0) {
                            def1 = players[0].BuffTurns;
                        } else if (players[0].BuffLevel != 0) {
                            def1 = 0;
                        }

                        auto poi = -1;
                        if (players[0].PoisonTurn > 0) {
                            poi = players[0].PoisonTurn;
                        } else if (players[0].PoisonEnable == true) {
                            poi = 0;
                        }

                        auto agl = -1;
                        if (players[0].speedTurn > 0) {
                            agl = players[0].speedTurn;
                        } else if (players[0].speedLevel != 0) {
                            agl = 0;
                        } else {
                            agl = -1;
                        }

                        BattleResult::add(result, action, basedamage, false,
                                          def1, poi, agl, counterJ - 1,
                                          player0_has_initiative, ehp, ahp,
                                          tmpState, players[0].specialChargeTurn, players[0].mp, defenseFlag, players[0].sleeping);
                    }
                    if (action == HEAL || action == MORE_HEAL || action == MIDHEAL ||
                        action == FULLHEAL || action == SPECIAL_MEDICINE || action == GOSPEL_SONG || action ==
                        SPECIAL_ANTIDOTE) {
                        Player::heal(players[0], basedamage);
                        if (action == SPECIAL_MEDICINE || action == SPECIAL_ANTIDOTE) {
                            if (mode != -1 && mode != -2) {
                                if (damages[exCounter] == -5) {
                                    exCounter++;
                                }
                                if (damages[exCounter] == -1) {
                                    startTurn = counterJ;
                                    return true;
                                }
                            }
                        }
                    } else {
                        Player::reduceHp(players[1], basedamage);

                        if (mode != -1 && mode != -2) {
                            if (action == ATTACK_ALLY || action == MIRACLE_SLASH) {
                                if (damages[exCounter] == -1) {
                                    startTurn = counterJ;
                                    return true;
                                }
                                //int need = ;
                                if (damages[exCounter++] != basedamage) {
                                    return false;
                                }
                                if (damages[exCounter] == -1) {
                                    startTurn = counterJ;
                                    return true;
                                }
                            }
                        }
                    }
                    //--------start_FUN_021594bc-------
                    if (Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])) {
                        (*position) += 1;
                        //TODO: 順序調べる


                        players[0].BuffTurns--;
                        if (players[0].BuffLevel != 0 && players[0].BuffTurns <= 0) {
                            //0x0215a8a8 DEF
                            const int probability[4] = {62, 75, 87, 100};
                            auto probability1 = probability[std::abs(players[0].BuffTurns)];
                            auto probability2 = lcg::getPercent(position, 100);
                            if (probability1 >= (probability2 + (probability1 == 75 ? 1 : 0))) {
                                players[0].BuffLevel = 0;
                                RecalculateBuff(players);
                            }
                        }

                        players[0].speedTurn--;
                        if (players[0].speedLevel != 0 && players[0].speedTurn <= 0) {
                            //0x0215a94c speed
                            const int probability[4] = {62, 75, 87, 100};
                            auto probability1 = probability[std::abs(players[0].speedTurn)];
                            auto probability2 = lcg::getPercent(position, 100);
                            if (probability1 >= (probability2 + (probability1 == 75 ? 1 : 0))) {
                                players[0].speedLevel = 0;
                                RecalculateBuff(players);
                            }
                        }

                        players[0].acrobaticStarTurn--;

                        if (players[0].acrobaticStar && players[0].acrobaticStarTurn == 0) {
                            players[0].acrobaticStar = false;
                            (*position)++; //0x0215aed4 チートで100にすると次のターンから判定が行われず永遠にアクロバットスター状態になる。
                            //3f800000
                            //3f600000
                            //3f200000
                            //3ec00000
                        }
                    }
                } else {
                    if (mode == -1) {
                        auto def1 = -1;
                        if (players[0].BuffTurns > 0) {
                            def1 = players[0].BuffTurns;
                        } else if (players[0].BuffLevel != 0) {
                            def1 = 0;
                        }

                        auto poi = -1;
                        if (players[0].PoisonTurn > 0) {
                            poi = players[0].PoisonTurn;
                        } else if (players[0].PoisonEnable == true) {
                            poi = 0;
                        }

                        auto agl = -1;
                        if (players[0].speedTurn > 0) {
                            agl = players[0].speedTurn;
                        } else if (players[0].speedLevel != 0) {
                            agl = 0;
                        } else {
                            agl = -1;
                        }

                        BattleResult::add(result, action, 0, false,
                                          def1, poi, agl, counterJ - 1,
                                          player0_has_initiative, ehp, ahp,
                                          tmpState, players[0].specialChargeTurn, players[0].mp, defenseFlag, players[0].sleeping);
                    }
                }
            }
            //--------end_FUN_021594bc-------
        }

        // 0x0215bd64 毒のダメージ処理
        if (players[0].PoisonTurn != -1) {
            players[0].PoisonTurn++;
        }
        if (players[0].PoisonEnable == true) {
            Player::reduceHp(players[0], (players[0].maxHp) >> 4); // 16分の1
        }

        if (Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])) {
            (*position) += 1;
        }
        camera::Main(position, actions, NowState);

#ifdef DEBUG2
        //DEBUG_COUT2((*position));
        if ((*position) == 514) {
            //std::cout << "!!" << std::endl;
        }
#endif

        if (!Player::isPlayerAlive(players[1])) {
            return false;
        }
        if (!Player::isPlayerAlive(players[0])) {
            return false;
        }
    }
    if (mode != -1 && mode != -2) {
        return true;
    } else {
        return false;
    }
}

double BattleEmulator::FUN_021dbc04(int baseHp, double maxHp) {
    auto hp = static_cast<double>(baseHp);
    if (hp == 0) {
        return 0;
    }
    return hp / maxHp;
}

//コンパイラが毎回コピーするコードを生成するからグローバルスコープに追い出しとく
const int proportionTable2[9] = {90, 90, 64, 32, 16, 8, 4, 2, 1}; //最後の項目を調べるのは手動　P:\lua\isilyudaru\hissatuteki.lua
// double proportionTable1[9] = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 3.0, 0.2, 0.1};// 21/70が2.99999...になるから最初から20/70より大きい2.89にしちゃう
const double Enemy_TensionTable[4] = {1.3, 2.0, 3.0, 4.5}; //一部の敵は特殊テンションテーブルを倍率として使う

/*
<?php
$base = 103;
$results = [];

for ($i = 9; $i >= 1; $i--) {
    $multiplier = $i / 10;
    $result = floor($base * $multiplier) + 1;
    $results[] = $result;
}

echo implode(", ", $results);
?>
*/


int BattleEmulator::callAttackFun(int32_t Id, int *position, Player *players, int attacker, int defender,
                                  uint64_t *NowState) {
    for (int j = 0; j < 2; ++j) {
        preHP[j] = players[j].hp;
    }
    int baseDamage = 0;
    double tmp = 0;
    bool kaisinn = false;
    bool hasKaisinn = false;
    bool kaihi = false;
    bool tate = false;
    int OffensivePower = players[attacker].defaultATK;
    int percent_tmp;
    auto totalDamage = 0;
    auto attackCount = 0;
    bool defenseFlag = false; //防御した場合0x021e81a0のほうが優先度高いらしい。なんで
    switch (Id & 0xffff) {
        case CRACK_ALLY:
            players[attacker].mp -= 3;
            (*position) += 2;
            (*position)++; //関係ない 0x021ec6f8
            if (lcg::getPercent(position, 0x2710) < 100) {
                kaisinn = true;
            }
            (*position)++; //盾 0x021586fc
            (*position)++; //偽回避 0x02157f58
            baseDamage = FUN_021e8458_typeD(position, 5, 30);
            if (kaisinn) {
                tmp = baseDamage * lcg::floatRand(position, 1.5, 2.0);
                baseDamage = static_cast<int>(tmp);
            }
            ProcessRage(position, baseDamage, players, kaisinn); // 適当
            if (kaisinn) {
                if (!players[1].rage) {
                    (*position)++; //会心時特殊処理　0x021e54fc
                    (*position)++; //会心時特殊処理　0x021eb8c8
                } else {
                    (*position)++; //会心時特殊処理　既に怒り狂ってる場合は1消費になる
                }
            }
            (*position)++; //0x021e54fc
            if (!players[0].specialCharge) {
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
            break;
        case WOOSH_ALLY:
            players[attacker].mp -= 3;
            (*position) += 2;
            if (lcg::getPercent(position, 0x2710) < WooshSlashKaisinnP) {
                kaisinn = true;
            }
            (*position)++; //関係ない 0x021ec6f8
            (*position)++; //偽回避 0x02157f58
            baseDamage = FUN_021e8458_typeD(position, 8, 16);
            if (kaisinn) {
                tmp = baseDamage * lcg::floatRand(position, 1.5, 2.0);
                baseDamage = static_cast<int>(tmp);
            }
            tmp = static_cast<double>(baseDamage) * 0.75;
            baseDamage = static_cast<int>(tmp);
            ProcessRage(position, baseDamage, players, kaisinn); // 適当
            if (kaisinn) {
                if (!players[1].rage) {
                    (*position)++; //会心時特殊処理　0x021e54fc
                    (*position)++; //会心時特殊処理　0x021eb8c8
                } else {
                    (*position)++; //会心時特殊処理　既に怒り狂ってる場合は1消費になる
                }
            }
            (*position)++; //0x021e54fc
            if (!players[0].specialCharge) {
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
            break;
        case CRACKLE:
            players[attacker].mp -= 8;
            (*position) += 2;
            if (lcg::getPercent(position, 0x2710) < 100) {
                kaisinn = true;
            }
            (*position)++; //関係ない 0x021ec6f8
            (*position)++; //盾ガード 0x021586fc
            (*position)++; //偽回避 0x02157f58
            baseDamage = FUN_021e8458_typeD(position, 8, 50);
            if (kaisinn) {
                tmp = baseDamage * lcg::floatRand(position, 1.5, 2.0);
                baseDamage = static_cast<int>(tmp);
            }
            ProcessRage(position, baseDamage, players, kaisinn); // 適当
            if (kaisinn) {
                if (!players[1].rage) {
                    (*position)++; //会心時特殊処理　0x021e54fc
                    (*position)++; //会心時特殊処理　0x021eb8c8
                } else {
                    (*position)++; //会心時特殊処理　既に怒り狂ってる場合は1消費になる
                }
            }
            (*position)++; //0x021e54fc
            if (!players[0].specialCharge) {
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
            break;
        case DECELERATLE:
        case KASAP:
            (*position) += 2;
            (*position)++; //会心 0x02158584
            (*position)++; //関係ない 0x021ec6f8
            if (lcg::getPercent(position, 100) < 75) {
                //0x02157f58 回避
                baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
                if (baseDamage == 0) {
                    baseDamage = lcg::getPercent(position, 2); // 0x021e81a0
                }

                if (baseDamage != 0) {
                    //TODO 0ダメージのときの消費を調べる
                    (*position)++; //0x021e54fc
                }
                if ((Id & 0xffff) == DECELERATLE) {
                    players[defender].speedLevel--;
                    players[defender].speedTurn = 7;
                } else if ((Id & 0xffff) == KASAP) {
                    players[defender].BuffLevel--;
                    players[defender].BuffTurns = 7;
                }
                RecalculateBuff(players);
            }
            process7A8(position, 0, players, defender);
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case SWEET_BREATH:
            (*position) += 2;
            (*position)++; //会心 0x02158584
        //(*position)++; //関係ない 0x021ec6f8
            if (players[0].acrobaticStar) {
                //アクロバットスターで、逃げた時だけ特殊な挙動をする
                /*
                --------start_FUN_02158dfc-------
                0x02159b10 0x00000064 734
                --------end_FUN_02158dfc-------
                --------start_FUN_021ebd9c_ct-------
                randIntRange: 0x0216139c 3 4 735
                randIntRange: 0x021613b0 6 8 736
                0x02158584 0x00002710 737
                kaisin: 10000/0
                0x021ec6f8 0x00000064 738
                0x02157f58 0x00000064 739
                kaihi 25
                float: 0x02075724 0xbfe40000 0x3fe40000 740
                float: 0x02075738 0xbf800000 0x3f800000 741
                0x021ed7a8 0x00000064 742
                --------end_FUN_021ebd9c_ct-------
                --------start_FUN_021594bc-------
                0x02159d40 0x00000064 743
                --------end_FUN_021594bc-------
                 */
                percent_tmp = lcg::getPercent(position, 100); //0x02157f58
                if (percent_tmp >= 0 && percent_tmp <= 49) {
                    percent_tmp = lcg::getPercent(position, 100);
                    if (percent_tmp < 25) {
                        baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
                        if (baseDamage == 0) {
                            baseDamage = lcg::getPercent(position, 2); // 0x021e81a0
                        }
                    }

                    if (!players[defender].specialCharge) {
                        (*position)++; //0x021ed7a8
                    }
                    return 0;
                }
            } else {
                (*position)++;
            }
            if (!players[0].paralysis && !players[0].sleeping) {
                //TODO 眠ってるときのやけつくいき
                if (!players[defender].acrobaticStar && lcg::getPercent(position, 100) < 2) {
                    //0x021587b0 回避
                    kaihi = true;
                }
            }
            percent_tmp = lcg::getPercent(position, 100);
            if (percent_tmp < 25 && !kaihi) {
                players[defender].sleeping = true;
                players[defender].sleepingTurn = 2;
                baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
                if (baseDamage == 0) {
                    baseDamage = lcg::getPercent(position, 2); // 0x021e81a0
                }

                if (baseDamage != 0) {
                    //TODO 0ダメージのときの消費を調べる
                    (*position)++; //0x021e54fc
                }
            }
            process7A8(position, 0, players, defender);
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case SPECIAL_MEDICINE:
        case SPECIAL_ANTIDOTE:
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //0x02158584 会心
            (*position)++; //0x02157f58 ニセ回避
            baseDamage = FUN_021e8458_typeC(position, 105, 105, 15);
            (*position)++; //0x021e54fc
            if (!players[0].specialCharge) {
                (*position)++; //0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
            if ((Id & 0xffff) == SPECIAL_ANTIDOTE) {
                players[attacker].PoisonEnable = false;
                players[attacker].PoisonTurn = -1;
                players[attacker].SpecialAntidoteCount--;
            } else {
                players[attacker].SpecialMedicineCount--;
            }
            resetCombo(NowState);
            break;
        case MAGIC_WATER:
            players[attacker].MagicWaterCount--;
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //0x02158584 会心
            (*position)++; //0x02157f58 ニセ回避
            baseDamage = FUN_021e8458_typeC(position, 33, 33, 3);
            (*position)++; //不明 0x021e54fc
            if (!players[attacker].specialCharge) {
                (*position)++; //0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
            players[attacker].mp += baseDamage;
            players[attacker].mp = std::min(players[attacker].mp, players[attacker].maxMp);
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::SLEEPING:
        case BattleEmulator::CURE_SLEEPING:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            (*position)++; //不明
            if (!players[attacker].specialCharge && !players[attacker].sleeping && !players[attacker].paralysis) {
                (*position)++; //必殺チャージ(敵)
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021ed7a8
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::DEFENCE:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            if (!players[attacker].specialCharge && !players[attacker].sleeping && !players[attacker].paralysis) {
                (*position)++; //必殺チャージ(敵) 0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::COUNTER:
            (*position)++; //0x02158584 会心(無効)
            if (lcg::getPercent(position, 0x2710) < kaisinnP) {
                kaisinn = true;
            }
            (*position)++; //みかわし(無効) xxx0x021587b0
        // if (!kaihi) {
            (*position)++; //盾ガード
        // }
            (*position)++; //回避
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (kaisinn) {
                tmp = players[attacker].atk * lcg::floatRand(position, 0.95, 1.05);
                baseDamage = static_cast<int>(tmp);
            }
        //if (!kaihi) {
            ProcessRage(position, baseDamage, players, kaisinn);
        //}
            Player::reduceHp(players[defender], baseDamage);

            baseDamage = 0;
            break;
        case BattleEmulator::ACROBATSTAR_KAIHI:
            //(*position) += 2;
            //(*position)++;//アクロバットスター判定
            (*position)++; //0x2710
        // if (lcg::getPercent(position, 0x2710) < 200) {
        //     kaisinn = true;
        // }
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (!players[defender].specialCharge) {
                (*position)++; //0x021ed7a8
            }
            baseDamage = 0;
            break;
        case BattleEmulator::ACROBATIC_STAR:
            players[0].acrobaticStar = true;
            players[0].acrobaticStarTurn = 6;
            players[0].specialCharge = false;
            players[0].specialChargeTurn = 0;

            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].defaultATK, players[defender].def);
            (*position)++; //不明
            break;
        case BattleEmulator::ATTACK_ENEMY:
        case BattleEmulator::SKY_ATTACK:
        case BattleEmulator::POISON_ATTACK:
            (*position) += 2;
            if (players[0].acrobaticStar) {
                //0x021ec6f8 アクロバットスター
                percent_tmp = lcg::getPercent(position, 100);
                if (percent_tmp >= 0 && percent_tmp <= 49) {
                    return callAttackFun(ACROBATSTAR_KAIHI, position, players, attacker, defender, NowState);
                }
                if (percent_tmp >= 50 && percent_tmp < 75) {
                    return callAttackFun(COUNTER, position, players, defender, attacker, NowState);
                }
            } else {
                (*position)++;
            }


            (*position)++; //会心
            if (!players[0].paralysis && !players[0].sleeping) {
                if (!players[defender].acrobaticStar && lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
                if (!kaihi && lcg::getPercent(position, 100) < 0.5000) {
                    tate = true;
                }
            }
            (*position)++; //回避

            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (tate || kaihi) {
                baseDamage = 0;
            } else {
                if ((Id & 0xffff) == POISON_ATTACK) {
                    if (lcg::getPercent(position, 100) < 12) {
                        //0x021e3978 毒
                        players[defender].PoisonEnable = true;
                        players[defender].PoisonTurn = 0;
                    }
                }

                tmp = static_cast<double>(baseDamage);
                if ((Id & 0xffff) == BattleEmulator::SKY_ATTACK) {
                    tmp = floor(tmp * 1.5);
                }

                //テンションがある場合、この時点でオフセットが計算されて、最低4ダメージが保証されて下の0x021e81a0でダメージがある判定になる。
                //1*(1+(30/10))で4ダメージが保証されるけど、事前に計算して定数にしとく。
                if (players[attacker].TensionLevel != 0) {
                    //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                    tmp *= Enemy_TensionTable[players[attacker].TensionLevel - 1];
                    tmp += (players[attacker].TensionLevel * 4); //4 = 1*(1+(30/10))
                    players[attacker].TensionLevel = 0;
                }

                baseDamage = static_cast<int>(tmp);


                //防御が適応される時期を調べる
                if (baseDamage == 0) {
                    // && players[0].defence != 0.1
                    baseDamage = lcg::getPercent(position, 2); //TODO: 0x021e81a0
                    if (baseDamage == 1) {
                        defenseFlag = true;
                    }
                }

                if (baseDamage != 0) {
                    tmp = static_cast<double>(baseDamage);
                    tmp = processCombo(Id & 0xffff, tmp, NowState);
                    baseDamage = static_cast<int>(tmp);
                }

                if (!defenseFlag && !players[0].paralysis && !players[0].sleeping) {
                    tmp = baseDamage * players[defender].defence;
                    baseDamage = static_cast<int>(tmp);
                }


                if (baseDamage != 0) {
                    (*position)++; //目を覚ました
                    (*position)++; //不明
                }


                if (baseDamage != 0 && players[0].sleeping) {
                    players[0].sleeping = false;
                    players[0].sleepingTurn = -1;
                }
            }
            process7A8(position, baseDamage, players, defender);
            players[attacker].TensionLevel = 0;

            break;
        case BattleEmulator::HEAL:
            players[attacker].mp -= 2;
            (*position) += 2;
            (*position)++; //関係ない
            if (lcg::getPercent(position, 0x2710) < 100) {
                kaisinn = true;
            }
            (*position)++; //回避
            baseDamage = FUN_021e8458_typeD(position, 5, 35);
            if (kaisinn) {
                tmp = baseDamage * lcg::floatRand(position, 1.5, 2.0);
                baseDamage = static_cast<int>(tmp);
            }
            (*position)++; //不明
            if (!players[attacker].specialCharge) {
                (*position)++; //関係ない
            }
        //0x021eb8c8, randIntRange: 0x021eb8f0 怒り狂っている場合←の消費が発生しない。
            if (!players[1].rage) {
                (*position)++;
            }
            (*position)++; //?
            if (kaisinn) {
                if (!players[1].rage) {
                    (*position)++; //会心時特殊処理　0x021e54fc
                    (*position)++; //会心時特殊処理　0x021eb8c8
                } else {
                    (*position)++; //会心時特殊処理　既に怒り狂ってる場合は1消費になる
                }
            }
            if (!players[0].paralysis && !players[0].sleeping) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
            resetCombo(NowState);
            break;
        case BattleEmulator::ATTACK_ALLY:
        case BattleEmulator::MERCURIAL_THRUST:
        case BattleEmulator::MIRACLE_SLASH:
        case DRAGON_SLASH:
        case ITEM_USE:
            (*position) += 2;
            (*position)++;
        //会心
            percent_tmp = lcg::getPercent(position, 0x2710);
            if (
                ((Id & 0xffff) == BattleEmulator::ATTACK_ALLY && percent_tmp < kaisinnP) ||
                ((Id & 0xffff) == BattleEmulator::DRAGON_SLASH && percent_tmp < DragonSlashKaisinnP)
            ) {
                kaisinn = true;
            }

        //みかわし(相手)
            if ((Id & 0xffff) != ITEM_USE && !players[0].paralysis) {
                // if (lcg::getPercent(position, 100) < -1) {
                //     kaihi = true;
                // }
                (*position)++; //0x021587b0 みかわし
                //if (!kaihi) {
                (*position)++; //盾ガード(幼女は盾を持っていないので0%)
                //}
            }

            (*position)++; //回避
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

            if ((Id & 0xffff) == ITEM_USE) {
                baseDamage = 0;
            }

            tmp = static_cast<double>(baseDamage);

            if (kaisinn) {
                //0x020759ec
                if ((Id & 0xffff) == BattleEmulator::MERCURIAL_THRUST || (Id & 0xffff) == BattleEmulator::MIRACLE_SLASH || (Id & 0xffff) == DRAGON_SLASH) {
                    tmp *= lcg::floatRand(position, 1.5, 2.0);
                } else {
                    tmp = OffensivePower * lcg::floatRand(position, 0.95, 1.05);
                }
            }

        //ミラクルのやつ
            if ((Id % 0xffff) == BattleEmulator::MIRACLE_SLASH) {
                players[attacker].mp -= 4;
                tmp *= 1.25;
            }

            baseDamage = static_cast<int>(tmp);

            ProcessRage(position, baseDamage, players, kaisinn); //不定消費は謎
            (*position)++; //目を覚ました
            (*position)++; //不明
            if (kaisinn) {
                if (!players[1].rage) {
                    (*position)++; //会心時特殊処理　0x021e54fc
                    (*position)++; //会心時特殊処理　0x021eb8c8
                } else {
                    (*position)++; //会心時特殊処理　既に怒り狂ってる場合は1消費になる
                }
            }
            if (players[defender].hp - baseDamage >= 0) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
        //ミラクルのやつ
            if ((Id % 0xffff) == BattleEmulator::MIRACLE_SLASH && baseDamage != 0) {
                tmp = static_cast<double>(baseDamage);
                tmp *= 0.25;
                Player::heal(players[attacker], static_cast<int>(tmp));
            }
            resetCombo(NowState);
            break;
        default:
            std::cout << "error!!!!! " << (Id & 0xffff) << std::endl;
    }
    return baseDamage;
}

//パーセントは絶対に100%にならないから誤差-1
int BattleEmulator::FUN_021e8458_typeC(int *position, double min, double max, double base) {
    //0x02075724
    auto result = lcg::floatRand(position, min, max);
    result += lcg::floatRand(position, -base, base);
    return static_cast<int>(result);
}

//パーセントは絶対に100%にならないから誤差-1
int BattleEmulator::FUN_021e8458_typeD(int *position, double difference, double base) {
    //0x021e8668
    auto result = lcg::floatRand(position, -difference, difference);
    result += base;
    return static_cast<int>(result);
}


int BattleEmulator::FUN_0207564c(int *position, int atk, int def) {
    [[assume(atk >= 0)]];
    [[assume(def >= 0)]];
    double result;
    double atk1;
    {
        double tmpAtk = atk * 0.5;
        double tmpDef = def * 0.25;
        atk1 = tmpAtk - tmpDef;
    }
    if (atk1 <= 0) [[unlikely]] {
        return 0;
    }
    auto atk2 = atk * 0.0625;
    if (atk1 > atk2) [[likely]] {
        auto atk4 = atk1 * 0.0625;
        result = atk1 + lcg::floatRand(position, -atk4, atk4);
        result = result + lcg::floatRandAttack(position);
    } else {
        result = lcg::floatRand(position, 0.0, atk2);
    }
    if (result <= 0) [[unlikely]] {
        return 0;
    }
    return static_cast<int>((result));
    //return 0;
}


void BattleEmulator::process7A8(int *position, int baseDamage, Player players[2], int defender) {
    if (players[defender].paralysis || players[defender].sleeping || players[defender].specialCharge
        || players[defender].hp <= baseDamage) {
        return;
    }
    if (baseDamage == 0) {
        (*position)++;
        return;
    }
    auto percent_tmp = lcg::getPercent(position, 100);
    for (int i = 0; i < 9; ++i) {
        if (baseDamage >= proportionTable3[i]) {
            if (percent_tmp < proportionTable2[i]) {
                players[defender].specialCharge = true;
                players[defender].specialChargeTurn = 6;
            }
            break;
        }
    }
}

int BattleEmulator::ProcessEnemyRandomAction2A(int *position) {
    //0x0208aca8
    const int patternTable[6] = {43, 42, 43, 43, 42, 43};
    //125
    //43+42+43+43
    int rand = lcg::getPercent(position, 0x100) + 1;

    //std::cout << "actions rand: " << rand << std::endl;

    for (int i = 0; i < 6; ++i) {
        if (rand <= patternTable[i]) {
            return i;
        }
        rand = rand - patternTable[i];
    }
    return 5;
}


#include <array>
#include <cstddef>

constexpr std::size_t TABLE_MAX = 256;
template<std::size_t N>
constexpr std::array<int, TABLE_MAX>
makeProbabilityTable(const std::array<int, N>& ratios,
                     const std::array<int, N>& ids)
{
    std::array<int, TABLE_MAX> table{};

    std::size_t index = 0;

    for (std::size_t i = 0; i < N; ++i)
    {
        for (int j = 0; j < ratios[i]; ++j)
        {
            table[index++] = ids[i];  // ← ここが重要
        }
    }

    return table;
}


template<std::size_t N>
constexpr int sum(const std::array<int, N>& arr)
{
    int s = 0;
    for (int v : arr) s += v;
    return s;
}

constexpr std::array<int, 6> ratios = {
    68,
    58,
    48,
    38,
    27,
    17  // 239 + 17 = 256
};

constexpr std::array<int, 6> ids = {
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::POISON_ATTACK,
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::DECELERATLE,
    BattleEmulator::KASAP,
    BattleEmulator::SWEET_BREATH
};

static_assert(sum(ratios) == TABLE_MAX, "Ratio sum must be 256");

constexpr auto actionTable = makeProbabilityTable(ratios, ids);


int BattleEmulator::ProcessEnemyRandomAction44(int *position) {
    //0x0208aca8
    int rnd = lcg::getPercent(position, 0x100);
    return actionTable[rnd];
}

int BattleEmulator::CalculateMoreHealBase(Player *players) {
    //ベホイミ
    double tmp1 = (players[0].HealPower - 200) * 0.5194;
    auto tmp2 = static_cast<int>(floor(tmp1));
    return 185 + tmp2;
}

int BattleEmulator::CalculateMidHealBase(Player *players) {
    //ｂ
    double tmp1 = (players[0].HealPower - 100) * 0.2392;
    auto tmp2 = static_cast<int>(floor(tmp1));
    return 85 + tmp2;
}

void BattleEmulator::RecalculateBuff(Player *players) {
    // 定数の倍率を格納した配列
    //const double ATKMultipliers[] = {0.5, 0.75, 1.0, 1.25, 1.5};
    const double DEFMultipliers[] = {0.25, 0.5, 1.0, 1.5, 2.0};
    const double SpeedMultipliers[] = {0.25, 0.5, 1.0, 1.5, 2.0};

    // BuffLevel に +2 して配列インデックスに変換
    int index = players[0].BuffLevel + 2;
    int index1 = players[0].speedLevel + 2;

    // インデックスが範囲外でないかチェック（-2 <= BuffLevel <= 2 の範囲であることを確認）
    if (index >= 0 && index < 5) {
        players[0].def = static_cast<int>(players[0].defaultDEF * DEFMultipliers[index]);
    }

    if (index1 >= 0 && index1 < 5) {
        players[0].speed = static_cast<int>(players[0].defaultSpeed * SpeedMultipliers[index1]);
    }

    //
    // int index1 = players[0].AtkBuffLevel + 2;
    // if (index >= 0 && index < 5) {
    //     players[0].atk = static_cast<int>(floor(players[0].defaultATK * ATKMultipliers[index1]));
    // }
}

void BattleEmulator::ProcessRage(int *position, int baseDamage, Player *players, bool kaisinn) {
    // if (kaisinn) {
    //     return;
    // }

    int hp_before = players[1].hp;
    int hp_after  = players[1].hp - baseDamage;
    int maxHp     = players[1].maxHp;

    if (hp_after < 0) {
        hp_after = 0;
    }

    //    if (percent1 < 0.5) {
    //        if (percent >= 0.5) {
    if (hp_after * 2 < maxHp) {
        if (hp_before * 2 >= maxHp) {
            if (!players[1].rage) {
                (*position)++;
                (*position)++;
            } else {
                (*position)++;
            }
        } else {
            // if (percent1 < 0.25) {
            //     if (percent >= 0.25) {
            if (hp_after * 4 < maxHp){
                if (hp_before * 4 >= maxHp){
                    if (!players[1].rage) {
                        (*position)++;
                        (*position)++;
                    } else {
                        (*position)++;
                    }
                }
            }
        }
    }
}

int BattleEmulator::ProcessMagicBurst(int *position) {
    auto rand1 = lcg::floatRand(position, 0.9, 1.0);
    auto rand2 = lcg::floatRand(position, 0.9, 1.1);
    auto damage = 30 * 6 * rand1;
    auto guaranteed = 160 * rand2;
    if (damage > guaranteed) {
        return static_cast<int>(floor(damage));
    } else {
        return static_cast<int>(floor(guaranteed));
    }
}
