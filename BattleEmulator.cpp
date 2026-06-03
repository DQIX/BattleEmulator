//
// Created by Owner on 2024/02/05.
//

#include <cstdint>
#include <iostream>
#include <cmath>
#include "BattleEmulator.h"
#include "lcg.h"
#include "Player.h"
#include "camera.h"
#include "debug.h"
#include "BattleResult.h"
#include "Equipment.h"


thread_local int32_t actions[3];
thread_local int actionsPosition = 0;
thread_local int preHP[3] = {0, 0, 0};
thread_local bool player0_has_initiative = false;
thread_local bool TiggerSkyAttack = false;


constexpr int Ally_Level = 50;
constexpr double Ally_TensionTable[4] = {1.5, 2.5, 4.0, 6.0};
constexpr int Ally_TensionLevel = 1 + static_cast<int>(Ally_Level / 10.0);
constexpr int shieldGuardP = 9; //盾ガード率 9%
constexpr double mitoreP = 0.2040;


thread_local int threadTurnProcessed = 0;
int startTurn = 0;

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


void inline BattleEmulator::resetCombo(uint64_t *NowState) {
    (*NowState) &= ~(0xFFF00000000);
}

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


std::string BattleEmulator::getActionName(int actionId) {
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
        case BattleEmulator::INSULATE:
            return "Insulate";
        case PSYCHE_UP_ALLY:
            return "Psyche up";
        case SPECIAL_MEDICINE:
            return "Special Medicine";
        case KAZAM:
            return "Kazam";
        case CLAW_SLASH:
            return "Claw Slash B6";
        case CLAW_SLASH_B1:
            return "Claw Slash B1";
        case CLAW_SLASH_A2:
            return "Claw Slash A1";
        case WAR_CRY:
            return "War Cry";
        case WAVE_OF_PANIC:
            return "Wave of Panic";
        case INACTIVE_ENEMY:
        case INACTIVE_ALLY:
            return "Inactive";
        default:
            return "Unknown Action";
    }
}

