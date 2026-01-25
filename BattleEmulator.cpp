//
// Created by Owner on 2024/02/05.
//

#include <cstdint>
#include <vector>
#include <iostream>
#include <cmath>
#include <array>
#include "BattleEmulator.h"

#include <cassert>

#include "lcg.h"
#include "Player.h"
#include "camera.h"
#include "debug.h"
#include "BattleResult.h"
#include "setting.h"

constexpr int baseHP = setting::Ally_MAX_HP;
constexpr double mitoreP = -0.0330;//見惚れ率
constexpr double kaisinnP = 200;//会心率
constexpr double ShieldGuardP = 0.5;//シールドガード率
constexpr double DragonSlashKaisinnP = kaisinnP / 2;
constexpr int WooshSlashKaisinnP = 100;

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
        case DRAGON_SLASH:
            return "Dragon Slash";
        case ITEM_USE:
            return "Item Use";
        case CRACK_ALLY:
            return "Crack";
        case INACTIVE_ENEMY:
        case INACTIVE_ALLY:
            return "Inactive";
        case BattleEmulator::TIDAL_WAVE:
            return "Tidal Wave";
        case BattleEmulator::MASSIVE_SWIPE:
            return "Massive Swipe";
        case BattleEmulator::VICTIMISER:
            return "Victimiser";
        case BattleEmulator::PUFF_PUFF:
            return "Puff Puff";
        case BattleEmulator::CRACK_ENEMY:
            return "Crack";
        case BattleEmulator::MANAZASHI:
            return "Manazashi";
        case BattleEmulator::HP_HOOVER:
            return "HP Hoover";
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
    bool player0_has_initiative = false;
    int genePosition = 0;
    int exCounter = 0;
    int exCounter1 = 0;
    uint64_t tmpState = -1;

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

        players[0].specialChargeTurn--;
        if (players[0].specialChargeTurn == -1) {
            players[0].specialCharge = false;
        }

        tmpState = (*NowState);

#ifdef DEBUG2
        std::cout << "c: " << counterJ << ", " << (*position) << std::endl;
        if ((*position) == 735) {
            std::cout << "!!" << std::endl;
        }
