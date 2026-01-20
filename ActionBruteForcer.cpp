// ActionBruteForcer.cpp
#include "ActionBruteForcer.h"

#include <cstring>
#include <optional>
#include <array>
#include <vector>
#include <cstdint>
#include <limits>
#include <queue>

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
        BattleEmulator::DEFENCE, 1, [](const Player &) { return true; },
    },
    {
        BattleEmulator::FLEE_ALLY, 1,[](const Player &) { return true; },
    },
    {
        BattleEmulator::MEDICINAL_HERBS, 1,
        [](const Player &Ally) { return Ally.medicinal_herbs_count >= 1; },
    },
    {
        BattleEmulator::HEAL, 1,
        [](const Player &Ally) { return Ally.mp >= 2; },
    },
    {
        BattleEmulator::CRACK_ALLY, 10,
        [](const Player &Ally) { return Ally.mp >= 3; },
    }
};

// Evaluate: 単純な例（差分）。実運用ではここを書き換えること。
static inline int EvaluatePlayers(const Player players[2]) {
    // プレイヤー側有利度（例）
    return players[1].hp;// * 1000 + players[0].mp;
}


// クラスの static メソッド実装
std::vector<::SearchResult> ActionBruteForcer::Search(
    const Player *rootPlayers,
    uint64_t rootNowState,
    int rootPosition,
    int F // この実装は F==3 を想定している（汎用化は後で可能）
) {
    if (F <= 0) F = 1;
    if (F > 6) F = 6;

    constexpr std::size_t BRANCH = ActionBruteForcer::ids; // 7
    std::size_t totalLeaves = 1;
    for (int i = 0; i < F; ++i) totalLeaves *= BRANCH;

    std::vector<SearchResult> resultsArr;
    resultsArr.resize(totalLeaves);
    std::size_t resultCount = 0;

    std::optional<BattleResult> dummyResult;

    int32_t Gene[350];
    for (int i = 0; i < 350; ++i) Gene[i] = -1;

    auto compare = [](const SearchResult& a, const SearchResult& b) { return a.score > b.score; };
    std::priority_queue<
        SearchResult,
        std::vector<SearchResult>,
        decltype(compare)
    > queue(compare);

    SimState root;
    std::memcpy(root.players, rootPlayers, sizeof(Player) * 2);
    root.NowState = rootNowState;
    root.position = rootPosition;
    root.firstAction = 0;

    for (auto action_table0: ACTION_TABLE) {
        int a0 = action_table0.action;
        SimState s1 = root;
        Gene[0] = a0;
        Gene[1] = -1;

        if (!action_table0.condition(s1.players[0])) {
            continue;
        }

        BattleEmulator::Main(
            &s1.position,
            1,
            Gene,
            s1.players,
            dummyResult,
            0ULL,
            nullptr,
            nullptr,
            -2,
            &s1.NowState,
            true
        );

        for (auto action_table1: ACTION_TABLE) {
            int a1 = action_table1.action;
            SimState s2 = s1;
            Gene[0] = a1;
            Gene[1] = -1;

            auto s2complete = s1.players[0].hp == 0;
            if (!action_table1.condition(s1.players[0])) {
                continue;
            }

            if (!s2complete) {
                BattleEmulator::Main(
                    &s2.position,
                    1,
                    Gene,
                    s2.players,
                    dummyResult,
                    0ULL,
                    nullptr,
                    nullptr,
                    -2,
                    &s2.NowState,
                    true
                );
            }

            for (auto action_table3: ACTION_TABLE) {
                int a2 = action_table3.action;
                SimState s3 = s2;
                Gene[0] = a2;
                Gene[1] = -1;

                auto s3complete = s1.players[0].hp == 0;

                if (!action_table3.condition(s1.players[0])) {
                    continue;
                }
                if (!s3complete) {
                    BattleEmulator::Main(
                        &s3.position,
                        1,
                        Gene,
                        s3.players,
                        dummyResult,
                        0ULL,
                        nullptr,
                        nullptr,
                        -2,
                        &s3.NowState,
                        true
                    );
                }

                if (s3.players[0].hp <= 0) continue;

                int score = EvaluatePlayers(s3.players);

                score += action_table0.cost;
                if (s2complete) {
                    score += action_table1.cost;
                }
                if (s3complete) {
                    score += action_table3.cost;
                }


                SearchResult r;
                r.actions[0] = a0;
                r.actions[1] = a1;
                r.actions[2] = a2;
                r.actions[3] = -1;
                r.score = score;
                r.depth = 3;
                queue.push(r);
            }
        }
    }

    std::vector<::SearchResult> results;

    for (int i = 0; i < 10; ++i) {
        const auto s1 = queue.top();
        queue.pop();
        results.push_back(s1);
    }

    return results;
}

// グローバルな free function が宣言されている場合のラッパー実装（必要なら）
std::vector<SearchResult> Search(
    const Player *rootPlayers,
    uint64_t rootNowState,
    int rootPosition,
    int F
) {
    return ActionBruteForcer::Search(rootPlayers, rootNowState, rootPosition, F);
}
