#include "ActionBruteForcer.h"

#include <cstring>

#include "setting.h"

// ================= 評価 =================

static inline uint64_t PackScore(
    int totalDepth,
    const Player players[2]
) {
    const auto depth = static_cast<uint64_t>(totalDepth);
    const auto enemyHp = static_cast<uint64_t>(players[1].hp);
    const auto allyLost = static_cast<uint64_t>(
        setting::Ally_MAX_HP - players[0].hp
    );
    const auto mpUsed = static_cast<uint64_t>(
        setting::ALLY_CURRENT_MP - players[0].mp
    );
    const auto herbUsed = static_cast<uint64_t>(
        setting::herbcount - players[0].medicinal_herbs_count
    );

    return (depth << 48)
         | (enemyHp << 32)
         | (allyLost << 16)
         | (mpUsed << 8)
         | herbUsed;
}

int64_t ActionBruteForcer::EvaluateTerminal(const Node& n, int totalDepth) {
    return static_cast<int64_t>(PackScore(totalDepth, n.players));
}

// ================= 1ステップ =================

static inline Node SimulateStep(
    const Node& cur,
    int action,
    int depth
) {
    Node next = cur;

    next.actions[depth] = action;
    next.depth = depth + 1;

    int gene[2] = { action, -1 };

    BattleEmulator::Main(
        &next.position,
        1,
        gene,
        next.players,
        nullptr,
        0ULL,
        nullptr,
        nullptr,
        -2,
        &next.nowState,
        true
    );

    //敵の回復は禁止?
    // if (ret1->actions[0] == BattleEmulator::HEAL_ENEMY || ret1->actions[1] == BattleEmulator::HEAL_ENEMY) {
    //     next.terminated = true;
    //     next.reason = TerminateReason::AllyDead;
    //     return next;
    // }

    if (next.players[0].hp <= 0) {
        next.terminated = true;
        next.reason = TerminateReason::AllyDead;
        return next;
    }

    if (next.players[1].hp <= 0) {
        next.terminated = true;
        next.reason = TerminateReason::EnemyDead;
        return next;
    }

    next.terminated = false;
    next.reason = TerminateReason::None;
    return next;
}

// ================= 静的バッファ =================


// ================= 探索 =================

ActionBruteForcer::ActionBruteForcer() {
    constexpr std::size_t nodeSize =
        sizeof(Node) * ActionBruteForcerConst::MAX_NODES;

    g_nodeBufA = static_cast<Node*>(std::malloc(nodeSize));
    g_nodeBufB = static_cast<Node*>(std::malloc(nodeSize));

    if (!g_nodeBufA || !g_nodeBufB) {
        std::free(g_nodeBufA);
        std::free(g_nodeBufB);
        throw std::bad_alloc();
    }
}

ActionBruteForcer::~ActionBruteForcer() {
    std::free(g_nodeBufA);
    std::free(g_nodeBufB);
}

void ActionBruteForcer::Search(
    const Player* rootPlayers,
    uint64_t rootNowState,
    int rootPosition,
    SearchOutput& out
) {
    Node* cur = g_nodeBufA;
    Node* nxt = g_nodeBufB;

    int curCount = 1;
    int nxtCount = 0;

    Node& root = cur[0];
    std::memcpy(root.players, rootPlayers, sizeof(Player) * 2);
    root.nowState = rootNowState;
    root.position = rootPosition;
    root.depth = 0;
    root.terminated = false;
    root.reason = TerminateReason::None;
    std::fill_n(root.actions, 10, -1);

    for (int depth = 0; depth < ActionBruteForcerConst::CONST_MAX_DEPTH; ++depth) {
        nxtCount = 0;

        for (int i = 0; i < curCount; ++i) {
            const Node& n = cur[i];

            if (n.terminated) {
                nxt[nxtCount++] = n;
                continue;
            }

            for (int a = 0; a < ActionBruteForcerConst::ACTION_TABLE_SIZE; ++a) {
                const ActionEntry& e = ACTION_TABLE[a];
                if (!e.condition(n.players[0])) continue;

                nxt[nxtCount++] = SimulateStep(n, e.action, depth);
            }
        }

        Node* tmp = cur;
        cur = nxt;
        nxt = tmp;
        curCount = nxtCount;
    }
    out.count = curCount;

    int offset = 0;
    for (int i = 0; i < curCount; ++i) {
        const Node& n = cur[i];
        if (n.reason == TerminateReason::AllyDead) {
            out.count--;
            continue;
        }
        out.nodes[offset++] = &n;
    }
}
