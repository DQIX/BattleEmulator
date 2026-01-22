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



std::ostream& operator<<(std::ostream& os, const SearchResult& r);

struct SimState { Player players[2]; uint64_t NowState{}; int position{}; int firstAction{}; };



class ActionBruteForcer {
public:
    static constexpr int ids = 7;

    static std::vector<SearchResult> Search(const Player *rootPlayers, uint64_t rootNowState, int rootPosition, int F, bool isFirstExec);
};

#endif //NEWDIRECTORY_ACTIONBRUTEFORCER_H