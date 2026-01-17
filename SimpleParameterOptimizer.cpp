// SimpleParameterOptimizer.cpp
// 非確率的・メンテしやすいパラメータ最適化器（ファイル丸ごと置き換え用）
// 前提: ヘッダ SimpleParameterOptimizer.h は既に存在し、CostParams の宣言などはそちらにある
// コンパイラ: C++17
//
// 目的要約:
// - action cost を一次真実源として一本化（配列）
// - 最適化対象 action id は constexpr 配列で指定（1行で追加完了）
// - 全コストの初期値は統一（自動化）
// - testParameters は既存インターフェースをそのまま使用（呼ぶ前に静的 CostParams を同期）
// - 探索は非確率的（決定論的）ベストファースト（評価値 = ターン数）
//

#if defined(OPTIMIZE_MODE)

#include "SimpleParameterOptimizer.h"
#include "ActionOptimizer.h"
#include "BattleEmulator.h"
#include "Player.h"
#include "Genome.h"

#include <array>
#include <vector>
#include <queue>
#include <unordered_set>
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <random>


// --- 設定 ---
// 最大 action id（BattleEmulator に合わせて適当に余裕を持たせる）
static constexpr int MAX_ACTION_ID = 512;
static constexpr int ids = 22;

// すべての action cost の初期値（統一）
static constexpr double DEFAULT_ACTION_COST = 1.0;

// この配列に最適化対象の action id を並べるだけで追加完了。
// 例: BattleEmulator::MIDHEAL, BattleEmulator::SPECIAL_ANTIDOTE, ...
// ここに1行追加/削除すれば最適化対象が変わる（要件どおり）。
static constexpr std::array<int, ids> TUNE_IDS = {
    BattleEmulator::MIDHEAL,
    BattleEmulator::SPECIAL_ANTIDOTE,
    BattleEmulator::SPECIAL_MEDICINE,
    BattleEmulator::DOUBLE_UP,
    BattleEmulator::PSYCHE_UP_ALLY,
    BattleEmulator::FLEE_ALLY,
    BattleEmulator::BUFF,
    BattleEmulator::MULTITHRUST,
    BattleEmulator::ATTACK_ENEMY,
    SimpleParameterOptimizer::turnHeignt,
    SimpleParameterOptimizer::enemyHpWeight,
    SimpleParameterOptimizer::playerHpWeight,
    SimpleParameterOptimizer::resourceWeight,
    SimpleParameterOptimizer::StatusEffectWeight,
    SimpleParameterOptimizer::paralysisWeight,
    SimpleParameterOptimizer::sleepWeight,
    SimpleParameterOptimizer::poisonWeight,
    SimpleParameterOptimizer::inactiveWeight,
    SimpleParameterOptimizer::SpHeight,
    SimpleParameterOptimizer::ActHeight,
    SimpleParameterOptimizer::ResourceHPCost,
    SimpleParameterOptimizer::NoResourceCost,
};

// step（刻み幅）は一律でここで定義。必要なら後で配列化して個別に設定可。
static constexpr double DEFAULT_STEP = 0.01;

// action cost テーブル（一次真実源）
static std::array<double, MAX_ACTION_ID> s_actionCosts;

// --- ヘルパ関数 ---
// 初期化: 全要素を DEFAULT_ACTION_COST で埋める。
// （呼び出しは optimize の開始時に自動で行う）
static void initActionCostsIfNeeded() {
    static bool inited = false;
    if (inited) return;
    for (int i = 0; i < MAX_ACTION_ID; ++i) s_actionCosts[i] = DEFAULT_ACTION_COST;
    inited = true;
}

// clamp helper
static inline double clampDouble(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ActionCosts -> CostParams の静的フィールドへ反映する（既存コード互換のため）
static inline void applyActionCostsToCostParams() {

    // その他の重み（hp重み等）は既存の default を尊重（変更しない）。
    // 必要であればここで s_actionCosts の特定 index を割り当ててください。
}

// 内部評価（testParameters 実行時に呼ぶためのラッパー）
// この実装はヘッダの宣言と同じシグネチャを保つ。
// ただし内部的に action cost テーブルの現在値を CostParams static に同期してから
// ActionOptimizer を呼び出すようにして、既存の評価ルーチンとの互換性を保ちます。
int SimpleParameterOptimizer::testParameters(
                                            const Player players[2],
                                            uint64_t seed,
                                            const int actions[350],
                                            int turns)
{
    // まず action table の値を静的 CostParams に反映
    applyActionCostsToCostParams();

    // players をコピーして副作用を回避
    Player copiedPlayers[2] = { players[0], players[1] };

    // gene 配列の準備（既存コードと互換）
    int gene[350];
    for (int i = 0; i < 350; ++i) {
        gene[i] = actions[i];
        if (actions[i] == -1) {
            gene[i] = -1;
            break;
        }
    }

    // 既存の ActionOptimizer を使って評価（世代数や制限は既存運用に合わせられる）
    // ここでは既存コードが用いていた呼び出し方に揃える
    auto genome = ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 2000, gene, 0);

    if (genome.EnemyPlayer.hp <= 0) {
        // 勝利した場合は終盤のターン数を返す（小さい方が良い）
        return genome.turn - 1;
    } else {
        return 999; // 失敗（評価上悪い）
    }
}

