//
// Created by owner on 2026/01/22.
//

#ifndef NEWDIRECTORY_BFS_H
#define NEWDIRECTORY_BFS_H
#include <cstdint>

#include "ActionBruteForcer.h"
#include "Player.h"


class BFS {
public:
    BFS(const Player* rp, uint64_t ns, int pos, int F);
    void Run();

    SearchResult *getBest();

private:
    struct Node {
        int count{};
        int index{};
        SearchResult children[10]{};
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

    Node nodes[6]{};
    SearchResult best[10]{};
};

#endif
