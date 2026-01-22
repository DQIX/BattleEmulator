//
// Created by owner on 2026/01/22.
//

#ifndef NEWDIRECTORY_BFS_H
#define NEWDIRECTORY_BFS_H
#include <cstdint>

#include "ActionBruteForcer.h"
#include "Player.h"

struct ResultPlan {
    int depth{};
    int actions[350]{};
};

class BFS {
public:
    BFS(const Player* rp, uint64_t ns, int pos, int F);

    void buildPlan(int leafNode, int leafChild, ResultPlan &out) const;

    void Run();

    ResultPlan *getBest();

private:
    struct Node {
        int count{};
        int index{};

        SearchResult children[10];

        int parentNode{-1};   // 親ノードの index
        int parentChild{-1};  // 親ノードの children の何番目か
    };



    int generateActions(
        const Player players[2],
        uint64_t nowState,
        int position,
        SearchResult* outChildren,
        int depth
    );

    void evaluate_and_update_best(const Node& n);


private:
    int maxDepth{};
    Player rootPlayers[2]{};
    uint64_t rootNowState{};
    int rootPosition{};

    Node nodes[10]{};
    ResultPlan best[10]{};
};

#endif
