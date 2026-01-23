#include "ActionBruteForcer.h"

#include <cstring>

#include "setting.h"

Node ActionBruteForcer::g_nodeBufA[ActionBruteForcerConst::MAX_NODES];
Node ActionBruteForcer::g_nodeBufB[ActionBruteForcerConst::MAX_NODES];
SearchResult ActionBruteForcer::g_results[ActionBruteForcerConst::MAX_NODES];

// ================= 評価 =================

static inline int64_t EvaluatePlayers(const Player players[2]) {
    int64_t score = 0;

    if (players[1].hp == 0) {
        score = -0xfffffffffffffLL;
    }

    score += static_cast<int64_t>(players[1].hp) * 1'000'000;
    score += static_cast<int64_t>(setting::Ally_MAX_HP - players[0].hp) * 1'000;
    score += static_cast<int64_t>(setting::ALLY_CURRENT_MP - players[0].mp) * 100;
    score += static_cast<int64_t>(setting::herbcount - players[0].medicinal_herbs_count) * 10;

    return score;
}

static inline int64_t EvaluateTerminal(const Node& n) {
    int64_t score = EvaluatePlayers(n.players);
    score += static_cast<int64_t>(n.depth) * 1'000'00000;

    if (n.terminated) {
        score += 5'000'000'000LL;
    }

    switch (n.reason) {
        case TerminateReason::EnemyDead:
            score += 5'000'000'000LL;
            break;
        case TerminateReason::AllyDead:
            score -= 5'000'000'000LL;
            break;
        default:
            break;
    }
    return score;
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
        -1,
        &next.nowState,
        true
    );

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

    for (int i = 0; i < curCount; ++i) {
        const Node& n = cur[i];
        SearchResult& r = out.results[i];

        r.score = EvaluateTerminal(n);
        r.depth = n.depth;
        std::memcpy(r.players, n.players, sizeof(Player) * 2);
        std::memcpy(r.actions, n.actions, sizeof(int) * 10);
        r.firstAction = n.actions[0];
        r.nowState = n.nowState;
        r.position = n.position;
        r.valid = true;
        r.isWin = (n.reason == TerminateReason::EnemyDead);
        r.isLose = (n.reason == TerminateReason::AllyDead);
    }
}
