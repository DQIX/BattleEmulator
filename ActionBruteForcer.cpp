// ActionBruteForcer.cpp
#include "ActionBruteForcer.h"

#include <cassert>
#include <cstring>
#include <optional>
#include <vector>
#include <cstdint>
#include <queue>
#include <ostream>
#include <array>      // 追加
#include <functional> // 追加

#include "lcg.h"
#include "setting.h"


static inline int64_t EvaluatePlayers(const Player players[2]) {
    int64_t score = 0;

    if (players[1].hp == 0) {
        score = -0xfffffffffffff;
    }

    // [最重要] 敵残HP（完全結果）
    score += static_cast<int64_t>(players[1].hp) * 1'000'000;

    // [重要] 味方残HP（生存余裕）
    score += static_cast<int64_t>(setting::Ally_MAX_HP - players[0].hp) * 1'000;

    // [補助] MP消費
    score += static_cast<int64_t>(setting::ALLY_CURRENT_MP - players[0].mp) * 100;

    // [補助] アイテム消費
    score += static_cast<int64_t>(setting::herbcount - players[0].medicinal_herbs_count) * 10;

    return score;
}


// Evaluate: 単純な例（差分）。実運用ではここを書き換えること。
static inline bool isHealEnemyInResult(const std::optional<BattleResult>& r) {
    return r && (r->actions[0] == BattleEmulator::HEAL_ENEMY
              || r->actions[1] == BattleEmulator::HEAL_ENEMY);
}

static int64_t EvaluateTerminal(
    const Node& n,
    int F
) {
    int64_t score = EvaluatePlayers(n.players);

    // 早く終わったボーナス（重要）
    score -= static_cast<int64_t>(n.depth) * 10'000;

    switch (n.reason) {
        case TerminateReason::EnemyDead:
            score -= 5'000'000'000LL;
            break;
        case TerminateReason::AllyDead:
            score += 5'000'000'000LL;
            break;
        case TerminateReason::HealEnemy:
            score += 1'000'000'000LL;
            break;
        default:
            break;
    }

    return score;
}


static Node simulate(
    const Node& cur,
    int action,
    int depth,
    bool isFirstExec
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

    // ===== 終端判定 =====

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

    if ((depth == 0 && !isFirstExec) || depth > 0) {
        if (isHealEnemyInResult(result)) {
            next.terminated = true;
            next.reason = TerminateReason::HealEnemy;
            return next;
        }
    }

    next.terminated = false;
    next.reason = TerminateReason::None;
    return next;
}

std::vector<SearchResult> ActionBruteForcer::Search(
    const Player* rootPlayers,
    uint64_t rootNowState,
    int rootPosition,
    bool isFirstExec
) {
    int F = CONST_MAX_DEPTH;

    std::vector<Node> current;
    std::vector<Node> next;

    Node root{};
    memcpy(root.players, rootPlayers, sizeof(Player) * 2);
    root.nowState = rootNowState;
    root.position = rootPosition;
    root.depth = 0;
    root.terminated = false;
    root.reason = TerminateReason::None;
    std::fill(std::begin(root.actions), std::end(root.actions), -1);

    current.push_back(root);

    // ===== レイヤード探索 =====
    for (int depth = 0; depth < F; ++depth) {
        next.clear();

        for (const Node& n : current) {
            if (n.terminated) {
                next.push_back(n);
                continue;
            }

            for (const auto& [action, cost, cond] : ACTION_TABLE) {
                if (!cond(n.players[0])) continue;

                Node child = simulate(n, action, depth, isFirstExec);
                next.push_back(child);
            }
        }

        current.swap(next);
    }

    // ===== 結果評価 =====
    std::vector<SearchResult> results;

    for (const Node& n : current) {
        SearchResult r{};
        r.score = EvaluateTerminal(n, F);
        r.depth = n.depth;
        memcpy(r.players, n.players, sizeof(Player) * 2);
        r.nowState = n.nowState;
        r.position = n.position;
        memcpy(r.actions, n.actions, sizeof(int) * 10);
        r.firstAction = n.actions[0];
        r.valid = true;

        results.push_back(r);
    }

    return results;
}

