#include "BFS.h"

BFS::BFS(const Player* rp, uint64_t ns, int pos, int F)
    : maxDepth(F), rootNowState(ns), rootPosition(pos)
{
    rootPlayers[0] = rp[0];
    rootPlayers[1] = rp[1];
}

int BFS::generateActions(
    const Player players[2],
    uint64_t nowState,
    int position,
    SearchResult* outChildren,
    int depth
) {
    ActionBruteForcer::Search(
        players,
        nowState,
        position,
        5,
        depth == 0,
        outChildren
    );
    return 10;
}

void BFS::evaluate_and_update_best(const Node& n) {
    // 仮：何もしない
}

void BFS::Run() {
    int depth = 0;
    int bestCount = 0;

    nodes[0].index = 0;
    nodes[0].count = generateActions(
        rootPlayers,
        rootNowState,
        rootPosition,
        nodes[0].children,
        depth
    );

    while (true) {
        Node& n = nodes[depth];
        if (depth == maxDepth) {
            goto backtrack;
        }

        for (int i = 0; i < n.count; ++i) {
            if (n.children[i].valid && n.children[i].players[1].hp <= 0) {
                best[bestCount++] = n.children[i]; // 最初に見つけたものが最適
                // ここで探索終了フラグを立ててもよい
            }
        }
        if (bestCount == 10) {
            break;
        }

        if (n.index < n.count) {
            Node& next = nodes[depth + 1];
            next.index = 0;

            const SearchResult& cur = n.children[n.index];

            if (!cur.valid) {
                n.index++;
                continue;
            }

            next.count = generateActions(
                cur.players,
                cur.nowState,
                cur.position,
                next.children,
                depth + 1
            );

            depth++;
            n.index++;
            continue;
        }

        backtrack:
                if (depth == 0) break;
        depth--;
    }
}

SearchResult* BFS::getBest() {
    return best; // 配列は先頭要素へのポインタに暗黙変換される
}