bool BattleEmulator::Main(int *position, int RunCount, const int32_t Gene[350], Player *players,
                         BattleResult* result,
                          uint64_t seed, const int eActions[350], const int damages[350], int mode,
                          uint64_t *NowState) {
    resetCombo(NowState);
    player0_has_initiative = false;
    TiggerSkyAttack = false;
    actionsPosition = 0;
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
        if (genePosition != -1) {
            genePosition = counterJ - 1;
        }
        TiggerSkyAttack = false;
        //現在ターンを保存
        (*NowState) &= ~0xFFFFF000;
        (*NowState) |= (static_cast<uint64_t>(counterJ) << 12ULL);

        if (players[0].dirtySpecialCharge) {
            players[0].specialCharge = false;
            players[0].dirtySpecialCharge = false;
        }
        players[0].specialChargeTurn--;
        if (players[0].specialChargeTurn == -1) {
            players[0].specialCharge = false;
        }

        TiggerSkyAttack = false;

        resetCombo(NowState);

        tmpState = (*NowState);

#ifdef DEBUG2
        DEBUG_COUT2((*position));
        //THIS DEBUG CODE!
        if ((*position) == 525) { //THIS DEBUG CODE!
            std::cout << "!!" << std::endl;
        }
#endif
        int ehp = players[1].hp;
        int ahp = players[0].hp;

        players[0].defence = 1.0;

        for (int32_t &action: actions) {
            action = -1;
        }
        actionsPosition = 0;
        double speed0 = players[0].speed * lcg::floatRand(position, 0.51, 1.0);
        double speed1 = players[1].speed * lcg::floatRand(position, 0.51, 1.0);

        // 素早さを比較
        if (speed0 > speed1) {
            player0_has_initiative = true;
        } else {
            player0_has_initiative = false;
        }

        auto counter = 0;
        int enemyAction[2] = {0, 0};
        int preAction = 0;
        while (counter != 2) {
            uint8_t state = (*NowState) & 0xf;
            if (state == TYPE_2A) {
                //TODO: 2回同じ行動が選択された時の挙動を調べる
                enemyAction[counter] = ProcessEnemyRandomAction2A(position);
                if (counter == 1 && enemyAction[0] == enemyAction[1]) {
                    if (preAction != SWITCH_2A) {
                        if (enemyAction[counter] == CLAW_SLASH_A2) {
                            enemyAction[counter] = ATTACK_ENEMY;
                        }else if (enemyAction[counter] == KAZAM){
                            enemyAction[counter] = CLAW_SLASH_A2;
                        }
                    }
                }
                preAction = enemyAction[counter];
                if (enemyAction[counter] == SWITCH_2B) {
                    (*NowState) &= ~0xff; //ついでに2Eステートもクリアしとく
                    (*NowState) |= TYPE_2B;
                    (*position) += 2;
                    //TODO
                    continue;
                }
                if (enemyAction[counter] == ATTACK_ENEMY) {
                    (*position)+=2;
                    if (!players[1].rage) {
                        (*position) += 1;
                    }
                }else if (enemyAction[counter] == CLAW_SLASH_A2 || enemyAction[counter] == KAZAM) {
                    if (!players[1].rage) {
                        (*position) += 1;
                    }
                }else if (enemyAction[counter] == WAR_CRY) {
                    (*position)+=2;
                }
            } else if (state == TYPE_2B) {
                enemyAction[counter] = ProcessEnemyRandomAction2B(position);
                preAction = enemyAction[counter];
                if (counter == 1 && (enemyAction[0] == enemyAction[1])) {
                    if (preAction != SWITCH_2B) {
                        if (enemyAction[counter] == WAVE_OF_PANIC) {
                            enemyAction[counter] = CLAW_SLASH_B1;
                        } else if (enemyAction[counter] == CLAW_SLASH_B1) {
                            enemyAction[counter] = WAVE_OF_PANIC;
                        } else if (enemyAction[counter] == CLAW_SLASH) {
                            enemyAction[counter] = SWITCH_2A;
                        }else if (enemyAction[counter] == DARK_BREATH) {
                            enemyAction[counter] = ATTACK_ENEMY;
                        }
                    }
                }
                if (enemyAction[counter] == SWITCH_2A) {
                    (*NowState) &= ~0xff;
                    (*NowState) |= TYPE_2A;
                    (*position) += 2;
                    continue;
                }
                if (enemyAction[counter] == ATTACK_ENEMY) {
                    (*position) += 2;
                    if (!players[1].rage) {
                        (*position) += 1;
                    }
                }else if (enemyAction[counter] == CLAW_SLASH || enemyAction[counter] == CLAW_SLASH_B1) {
                    if (!players[1].rage) {
                        (*position)++;
                    }
                }
            }
            if (counter == 0) {
                (*position)++;
            }
            if (mode != -1 && mode != -2) {
                int need = eActions[exCounter1++];
                if (need == -1) {
                    startTurn = counterJ - 1;
                    return true;
                }
                auto & ac = enemyAction[counter];
                if (need == CLAW_SLASH && (ac == CLAW_SLASH_A2 || ac == CLAW_SLASH_B1 || ac == CLAW_SLASH)) {

                }else if (need != ac) {
                    return false;
                }
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
            //genePosition++;
            if (actionTable == HEAL && players[0].mp <= 0) {
                if (players[0].SpecialMedicineCount >= 1) {
                    actionTable = MEDICINAL_HERBS;
                } else {
                    actionTable = ATTACK_ALLY;
                }
            }
        } else {
            if (players[0].hp >= 35) {
                actionTable = ATTACK_ALLY;
            } else {
                if (players[0].mp >= 2) {
                    actionTable = HEAL;
                } else if (players[0].SpecialMedicineCount >= 1) {
                    actionTable = MEDICINAL_HERBS;
                } else {
                    actionTable = ATTACK_ALLY;
                }
            }
        }


        //途中で解除してもいいように2回チェックする
        if (players[0].sleeping) {
            actionTable = SLEEPING;
        } else if (!players[0].paralysis && actionTable == BattleEmulator::MERCURIAL_THRUST) {
            player0_has_initiative = true;
        }

        if (actionTable == DEFENCE) {
            players[0].defence = 0.5;
        }
        if (actionTable == DEFENDING_CHAMPION) {
            //例え寝てる場合でもmpが減る
            players[0].defence = 0.1;
            players[0].mp -= 3; //後攻睡眠などにより大防御の行動ができない場合は事前に減る
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
                for (int c: enemyAction) {
                    //--------start_FUN_02158dfc-------
                    if (lcg::getPercent(position, 100) < mitoreP) {
                        //0x021588ec
                        //次の乱数が90%以上(一致含む)なら見惚れないらしい。
                        int mitore = lcg::getPercent(position, 100); //0x02158964
                        if (mitore < 90) {
                            c = INACTIVE_ENEMY;
                        }
                    }
                    (*position)++;//0x02159b10
                    //--------end_FUN_02158dfc-------
                    basedamage = callAttackFun(c, position, players, 1, 0, NowState);

                    if (players[0].sleeping) {
                        actionTable = SLEEPING;
                    }

                    if (mode == -1) {
                        auto atk1 = -1;
                        if (players[0].AtkBuffTurn > 0) {
                            atk1 = players[0].AtkBuffTurn;
                        } else if (players[0].AtkBuffLevel != 0) {
                            atk1 = 0;
                        }
                        auto def1 = -1;
                        if (players[0].BuffTurns > 0) {
                            def1 = players[0].BuffTurns;
                        } else if (players[0].BuffLevel != 0) {
                            def1 = 0;
                        }
                        auto mmt1 = -1;
                        if (players[0].MagicMirrorTurn > 0) {
                            mmt1 = players[0].MagicMirrorTurn;
                        } else if (players[0].hasMagicMirror) {
                            mmt1 = 0;
                        }
                        BattleResult::add(result, c, basedamage, true, atk1,
                                          def1, mmt1, counterJ - 1,
                                          player0_has_initiative, ehp,
                                          ahp, tmpState, players[0].specialChargeTurn, players[0].mp);
                    } else if (mode != -1 && mode != -2) {
                        if (
                            c == ATTACK_ENEMY ||
                            c == CLAW_SLASH_A2 ||
                            c == KAZAM ||
                            c == DARK_BREATH ||
                            c == CLAW_SLASH_B1 ||
                            c == CLAW_SLASH
                        ) {
                            if (damages[exCounter] == -1) {
                                startTurn = counterJ - 1;
                                return true;
                            }
                            if (damages[exCounter++] != basedamage) {
                                return false;
                            }
                        }
                    }
                    if ((c == KAZAM) && players[0].hasMagicMirror) {
                        Player::reduceHp(players[1], basedamage);
                    } else {
                        Player::reduceHp(players[0], basedamage);
                    }
                    //--------start_FUN_021594bc-------
                    if (Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])) {
                        (*position) += 1;
                    } else {
                        break;
                    }

                    if (players[1].rage) {
                        players[1].rageTurns--;
                        if (players[1].rageTurns <= 0) {
                            players[1].rage = false;
                        }
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
                    if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                        (*position) += 1;
                    } else if (players[0].inactive) {
                        players[0].inactive = false;
                        action = INACTIVE_ALLY;
                        players[0].defence = 1.0;
                        (*position)++;
                    } else if (players[0].paralysis) {
                        action = PARALYSIS;
                        //if (players[0].paralysisTurns != 0) {
                        players[0].paralysisTurns--;
                        //}
                        if (players[0].paralysisTurns <= 0) {
                            int paralysisTable[4] = {62, 75, 87, 100};
                            auto probability1 = paralysisTable[std::abs(players[0].paralysisTurns)];
                            auto probability2 = lcg::getPercent(position, 100);
                            if (probability1 >= (probability2 + (probability1 == 75 ? 1 : 0))) {
                                // 0.5 < 0.65
                                players[0].paralysis = false;
                                players[0].paralysisLevel = 0;
                                action = CURE_PARALYSIS;
                            }
                        } else {
                            (*position) += 1;
                        }
                    } else if (players[0].sleeping) {
                        action = SLEEPING;

                        players[0].sleepingTurn--;
                        if (players[0].sleepingTurn <= 0) {
                            constexpr int sleepTable[4] = {37, 62, 87, 100};
                            auto probability1 = sleepTable[std::abs(players[0].sleepingTurn)];
                            auto probability2 = lcg::getPercent(position, 100);
                            if (probability1 >= probability2) {
                                players[0].sleeping = false;
                                action = CURE_SLEEPING;
                            }
                        } else {
                            (*position)++;
                        }
                    }


                    //--------end_FUN_02158dfc-------
                    basedamage = callAttackFun(action, position, players, 0, 1, NowState);
                    if (mode == -1) {
                        auto atk1 = -1;
                        if (players[0].AtkBuffTurn > 0) {
                            atk1 = players[0].AtkBuffTurn;
                        } else if (players[0].AtkBuffLevel != 0) {
                            atk1 = 0;
                        }
                        auto def1 = -1;
                        if (players[0].BuffTurns > 0) {
                            def1 = players[0].BuffTurns;
                        } else if (players[0].BuffLevel != 0) {
                            def1 = 0;
                        }
                        auto mmt1 = -1;
                        if (players[0].MagicMirrorTurn > 0) {
                            mmt1 = players[0].MagicMirrorTurn;
                        } else if (players[0].hasMagicMirror) {
                            mmt1 = 0;
                        }
                        BattleResult::add(result, action, basedamage, false, atk1,
                                          def1, mmt1, counterJ - 1,
                                          player0_has_initiative, ehp, ahp,
                                          tmpState, players[0].specialChargeTurn, players[0].mp);
                    }
                    if (action == HEAL || action == MEDICINAL_HERBS || action == MORE_HEAL || action == MIDHEAL ||
                        action == FULLHEAL || action == SPECIAL_MEDICINE || action == GOSPEL_SONG) {
                        Player::heal(players[0], basedamage);
                    } else {
                        Player::reduceHp(players[1], basedamage);

                        if (mode != -1 && mode != -2) {
                            if (action == MULTITHRUST || action == ATTACK_ALLY || action == MERCURIAL_THRUST) {
                                if (damages[exCounter] == -1) {
                                    startTurn = counterJ - 1;
                                    return true;
                                }
                                //int need = ;
                                if (damages[exCounter++] != basedamage) {
                                    return false;
                                }
                            }
                        }
                    }
                    //--------start_FUN_021594bc-------
                    if (Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])) {
                        (*position) += 1;
                        //TODO: 順序調べる
                        players[0].MagicMirrorTurn--;
                        if (players[0].hasMagicMirror && players[0].MagicMirrorTurn <= 0) {
                            constexpr int probability[4] = {62, 75, 87, 100};
                            auto probability1 = probability[std::abs(players[0].MagicMirrorTurn)];
                            auto probability2 = lcg::getPercent(position, 100);
                            if (probability1 >= (probability2 + (probability1 == 75 ? 1 : 0))) {
                                // 0x0215a050 MMT
                                players[0].hasMagicMirror = false;
                            }
                        }

                        players[0].AtkBuffTurn--;
                        if (players[0].AtkBuffLevel != 0 && players[0].AtkBuffTurn <= 0) {
                            //0x0215a804 ATK
                            constexpr int probability[4] = {62, 75, 87, 100};
                            auto probability1 = probability[std::abs(players[0].AtkBuffTurn)];
                            auto probability2 = lcg::getPercent(position, 100);
                            if (probability1 >= (probability2 + (probability1 == 75 ? 1 : 0))) {
                                players[0].AtkBuffLevel = 0;
                                RecalculateBuff(players);
                            }
                        }
                        players[0].BuffTurns--;
                        if (players[0].BuffLevel != 0 && players[0].BuffTurns <= 0) {
                            //0x0215a8a8 DEF
                            constexpr int probability[4] = {62, 75, 87, 100};
                            auto probability1 = probability[std::abs(players[0].BuffTurns)];
                            auto probability2 = lcg::getPercent(position, 100);
                            if (probability1 >= (probability2 + (probability1 == 75 ? 1 : 0))) {
                                players[0].BuffLevel = 0;
                                RecalculateBuff(players);
                            }
                        }

                        players[0].SpeedTurns--;
                        if (players[0].SpeedLevel != 0 && players[0].SpeedTurns <= 0) {
                            //0x0215a94c speed
                            const int probability[4] = {62, 75, 87, 100};
                            auto probability1 = probability[std::abs(players[0].SpeedTurns)];
                            auto probability2 = lcg::getPercent(position, 100);
                            if (probability1 >= (probability2 + (probability1 == 75 ? 1 : 0))) {
                                players[0].SpeedLevel = 0;
                                RecalculateBuff(players);
                            }
                        }

                        players[0].InsulateTurns--;
                        if (players[0].InsulateLevel != 0 && players[0].InsulateTurns <= 0) {
                            //0x0215ac74 ins
                            constexpr int probability[4] = {62, 75, 87, 100};
                            auto probability1 = probability[std::abs(players[0].InsulateTurns)];
                            auto probability2 = lcg::getPercent(position, 100);
                            if (probability1 >= (probability2 + (probability1 == 75 ? 1 : 0))) {
                                players[0].InsulateLevel = 0;
                                RecalculateBuff(players);
                            }
                        }
                    }
                } else {
                    if (mode == -1) {
                        auto atk1 = -1;
                        if (players[0].AtkBuffTurn > 0) {
                            atk1 = players[0].AtkBuffTurn;
                        } else if (players[0].AtkBuffLevel != 0) {
                            atk1 = 0;
                        }
                        auto def1 = -1;
                        if (players[0].BuffTurns > 0) {
                            def1 = players[0].BuffTurns;
                        } else if (players[0].BuffLevel != 0) {
                            def1 = 0;
                        }
                        auto mmt1 = -1;
                        if (players[0].MagicMirrorTurn > 0) {
                            mmt1 = players[0].MagicMirrorTurn;
                        } else if (players[0].hasMagicMirror) {
                            mmt1 = 0;
                        }
                        BattleResult::add(result, action, 0, false, atk1,
                                          def1, mmt1, counterJ - 1,
                                          player0_has_initiative, ehp, ahp,
                                          tmpState, players[0].specialChargeTurn, players[0].mp);
                    }
                }
            }
            //--------end_FUN_021594bc-------
        }
        if (Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])) {
            (*position) += 1;
        }
        camera::Main(position, actions, NowState, player0_has_initiative, TiggerSkyAttack);

