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

struct ActionEntry {
    int action;
    int cost;

    bool (*condition)(const Player &);
};

constexpr ActionEntry ACTION_TABLE[] = {
    {
        BattleEmulator::ATTACK_ALLY, 0, [](const Player &) { return true; },
    },
    {
        BattleEmulator::DRAGON_SLASH, 0, [](const Player &) { return true; },
    },
    {
        BattleEmulator::DEFENCE, 30, [](const Player &) { return true; },
    },
    {
        BattleEmulator::FLEE_ALLY, 1,[](const Player &) { return true; },
    },
    {
        BattleEmulator::MEDICINAL_HERBS, 1,
        [](const Player &Ally) { return Ally.medicinal_herbs_count >= 1; },
    },
    {
        BattleEmulator::HEAL, -20,
        [](const Player &Ally) { return Ally.mp >= 2; },
    },
    {
        BattleEmulator::CRACK_ALLY, 30,
        [](const Player &Ally) { return Ally.mp >= 3; },
    }
};

// Evaluate: 単純な例（差分）。実運用ではここを書き換えること。
static inline int64_t EvaluatePlayers(const Player players[2]) {
    int64_t score = 0;

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


// クラスの static メソッド実装
void ActionBruteForcer::Search(
    const Player *rootPlayers,
    uint64_t rootNowState,
    const int rootPosition,
    int F, // この実装は F==3 を想定している（汎用化は後で可能）
    bool isFirstExec,
    SearchResult best[10]
) {

    if (F <= 0) F = 1;
    if (F > 6) F = 6;

    int bestCount = 0;
    int worstIdx = -1;
    int64_t worstScore = INT64_MAX;

    constexpr std::size_t BRANCH = ActionBruteForcer::ids; // 7
    std::size_t totalLeaves = 1;
    for (int i = 0; i < F; ++i) totalLeaves *= BRANCH;

    std::optional<BattleResult> dummyResult = BattleResult();
    int32_t Gene[350];
    for (int & i : Gene) i = -1;

    SimState root;
    std::memcpy(root.players, rootPlayers, sizeof(Player) * 2);
    root.NowState = rootNowState;
    root.position = rootPosition;
    root.firstAction = 0;

    const int targetDepth = F; // 元コードは a0,a1,a2,a3 の4手固定

    std::array<int, 6> actions{};
    actions.fill(-1);

    std::array<int, 6> chosenCost{};
    chosenCost.fill(0);

    // 「その手を実行する直前に既に完了(HP==0)だったか」を保持（元の s2complete/s3complete と同じ意味）
    std::array<bool, 6> completeBefore{};
    completeBefore.fill(false);

    auto isHealEnemyInResult = [&](const std::optional<BattleResult>& r) -> bool {
        return r && (r->actions[0] == BattleEmulator::HEAL_ENEMY || r->actions[1] == BattleEmulator::HEAL_ENEMY);
    };

    std::function<void(int, const SimState&)> dfs = [&](int depth, const SimState& cur) {
        // depth: 0..targetDepth-1 の手番
        if (cur.players[1].hp == 0 || depth == targetDepth) {
            // 評価（元コードの a3 まで回した後）
            auto score = EvaluatePlayers(cur.players);

            // cost 加算ルールは「元コードと同じ」：
            // - a0 の cost は常に加算
            // - a1 の cost は s2complete のときのみ加算（= a1直前にHP==0）
            // - a2 の cost は s3complete のときのみ加算（= a2直前にHP==0）
            // - a3 の cost は元コード同様 “加算しない”
            score += chosenCost[0];
            if (depth <= 2) score += chosenCost[1];
            if (depth <= 3) score += chosenCost[2];
            if (depth <= 4) score += chosenCost[3];
            if (depth <= 5) score += chosenCost[4];

            SearchResult r;
            r.actions[0] = actions[0];
            r.actions[1] = actions[1];
            r.actions[2] = actions[2];
            r.actions[3] = actions[3];
            r.actions[4] = actions[4];
            r.actions[5] = -1;
            r.valid = true;
            if (depth != targetDepth) {
                r.actions[depth] = -1;
            }
            r.nowState = cur.NowState;
            r.position = cur.position;
            memcpy(r.players, cur.players, sizeof(Player) * 2);
            r.score = score;
            r.depth = std::min(depth, targetDepth);
            tryInsertBest(best, bestCount, worstIdx, worstScore, r);
            return;
        }

        for (const auto &[action, cost, condition] : ACTION_TABLE) {
            SimState next = cur;

            if (!condition(next.players[0])) {
                continue;
            }

            actions[depth] = action;
            chosenCost[depth] = cost;

            Gene[0] = action;
            Gene[1] = -1;

            dummyResult->clear();

            BattleEmulator::Main(
                &next.position,
                1,
                Gene,
                next.players,
                dummyResult,
                0ULL,
                nullptr,
                nullptr,
                -1,
                &next.NowState,
                true
            );

            // 枝刈り（深さごとに元コードと同じタイミングで実施）
            if (depth == 0) {
                if (next.players[0].hp <= 0) continue;
                if (!isFirstExec && isHealEnemyInResult(dummyResult)) {
                    continue;
                }
            } else {
                if (next.players[0].hp <= 0) continue;
                if (isHealEnemyInResult(dummyResult)) {
                    continue;
                }
            }
            dfs(depth + 1, next);
        }

        actions[depth] = -1; // 念のため戻す（可読性＆事故防止）
    };

    dfs(0, root);

    for (int i = 0; i < bestCount; ++i) {
        auto n =  best[i].nowState;
        auto p =  best[i].position;
        memcpy(best[i].players, rootPlayers, sizeof(Player) * 2);
        best[i].nowState = rootNowState;
        best[i].position = rootPosition;

        BattleEmulator::Main(
            &best[i].position,
            best[i].depth,
            best[i].actions,
            best[i].players,
            dummyResult,
            0ULL,
            nullptr,
            nullptr,
            -1,
            &best[i].nowState,
            true
        );

        assert(best[i].position == p);
        assert(best[i].nowState == n);
    }
}


std::ostream& operator<<(std::ostream& os, const SearchResult& r) {
    os << "SearchResult{"
       << "score=" << r.score
       << ", depth=" << r.depth
       << ", firstAction=" << r.firstAction
       << ", actions=[";

    // -1 で打ち切る前提
    for (int i = 0; i < 5; ++i) {
        if (r.actions[i] < 0) break;
        if (i != 0) os << ',';
        os << r.actions[i];
    }

    os << "]}";
    return os;
}
