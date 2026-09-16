//
// Created by Owner on 2024/02/05.
//

#include <cstdint>
#include <iostream>
#include <cmath>
#include <array>
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

#if defined(RUBII)

constexpr int Ally_Level = 49;
constexpr double Ally_TensionTable[4] = {1.5, 2.5, 4.0, 6.0};
constexpr int shieldGuardP = 9; //盾ガード率 9%
constexpr int kaisinnP = 500;
constexpr int WooshSlashKaisinnP = 100;

#elif defined(NUSISAMA2)

constexpr int Ally_Level = 51;
constexpr double Ally_TensionTable[4] = {1.5, 2.5, 4.0, 6.0};
constexpr int shieldGuardP = 10; // みかがみの盾 10%
constexpr int kaisinnP = 500;
constexpr int WooshSlashKaisinnP = 100;

#endif

constexpr int determineTurn(const int level) {
    return (level >= 10 && level <= 24)
               ? 6
               : (level >= 25 && level <= 49)
                     ? 7
                     : (level >= 50 && level <= 74)
                           ? 8
                           : (level >= 75 && level <= 99)
                                 ? 9
                                 : 0;
}
constexpr int SpecialChargeTurns = determineTurn(Ally_Level);
constexpr int Ally_TensionLevel = 1 + static_cast<int>(Ally_Level / 10.0);
constexpr int DragonSlashKaisinnP = kaisinnP / 2;
constexpr int multithrust3KaisinnP = DragonSlashKaisinnP / 3;
constexpr int multithrust4KaisinnP = DragonSlashKaisinnP / 4;

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
        case BattleEmulator::TIDAL_WAVE:
            return "Tidal Wave";
        case BattleEmulator::MASSIVE_SWIPE:
            return "Massive Swipe";
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
        case MULTISLASH:
            return "Multi Slash";
        case FLAME_SLASH:
            return "Flame Slash";
        case KACRACKLE_SLASH:
            return "Kacrackle Slash";
        case HATCHET_MAN:
            return "Hatchet Man";
        case UPWARD_SLICE:
            return "Upward Slice";
        case INACTIVE_ALLY:
            return "inactive";
        default:
            return "Unknown Action";
    }
}