#ifdef DEBUG2
        DEBUG_COUT2((*position));
        if ((*position) == 176) {
            std::cout << "!!" << std::endl;
        }
#endif

        if (!Player::isPlayerAlive(players[1])) {
            return false;
        }
        if (!Player::isPlayerAlive(players[0])) {
            return false;
        }

        //Player::heal(players[0], 25);
    }
    if (mode != -1 && mode != -2) {
        startTurn = RunCount - 2;
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
const int proportionTable3[9] = {279, 248, 217, 186, 155, 124, 93, 62, 31}; //6ダメージ以下で0% 309
// double proportionTable1[9] = {0.9, 0.8, 0.7, 0.6, 0.5, 0.4, 3.0, 0.2, 0.1};// 21/70が2.99999...になるから最初から20/70より大きい2.89にしちゃう
constexpr double Enemy_TensionTable[4] = {1.3, 2.0, 3.0, 4.5}; //一部の敵は特殊テンションテーブルを倍率として使う


int BattleEmulator::callAttackFun(int32_t Id, int *position, Player *players, int attacker, int defender,
                                  uint64_t *NowState) {
    auto preEnemyHp = players[1].hp;
    actions[actionsPosition++] = Id;
    int baseDamage = 0;
    double tmp, tmp1 = 0;
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
        case WAVE_OF_PANIC: {
                (*position) += 2;
                (*position)++; //会心 0x02158584
                (*position)++; //関係ない 0x021ec6f8
                if(lcg::getPercent(position, 100) < 25){
                    //0x02157f58 回避
                    baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
                    if(baseDamage == 0){
                        baseDamage = lcg::getPercent(position, 2); // 0x021e81a0
                    }

                    if(baseDamage != 0){
                        //TODO 0ダメージのときの消費を調べる
                        (*position)++; //0x021e54fc
                    }
                    players[0].BuffLevel--;
                    players[0].BuffTurns = 7;
                    players[0].SpeedLevel--;
                    players[0].SpeedTurns = 7;

                    RecalculateBuff(players);
                }
                process7A8(position, 0, players, defender);
                baseDamage = 0;
                resetCombo(NowState);
                break;
        }
        case KAZAM:
            (*position) += 2;
            (*position)++; // 0x021ec6f8
            (*position)++; // 会心
            (*position)++; // 回避
            if (players[defender].hasMagicMirror) {
                (*position)++; // 会心(2)
                baseDamage = FUN_021e8458_typeD(position, 30.0, 150.0);
                tmp = baseDamage * 0.5;//闇0.5倍
                tmp = processCombo(Id & 0xffff, tmp, NowState);
                baseDamage = static_cast<int>(floor(tmp));
                (*position)++; //不明 0x021e54fc
                ProcessRage(position, baseDamage, players, preEnemyHp);
            }else{
                baseDamage = FUN_021e8458_typeD(position, 30.0, 112.0);

                tmp = static_cast<double>(baseDamage);

                tmp = Equipments::applyDamageReduction(tmp, Attribute::Darkness);

                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                    tmp *= players[defender].defence;
                }

                if (players[defender].TensionLevel == 4) {
                    tmp *= 0.5;
                }

                baseDamage = static_cast<int>(floor(tmp));

                (*position)++; // 0x021e54fc
                process7A8(position, baseDamage, players, defender);
                resetCombo(NowState);
            }
            break;

        case WAR_CRY:
            (*position) += 2;
            (*position)++; //会心
            (*position)++; //不明
            if (lcg::getPercent(position, 100) < 34) {
                if (players[defender].TensionLevel != 4) {
                    players[defender].inactive = true;
                    players[defender].TensionLevel = 0;
                }
                baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
                if (baseDamage == 0) {
                    baseDamage = lcg::getPercent(position, 2); //1a8
                }
                if (baseDamage != 0) {
                    (*position)++; //0x021e54fc
                }
            }
            baseDamage = 0;
            process7A8(position, 0, players, defender);
            resetCombo(NowState);
            break;
        case BattleEmulator::INSULATE:
            players[0].mp -= 4;
            (*position) += 2;
            (*position)++; // 関係ない
            (*position)++; // 会心判定
            (*position)++; // 回避

            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }

            if (!players[0].specialCharge && !players[0].sleeping && !players[0].paralysis && !players[0].inactive) {
                (*position)++; //0x021ed7a8 必殺チャージ(敵) 0%
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }

            if (players[0].InsulateLevel != 2) {
                players[0].InsulateLevel++;
                players[0].InsulateTurns = 6;
                RecalculateBuff(players);
            }

            baseDamage = 0;
            resetCombo(NowState);
            break;
        case PSYCHE_UP_ALLY:
            (*position) += 2;
            (*position)++; // 0x021ec6f8
            (*position)++; // 0x02158584 会心
            (*position)++; // 0x02157f58 偽回避
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (players[attacker].TensionLevel < 3 || (players[attacker].TensionLevel == 3 && lcg::getPercent(position, 2) == 0)) {
                //0x02087fb4 テンション
                players[attacker].TensionLevel++;
            }
            if (!players[0].specialCharge && !players[0].sleeping && !players[0].paralysis && !players[0].inactive) {
                (*position)++; //0x021ed7a8 必殺チャージ(敵) 0%
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case GOSPEL_SONG:
            (*position) += 2;
            (*position)++; //0x02158584 会心
            (*position)++; //0x021ec6f8 不明
            (*position)++; //0x02157f58 ニセ回避
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[attacker].def);
            if(baseDamage == 0){
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if(baseDamage != 0){
                (*position)++; //不明 0x021e54fc
            }
            baseDamage = std::max(static_cast<int>(std::round(players[attacker].maxHp * 0.4)), 75);//(*code 24) 021e1cc4
            resetCombo(NowState);
            players[0].specialCharge = false;
            if(players[0].BuffLevel < 0){
                players[0].BuffLevel = 0;
                players[0].BuffTurns = -1;
                RecalculateBuff(players);
            }

            if(players[0].SpeedLevel < 0){
                players[0].SpeedLevel = 0;
                players[0].SpeedLevel = -1;
                RecalculateBuff(players);
            }
            break;
        case SPECIAL_MEDICINE:
            players[attacker].SpecialMedicineCount--;
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
                    players[attacker].specialChargeTurn = 8;
                }
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
                    players[attacker].specialChargeTurn = 8;
                }
            }
            players[attacker].mp += baseDamage;
            players[attacker].mp = std::min(players[attacker].mp, players[attacker].maxMp);
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case ELFIN_ELIXIR:
            players[attacker].ElfinElixirCount--;
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //0x02158584 会心
            (*position)++; //0x02157f58 ニセ回避
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            if (!players[attacker].specialCharge) {
                (*position)++; //0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            players[attacker].mp = players[attacker].maxMp;
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case SAGE_ELIXIR:
            players[attacker].SageElixirCount--;
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //0x02158584 会心
            (*position)++; //0x02157f58 ニセ回避
            baseDamage = FUN_021e8458_typeC(position, 95, 95, 5);
            (*position)++; //不明 0x021e54fc
            if (!players[attacker].specialCharge) {
                (*position)++; //0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            players[attacker].mp += baseDamage;
            players[attacker].mp = std::min(players[attacker].mp, players[attacker].maxMp);
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case THUNDER_THRUST:
            (*position) += 2;
            (*position)++; //不明 0x021ec6f8
            (*position)++; //会心
            if (lcg::getPercent(position, 100) < 2) {
                //0x021587b0 本物みかわし
                kaihi = true;
            } else {
                (*position)++; //0x021586fc 盾
            }
            (*position)++; //0x02157f58 ニセ回避
            FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (lcg::getPercent(position, 2) == 0) {
                tmp = OffensivePower * lcg::floatRand(position, 0.95, 1.05);
                baseDamage = static_cast<int>(tmp);
            } else {
                kaihi = true;
            }

            if (kaihi) {
                if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                    (*position)++; //0x021ed7a8
                }
                baseDamage = 0;
            } else {
                if (baseDamage != 0) {
                    (*position)++; //目を覚ました
                    (*position)++; //不明

                    //怒り関連のやつ
                    (*position)++; //0x021eb8c8
                    (*position)++; //0x021eb8f0
                }
            }
            if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                players[attacker].specialCharge = true;
                players[attacker].specialChargeTurn = 8;
            }
            resetCombo(NowState);
            break;
        case RESTORE_MP:
            (*position) += 2;
            (*position)++; //不明　0x021ec6f8
            (*position)++; //会心
            (*position)++; //ニセ回避 0x02157f58
            FUN_0207564c(position, players[attacker].atk, players[attacker].def);
            (*position)++; //不明 0x021e54fc
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case MAGIC_BURST:
            (*position) += 2;
            (*position)++; //会心
            (*position)++; //不明　0x021ec6f8
            (*position)++; //ニセ回避 0x02157f58
            FUN_0207564c(position, players[attacker].atk, players[defender].def);
            baseDamage = ProcessMagicBurst(position);
            (*position)++; //不明 0x021e54fc

            if (players[defender].TensionLevel == 4) {
                tmp = baseDamage * 0.5;
            } else {
                tmp = baseDamage;
            }
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[0].defence;
            }
            baseDamage = static_cast<int>(floor(tmp));

            process7A8(position, baseDamage, players, defender);
            resetCombo(NowState);
            break;
        case MEDITATION:
            (*position) += 2;
            (*position)++; //不明　0x021ec6f8
            (*position)++; //会心
            (*position)++; //ニセ回避 0x02157f58
            (*position)++; //float: 0x021e88d8 0x80000000 00000000 1204
            (*position)++; //不明 0x021e54fc
            baseDamage = 0;
            resetCombo(NowState);
            return 500;
        case DEFENDING_CHAMPION:
            (*position) += 2;
            (*position)++; //不明　0x021ec6f8
            (*position)++; //会心
            (*position)++; //ニセ回避 0x02157f58
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                //0x021e81a0
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //0x021e54fc
            }
            if (!players[0].specialCharge) {
                // 不明
                (*position)++; //0x021ed7a8 必殺チャージ(敵)
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case DARK_BREATH:
            (*position) += 2;
            (*position)++; //会心
            (*position)++; //不明
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
            }
            (*position)++; //ニセ回避 0x02157f58
            baseDamage = FUN_021e8458_typeD(position, 10, 65);
            tmp = Equipments::applyDamageReduction(baseDamage, Attribute::Darkness);
            tmp *= 1.0 - (players[0].InsulateLevel * 0.25);

            if (players[defender].TensionLevel == 4) {
                tmp *= 0.5;
            }

            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[defender].defence;
            }
            baseDamage = static_cast<int>(floor(tmp));
            if (!kaihi) {
                (*position)++; //0x021e54fc 不明
            } else {
                baseDamage = 0;
            }
            process7A8(position, baseDamage, players, defender);
            break;
        case PSYCHE_UP:
            (*position) += 2;
            (*position)++; //不明
            (*position)++; //会心
            (*position)++; //ニセ回避 0x02157f58
            FUN_0207564c(position, players[attacker].atk, players[attacker].def);
            players[attacker].TensionLevel++;
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case FULLHEAL:
            players[attacker].mp -= 24;
            (*position) += 2;
            (*position)++; //不明　0x021ec6f8
            (*position)++; //会心
            (*position)++; //ニセ回避 0x02157f58
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //関係ない 0x021e54fc???
            }

            //0x021eb8c8, randIntRange: 0x021eb8f0 怒り狂っている場合←の消費が発生しない。

            if (!players[0].specialCharge && !players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                (*position)++; //0x021ed7a8
            }

            if (!players[1].rage) {
                (*position)++; //0x021eb8c8
            }
            (*position)++; //? 0x021eb8f0
            if (!players[0].specialCharge && !players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 1) {
                    // 0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            resetCombo(NowState);
            return 999;
            break;
        case DISRUPTIVE_WAVE:
            //TODO: 必殺チャージ無しのときに7a8があるかどうか調べる
            (*position) += 2;
            (*position)++; //0x021ec6f8 会心
            (*position)++; //0x021ec6f8 不明
            (*position)++; //ニセ回避 0x02157f58
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (baseDamage == 0) {
                //0ダメージだと特殊消費がはいるっぽい。凍てつく波動だけの仕様であることを祈る
                baseDamage = lcg::getPercent(position, 2); // 0x021e81a0
            }
            //ダメージが0の場合0x021e54fcは発生しない模様。
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            process7A8(position, 0, players, defender); //必殺チャージ(敵)　0x021ed7a8

            players[0].hasMagicMirror = false;
            players[0].MagicMirrorTurn = -1;
            players[0].AtkBuffLevel = 0;
            players[0].AtkBuffTurn = -1;
            players[0].BuffLevel = 0;
            players[0].BuffTurns = -1;
            players[0].TensionLevel = 0;


            RecalculateBuff(players);
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case MIDHEAL:
            players[attacker].mp -= 4;
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            if (lcg::getPercent(position, 0x2710) < 100) {
                kaisinn = true;
            }
            (*position)++; //回避

            baseDamage = FUN_021e8458_typeD(position, 10, CalculateMidHealBase(players));
            if (kaisinn) {
                tmp1 *= lcg::floatRand(position, 1.5, 2.0); //TODO
            } else {
                tmp1 = baseDamage;
            }


            if (players[attacker].TensionLevel != 0) {
                //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                tmp = baseDamage * Ally_TensionTable[players[attacker].TensionLevel - 1];
                tmp += (players[attacker].TensionLevel * Ally_TensionLevel);
                players[attacker].TensionLevel = 0;
            } else {
                tmp = baseDamage;
            }

            if (kaisinn) {
                if (tmp * 1.2000 <= tmp1) {
                    tmp = tmp1;
                } else {
                    tmp *= 1.2000;
                }
            }

            baseDamage = static_cast<int>(floor(tmp));

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
            if (!players[0].paralysis) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            resetCombo(NowState);
            break;
        case LULLAB_EYE:
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //会心 0x02158584
            (*position)++; //ニセ回避 0x02157f58
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (baseDamage == 0) {
                //0x021e81a0
                baseDamage = lcg::getPercent(position, 2);
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            if (!players[0].paralysis) {
                players[0].sleeping = true;
                players[0].sleepingTurn = 2;
                players[0].TensionLevel = 0;
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case LIGHTNING_STORM:
            (*position) += 2;
            (*position)++; //会心 0x02158584
            (*position)++; //0x021ec6f8 不明
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                //TODO
                if (lcg::getPercent(position, 100) < shieldGuardP) {
                    //盾ガード 0x021586fc
                    tate = true;
                }
            }
            (*position)++; //ニセ回避 0x02157f58 100%
            baseDamage = FUN_021e8458_typeD(position, 15, 80);
            tmp = Equipments::applyDamageReduction(baseDamage, Attribute::ThunderExplosion);
            if (tate) {
                baseDamage = 0;
            } else {
                (*position)++; //?? 0x02158ac4
                (*position)++; //?? 0x021e54fc
                if (baseDamage != 0 && players[0].sleeping) {
                    players[0].sleeping = false;
                    players[0].sleepingTurn = -1;
                }
                if (players[defender].TensionLevel == 4) {
                    tmp *= 0.5;
                }
                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                    tmp *= players[defender].defence;
                }
                baseDamage = static_cast<int>(floor(tmp));
            }
            process7A8(position, baseDamage, players, defender);

            resetCombo(NowState);
            break;
        case MULTITHRUST:
            players[attacker].mp -= 4;
            attackCount = lcg::intRangeRand(position, 3, 4);
            (*position)++;
            (*position) += attackCount;
            hasKaisinn = false;
            for (int i = 0; i < attackCount; ++i) {
                kaihi = false;
                kaisinn = false;
                (*position)++; //0x021ec6f8 不明
                if (attackCount == 4) {
                    if (lcg::getPercent(position, 0x2710) < 62) {
                        kaisinn = true;
                        hasKaisinn = true;
                    }
                } else {
                    if (lcg::getPercent(position, 0x2710) < 83) {
                        kaisinn = true;
                        hasKaisinn = true;
                    }
                }
                if (lcg::getPercent(position, 100) < 4) {
                    kaihi = true;
                } else {
                    (*position)++; //盾ガード 0x021586fc 0%
                }
                (*position)++; //ニセ回避 0x02157f58 100%
                baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

                tmp = floor(baseDamage * 0.5);
                if (kaisinn == true) {
                    tmp1 = tmp * lcg::floatRand(position, 1.5, 2.0);
                }

                if (players[attacker].TensionLevel != 0) {
                    //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                    tmp *= Ally_TensionTable[players[attacker].TensionLevel - 1];
                    tmp += (players[attacker].TensionLevel * Ally_TensionLevel);
                }

                if (kaisinn) {
                    if (tmp * 1.2000 <= tmp1) {
                        tmp = tmp1;
                    } else {
                        tmp *= 1.2000;
                    }
                }

                //ここの小数点以下は引き継がれる
                tmp = tmp; //1.25倍は雷属性になってるから
                baseDamage = static_cast<int>(floor(tmp));

                if (!kaihi) {
                    ProcessRage(position, baseDamage, players, preEnemyHp);
                    (*position)++; //目を覚ました
                    (*position)++; //不明 0x021e54fc
                } else {
                    baseDamage = 0;
                }

                preEnemyHp = std::max(0, preEnemyHp - baseDamage);
                totalDamage += baseDamage;
                if (preEnemyHp <= 0) {
                    return totalDamage;
                }
            }
            if (hasKaisinn) {
                (*position) += 2;
            }
            if (preEnemyHp > 0) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            //0x021ec6f8が多分の残りの攻撃回数だけ発生する

            if (players[attacker].TensionLevel != 0) {
                players[attacker].TensionLevel = 0;
            }
            resetCombo(NowState);
            return totalDamage;
        case MERA_ZOMA:
            (*position) += 2;
            (*position)++; //0x021ec6f8 不明
            (*position)++; //敵の会心判定
            if (players[0].hasMagicMirror) {
                if (lcg::getPercent(position, 0x2710) < 100) {
                    //こっちの会心判定
                    kaisinn = true;
                }
                (*position)++; //盾ガード 0x021586fc 0%
                (*position)++; //ニセ回避 0x02157f58 100%
                tmp = BattleEmulator::FUN_021e8458_typeD(position, 12, 190);
                if (kaisinn) {
                    tmp *= lcg::floatRand(position, 1.5, 2.0);
                }
                tmp *= 1.25;
                tmp = processCombo(Id & 0xffff, tmp, NowState);
                baseDamage = static_cast<int>(floor(tmp));
                (*position)++; //不明 0x021e54fc
                ProcessRage(position, baseDamage, players, preEnemyHp);
            } else {
                if (!players[0].paralysis && !players[0].sleeping&& !players[0].inactive) {
                    if (lcg::getPercent(position, 100) < shieldGuardP) {
                        //TODO 盾の条件調べる
                        tate = true;
                    }
                }
                (*position)++; //ニセ回避 0x02157f58 100%
                baseDamage = FUN_021e8458_typeD(position, 12, 116);
                tmp = Equipments::applyDamageReduction(baseDamage, Attribute::Fire);
                if (players[defender].TensionLevel == 4) {
                    tmp *= 0.5;
                }
                if (!players[0].paralysis && !players[0].sleeping&& !players[0].inactive) {
                    tmp *= players[defender].defence;
                }
                tmp = processCombo(Id & 0xffff, tmp, NowState);
                baseDamage = static_cast<int>(floor(tmp));
                if (!tate) {
                    (*position)++; //0x021e54fc 不明
                } else {
                    baseDamage = 0;
                }
                process7A8(position, baseDamage, players, defender); //必殺チャージ(敵)　0x021ed7a8
            }
            break;
        case BattleEmulator::FREEZING_BLIZZARD:
            (*position) += 2;
            (*position)++; // 会心判定
            (*position)++; //0x021ec6f8 不明
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 2) {
                    //0x021587b0 本物みかわし
                    kaihi = true;
                }
            }
            (*position)++; // 0x02157f58 ニセ回避

            baseDamage = FUN_021e8458_typeD(position, 15, 75);
            tmp = Equipments::applyDamageReduction(baseDamage, Attribute::Ice);
            tmp *= 1.0 - (players[0].InsulateLevel * 0.25);
            if (!kaihi) {
                (*position)++; //0x021e54fc 不明
            } else {
                tmp = 0;
            }
            if (players[defender].TensionLevel == 4) {
                tmp *= 0.5;
            }
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[defender].defence;
            }
            baseDamage = static_cast<int>(floor(tmp));
            process7A8(position, baseDamage, players, defender);
            resetCombo(NowState);
            break;
        case BattleEmulator::DOUBLE_UP:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            (*position)++; //かぶとわりの判定　0x021e3e7c
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            if (!players[attacker].specialCharge) {
                (*position)++; //必殺(敵)　0x021ed7a8
                if (!players[0].paralysis) {
                    if (lcg::getPercent(position, 100) < 1) {
                        players[attacker].specialCharge = true;
                        players[attacker].specialChargeTurn = 8;
                    }
                }
            }

            if (players[0].AtkBuffLevel != 2) {
                players[0].AtkBuffLevel += 2;
                players[0].AtkBuffLevel = std::min(players[0].AtkBuffLevel, 2);
                players[0].AtkBuffTurn = 6;
            }

            if (players[0].BuffLevel != -2) {
                players[0].BuffLevel--;
                players[0].BuffTurns = 7;
            }

            RecalculateBuff(players);
            resetCombo(NowState);
            baseDamage = 0;
            break;
        case BattleEmulator::MORE_HEAL:
            players[attacker].mp -= 8;
            (*position) += 2;
            (*position)++; //関係ない
            if (lcg::getPercent(position, 0x2710) < 100) {
                kaisinn = true;
            }
            (*position)++; //回避

            baseDamage = FUN_021e8458_typeD(position, 20, CalculateMoreHealBase(players));
            if (kaisinn) {
                tmp1 = baseDamage * lcg::floatRand(position, 1.5, 2.0); //TODO
            } else {
                tmp1 = baseDamage;
            }

            if (players[attacker].TensionLevel != 0) {
                //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                tmp = baseDamage * Ally_TensionTable[players[attacker].TensionLevel - 1];
                tmp += (players[attacker].TensionLevel * Ally_TensionLevel);
                players[attacker].TensionLevel = 0;
            } else {
                tmp = baseDamage;
            }

            if (kaisinn) {
                if (tmp * 1.2000 <= tmp1) {
                    tmp = tmp1;
                } else {
                    tmp *= 1.2000;
                }
            }
            baseDamage = static_cast<int>(floor(tmp));

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
            if (!players[0].paralysis) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            resetCombo(NowState);
            break;
        case BattleEmulator::MAGIC_MIRROR:
            players[attacker].mp -= 4;
            (*position) += 5;
            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }
            if (!players[0].specialCharge) {
                (*position)++; //0x021ed7a8
                if (lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            players[0].hasMagicMirror = true;
            players[0].MagicMirrorTurn = 6;
            resetCombo(NowState);
            baseDamage = 0;
            break;
        case BattleEmulator::CRITICAL_ATTACK:
            (*position) += 2;
            (*position)++; // アクロバットスターとか

            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
                if (!kaihi && lcg::getPercent(position, 100) < shieldGuardP) {
                    tate = true;
                }
            }
            (*position)++; //回避

            FUN_0207564c(position, players[attacker].atk, players[defender].def);
            baseDamage = static_cast<int>(floor(players[1].defaultATK * lcg::floatRand(position, 0.8500, 0.9500)));

            //TODO: この処理を直す
            if (baseDamage != 0) {
                players[0].sleeping = false;
                players[0].sleepingTurn = -1;
            }

            if (kaihi) {
                if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                    (*position)++; //0x021ed7a8
                }
                baseDamage = 0;
            } else if (tate) {
                if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                    (*position)++; //0x021ed7a8
                }
                baseDamage = 0;
            } else {
                if (baseDamage == 0) {
                    baseDamage = lcg::getPercent(position, 2); //TODO: 0x021e81a0
                }

                if (baseDamage != 0) {
                    (*position)++; //目を覚ました
                    (*position)++; //不明
                }
                if (players[defender].TensionLevel == 4) {
                    tmp = baseDamage * 0.5;
                } else {
                    tmp = baseDamage;
                }
                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                    tmp *= players[defender].defence;
                }
                baseDamage = static_cast<int>(floor(tmp));

                process7A8(position, baseDamage, players, defender);
            }


            players[defender].sleeping = false;
            players[defender].sleepingTurn = -1;


            resetCombo(NowState);
            break;
        case BattleEmulator::BUFF:
            players[0].mp -= 3;
            (*position) += 2;
            (*position)++; // 関係ない
            (*position)++; // 会心判定
            (*position)++; // 回避

            baseDamage = FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            if (baseDamage == 0) {
                baseDamage = lcg::getPercent(position, 2); //0x021e81a0
            }
            if (baseDamage != 0) {
                (*position)++; //不明 0x021e54fc
            }

            if (!players[0].specialCharge && !players[0].sleeping && !players[0].paralysis && !players[0].inactive) {
                (*position)++; //0x021ed7a8 必殺チャージ(敵) 0%
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }

            if (players[0].BuffLevel != 2) {
                players[0].BuffLevel++;
                players[0].BuffTurns = 7;
                RecalculateBuff(players);
            }

            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::ULTRA_HIGH_SPEED_COMBO:
            (*position) += 2;
            (*position) += 4;
            for (int i = 0; i < 4; ++i) {
                if (preHP[defender] > totalDamage) {
                    //hp0時特殊消費
                    defenseFlag = false;
                    kaihi = false;
                    tate = false;
                    (*position)++; // アクロバットスターとか

                    (*position)++; //会心
                    if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                        if (lcg::getPercent(position, 100) < 2) {
                            // = 10%
                            kaihi = true;
                        }
                        if (!kaihi && lcg::getPercent(position, 100) < shieldGuardP) {
                            //TODO 盾の条件調べる 盾ガード
                            tate = true;
                        }
                    }
                    (*position)++; //回避

                    baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

                    if (kaihi) {
                        if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                            (*position)++; //0x021ed7a8
                        }
                        baseDamage = 0;
                    } else if (tate) {
                        if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                            (*position)++; //0x021ed7a8
                        }
                        baseDamage = 0;
                    } else {
                        tmp = baseDamage * 0.3;
                        baseDamage = static_cast<int>(floor(tmp));
                        if (baseDamage == 0) {
                            //&& players[0].defence != 0.1
                            baseDamage = lcg::getPercent(position, 2); //TODO: 0x021e81a0
                            if (baseDamage == 1) {
                                defenseFlag = true;
                            }
                        }
                        tmp = static_cast<double>(baseDamage);
                        if (players[defender].TensionLevel == 4) {
                            tmp *= 0.5;
                        }

                        if (!defenseFlag && !players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                            tmp = tmp * players[defender].defence;
                        }
                        baseDamage = static_cast<int>(floor(tmp));

                        if (baseDamage != 0) {
                            (*position)++; //目を覚ました
                            (*position)++; //不明
                        }


                        if (baseDamage != 0 && players[0].sleeping) {
                            players[0].sleeping = false;
                            players[0].sleepingTurn = -1;
                        }

                        //hp0時特殊消費
                        if (preHP[defender] > (totalDamage + baseDamage)) {
                            process7A8(position, baseDamage, players, defender);
                        }
                    }
                } else {
                    //hp0時特殊消費
                    baseDamage = 0;
                    (*position)++; //0x021ec6f8
                }
                totalDamage += baseDamage;
            }

            resetCombo(NowState);
            return totalDamage;
        case BattleEmulator::MEDICINAL_HERBS:
            players[0].SpecialMedicineCount--;
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
                    players[attacker].specialChargeTurn = 8;
                }
            }
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
                    players[attacker].specialChargeTurn = 8;
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
                    players[attacker].specialChargeTurn = 8;
                }
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::CURE_PARALYSIS:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].defaultATK, players[defender].def); // 0x021e81a0要検討
            (*position)++; //不明 0x021e54fc?
            if (!players[attacker].specialCharge && !players[attacker].sleeping && !players[attacker].paralysis) {
                (*position)++; //必殺チャージ(敵)
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021ed7a8
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::LAUGH:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].atk, players[attacker].def);
            (*position)++; //不明
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::BURNING_BREATH:
            (*position) += 2;
            (*position)++; //会心
            (*position)++; //関係ない
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                //TODO 眠ってるときのやけつくいき
                if (lcg::getPercent(position, 100) < 2) {
                    //0x021587b0 回避
                    kaihi = true;
                }
            }