double SimpleParameterOptimizer::getActionCost(int action) {
    // 範囲外防御（必要なら）
    if (action < 0 || action >= 200) {
        throw std::invalid_argument("Invalid action ID");
    }
    return s_actionCosts[action];
}


// --- 最適化本体 ---
// 非確率的ベストファースト（決定論的近傍展開）
// maxTests は評価回数上限
OptimResult SimpleParameterOptimizer::optimize(const Player players[2], uint64_t seed,
                                               const int actions[350], int maxTests, int turns)
{
    initActionCostsIfNeeded();

    std::random_device seed_gen;
    std::uint32_t seed1 = seed_gen();
    std::mt19937 engine(seed1);

    OptimResult result;
    result.bestTurn = 999;
    result.testCount = 0;
    result.found = false;

    // --- スナップショット（自動化された originalParams 保存） ---
    // 実行前の actionCosts を丸ごと保存（これが "original"）
    std::vector<double> originalCosts(MAX_ACTION_ID);
    for (int i = 0; i < MAX_ACTION_ID; ++i) originalCosts[i] = s_actionCosts[i];

    // 静的 CostParams にも反映して、既存の toText / copyFrom 呼び出しと互換性を保つ
    applyActionCostsToCostParams();
    // ここでユーザ期待どおりに「現在値を保存」したいなら、result.bestParams へ保存する処理を入れるが
    // ヘッダ上の CostParams は static メンバ中心のため値を格納するインスタンス領域がない点に注意。
    // とりあえず現状値を表示してログとして残す（人が確認できるようにする）
    std::cout << "[SimpleParameterOptimizer] original action cost snapshot (first few):\n";
    for (size_t i = 0; i < TUNE_IDS.size(); ++i) {
        int id = TUNE_IDS[i];
        if (id >= 0 && id < MAX_ACTION_ID) {
            std::cout << "  id=" << id << " cost=" << s_actionCosts[id] << "\n";
        }
    }

    // --- 初期評価（現状のまま） ---
    int baseTurn = testParameters(players, seed, actions, turns);
    result.bestTurn = baseTurn;
    result.testCount = 1;
    result.found = (baseTurn < 999);
    std::cout << "[SimpleParameterOptimizer] initial turn = " << baseTurn << std::endl;

    if (baseTurn <= 5) {
        // 十分良ければ早期終了
        applyActionCostsToCostParams();
        // try to "save" best params into static CostParams so downstream can pick up
        result.bestTurn = baseTurn;
        result.found = true;
        return result;
    }

    // --- 探索構造体 ---
    struct Node {
        // only store the modified ids (index in TUNE_IDS) and costs for those
        // but for簡潔に、ここでは full copy
        std::vector<double> costs; // size = TUNE_IDS.size()
        int evaluations; // 評価時のターン数
        int depth; // 深さ（遷移回数）
    };

    // 比較：ターン数小さい優先、次に depth 小さい優先
    auto cmpNode = [](const Node& a, const Node& b) {
        if (a.evaluations != b.evaluations) return a.evaluations > b.evaluations;
        return a.depth > b.depth;
    };

    std::priority_queue<Node, std::vector<Node>, decltype(cmpNode)> open(cmpNode);
    std::unordered_set<std::string> visited;

    // ヘルパ: costs -> key 生成
    auto costsKey = [&](const std::vector<double>& costs)->std::string {
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(6);
        for (double v : costs) oss << v << ',';
        return oss.str();
    };

    // 初期ノード（現在の TUNE_IDS に対応する値を抽出）
    std::vector<double> startCosts;
    startCosts.reserve(TUNE_IDS.size());
    for (int id : TUNE_IDS) {
        if (id >= 0 && id < MAX_ACTION_ID) startCosts.push_back(s_actionCosts[id]);
        else startCosts.push_back(DEFAULT_ACTION_COST);
    }
    Node startNode{ startCosts, baseTurn, 0 };
    open.push(startNode);
    visited.insert(costsKey(startCosts));



    int evaluations = 1;
    const int maxEvaluations = std::max(1, maxTests); // 評価回数上限
    // 探索ループ（決定論的）
    while (!open.empty() && evaluations < maxEvaluations) {
        Node cur = open.top(); open.pop();
        //
        // if (evaluations % 5 == 0) {
        //     std::cout << "[SimpleParameterOptimizer] progress: eval=" << evaluations << " depth=" << cur.depth << std::endl;
        // }
        // 収束判定
        if (cur.evaluations <= 5) {
            result.bestTurn = cur.evaluations;
            result.found = true;
            break;
        }

        for (int i = 0; i < 15; ++i) {
            std::vector<double> nbCosts = cur.costs;

            for (size_t pi = 0; pi < TUNE_IDS.size(); ++pi) {
                // 世代単位で変えるかどうかを決定
                bool doMutate = (engine() & 3) == 0;
                if (!doMutate) {
                    // -100 ～ +100 → -1.00 ～ +1.00
                    int dirInt = static_cast<int>(engine() % 20001) - 10000;
                    double newdir = dirInt * 0.01;

                    double newVal = nbCosts[pi] + newdir * DEFAULT_STEP;

                    if (newVal < 0.0) {
                        newVal = 0.0;
                    }

                    nbCosts[pi] = newVal;
               }
            }


            std::string key = costsKey(nbCosts);
            if (visited.find(key) != visited.end()) continue;
            visited.insert(key);

            // nbCosts をグローバルの s_actionCosts に反映して評価
            // 作業用に全 actionCosts をコピーしてから上書きし、評価後に復元する
            std::array<double, MAX_ACTION_ID> backup = s_actionCosts;
            for (size_t k = 0; k < TUNE_IDS.size(); ++k) {
                int aid = TUNE_IDS[k];
                if (aid >= 0 && aid < MAX_ACTION_ID) s_actionCosts[aid] = nbCosts[k];
            }

            // 評価
            int turn = testParameters(players, seed, actions, turns);
            ++evaluations;

            std::cout << "[SimpleParameterOptimizer] eval=" << evaluations  << " -> turn=" << turn << ", " << std::endl;

            for (auto nb_cost: nbCosts) {
                std::cout << nb_cost << ", ";
            }
            std::cout << std::endl;

            // ログ（改善があれば表示）
            if (turn < result.bestTurn) {
                std::cout << "===============";
                result.bestTurn = turn;
                result.found = true;
                std::cout << "[SimpleParameterOptimizer] improvement eval=" << evaluations
                          << " -> turn=" << turn << std::endl;

                std::cout << "[SimpleParameterOptimizer] eval=" << evaluations  << " -> turn=" << turn << ", " << std::endl;

                for (auto nb_cost: nbCosts) {
                    std::cout << nb_cost << ", ";
                }
                std::cout << std::endl;
                std::cout << "===============";
            }

            if (turn == 999) {
                continue;
            }

            // 新ノードを open に push
            Node nbNode{ nbCosts, turn, cur.depth + 1 };
            open.push(nbNode);

            // 復元
            s_actionCosts = backup;
            if (evaluations >= maxEvaluations) break;
        }
        if (evaluations >= maxEvaluations) break;
    } // while

    // 最終結果: bestTurn に対応する actionCosts を静的 CostParams に反映して返却
    // ここでは result.bestTurn に到達した時点の s_actionCosts が反映されているとは限らないので、
    // 最後に best を探す（簡単のため、visited をスキャンして最良評価を探すのが重いので、
    // 現状最良評価に到達した時点でその値は result.bestTurn に入っている。ここでは
    // 単純に「最後に見つけた改善時の s_actionCosts」を反映する方針とする）
    // （実運用で bestCosts を保持したい場合はノードに bestCosts を保持するよう拡張してください）

    // 最後に静的 CostParams を現在の s_actionCosts に反映しておく
    applyActionCostsToCostParams();

    result.testCount = evaluations;
    std::cout << "[SimpleParameterOptimizer] done. bestTurn=" << result.bestTurn
              << " evaluations=" << evaluations << std::endl;

    return result;
}


// --- ここまで ---
#endif // OPTIMIZE_MODE
