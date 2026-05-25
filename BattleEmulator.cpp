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

std::string BattleEmulator::getActionName(int actionId) {
    switch (actionId) {
        case BattleEmulator::ATTACK_ENEMY:
            return "Attack";
        case BattleEmulator::MEDICINAL_HERBS:
            return "Medicinal Herbs";
        case BattleEmulator::ATTACK_ALLY:
            return "Attack";
        case BattleEmulator::HEAL:
            return "Heal";
        case BattleEmulator::DEFENCE:
            return "Defence";
        case BattleEmulator::MAGIC_MIRROR:
            return "magic mirror";
        case BattleEmulator::FLEE_ALLY:
            return "Flee";
        case BattleEmulator::RUBBLE:
            return "Rubble";
        default:
            return "Unknown Action";
    }
}

bool BattleEmulator::Main(int *position, int RunCount, const int32_t Gene[350], Player *players,
                          BattleResult* result,
                           uint64_t seed, const int eActions[350], const int damages[350], int mode,
                           uint64_t *NowState) {
    (void) seed;
    (void) eActions;
    int genePosition = 0;
    int exCounter = 0;
    uint64_t nowState = *NowState;
    const bool recordResult = mode == -1;
    const bool validateDamage = mode != -1 && mode != -2;

    auto startPos = static_cast<int>((nowState >> 12) & 0xfffff);
    if (startPos != 0) {
        startPos++;
        RunCount += startPos;
    } else {
        startPos = 1;
        RunCount++;
    }
    for (int counterJ = startPos; counterJ < RunCount; ++counterJ) {
        ++threadTurnProcessed;
        if (genePosition != -1) {
            genePosition = counterJ - 1;
        }
        //現在ターンを保存
        nowState = (nowState & ~0xFFFFF000ULL) | (static_cast<uint64_t>(counterJ) << 12ULL);

#ifdef DEBUG2
        DEBUG_COUT2((*position));
        //THIS DEBUG CODE!
        if ((*position) == 187) { //THIS DEBUG CODE!
            std::cout << "!!" << std::endl;
        }
#endif
        int ehp = players[1].hp;
        int ahp = players[0].hp;

        int32_t turnActions[3] = {0, 0, 0};
        int turnActionPosition = 0;
        double speed0 = players[0].speed * lcg::floatRand051_1(position);
        double speed1 = players[1].speed * lcg::floatRand051_1(position);

        // 素早さを比較
        const bool player0_has_initiative = speed0 > speed1;

        int c = ProcessEnemyRandomAction2b(position);
        int32_t action = -1;

        (*position) += 3;
        if (c == ATTACK_ENEMY) {
            (*position)++;
        }

        if (genePosition != -1) {
            const int32_t gene = Gene[genePosition];
            if (gene == 0 || gene == -1) {
                genePosition = -1;
                //throw std::invalid_argument("GenePosition is invalid");
            } else {
                action = gene;
            }
        }
        if (action == -1) {
            action = ATTACK_ALLY;
        }
        const bool isDefending = action == DEFENCE;
        // ソートされた結果を出力
        for (int t = 0; t < 2; ++t) {
            if (players[1].hp == 0) {
                break;
            }
            if (players[0].hp == 0) {
                break;
            }

            int basedamage = 0;
            if ((t == 0 && !player0_has_initiative) || (t == 1 && player0_has_initiative)) {
                //--------start_FUN_02158dfc-------
                (*position)++;//0x021588ec
                (*position)++;//0x02159b10
                //--------end_FUN_02158dfc-------
                turnActions[turnActionPosition++] = c;
                basedamage = callAttackFun(c, position, players, 1, 0, &nowState, isDefending);

                if (recordResult) {
                    BattleResult::add(result, c, basedamage, true, counterJ - 1,
                                      player0_has_initiative, ehp,
                                      ahp, players[0].mp);
                } else if (validateDamage) {
                    if (
                        c == ATTACK_ENEMY ||
                        c == RUBBLE
                    ) {
                        if (damages[exCounter] == -1) {
                            startTurn = counterJ - 1;
                            *NowState = nowState;
                            return true;
                        }
                        if (damages[exCounter++] != basedamage) {
                            *NowState = nowState;
                            return false;
                        }
                    }
                }
                if (c == BattleEmulator::MEDITATION) {
                    Player::heal(players[1], basedamage);
                } else {
                    Player::reduceHp(players[0], basedamage);
                }
                //--------start_FUN_021594bc-------
                if (players[0].hp != 0 && players[1].hp != 0) {
                    (*position) += 1;
                } else {
                    break;
                }
                //--------end_FUN_021594bc-------
            } else {
                //--------start_FUN_02158dfc-------
                (*position) += 1;
                //--------end_FUN_02158dfc-------
                turnActions[turnActionPosition++] = action;
                basedamage = callAttackFun(action, position, players, 0, 1, &nowState, isDefending);
                if (recordResult) {
                    BattleResult::add(result, action, basedamage, false, counterJ - 1,
                                      player0_has_initiative, ehp, ahp,players[0].mp);
                }
                if (action == HEAL || action == MEDICINAL_HERBS) {
                    Player::heal(players[0], basedamage);
                } else {
                    Player::reduceHp(players[1], basedamage);

                    if (validateDamage) {
                        if (action == ATTACK_ALLY) {
                            if (damages[exCounter] == -1) {
                                startTurn = counterJ - 1;
                                *NowState = nowState;
                                return true;
                            }
                            //int need = ;
                            if (damages[exCounter++] != basedamage) {
                                *NowState = nowState;
                                return false;
                            }
                        }
                    }
                }
                //--------start_FUN_021594bc-------
                if (players[0].hp != 0 && players[1].hp != 0) {
                    (*position) += 1;
                    //TODO: 順序調べる
                }
            }
            //--------end_FUN_021594bc-------
        }
        if (players[0].hp != 0 && players[1].hp != 0) {
            (*position) += 1;
        }
        camera::Main(position, turnActions, &nowState, player0_has_initiative);

        if (players[1].hp == 0) {
            *NowState = nowState;
            return false;
        }
        if (players[0].hp == 0) {
            *NowState = nowState;
            return false;
        }

        //Player::heal(players[0], 25);
    }
    if (mode != -1 && mode != -2) {
        startTurn = RunCount - 2;
        *NowState = nowState;
        return true;
    } else {
        *NowState = nowState;
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

int BattleEmulator::callAttackFun(int32_t Id, int *position, Player *players, int attacker, int defender,
                                  uint64_t *NowState, bool isDefending) {
    const int preDefenderHp = players[defender].hp;
    int baseDamage = 0;
    switch (Id & 0xffff) {
        // case BattleEmulator::MEDICINAL_HERBS:
        //     (*position) += 2;
        //     (*position)++; // 関係ない
        //     (*position)++; // 会心判定
        //     (*position)++; // 回避
        //     baseDamage = FUN_021e8458_typeC(position, 35.0, 35.0, 5.0);
        //     (*position)++; // 不明
        //     break;
        case BattleEmulator::DEFENCE:
            (*position) += 2;
            (*position)++; //関係ない
            (*position)++; //会心
            (*position)++; //回避
            FUN_0207564c(position, players[attacker].atk, players[attacker].def);
            baseDamage = 0;
            break;
        case BattleEmulator::ATTACK_ENEMY:
            {
                bool kaihi = false;
                (*position) += 2;
                (*position)++; // アクロバットスターとか

                (*position)++; //会心
                if (lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }
                (*position)++;//盾
                (*position)++; //回避

                baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);

                if (kaihi) {
                    baseDamage = 0;
                } else {
                    if (isDefending) {
                        baseDamage >>= 1;
                    }

                    if (baseDamage != 0) {
                        (*position)++; //目を覚ました
                        (*position)++; //不明
                    }
                }

                break;
            }
        case BattleEmulator::HEAL:
            {
                double tmp = 0;
                bool kaisinn = false;
                players[attacker].mp -= 2;
                (*position) += 2;
                (*position)++; //関係ない
                if (lcg::getPercent(position, 0x2710) < 100) {
                    kaisinn = true;
                }
                (*position)++; //回避
                baseDamage = FUN_021e8458_typeD(position, 5, 35);
                if (kaisinn) {
                    tmp = baseDamage * lcg::floatRand(position, 1.5, 2.0); //TODO
                } else {
                    tmp = baseDamage;
                }
                baseDamage = static_cast<int>(floor(tmp));
                (*position)++; //不明
                (*position)++; //関係ない
                (*position)++; //関係ない
                if (kaisinn) {
                    (*position)++; //会心時特殊処理　0x021e54fc
                    (*position)++; //会心時特殊処理　0x021eb8c8
                }
                break;
            }
        case BattleEmulator::ATTACK_ALLY:
            {
                bool kaisinn = false, kaihi = false;
                (*position) += 2;
                (*position)++;
                //会心
                if (lcg::getPercent(position, 0x2710) < 200) {
                    kaisinn = true;
                }

                (*position)++; //みかわし(相手)
                (*position)++; //盾ガード(幼女は盾を持っていないので0%)
                (*position)++; //回避

                if (kaisinn) {
                    //0x020759ec
                    double tmp =  players[attacker].atk * lcg::floatRand(position, 0.95, 1.05);
                    baseDamage = static_cast<int>((tmp));//切り捨て
                } else {
                    baseDamage = FUN_0207564c(position, players[attacker].atk, players[defender].def);
                }

                if (!kaihi) {
                    ProcessRage(position, baseDamage, players, preDefenderHp);
                    (*position)++; //目を覚ました
                    (*position)++; //不明
                } else {
                    baseDamage = 0;
                }
                if (kaisinn) {
                    (*position)++; //会心時特殊処理　0x021e54fc
                    (*position)++; //会心時特殊処理　0x021eb8c8
                }
                break;
            }
        case RUBBLE:
            {
                bool kaihi = false;
                (*position) += 2;
                (*position)++;//会心
                (*position)++;//?
                if (lcg::getPercent(position, 100) < 2) {
                    kaihi = true;
                }else{
                    (*position)++;//盾
                }
                (*position)++;//回避
                baseDamage = FUN_021e8458_typeD(position, 1, 6);

                if (kaihi) {
                    //(*position)++;//0x021ed7a8
                    baseDamage = 0;
                } else {
                    (*position)++;//目を覚ました
                    (*position)++;//不明
                }
                if (isDefending) {
                    baseDamage >>= 1;
                }
                break;
            }
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

void BattleEmulator::ProcessRage(int *position, int baseDamage, const Player *players, int preEnemyHp) {
    const double maxHp = players[1].maxHp;
    const int afterHp = preEnemyHp - baseDamage;
    if (static_cast<double>(afterHp) < maxHp * 0.5) {
        if (static_cast<double>(preEnemyHp) >= maxHp * 0.5) {
            (*position)++;
            (*position)++;
        } else {
            if (static_cast<double>(afterHp) < maxHp * 0.25) {
                if (static_cast<double>(preEnemyHp) >= maxHp * 0.25) {
                    (*position)++;
                    (*position)++;
                }
            }
        }
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
    0x2b,
    0x2a,
    0x2b,
    0x2b,
    0x2a,
    0x2b  // 239 + 17 = 256
};

constexpr std::array<int, 6> ids = {
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::RUBBLE,
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::RUBBLE,
    BattleEmulator::ATTACK_ENEMY,
    BattleEmulator::ATTACK_ENEMY
};

static_assert(sum(ratios) == TABLE_MAX, "Ratio sum must be 256");

constexpr auto actionTable = makeProbabilityTable(ratios, ids);


int BattleEmulator::ProcessEnemyRandomAction2b(int *position) {
    //0x0208aca8
    int rnd = static_cast<int>(static_cast<uint32_t>(lcg::getTop32(position)) >> 24);
    return actionTable[rnd];
}