#if defined(SUPER)
            (*position)++;
            if (false) {
#else
            if (lcg::getPercent(position, 100) < 13 && !kaihi) {
#endif

                if (players[defender].paralysisLevel == 3) {
                    //std::cerr << "paralysisLevel == 2" << std::endl;
                }
                if(players[defender].TensionLevel != 4){
                    players[defender].paralysis = true;
                    players[defender].paralysisTurns = 4;
                    players[defender].paralysisLevel++;
                    players[0].sleeping = false;
                    players[0].sleepingTurn = -1;
                    players[0].TensionLevel = 0;
                }
                baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
                if (baseDamage == 0) {
                    baseDamage = lcg::getPercent(position, 2); // 0x021e81a0
                }

                if (baseDamage != 0) {
                    //TODO 0ダメージのときの消費を調べる
                    (*position)++; //0x021e54fc
                }
            }
            if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                (*position)++; //0x021ed7a8 必殺(敵)
            }
            baseDamage = 0;
            resetCombo(NowState);
            break;
        case BattleEmulator::ATTACK_ENEMY:
        case BattleEmulator::SKY_ATTACK:
        case CLAW_SLASH:
        case CLAW_SLASH_B1:
        case CLAW_SLASH_A2:
            (*position) += 2;
            (*position)++; // アクロバットスターとか

            (*position)++; //会心
            if (Id == CLAW_SLASH_B1 || Id == CLAW_SLASH_A2) {
                Id = CLAW_SLASH;
            }
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
                if (!kaihi && lcg::getPercent(position, 100) < shieldGuardP) {
                    tate = true;
                }
            }
            (*position)++; //回避

            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

            if (kaihi) {
                //                if (baseDamage == 0) {
                //                    (*position)++;//0x021e81a0
                //                }

                if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                    (*position)++; //0x021ed7a8
                }
                baseDamage = 0;
            } else if (tate) {
                //                if (baseDamage == 0) {
                //                    (*position)++;//0x021e81a0
                //                }

                if (!players[0].paralysis && !players[0].sleeping && !players[0].specialCharge && !players[0].inactive) {
                    (*position)++; //0x021ed7a8
                }
                baseDamage = 0;
            } else {
                tmp = static_cast<double>(baseDamage);
                if ((Id & 0xffff) == CLAW_SLASH) {
                    tmp = floor(tmp * 1.25);
                }


                //テンションがある場合、この時点でオフセットが計算されて、最低4ダメージが保証されて下の0x021e81a0でダメージがある判定になる。
                //1*(1+(30/10))で4ダメージが保証されるけど、事前に計算して定数にしとく。
                if (players[attacker].TensionLevel != 0) {
                    //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                    tmp *= Enemy_TensionTable[players[attacker].TensionLevel - 1];
                    tmp += (players[attacker].TensionLevel * 4); //4 = 1*(1+(30/10))
                    players[attacker].TensionLevel = 0;
                }

                baseDamage = static_cast<int>(floor(tmp));


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
                    baseDamage = static_cast<int>(floor(tmp));
                }

                if (players[defender].TensionLevel == 4) {
                    tmp = baseDamage * 0.5;
                } else {
                    tmp = baseDamage;
                }

                if (!defenseFlag && !players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                    tmp *= players[defender].defence;
                }
                baseDamage = static_cast<int>(floor(tmp));


                if (baseDamage != 0) {
                    (*position)++; //目を覚ました
                    (*position)++; //不明
                } else {
                    if (Id == SKY_ATTACK) {
                        TiggerSkyAttack = true;
                    }
                }


                if (baseDamage != 0 && players[0].sleeping) {
                    players[0].sleeping = false;
                    players[0].sleepingTurn = -1;
                }

                process7A8(position, baseDamage, players, defender);
            }

            players[attacker].TensionLevel = 0;

            break;
        case BattleEmulator::INACTIVE_ALLY:
        case BattleEmulator::PARALYSIS:
        case BattleEmulator::INACTIVE_ENEMY:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].defaultATK, players[attacker].def);
            (*position)++; //不明 0x021e54fc
            baseDamage = 0;
            resetCombo(NowState);
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
                tmp1 = baseDamage * lcg::floatRand(position, 1.5, 2.0); //TODO
            } else {
                tmp1 = baseDamage;
            }

            if (players[attacker].TensionLevel != 0) {
                //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                tmp = baseDamage * Ally_TensionTable[players[attacker].TensionLevel - 1];
                tmp += (players[attacker].TensionLevel * Ally_TensionLevel);
                players[attacker].TensionLevel = 0;
            } else {
                tmp = baseDamage;
            }

            if (kaisinn) {
                if (tmp * 1.2000 <= tmp1) {
                    tmp = tmp1;
                } else {
                    tmp *= 1.2000;
                }
            }
            baseDamage = static_cast<int>(floor(tmp));
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
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = 8;
                }
            }
            resetCombo(NowState);
            break;
        case BattleEmulator::ATTACK_ALLY:
        case BattleEmulator::MERCURIAL_THRUST:
            (*position) += 2;
            (*position)++;
            //会心
            percent_tmp = lcg::getPercent(position, 0x2710);
            if (((Id & 0xffff) == BattleEmulator::ATTACK_ALLY && percent_tmp < 500) ||
                ((Id & 0xffff) == BattleEmulator::MERCURIAL_THRUST && percent_tmp < 250)) {
                kaisinn = true;
            }

            //みかわし(相手)
            if (!players[0].paralysis) {
                if (lcg::getPercent(position, 100) < 4) {
                    kaihi = true;
                }
                if (!kaihi) {
                    (*position)++; //盾ガード(幼女は盾を持っていないので0%)
                }
            }

            (*position)++; //回避
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

            if ((Id & 0xffff) == BattleEmulator::MERCURIAL_THRUST) {
                tmp = floor(baseDamage * 0.75);
            } else {
                tmp = static_cast<double>(baseDamage);
            }

            if (kaisinn) {
                //0x020759ec
                if ((Id & 0xffff) == BattleEmulator::MERCURIAL_THRUST) {
                    tmp1 *= lcg::floatRand(position, 1.5, 2.0);
                } else {
                    tmp1 = OffensivePower * lcg::floatRand(position, 0.95, 1.05);
                }
            }

            if (players[attacker].TensionLevel != 0) {
                //TODO ダメージが正しいか調べる 特殊県産式の引数も調べる https://dragonquest9.com/?%E3%83%80%E3%83%A1%E3%83%BC%E3%82%B8%E3%81%AB%E3%81%A4%E3%81%84%E3%81%A6#tension
                tmp *= Ally_TensionTable[players[attacker].TensionLevel - 1];
                tmp += (players[attacker].TensionLevel * Ally_TensionLevel);
                players[attacker].TensionLevel = 0;
            }

            if (kaisinn) {
                if (tmp * 1.2000 <= tmp1) {
                    tmp = tmp1;
                } else {
                    tmp *= 1.2000;
                }
            }

            tmp *= 1.25; //雷属性
            baseDamage = static_cast<int>(floor(tmp));

            if (!kaihi) {
                ProcessRage(position, baseDamage, players, preEnemyHp);
                (*position)++; //目を覚ました
                (*position)++; //不明
            } else {
                baseDamage = 0;
            }
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
                    players[attacker].specialChargeTurn = 8;
                }
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
    return static_cast<int>(floor(result));
}