bool BattleEmulator::Main(int *position, int RunCount, const int32_t Gene[350], Player *players,
                         BattleResult* result,
                           uint64_t seed, const int eActions[350], const int damages[350], int mode,
                           uint64_t *NowState, const bool traceBoundaries) {
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
        auto defenseFlag = false;
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
        if ((*position) == 216) { //THIS DEBUG CODE!
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

        int enemyAction[2] = {0, 0};
        enemyAction[0] = ProcessNusisama2Action(position);
        if (enemyAction[0] == ATTACK_ENEMY || enemyAction[0] == CRITICAL_ATTACK) {
            (*position)++; // lr=0x02156874, max=2, single-target selection
        }
        (*position)++; // lr=0x0216139c, range=[3,4], first enemy action record
        (*position)++; // lr=0x021613b0, range=[6,8], first enemy action record
        (*position)++; // lr=0x02160d64, max=2, extra-action count
        enemyAction[1] = ProcessNusisama2Action(position);
        if (enemyAction[0] == enemyAction[1]) {
            if (enemyAction[1] == TIDAL_WAVE) {
                enemyAction[1] = ATTACK_ENEMY;
            } else if (enemyAction[1] == MASSIVE_SWIPE) {
                enemyAction[1] = TIDAL_WAVE;
            } else if (enemyAction[1] == CRITICAL_ATTACK) {
                enemyAction[1] = ATTACK_ENEMY;
            }
        }
        if (enemyAction[1] == ATTACK_ENEMY || enemyAction[1] == CRITICAL_ATTACK) {
            (*position)++; // lr=0x02156874, max=2, single-target selection
        }
        (*position)++; // lr=0x0216139c, range=[3,4], second enemy action record
        (*position)++; // lr=0x021613b0, range=[6,8], second enemy action record

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
        } else if (!players[0].paralysis && !players[0].inactive && actionTable == BattleEmulator::MERCURIAL_THRUST) {
            player0_has_initiative = true;
        }

        if (actionTable == DEFENCE) {
            players[0].defence = 0.5;
            defenseFlag = true;
        }
        if (actionTable == DEFENDING_CHAMPION) {
            //例え寝てる場合でもmpが減る
            players[0].defence = 0.1;
            players[0].mp -= 3; //後攻睡眠などにより大防御の行動ができない場合は事前に減る
            defenseFlag = true;
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
                for (const int counter : enemyAction) {
                    if (mode != -1 && mode != -2) {
                        int need = eActions[exCounter1++];
                        if (need == -1) {
                            startTurn = counterJ - 1;
                            return true;
                        }
                        if (need != counter) {
                            return false;
                        }
                    }

                    const auto c = counter;
                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "start FUN_02158dfc", *position);
                    (*position)++; // lr=0x021588ec, max=100, charm/みとれ check
                    (*position)++; // lr=0x02159b10, max=100, enemy pre-action status
                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "end FUN_02158dfc", *position);
                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "start FUN_021ebd9c_ct", *position);
                    basedamage = callAttackFun(c, position, players, 1, 0, NowState);
                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "end FUN_021ebd9c_ct", *position);

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
                                           ahp, tmpState, players[0].specialChargeTurn, players[0].mp, defenseFlag,
                                            1, players[1].mp);
                    } else if (mode != -1 && mode != -2) {
                        if (
                            c == ATTACK_ENEMY ||
                            c == MASSIVE_SWIPE ||
                            c == TIDAL_WAVE ||
                            c == CRITICAL_ATTACK
                        ) {
                            if (damages[exCounter] == -1) {
                                startTurn = counterJ - 1;
                                return true;
                            }
                            //                            int need = damages[exCounter++] - basedamage;
                            //                            if (std::abs(need) == 0) {
                            //                                return false;
                            //                            }
                            if (damages[exCounter++] != basedamage) {
                                return false;
                            }
                        }
                    }
                    if (c == BattleEmulator::MEDITATION) {
                        Player::heal(players[1], basedamage);
                    } else if (c == BattleEmulator::MERA_ZOMA && players[0].hasMagicMirror) {
                        Player::reduceHp(players[1], basedamage);
                    } else {
                        Player::reduceHp(players[0], basedamage);
                    }
                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "start FUN_021594bc", *position);
                    if (Player::isPlayerAlive(players[0]) && Player::isPlayerAlive(players[1])) {
                        (*position) += 1;
                    } else {
                        DEBUG_TRACE_BOUNDARY(traceBoundaries, "end FUN_021594bc", *position);
                        break;
                    }
                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "end FUN_021594bc", *position);
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
                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "start FUN_02158dfc", *position);
                    if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                        (*position) += 1;
                    } else if (players[0].inactive) {
                        players[0].inactive = false;
                        action = INACTIVE_ALLY;
                        players[0].defence = 1.0;
                        (*position)++;
                    }else if (players[0].paralysis) {
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


                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "end FUN_02158dfc", *position);
                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "start FUN_021ebd9c_ct", *position);
                    basedamage = callAttackFun(action, position, players, 0, 1, NowState);
                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "end FUN_021ebd9c_ct", *position);
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
                                           tmpState, players[0].specialChargeTurn, players[0].mp, defenseFlag,
                                           0, players[0].mp);
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
                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "start FUN_021594bc", *position);
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
                    DEBUG_TRACE_BOUNDARY(traceBoundaries, "end FUN_021594bc", *position);
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
                                           tmpState, players[0].specialChargeTurn, players[0].mp, defenseFlag,
                                           0, players[0].mp);
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
        if ((*position) == 557) {
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
    for (int j = 0; j < 2; ++j) {
        preHP[j] = players[j].hp;
    }
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
        case HATCHET_MAN:
            players[attacker].mp -= 8;
            (*position) += 2;
            (*position)++; //0x021ec6f8
            (*position)++; //0x02158584 会心
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                //TODO
                if (lcg::getPercent(position, 100) < 2) {
                    //0x021587b0 本物みかわし
                    kaihi = true;
                }
                if (!kaihi && lcg::getPercent(position, 100) < shieldGuardP) {
                    //盾ガード 0x021586fc
                    tate = true;
                }
            }
            (*position)++; //0x02157f58 ニセ回避
            FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (lcg::getPercent(position, 2) == 0) {
                kaisinn = true;
            } else {
                kaisinn = false;
            }

            if (kaisinn) {
                tmp = players[attacker].defaultATK * lcg::floatRand(position, 0.9500, 1.0500);
                tmp = tmp * players[defender].defence;
                baseDamage = static_cast<int>(floor(tmp));
                if (tate || kaihi) {
                    baseDamage = 0;
                }

                if (baseDamage != 0) {
                    (*position)++; //0x02158ac4 目を覚ました
                    (*position)++; //0x021e54fc 不明
                }
                process7A8(position, baseDamage, players, defender);
                resetCombo(NowState);
            } else {
                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive && !players[0].
                    specialCharge) {
                    (*position)++; //0x021ed7a8
                    }
                baseDamage = 0;
            }
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

            if (!players[0].specialCharge && !players[0].sleeping && !players[0].paralysis) {
                (*position)++; //0x021ed7a8 必殺チャージ(敵) 0%
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
            if (!players[0].specialCharge && !players[0].sleeping && !players[0].paralysis) {
                (*position)++; //0x021ed7a8 必殺チャージ(敵) 0%
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    if (lcg::getPercent(position, 0x2710) < multithrust4KaisinnP) {
                        kaisinn = true;
                        hasKaisinn = true;
                    }
                } else {
                    if (lcg::getPercent(position, 0x2710) < multithrust3KaisinnP) {
                        kaisinn = true;
                        hasKaisinn = true;
                    }
                }
                (*position)++; // lr=0x021587b0, max=100, ぬしさま2回避0%（量子化）
                (*position)++; // lr=0x021586fc, max=100, ぬしさま2盾ガード0%（量子化）
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
#if !defined(NUSISAMA2)
                tmp = tmp * 1.25 * 1.1; //1.25倍は雷属性になってるから
#endif
                baseDamage = static_cast<int>(floor(tmp));

                if (!kaihi) {
                    ProcessRage(position, baseDamage, players);
                    (*position)++; //目を覚ました
                    (*position)++; //不明 0x021e54fc
                } else {
                    baseDamage = 0;
                }

                preHP[1] = std::max(0, preHP[1] - baseDamage);
                totalDamage += baseDamage;
                if (preHP[1] <= 0) {
                    return totalDamage;
                }
            }
            if (hasKaisinn) {
                (*position) += 2;
            }
            if (preHP[1] > 0) {
                if (!players[attacker].specialCharge && lcg::getPercent(position, 100) < 1) {
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                ProcessRage(position, baseDamage, players);
            } else {
                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
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
                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
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
                        players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
                }
            }
            players[0].hasMagicMirror = true;
            players[0].MagicMirrorTurn = 6;
            resetCombo(NowState);
            baseDamage = 0;
            break;
        case BattleEmulator::MASSIVE_SWIPE:
            // DQ9 0x05e: one-target battle still uses the group-attack path.
            (*position) += 2; // lr=0x021613b0 range[6,8], lr=0x02075628 max=3
            (*position)++; // lr=0x02158584, max=10000, critical threshold=0
            (*position)++; // lr=0x021ec6f8, max=100
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 2) { // lr=0x021587b0, max=100
                    kaihi = true;
                }
                if (!kaihi && lcg::getPercent(position, 100) < shieldGuardP) { // lr=0x021586fc, max=100
                    tate = true;
                }
            }
            (*position)++; // lr=0x02157f58, max=100, avoidance stage
            // lr=0x02075724 float[-4.296875,4.296875], then lr=0x02075738 float[-1,1].
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
            if (kaihi || tate) {
                baseDamage = 0;
            } else {
                if (baseDamage == 0) {
                    baseDamage = lcg::getPercent(position, 2); // lr=0x021e81a0, max=2
                }
                tmp = baseDamage;
                if (players[defender].TensionLevel == 4) {
                    tmp *= 0.5;
                }
                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                    tmp *= players[defender].defence;
                }
                baseDamage = static_cast<int>(floor(tmp));
            }
            if (baseDamage != 0) {
                (*position)++; // lr=0x02158ac4, max=100
                (*position)++; // lr=0x021e54fc, max=100, 武器特殊効果（0ダメージ時は消費なし）
                players[defender].sleeping = false;
                players[defender].sleepingTurn = -1;
            }
            // process7A8: lr=0x021ed7a8, max=100（0ダメージでも1回消費）。
            process7A8(position, baseDamage, players, defender);
            resetCombo(NowState);
            break;
        case BattleEmulator::TIDAL_WAVE:
            // Selector 43 (DQ9 0x225): min(2 * power + 30, 150), power=Lv25,
            // then multiply by 1 + uniform[-0.1, 0.1].
            (*position) += 2; // lr=0x021613b0 range[6,8], lr=0x02075628 max=3
            (*position)++; // lr=0x02158584, max=10000, critical threshold=0
            (*position)++; // lr=0x021ec6f8, max=100
            (*position)++; // lr=0x02157f58, max=100, avoidance stage
            // lr=0x02075724 float[-4.296875,4.296875], then lr=0x02075738 float[-1,1].
            baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
            tmp = 80.0 * (1.0 + lcg::floatRand(position, -0.1, 0.1)); // lr=0x021d9e74
            if (players[defender].TensionLevel == 4) {
                tmp *= 0.5;
            }
            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                tmp *= players[defender].defence;
            }
            baseDamage = static_cast<int>(floor(tmp));
            (*position)++; // lr=0x02158ac4, max=100（0ダメージでも消費）
            if (baseDamage != 0) {
                (*position)++; // lr=0x021e54fc, max=100, 武器特殊効果（0ダメージ時は消費なし）
                players[defender].sleeping = false;
                players[defender].sleepingTurn = -1;
            }
            // process7A8: lr=0x021ed7a8, max=100.
            process7A8(position, baseDamage, players, defender);
            resetCombo(NowState);
            break;
        case BattleEmulator::CRITICAL_ATTACK:
            (*position) += 2; // lr=0x021613b0 range[6,8], lr=0x02075628 max=3
            (*position)++; // lr=0x021ec6f8, max=100

            if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                if (lcg::getPercent(position, 100) < 2) { // lr=0x021587b0, max=100
                    kaihi = true;
                }
                if (!kaihi && lcg::getPercent(position, 100) < shieldGuardP) { // lr=0x021586fc, max=100
                    tate = true;
                }
            }
            (*position)++; // lr=0x02157f58, max=100, avoidance stage

            baseDamage = static_cast<int>(floor(players[1].defaultATK * lcg::floatRand(position, 0.8500, 0.9500))); // lr=0x021d9464

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
                    (*position)++; // lr=0x02158ac4, max=100, wake-up path
                    (*position)++; // lr=0x021e54fc, max=100, 武器特殊効果
                }
                if (players[defender].TensionLevel == 4) {
                    // スーパーハイテンション中は受けるダメージを半減する。
                    tmp = baseDamage * 0.5;
                } else {
                    tmp = baseDamage;
                }
                if (!players[0].paralysis && !players[0].sleeping && !players[0].inactive) {
                    tmp *= players[defender].defence;
                }
                baseDamage = static_cast<int>(floor(tmp));

                // process7A8: lr=0x021ed7a8, max=100.
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

            if (!players[0].specialCharge && !players[0].sleeping && !players[0].paralysis) {
                (*position)++; //0x021ed7a8 必殺チャージ(敵) 0%
                if (lcg::getPercent(position, 100) < 1) {
                    //0x021edaf4
                    players[attacker].specialCharge = true;
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
        case MULTISLASH:
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
                        tmp = baseDamage * 0.5;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
        case FLAME_SLASH:
        case KACRACKLE_SLASH:
        case UPWARD_SLICE:
            (*position) += 2;
            (*position)++; // アクロバットスターとか

            (*position)++; //会心
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
                if ((Id & 0xffff) == FLAME_SLASH) {
                    tmp = floor(tmp * 1.2);
                    tmp = Equipments::applyDamageReduction(tmp, Attribute::Fire);
                } else if ((Id & 0xffff) == KACRACKLE_SLASH) {
                    tmp = floor(tmp * 1.2);
                    tmp = Equipments::applyDamageReduction(tmp, Attribute::Ice);
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

                if (baseDamage != 0 && (Id & 0xffff) == UPWARD_SLICE) {
                    (*position)++; // 0x021e34e8??
                }

                if (baseDamage != 0) {
                    tmp = static_cast<double>(baseDamage);
                    tmp = processCombo(Id & 0xffff, tmp, NowState);
                    baseDamage = static_cast<int>(floor(tmp));

                    if ((Id & 0xffff) == UPWARD_SLICE && !players[defender].inactive) {
                        players[defender].inactive = true;
                    }
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
            baseDamage = FUN_021e8458_typeD(position, 5, CalculateHealBase(players));
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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
            (*position)++; // lr=0x021587b0, max=100, ぬしさま2回避0%（量子化）
            (*position)++; // lr=0x021586fc, max=100, ぬしさま2盾ガード0%（量子化）

            (*position)++; // lr=0x02157f58, max=100, avoidance stage（量子化）
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

#if !defined(NUSISAMA2)
            tmp *= 1.25 * 1.1; //旧ブランチの雷属性・弱点補正
#endif
            baseDamage = static_cast<int>(floor(tmp));

            if (!kaihi) {
                ProcessRage(position, baseDamage, players);
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
                    players[attacker].specialChargeTurn = SpecialChargeTurns;
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


int BattleEmulator::ProcessEnemyRandomAction44(int *position) {
    //0x0208aca8
    const int patternTable[6] = {68, 58, 48, 38, 27, 17};
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

namespace {
constexpr std::size_t kNusisama2TableMax = 256;

template<std::size_t N>
constexpr std::array<int, kNusisama2TableMax> makeNusisama2ProbabilityTable(
    const std::array<int, N>& ratios, const std::array<int, N>& ids) {
    std::array<int, kNusisama2TableMax> table{};
    std::size_t index = 0;
    for (std::size_t i = 0; i < N; ++i) {
        for (int j = 0; j < ratios[i]; ++j) {
            table[index++] = ids[i];
        }
    }
    return table;
}

template<std::size_t N>
constexpr int sumNusisama2Ratios(const std::array<int, N>& values) {
    int result = 0;
    for (const int value : values) result += value;
    return result;
}

constexpr std::array<int, 6> kNusisama2Ratios = {68, 58, 48, 38, 27, 17};
constexpr std::array<int, 6> kNusisama2Actions = {
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::TIDAL_WAVE,
    BattleEmulator::MASSIVE_SWIPE,
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::CRITICAL_ATTACK,
};
static_assert(sumNusisama2Ratios(kNusisama2Ratios) == kNusisama2TableMax);
constexpr auto kNusisama2ActionTable =
    makeNusisama2ProbabilityTable(kNusisama2Ratios, kNusisama2Actions);
}

int BattleEmulator::ProcessNusisama2Action(int *position) {
    const int roll = static_cast<int>(static_cast<uint32_t>(lcg::getTop32(position)) >> 24); // lr=0x0208aca8, max=256
    return kNusisama2ActionTable[roll];
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

int BattleEmulator::CalculateHealBase(const Player *players) {
    double tmp1 = (players[0].HealPower - 50) * 0.1317;
    auto tmp2 = static_cast<int>(floor(tmp1));
    return 35 + tmp2;
}

int BattleEmulator::CalculateMidHealBase(const Player *players) {
    //ｂ
    double tmp1 = (players[0].HealPower - 100) * 0.2392;
    auto tmp2 = static_cast<int>(floor(tmp1));
    return 85 + tmp2;
}

void BattleEmulator::RecalculateBuff(Player *players) {
    // 定数の倍率を格納した配列
    const double ATKMultipliers[] = {0.5, 0.75, 1.0, 1.25, 1.5};
    const double DEFMultipliers[] = {0.25, 0.5, 1.0, 1.5, 2.0};

    // BuffLevel に +2 して配列インデックスに変換
    int index = players[0].BuffLevel + 2;

    // インデックスが範囲外でないかチェック（-2 <= BuffLevel <= 2 の範囲であることを確認）
    if (index >= 0 && index < 5) {
        players[0].def = static_cast<int>(floor(players[0].defaultDEF * DEFMultipliers[index]));
    }

    int index1 = players[0].AtkBuffLevel + 2;
    if (index >= 0 && index < 5) {
        players[0].atk = static_cast<int>(floor(players[0].defaultATK * ATKMultipliers[index1]));
    }
}

void BattleEmulator::ProcessRage(int *position, int baseDamage, const Player *players) {
    auto percent1 = FUN_021dbc04(preHP[1] - baseDamage, players[1].maxHp);
    if (percent1 < 0.5) {
        double percent = FUN_021dbc04(preHP[1], players[1].maxHp);
        if (percent >= 0.5) {
            if (!players[1].rage) {
                (*position)++;
                (*position)++;
            } else {
                (*position)++;
            }
        } else {
            if (percent1 < 0.25) {
                if (percent >= 0.25) {
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