#endif
        int ehp = players[1].hp;
        int ahp = players[0].hp;

        players[0].defence = 1.0;

        int32_t actions[3] = {0, 0, 0};
        int actionsPosition = 0;

        double speed0 = players[0].speed * lcg::floatRand(position, 0.51, 1.0);
        double speed1 = players[1].speed * lcg::floatRand(position, 0.51, 1.0);
        // 素早さを比較
        if (speed0 > speed1) {
            player0_has_initiative = true;
        } else {
            player0_has_initiative = false;
        }
        (*position)++;

        int32_t actionTable = -1;

        if (genePosition == -1 || Gene[genePosition] == 0 || Gene[genePosition] == -1) {
            genePosition = -1;
            //throw std::invalid_argument("GenePosition is invalid");
        }
        if (genePosition != -1 && Gene[genePosition] != 0 && Gene[genePosition] != -1 && Gene[genePosition] != -10) {
            actionTable = Gene[genePosition] & 0x3ff;
            if (actionTable == HEAL && players[0].mp <= 0) {
                if (players[0].medicinal_herbs_count >= 1) {
                    actionTable = MEDICINAL_HERBS;
                } else {
                    actionTable = ATTACK_ALLY;
                }
            }
            //麻痺ったときに不正な防御しないように
            if (actionTable == INACTIVE_ALLY || actionTable == PARALYSIS) {
                actionTable = ATTACK_ALLY;
            }
        } else {
            //assert(false);
            if (players[0].hp >= 35) {
                actionTable = ATTACK_ALLY;
            } else {
                if (players[0].mp >= 2) {
                    actionTable = HEAL;
                } else if (players[0].medicinal_herbs_count >= 1) {
                    actionTable = MEDICINAL_HERBS;
                } else {
                    actionTable = ATTACK_ALLY;
                }
            }
            if (players[0].hp >= 30) {
                if (!players[0].acrobaticStar && players[0].specialCharge && players[0].specialChargeTurn >= 0) {
                    actionTable = ACROBATIC_STAR;
                }
            }
        }

        if (genePosition != -1 && logicalTurnStart) {
            genePosition++;
        }

        if (actionTable == DEFENCE && players[0].paralysis) {
            actionTable = ATTACK_ALLY;
        }

        if (actionTable == DEFENCE) {
            players[0].defence = 0.5;
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
                auto c = 0;
                //--------start_FUN_02158dfc-------

                if (lcg::getPercent(position, 100) < mitoreP) {
                    //0x021588ec
                    //次の乱数が90%以上(一致含む)なら見惚れないらしい。
                    int mitore = lcg::getPercent(position, 100); //0x02158964
                    if (mitore < 90) {
                        c = INACTIVE_ENEMY;
                    }
                }
                if (c != INACTIVE_ENEMY) {
                    //int table[6] = {VICTIMISER, HP_HOOVER, CRACK_ENEMY, ATTACK_ENEMY, MANAZASHI, PUFF_PUFF};
                    c = ProcessEnemyRandomAction2B(position);
                    //制限行動ぱふぱふ
                    if (c == PUFF_PUFF && players[0].inactive) {
                        c = MANAZASHI;
                    }
                    if (c == MANAZASHI) {
                        //randIntRange: 0x0216139c 3 4 90
                        //randIntRange: 0x021613b0 6 8 91
                        (*position)++;
                        (*position)++;
                    } else {
                        if (!players[0].rage) {
                            //攻撃先決定の乱数消費がなくなる
                            (*position)++;
                        }
                    }
                }
                if (c == VICTIMISER && !players[0].paralysis) {
                    c = HP_HOOVER;
                }

                (*position)++;
                //--------end_FUN_02158dfc-------
                basedamage = callAttackFun(c, position, players, 1, 0, NowState);
                actions[actionsPosition++] = c;

                if (mode != -1 && mode != -2) {
                    if (damages[exCounter] == -1) {
                        startTurn = counterJ;
                        return true;
                    }
                    if (basedamage == 0 && damages[exCounter] == 0) {
                        exCounter++;
                    } else if (basedamage != 0 && damages[exCounter++] != basedamage) {
                        return false;
                    }
                    if (damages[exCounter] == -1) {
                        startTurn = counterJ;
                        return true;
                    }
                    if (mode <= exCounter) {
                        startTurn = counterJ;
                        return true;
                    }
                } else if (mode == -1) {
                    auto p = players[0].paralysisTurns;
                    if (players[0].paralysis == false) {
                        p = -1;
                    } else if (p <= 0) {
                        p = 0;
                    }
                    BattleResult::add(result, c, basedamage, true,
                                      counterJ - 1,
                                      player0_has_initiative, ehp,
                                      ahp, tmpState, players[0].specialChargeTurn, players[0].mp, p,
                                      players[0].inactive);
                }

                Player::reduceHp(players[0], basedamage);

                //--------start_FUN_021594bc-------
                if (Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])) {
                    (*position) += 1;
                } else {
                    break;
                }

                if (players[0].rage) {
                    players[0].rageTurns--;
                    if (players[0].rageTurns <= 0) {
                        players[0].rage = false;
                    }
                }
                //--------end_FUN_021594bc-------
            } else {
                int32_t action = actionTable & 0xffff;
                auto skipTurn = false;
                if (action == BattleEmulator::FLEE_ALLY) {
                    skipTurn = true;
                }
                if (!skipTurn) {
                    //--------start_FUN_02158dfc-------
                    if (!players[0].paralysis) {
                        (*position) += 1;
                    } else {
                        action = PARALYSIS;
                        //if (players[0].paralysisTurns != 0) {
                        players[0].paralysisTurns--;
                        //}
                        if (players[0].paralysisTurns <= 0) {
                            double paralysisTable[4] = {0.6250, 0.7500, 0.8750, 1.0000};
                            auto probability1 = paralysisTable[std::abs(players[0].paralysisTurns)];
                            auto probability2 = lcg::getPercent(position, 100) * 0.01;
                            if (probability1 >= probability2) {
                                // 0.5 < 0.65
                                players[0].paralysis = false;
                                players[0].paralysisLevel = 0;
                                action = CURE_PARALYSIS;
                            }
                        } else {
                            (*position) += 1;
                        }
                    }

                    if (players[0].inactive) {
                        players[0].inactive = false;
                        if (action != CURE_PARALYSIS && action != PARALYSIS) {
                            action = INACTIVE_ALLY;
                        }
                    }


                    //--------end_FUN_02158dfc-------
                    basedamage = callAttackFun(action, position, players, 0, 1, NowState);
                    actions[actionsPosition++] = action;

                    if (mode == -1) {
                        auto p = players[0].paralysisTurns;
                        if (players[0].paralysis == false) {
                            p = -1;
                        } else if (p <= 0) {
                            p = 0;
                        }
                        BattleResult::add(result, action, basedamage, false, counterJ - 1,
                                          player0_has_initiative, ehp, ahp,
                                          tmpState, players[0].specialChargeTurn, players[0].mp, p,
                                          players[0].inactive);
                    }
                    if (action == HEAL || action == MEDICINAL_HERBS || action == MORE_HEAL || action == MIDHEAL ||
                        action == FULLHEAL || action == SPECIAL_MEDICINE || action == GOSPEL_SONG || action ==
                        SPECIAL_ANTIDOTE) {
                        if (action == HEAL) {
                            if (mode != -1 && mode != -2) {
                                if (damages[exCounter] == -1) {
                                    startTurn = counterJ;
                                    return true;
                                }
                                if (damages[exCounter] == -2) {
                                    ++exCounter; // -2マーカーをスキップ
                                    if (damages[exCounter] != -2) {
                                        // マーカー後の値がベース回復量でない場合、キャラクターの欠損HPと一致するかチェック
                                        if (damages[exCounter] != basedamage) {
                                            if (damages[exCounter] != (players[0].maxHp - players[0].hp)) {
                                                return false;
                                            }
                                        }
                                    }
                                    ++exCounter; // 値の検査が終わったのでインクリメント
                                } else {
                                    return false;
                                }

                                if (damages[exCounter] == -1) {
                                    startTurn = counterJ;
                                    return true;
                                }
                            }
                        }
                        Player::heal(players[0], basedamage);
                    } else {
                        Player::reduceHp(players[1], basedamage);
                        if (mode != -1 && mode != -2) {
                            if (damages[exCounter] == -1) {
                                startTurn = counterJ;
                                return true;
                            }
                            if (action != PARALYSIS && action != CURE_PARALYSIS && action != INACTIVE_ALLY) {
                                if (damages[exCounter++] != -3) {
                                    return false;
                                }
                                if (damages[exCounter++] != basedamage) {
                                    return false;
                                }
                                if (damages[exCounter] == -1) {
                                    startTurn = counterJ;
                                    return true;
                                }
                            }else if (action == INACTIVE_ALLY) {
                                if (damages[exCounter] == -4) {
                                    exCounter++;
                                }
                            }
                        }
                    }
                    //--------start_FUN_021594bc-------
                    if (Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])) {
                        (*position) += 1;
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
                        auto p = players[0].paralysisTurns;
                        if (players[0].paralysis == false) {
                            p = -1;
                        } else if (p <= 0) {
                            p = 0;
                        }
                        BattleResult::add(result, action, 0, false, counterJ - 1,
                                          player0_has_initiative, ehp, ahp,
                                          tmpState, players[0].specialChargeTurn, players[0].mp, p,
                                          players[0].inactive);
                    }
                }
            }
        }
        //--------end_FUN_021594bc-------

        if (Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])) {
            (*position) += 1;
        }
        camera::Main(position, actions, NowState, player0_has_initiative, false);