//パーセントは絶対に100%にならないから誤差-1
int BattleEmulator::FUN_021e8458_typeD(int *position, double difference, double base) {
    //0x021e8668
    auto result = lcg::floatRand(position, -difference, difference);
    result += base;
    return static_cast<int>(floor(result));
}


#if !defined(__EMSCRIPTEN__)

#if defined(_MSC_VER) && !defined(__clang__)
#include <__msvc_int128.hpp>
using u128 = std::_Unsigned128;
#else
using u128 = unsigned __int128;
#endif


int BattleEmulator::FUN_0207564c(int *position, int atk, int def) {
    [[assume(atk >= 0)]];
    [[assume(def >= 0)]];
    int base = 2 * atk - def;
    if (base <= 0) [[unlikely]] {
        return 0;
    }

    int64_t atk1_fp = static_cast<int64_t>(base) << 30;
    int64_t atk2_fp = static_cast<int64_t>(atk) << 28; // atk * 1/16

    int64_t result_fp;

    if (atk1_fp > atk2_fp) [[likely]] {
        int64_t atk4_fp = atk1_fp >> 4; // /16

        // floatRand(-atk4, atk4): -atk4 + top/2^32 * (2*atk4)
        uint32_t r1 = lcg::getTop32(position);
        auto spread_u = static_cast<uint64_t>(
            (static_cast<u128>(r1) * static_cast<u128>(static_cast<uint64_t>(atk4_fp))) >> 31);
        int64_t spread = static_cast<int64_t>(spread_u) - atk4_fp;

        // floatRandAttack(-1, 1): -1 + top/2^31
        uint32_t r2 = lcg::getTop32(position);
        int64_t attack = (static_cast<int64_t>(r2) << 1) - (1ll << 32);

        result_fp = atk1_fp + spread + attack;
    } else {
        // floatRand(0, atk2)
        uint32_t r = lcg::getTop32(position);
        auto result_u = static_cast<uint64_t>(
            (static_cast<u128>(r) * static_cast<u128>(static_cast<uint64_t>(atk2_fp))) >> 32);
        result_fp = static_cast<int64_t>(result_u);
    }

    if (result_fp <= 0) [[unlikely]] {
        return 0;
    }

    return static_cast<int>(result_fp >> 32);
}

