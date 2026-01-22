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
    int cost;

    bool (*condition)(const Player &);
};

constexpr ActionEntry ACTION_TABLE[] = {
    {
        BattleEmulator::ATTACK_ALLY, 0, [](const Player &) { return true; },
    },
    {
        BattleEmulator::DRAGON_SLASH, 0, [](const Player &) { return true; },
    },
    {
        BattleEmulator::DEFENCE, 30, [](const Player &) { return true; },
    },
    {
        BattleEmulator::FLEE_ALLY, 1,[](const Player &) { return true; },
    },
    {
        BattleEmulator::MEDICINAL_HERBS, 100,
        [](const Player &Ally) { return Ally.medicinal_herbs_count >= 1; },
    },
    {
        BattleEmulator::HEAL, 100,
        [](const Player &Ally) { return Ally.mp >= 2; },
    },
    {
        BattleEmulator::CRACK_ALLY, 30,
        [](const Player &Ally) { return Ally.mp >= 3; },
    }
};

// constexpr 整数累乗（コンパイル時計算）
constexpr std::size_t ipow(std::size_t base, std::size_t exp) {
    std::size_t r = 1;
    for (std::size_t i = 0; i < exp; ++i) r *= base;
    return r;
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
};

struct SimState { Player players[2]; uint64_t NowState{}; int position{}; int firstAction{}; };

class ActionBruteForcer {
public:
    static constexpr int CONST_MAX_DEPTH = 4;
    static constexpr int ACTION_TABLE_SIZE = std::size(ACTION_TABLE);
    static constexpr int MAX_NODES = ipow(ACTION_TABLE_SIZE, CONST_MAX_DEPTH);

    static Node current[MAX_NODES];
    static Node next[MAX_NODES];
    static SearchResult results[MAX_NODES];

    static std::vector<SearchResult> Search(const Player *rootPlayers, uint64_t rootNowState, int rootPosition, bool isFirstExec);
};

#endif //NEWDIRECTORY_ACTIONBRUTEFORCER_H