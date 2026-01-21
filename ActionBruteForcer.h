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
    int64_t score{};   // ★ int64_t
    int actions[5]{};
    int depth{};
    Player players[2];
    uint64_t nowState{};
    int position{};
};


std::ostream& operator<<(std::ostream& os, const SearchResult& r);

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

    static void Search(const Player *rootPlayers, uint64_t rootNowState, int rootPosition, int F, bool isFirstExec, SearchResult *best);

private:
    static inline void tryInsertBest(
        SearchResult best[10],
        int& bestCount,
        int& worstIdx,
        int64_t& worstScore,
        const SearchResult& cand
    ) {
        // まだ空きがある
        if (bestCount < 10) {
            best[bestCount] = cand;

            if (bestCount == 0 || cand.score > worstScore) {
                worstScore = cand.score;
                worstIdx = bestCount;
            }

            ++bestCount;
            return;
        }

        // 最悪（最大）より悪いなら捨てる
        if (cand.score >= worstScore) {
            return;
        }

        // 最悪を差し替え
        best[worstIdx] = cand;

        // 新しい最悪を線形探索
        worstScore = best[0].score;
        worstIdx = 0;
        for (int i = 1; i < 10; ++i) {
            if (best[i].score > worstScore) {
                worstScore = best[i].score;
                worstIdx = i;
            }
        }
    }
};

#endif //NEWDIRECTORY_ACTIONBRUTEFORCER_H