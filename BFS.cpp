#include "BFS.h"
#include <cstring>

BFS::BFS(const Player* rp, uint64_t ns, int pos, int F)
    : maxDepth(F), rootNowState(ns), rootPosition(pos)
{
    rootPlayers[0] = rp[0];
    rootPlayers[1] = rp[1];
}

void BFS::buildPlan(
    int leafNode,
    int leafChild,
    ResultPlan& out
) const {
    int stackNodes[400];
    int stackChildren[400];
    int sp = 0;

    int n = leafNode;
    int c = leafChild;

    // 親を遡ってスタックに積む
    while (n >= 0) {
        stackNodes[sp] = n;
        stackChildren[sp] = c;
        sp++;

        c = nodes[n].parentChild;
        n = nodes[n].parentNode;
    }

    // 正順で memcpy append
    int offset = 0;
    for (int i = sp - 1; i >= 0; --i) {
        const SearchResult& r =
            nodes[stackNodes[i]].children[stackChildren[i]];

        memcpy(
            out.actions + offset,
            r.actions,
            sizeof(int) * r.depth
        );

        offset += r.depth;
    }

    out.depth = offset;
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

void BFS::Run() {
    int depth = 0;
    int bestCount = 0;

    nodes[0].index = 0;
    nodes[0].parentNode = -1;
    nodes[0].parentChild = -1;

    nodes[0].count = generateActions(
        rootPlayers,
        rootNowState,
        rootPosition,
        nodes[0].children,
        depth
    );

    while (true) {
        Node& n = nodes[depth];

        // ★ 撃破判定
        for (int i = 0; i < n.count; ++i) {
            if (!n.children[i].valid) continue;
            if (n.children[i].players[1].hp > 0) continue;

            ResultPlan ret{};
            // ===== plan 再構築 =====
            buildPlan(depth, n.index, ret);
            best[bestCount] = ret;
            bestCount++;
            if (bestCount == 10) return;
        }

        if (depth == maxDepth) goto backtrack;

        if (n.index < n.count) {
            const SearchResult& cur = n.children[n.index];
            if (!cur.valid) {
                n.index++;
                continue;
            }

            Node& next = nodes[depth + 1];
            next.index = 0;

            next.parentNode = depth;
            next.parentChild = n.index;

            next.count = generateActions(
                cur.players,
                cur.nowState,
                cur.position,
                next.children,
                depth + 1
            );

            n.index++;
            depth++;
            continue;
        }

    backtrack:
        if (depth == 0) break;
        depth--;
    }
}

ResultPlan* BFS::getBest() {
    return best;
}
