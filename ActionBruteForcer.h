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


struct SearchResult {
    int firstAction{};
    int score{};
    int actions[5]{};
    int depth{};
};

struct SimState { Player players[2]; uint64_t NowState{}; int position{}; int firstAction{}; };

std::vector<SearchResult> Search(
    const Player* rootPlayers,
    uint64_t rootNowState,
    int rootPosition,
    int F
);

class ActionBruteForcer {
public:
    static constexpr int ids = 7;

    static constexpr std::array<int, ids> TUNE_IDS = {
        BattleEmulator::ATTACK_ALLY,
        BattleEmulator::DRAGON_SLASH,
        BattleEmulator::DEFENCE,
        BattleEmulator::FLEE_ALLY,
        BattleEmulator::MEDICINAL_HERBS,
        BattleEmulator::HEAL,
        BattleEmulator::CRACK_ALLY,
    };

    static_assert(TUNE_IDS.size() == ids);

    static std::vector<::SearchResult> Search(const Player *rootPlayers, uint64_t rootNowState, int rootPosition, int F);
};


struct Node {
    Player players[2];
    uint64_t nowState{};
    int position{};
    uint8_t firstAction{};
};

constexpr int F = 3;
constexpr int MAX_WIDTH = ipow(F, ActionBruteForcer::TUNE_IDS.size()) + 100;

alignas(64) static Node layerA[MAX_WIDTH];
alignas(64) static Node layerB[MAX_WIDTH];






#endif //NEWDIRECTORY_ACTIONBRUTEFORCER_H