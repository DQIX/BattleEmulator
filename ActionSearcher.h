#ifndef NEWDIRECTORY_ACTIONSEARCHER_H
#define NEWDIRECTORY_ACTIONSEARCHER_H

#include <cstdint>
#include "ActionBruteForcer.h"
#include "Player.h"

// score は「小さいほど良い（minimize）」
// 敵撃破は極小値になる

struct SearchPlan {
    int depth{};
    int actions[350]{};
};

class ActionSearcher {
public:
    static constexpr int MAX_LAYER = 1024;
    static constexpr int BEST_LIMIT = 10;
    static constexpr int NODE_EXPAND_LIMIT = 15;

public:
    ActionSearcher(
        const Player* rootPlayers,
        uint64_t rootNowState,
        int rootPosition,
        int maxDepth
    );

    void Run();
    int getBest(SearchPlan* out) const;

private:
    int expandNode(
        const SearchResult& cur,
        SearchResult* out,
        int maxOut,
        int depth
    );

    static int selectTopK(
        SearchResult* src,
        int srcCount,
        SearchResult* dst,
        int K
    );

    static int beamWidthForDepth(int depth);

private:
    // root
    Player rootPlayers_[2];
    uint64_t rootNowState_;
    int rootPosition_;
    int maxDepth_;

    // buffers（スタック上）
    SearchResult bufA_[MAX_LAYER];
    SearchResult bufB_[MAX_LAYER];

    SearchResult* cur_;
    SearchResult* next_;
    int curCount_;
    int nextCount_;

    // success
    SearchPlan best_[BEST_LIMIT];
    int bestCount_;
};

#endif
