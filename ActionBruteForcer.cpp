// ActionBruteForcer.cpp
#include "ActionBruteForcer.h"

#include <cstring>
#include <optional>
#include <vector>
#include <cstdint>
#include <queue>
#include <ostream>

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
    int rootPosition,
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
            -1,
            &s1.NowState,
            true
        );

        if (!isFirstExec && dummyResult->actions[0] == BattleEmulator::HEAL_ENEMY || dummyResult->actions[1] == BattleEmulator::HEAL_ENEMY) {
            continue;
        }

        for (auto action_table1: ACTION_TABLE) {
            int a1 = action_table1.action;
            SimState s2 = s1;
            Gene[0] = a1;
            Gene[1] = -1;

            auto s2complete = s1.players[0].hp == 0;
            if (!action_table1.condition(s1.players[0])) {
                continue;
            }

            dummyResult->clear();
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
                    -1,
                    &s2.NowState,
                    true
                );
            }

            if (dummyResult->actions[0] == BattleEmulator::HEAL_ENEMY || dummyResult->actions[1] == BattleEmulator::HEAL_ENEMY) {
                continue;
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
                dummyResult->clear();
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
                        -1,
                        &s3.NowState,
                        true
                    );
                }

                if (s3.players[0].hp <= 0) continue;

                if (dummyResult->actions[0] == BattleEmulator::HEAL_ENEMY || dummyResult->actions[1] == BattleEmulator::HEAL_ENEMY) {
                    continue;
                }

                for (auto action_table4: ACTION_TABLE) {
                    int a3 = action_table4.action;
                    SimState s4 = s3;
                    Gene[0] = a3;
                    Gene[1] = -1;

                    auto s4complete = s1.players[0].hp == 0;

                    if (!action_table4.condition(s1.players[0])) {
                        continue;
                    }
                    dummyResult->clear();
                    if (!s4complete) {
                        BattleEmulator::Main(
                            &s4.position,
                            1,
                            Gene,
                            s4.players,
                            dummyResult,
                            0ULL,
                            nullptr,
                            nullptr,
                            -1,
                            &s4.NowState,
                            true
                        );
                    }

                    if (s4.players[0].hp <= 0) continue;

                    if (dummyResult->actions[0] == BattleEmulator::HEAL_ENEMY || dummyResult->actions[1] == BattleEmulator::HEAL_ENEMY) {
                        continue;
                    }

                    int score = EvaluatePlayers(s4.players);

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
                    r.actions[3] = a3;
                    r.actions[4] = -1;
                    r.score = score;
                    r.depth = 3;
                    tryInsertBest(best, bestCount, worstIdx, worstScore, r);
                }
            }
        }
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