#else

int BattleEmulator::FUN_0207564c(int *position, int atk, int def) {
    [[assume(atk >= 0)]];
    [[assume(def >= 0)]];
    double result;
    const double atk1 = (2*atk - def) * 0.25;
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

#endif

void BattleEmulator::process7A8(int *position, int baseDamage, Player players[2], int defender) {
    if (players[defender].paralysis || players[defender].sleeping || players[defender].specialCharge || players[defender].inactive || players[defender].hp <= baseDamage) {
        return;
    }
    if (baseDamage == 0) {
        (*position)++;
        return;
    }
    auto percent_tmp = lcg::getPercent(position, 100);
    double tmp = baseDamage;

    auto baseDamage_tmp = static_cast<int>(floor(tmp));
    for (int i = 0; i < 9; ++i) {
        if (baseDamage_tmp >= proportionTable3[i]) {
            if (percent_tmp < proportionTable2[i]) {
                players[defender].specialCharge = true;
                players[defender].specialChargeTurn = 8;
            }
            break;
        }
    }
}
int BattleEmulator::FUN_0208aecc(int *position, uint64_t *NowState) {
    uint64_t previousState = ((*NowState) >> 4) & 0xf;
    if (previousState == 3) {
        previousState = 0;
    }
    uint64_t r0_var2 = lcg::getSeed(position);
    uint64_t r3_var3 = previousState;
    uint64_t r2_var5 = r0_var2 & 0x1;
    uint64_t r0_var6 = r3_var3 << 0x1 & 0xFFFFFFFF;
    uint64_t r1_var7 = r0_var6 & 0xff;
    uint64_t r0_var8 = r3_var3 + 0x1;
    uint64_t r3_var9 = r1_var7 + r2_var5;
    previousState = static_cast<int>(r0_var8);
    uint64_t r3_var12 = r3_var9 & 0xff;
    (*NowState) &= ~0xf0;
    (*NowState) |= (previousState << 4);
    return static_cast<int>(r3_var12);
}

int BattleEmulator::CalculateMoreHealBase(const Player *players) {
    //ベホイミ
    double tmp1 = (players[0].HealPower - 200) * 0.5194;
    auto tmp2 = static_cast<int>(floor(tmp1));
    return 185 + tmp2;
}

int BattleEmulator::CalculateMidHealBase(const Player *players) {
    //ｂ
    double tmp1 = (players[0].HealPower - 100) * 0.2392;
    auto tmp2 = static_cast<int>(floor(tmp1));
    return 85 + tmp2;
}

void BattleEmulator::RecalculateBuff(Player *players) {
    // 定数の倍率を格納した配列
    // BuffLevel に +2 して配列インデックスに変換
    int index = players[0].BuffLevel + 2;

    // インデックスが範囲外でないかチェック（-2 <= BuffLevel <= 2 の範囲であることを確認）
    if (index >= 0 && index < 5) {
        constexpr double DEFMultipliers[] = {0.25, 0.5, 1.0, 1.5, 2.0};
        players[0].def = static_cast<int>(floor(players[0].defaultDEF * DEFMultipliers[index]));
    }

    int index1 = players[0].AtkBuffLevel + 2;
    if (index1 >= 0 && index1 < 5) {
        constexpr  double ATKMultipliers[] = {0.5, 0.75, 1.0, 1.25, 1.5};
        players[0].atk = static_cast<int>(floor(players[0].defaultATK * ATKMultipliers[index1]));
    }
    int index2 = players[0].SpeedLevel + 2;
    if (index >= 0 && index < 5) {
        constexpr double SPEMultipliers[] = {0.25, 0.5, 1.0, 1.5, 2.0};
        players[0].speed = static_cast<int>(floor(players[0].defaultSpeed * SPEMultipliers[index2]));
    }
}

void BattleEmulator::ProcessRage(int *position, int baseDamage, Player *players, int preEnemyHp) {
    const int maxHp = players[1].maxHp;
    const int afterHp = preEnemyHp - baseDamage;
    if (static_cast<int64_t>(afterHp) * 2 < maxHp) {
        if (static_cast<int64_t>(preEnemyHp) * 2 >= maxHp) {
            if (!players[1].rage) {
                (*position)++;
                (*position)++;
            } else {
                (*position)++;
            }
        } else {
            if (static_cast<int64_t>(afterHp) * 4 < maxHp) {
                if (static_cast<int64_t>(preEnemyHp) * 4 >= maxHp) {
                    if (!players[1].rage) {
                        (*position)++;
                        players[1].rage = true;
                        players[1].rageTurns = lcg::intRangeRand(position, 2, 4);
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
    0x44,
    0x3a,
    0x30,
    0x26,
    0x1b,
    0x11  // 239 + 17 = 256
};

constexpr std::array<int, 6> ids = {
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::CLAW_SLASH_A2,
    BattleEmulator::KAZAM,
    BattleEmulator::SWITCH_2B,
    BattleEmulator::WAR_CRY,
    BattleEmulator::ATTACK_ENEMY
};

static_assert(sum(ratios) == TABLE_MAX, "Ratio sum must be 256");

constexpr auto actionTable = makeProbabilityTable(ratios, ids);


int BattleEmulator::ProcessEnemyRandomAction2A(int *position) {
    //0x0208aca8
    int rnd = static_cast<int>(static_cast<uint32_t>(lcg::getTop32(position)) >> 24);
    return actionTable[rnd];
}


constexpr std::array<int, 6> ratios2 = {
    0x44,
    0x3a,
    0x30,
    0x26,
    0x1b,
    0x11  // 239 + 17 = 256
};

constexpr std::array<int, 6> ids2 = {
    BattleEmulator::CLAW_SLASH_B1,
    BattleEmulator::WAVE_OF_PANIC,
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::DARK_BREATH,
    BattleEmulator::SWITCH_2A,
    BattleEmulator::CLAW_SLASH
};

static_assert(sum(ratios) == TABLE_MAX, "Ratio sum must be 256");

constexpr auto actionTable2 = makeProbabilityTable(ratios2, ids2);


int BattleEmulator::ProcessEnemyRandomAction2B(int *position) {
    //0x0208aca8
    int rnd = static_cast<int>(static_cast<uint32_t>(lcg::getTop32(position)) >> 24);
    return actionTable2[rnd];
}