#ifdef DEBUG2
        //DEBUG_COUT2((*position));
        if ((*position) == 735) {
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

constexpr auto proportionTable3 = makeProportionTable3();

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
    int baseDamage = 0;
    double tmp = 0;
    bool kaisinn = false;
    bool kaihi = false;
    bool tate = false;
    int baseDamage_tmp = -1;
    int OffensivePower = players[attacker].atk;
    double percent1 = 0.0;
    int percent_tmp;
    switch (Id & 0xffff) {
        case BattleEmulator::MEDICINAL_HERBS:
            players[0].medicinal_herbs_count--;
            (*position) += 2;
            (*position)++; // 関係ない
            (*position)++; // 会心判定
            (*position)++; // 回避
            baseDamage = FUN_021e8458_typeC(position, 35.0, 35.0, 5.0);
            (*position)++; // 不明
            if (!players[attacker].specialCharge) {
                (*position)++; // 必殺チャージ(敵)　0%
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021ed7a8
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
            break;
        case BattleEmulator::ACROBATSTAR_KAIHI:
            //(*position) += 2;
            //(*position)++;//アクロバットスター判定
            if (lcg::getPercent(position, 0x2710) < kaisinnP) {
                kaisinn = true;
            }
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (!players[defender].specialCharge) {
                (*position)++; //0x021ed7a8
            }
            return 0;
            break;
        case BattleEmulator::COUNTER:
            //(*position) += 2;
            //(*position)++;//アクロバットスター判定
            (*position)++; //0x02158584 会心(無効)
            if (lcg::getPercent(position, 0x2710) < kaisinnP) {
                kaisinn = true;
            }
            if (lcg::getPercent(position, 100) < 2) {
                kaihi = true;
            }
            if (!kaihi) {
                (*position)++; //盾ガード
            }
            (*position)++; //回避
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (kaisinn) {
                tmp = players[attacker].atk * lcg::floatRand(position, 0.95, 1.05);
                baseDamage = static_cast<int>((tmp));
            }
            if (!kaihi) {
                ProcessRage(position, baseDamage, players);
                Player::reduceHp(players[defender], baseDamage);
            }
            return 0;
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
            FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            (*position)++; //不明
            break;
        case BattleEmulator::DEFENCE:
        case BattleEmulator::CURE_PARALYSIS:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            (*position)++; //不明
            if (!players[attacker].specialCharge) {
                (*position)++; //必殺チャージ(敵)
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021ed7a8
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
            break;
        case BattleEmulator::VICTIMISER:
            (*position) += 2;
            (*position)++; //アクロバットスターは絶対に発動しない
            (*position)++; //会心
            if (!players[0].paralysis && !players[0].inactive) {
                //アクロバットスターが外れた場合でもみかわしの処理は行わない
                if (!players[0].acrobaticStar && lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
                if (!kaihi && lcg::getPercent(position, 100) < 0.5) {
                    tate = true;
                }
            }
            (*position)++; //回避

            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
            baseDamage *= 1.5;
            if (kaihi || tate) {
                baseDamage = 0;
            } else {
                (*position)++; //目を覚ました
                (*position)++; //不明
            }
            tmp = baseDamage * players[defender].defence;
            baseDamage = static_cast<int>((tmp));
            process7A8(position, baseDamage, players, defender);
            break;
        case BattleEmulator::ATTACK_ENEMY:
            (*position) += 2;
            if (players[0].acrobaticStar && !players[0].paralysis && !players[0].inactive) {
                percent_tmp = lcg::getPercent(position, 100);
                if (percent_tmp >= 0 && percent_tmp <= 49) {
                    return callAttackFun(ACROBATSTAR_KAIHI, position, players, attacker, defender, NowState);
                } else if (percent_tmp >= 50 && percent_tmp < 75) {
                    return callAttackFun(COUNTER, position, players, defender, attacker, NowState);
                }
            } else {
                (*position)++;
            }

            (*position)++; //会心
            if (!players[0].paralysis && !players[0].inactive) {
                if (!players[0].acrobaticStar && lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
                if (!kaihi && lcg::getPercent(position, 100) < 0.5) {
                    tate = true;
                }
            }
            (*position)++; //回避

            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

            if (kaihi || tate) {
                baseDamage = 0;
            } else {
                (*position)++; //目を覚ました
                (*position)++; //不明
            }
            tmp = baseDamage * players[defender].defence;
            baseDamage = static_cast<int>((tmp));
            process7A8(position, baseDamage, players, defender);
            break;
        case BattleEmulator::INACTIVE_ALLY:
        case BattleEmulator::PARALYSIS:
        case BattleEmulator::INACTIVE_ENEMY:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            (*position)++; //不明
            break;
        case BattleEmulator::PUFF_PUFF:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            if (lcg::getPercent(position, 100) < 50) {
                //なぜか逆だけど回避50%
                players[defender].inactive = true;
            }
            if (!players[defender].inactive && players[defender].paralysis) {
                return 0;
            }
            if (players[defender].inactive) {
                FUN_0207564c(position, players[attacker].atk, players[defender].def);
                (*position)++; //不明
            } else {
                if (!players[defender].specialCharge) {
                    (*position)++; //必殺チャージ
                }
            }
            break;
        case BattleEmulator::HEAL:
            (*position) += 2;
            (*position)++; //関係ない
            if (lcg::getPercent(position, 0x2710) < WooshSlashKaisinnP) {
                kaisinn = true;
            }
            (*position)++; //回避
            baseDamage = FUN_021e8458_typeD(position, 5, 35);
            if (kaisinn) {
                tmp = baseDamage * lcg::floatRand(position, 1.5, 2.0);
                baseDamage = static_cast<int>((tmp));
            }
            (*position)++; //不明
            if (!players[attacker].specialCharge) {
                (*position)++; //関係ない
            }
        //0x021eb8c8, randIntRange: 0x021eb8f0 怒り狂っている場合←の消費が発生しない。
            if (!players[0].rage) {
                (*position)++;
            }
            (*position)++; //?
            if (kaisinn) {
                if (!players[0].rage) {
                    (*position)++; //会心時特殊処理　0x021e54fc
                    (*position)++; //会心時特殊処理　0x021eb8c8
                } else {
                    (*position)++; //会心時特殊処理　既に怒り狂ってる場合は1消費になる
                }
            }
            if (!players[0].paralysis && !players[0].inactive) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 6;
                }
            }
            players[attacker].mp -= 2;
            break;
        case BattleEmulator::MANAZASHI:
            (*position) += 2;
            (*position)++; //会心
            (*position)++; //関係ない
            (*position)++; //回避
            baseDamage = FUN_021e8458_typeD(position, 4, 12);
            if (lcg::getPercent(position, 100) < 25) {
                // 麻痺判定
                if (players[defender].paralysisLevel == 3) {
                    //std::cerr << "paralysisLevel == 2" << std::endl;
                }
                players[defender].paralysis = true;
                players[defender].paralysisTurns = 4;
                players[defender].paralysisLevel++;
            }
            (*position)++; //不明
            tmp = baseDamage * players[defender].defence;
            baseDamage = static_cast<int>((tmp));
            process7A8(position, baseDamage, players, defender);
            break;
        case BattleEmulator::CRACK_ENEMY:
            (*position) += 2;
            (*position)++;
            (*position)++; //会心
            if ((!players[0].paralysis && !players[0].inactive) && lcg::getPercent(position, 100) < 0.5) {
                tate = true;
            }
            (*position)++; //回避
            baseDamage = FUN_021e8458_typeD(position, 5, 17);
            if (tate) {
                baseDamage = 0;
            } else {
                (*position)++;
            }
            tmp = baseDamage * players[defender].defence;
            baseDamage = static_cast<int>((tmp));
            process7A8(position, baseDamage, players, defender);
            break;
        case BattleEmulator::HP_HOOVER: //バンパイアエッジ
            (*position) += 2;
            if (players[0].acrobaticStar && !players[0].paralysis && !players[0].inactive) {
                percent_tmp = lcg::getPercent(position, 100);
                if (percent_tmp >= 0 && percent_tmp <= 49) {
                    return callAttackFun(ACROBATSTAR_KAIHI, position, players, attacker, defender, NowState);
                } else if (percent_tmp >= 50 && percent_tmp < 75) {
                    return callAttackFun(COUNTER, position, players, defender, attacker, NowState);
                }
            } else {
                (*position)++;
            }
            (*position)++; //会心
        //麻痺時はみかわし、盾ガードの判定が発生しない
            if (!players[0].paralysis && !players[0].inactive) {
                if (!players[0].acrobaticStar && lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
                if (!kaihi && lcg::getPercent(position, 100) < 0.5) {
                    tate = true;
                }
            }
            (*position)++; //回避

            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

            if (kaihi || tate) {
                baseDamage = 0;
            } else {
                (*position)++; //目を覚ました
                (*position)++; //不明
                tmp = baseDamage * players[defender].defence;
                baseDamage = static_cast<int>((tmp));
                Player::heal(players[attacker], (baseDamage >> 2)); // /4
            }
            process7A8(position, baseDamage, players, defender);
            break;
        case CRACK_ALLY:
            players[attacker].mp -= 3;
            (*position) += 2;
            (*position)++; //関係ない 0x021ec6f8
            if (lcg::getPercent(position, 0x2710) < WooshSlashKaisinnP) {
                kaisinn = true;
            }
            (*position)++; //盾 0x021586fc
            (*position)++; //偽回避 0x02157f58
            baseDamage = FUN_021e8458_typeD(position, 5, 30);
            if (kaisinn) {
                tmp = baseDamage * lcg::floatRand(position, 1.5, 2.0);
                baseDamage = static_cast<int>((tmp));
            }
            tmp = baseDamage * 0.5;
            baseDamage = static_cast<int>((tmp));

            ProcessRage(position, baseDamage, players); // 適当
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
        case BattleEmulator::ATTACK_ALLY:
        case DRAGON_SLASH:
            (*position) += 2;
            (*position)++;
        //会心
            percent_tmp = lcg::getPercent(position, 0x2710);
        //会心
            if (
                ((Id & 0xffff) == BattleEmulator::ATTACK_ALLY && percent_tmp < kaisinnP) ||
                ((Id & 0xffff) == BattleEmulator::DRAGON_SLASH && percent_tmp < DragonSlashKaisinnP)
            ) {
                kaisinn = true;
            }

        //みかわし(相手)
            if (!players[0].paralysis && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
                if (!kaihi) {
                    (*position)++; //盾ガード(幼女は盾を持っていないので0%)
                }
            }
            (*position)++; //回避
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
        //            ProcessFUN_021db2a0(position, attacker, players);

            tmp = static_cast<double>(baseDamage);
            if (kaisinn) {
                //0x020759ec
                if ((Id & 0xffff) == BattleEmulator::MERCURIAL_THRUST || (Id & 0xffff) ==
                    BattleEmulator::MIRACLE_SLASH || (Id & 0xffff) == DRAGON_SLASH) {
                    tmp *= lcg::floatRand(position, 1.5, 2.0);
                } else {
                    tmp = OffensivePower * lcg::floatRand(position, 0.95, 1.05);
                }
            }

            baseDamage = static_cast<int>((tmp));

            if (!kaihi) {
                ProcessRage(position, baseDamage, players);
                (*position)++; //目を覚ました
                (*position)++; //不明
            } else {
                baseDamage = 0;
            }
            if (kaisinn) {
                if (!players[0].rage) {
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
        //std::cout << (players[attacker].specialCharge ? "true" : "fa") << std::endl;
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
    return static_cast<int>((result));
}

//パーセントは絶対に100%にならないから誤差-1
int BattleEmulator::FUN_021e8458_typeD(int *position, double difference, double base) {
    //0x021e8668
    auto result = lcg::floatRand(position, -difference, difference);
    result += base;
    return static_cast<int>((result));
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

void BattleEmulator::process7A8(int *position, int baseDamage, Player *players, int defender) {
    if (!players[defender].paralysis && !players[defender].sleeping && !players[defender].inactive) {
        if (players[defender].hp > baseDamage) {
            //hpが0以下の場合必殺チャージの判定は発生しない。
            //必殺チャージ(敵)
            if (!players[defender].specialCharge) {
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
        }
    } /*else if (players[defender].sleeping) {
        (*position)++;
    }*/
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


int BattleEmulator::ProcessEnemyRandomAction2B(int *position) {
    //0x0208aca8
    //VICTIMISER, HP_HOOVER, CRACK_ENEMY, ATTACK_ENEMY, MANAZASHI, PUFF_PUFF
    int rnd = lcg::getPercent(position, 0x100) + 1;
    if (rnd <= 43) return VICTIMISER;
    if (rnd <= 43 + 42) return HP_HOOVER;
    if (rnd <= 43 + 42 + 43) return CRACK_ENEMY;
    if (rnd <= 43 + 42 + 43 + 43) return ATTACK_ENEMY;
    if (rnd <= 43 + 42 + 43 + 43 + 42) return MANAZASHI;
    return PUFF_PUFF;
}

int BattleEmulator::CalculateMoreHealBase(Player *players) {
    //ベホイミ
    double tmp1 = (players[0].HealPower - 200) * 0.5194;
    auto tmp2 = static_cast<int>((tmp1));
    return 185 + tmp2;
}

int BattleEmulator::CalculateMidHealBase(Player *players) {
    //ｂ
    double tmp1 = (players[0].HealPower - 100) * 0.2392;
    auto tmp2 = static_cast<int>((tmp1));
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
        players[0].def = static_cast<int>(floor(players[0].defaultDEF * DEFMultipliers[index]));
    }

    if (index1 >= 0 && index1 < 5) {
        players[0].speed = static_cast<int>(floor(players[0].defaultSpeed * SpeedMultipliers[index1]));
    }

    //
    // int index1 = players[0].AtkBuffLevel + 2;
    // if (index >= 0 && index < 5) {
    //     players[0].atk = static_cast<int>(floor(players[0].defaultATK * ATKMultipliers[index1]));
    // }
}

void BattleEmulator::ProcessRage(int *position, int baseDamage, Player *players) {
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
                players[0].rage = true;
                players[0].rageTurns = lcg::intRangeRand(position, 2, 4);
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
