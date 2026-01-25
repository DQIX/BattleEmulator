//
// Created by owner on 2026/01/20.
//

#ifndef NEWDIRECTORY_ACTIONBRUTEFORCER_H
#define NEWDIRECTORY_ACTIONBRUTEFORCER_H
#include <cstdint>
#include <vector>
#include <array>

#include "BattleEmulator.h"
#include "Player.h"

struct ActionEntry {
    int action;

    bool (*condition)(const Player &);
};


constexpr ActionEntry ACTION_TABLE[] = {
    {
        BattleEmulator::ATTACK_ALLY, [](const Player &) { return true; },
    },
    {
        BattleEmulator::DRAGON_SLASH, [](const Player &) { return true; },
    },
    {
        BattleEmulator::DEFENCE, [](const Player &) { return true; },
    },
    {
        BattleEmulator::FLEE_ALLY, [](const Player &) { return true; },
    },
    {
        BattleEmulator::SPECIAL_ANTIDOTE,
        [](const Player &Ally) {
            return Ally.SpecialMedicineCount >= 1 &&
                   Ally.PoisonEnable;
        }
    },
    {
        BattleEmulator::HEAL,
        [](const Player &Ally) { return Ally.mp >= 2; },
    },
    {
        BattleEmulator::CRACK_ALLY,
        [](const Player &Ally) { return Ally.mp >= 3; },
    },
    {
        BattleEmulator::ACROBATIC_STAR,
        [](const Player &Ally) { return Ally.specialCharge == true && Ally.specialChargeTurn != 0; },
    },
    {
        BattleEmulator::SPECIAL_MEDICINE,
        [](const Player &Ally) {
            return Ally.SpecialMedicineCount >= 1 &&
                   !Ally.PoisonEnable;
        }
    },
    {
        BattleEmulator::WOOSH_ALLY,
        [](const Player &Ally) { return Ally.mp >= 3; }
    },
    {
        BattleEmulator::ACROBATIC_STAR,
        [](const Player &Ally) {
            return Ally.specialCharge &&
                   Ally.specialChargeTurn != 0;
        }
    },
};


// constexpr 整数累乗（コンパイル時計算）
constexpr int32_t ipow(int32_t base, int32_t exp) {
    int32_t r = 1;
    for (int32_t i = 0; i < exp; ++i) r *= base;
    return r;
}


namespace ActionBruteForcerConst {
    constexpr int32_t CONST_MAX_DEPTH = 5;
    constexpr int32_t ACTION_TABLE_SIZE = std::size(ACTION_TABLE);
    constexpr int32_t MAX_NODES = ipow(ACTION_TABLE_SIZE, CONST_MAX_DEPTH);
}


enum class TerminateReason {
    None,
    AllyDead,
    EnemyDead,
    HealEnemy,
};

struct Node {
    Player players[2];
    uint64_t nowState;
    int position;

    int actions[10];
    int depth;

    bool terminated;
    TerminateReason reason;
};



struct SearchResult {
    int firstAction{};
    int64_t score{};   // ★ int64_t
    int actions[10]{};
    int depth{};
    Player players[2];
    uint64_t nowState{};
    int position{};
    bool valid = false;
    bool isWin;
    bool isLose;
    int nodeId = -1;
    int parentIndex = -1;
    int fragOffset = -1;
    uint8_t fragLen = 0;     // 0..ActionBruteForcerConst::CONST_MAX_DEPTH
};

struct SearchOutput {
    int count = 0;
    const Node* nodes[ActionBruteForcerConst::MAX_NODES];
};

class ActionBruteForcer {
public:
    ActionBruteForcer();
    ~ActionBruteForcer();

    void Search(const Player *rootPlayers, uint64_t rootNowState, int rootPosition, SearchOutput &out);
    static int64_t EvaluateTerminal(const Node& n, int totalDepth);
private:
    Node* g_nodeBufA = nullptr;
    Node* g_nodeBufB = nullptr;
};



#endif //NEWDIRECTORY_ACTIONBRUTEFORCER_H
