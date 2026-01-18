// SimpleParameterOptimizer_GA.cpp
// シンプルさを保ったまま遺伝的アルゴリズム(GA)へ置き換える実装
// ファイル丸ごと置換用。ヘッダ SimpleParameterOptimizer.h は既に存在する前提。
// コンパイラ: C++17

#if defined(OPTIMIZE_MODE)

#include "SimpleParameterOptimizer.h"
#include "ActionOptimizer.h"
#include "BattleEmulator.h"
#include "Player.h"
#include "Genome.h"

#include <array>
#include <vector>
#include <iostream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include <random>
#include <chrono>
#include <limits>
#include <string>

// --- 設定 ---
static constexpr int MAX_ACTION_ID = 512;
static constexpr int ids = 26;
static constexpr double DEFAULT_ACTION_COST = 1.0;
static constexpr double DEFAULT_STEP = 0.5; // 変異の基本スケール

// GA パラメータ（必要なら調整）
static constexpr int GA_POPULATION = 50; // 1世代あたり生成する子の数
static constexpr double GA_MUTATION_PROB = 0.15; // 各遺伝子が変異する確率
static constexpr double GA_CROSSOVER_PROB = 0.9; // 親から交叉する確率
static constexpr int GA_EVAL_SEEDS = 30;

// --- Stability tuning parameters (内部定義・調整可能) ---
constexpr int STABILITY_CHECKS = 100;           // 世代ごとに最良個体を何回別 seed で再評価するか
constexpr double GA_INSTABILITY_WEIGHT = 10.0; // instability を fitness に掛ける重み（経験則で調整）

// ---- 安定性チェック用: ランダム追加 actions（compile 時に決める） ----
// ここを編集するだけで「追加しうる行動」を切り替え可能
static constexpr std::array<int, 3> STABILITY_RANDOM_ACTION_POOL = {
    BattleEmulator::BUFF,
    BattleEmulator::SPECIAL_MEDICINE,
    BattleEmulator::PSYCHE_UP_ALLY, // ※ もし敵の PSYCHE_UP を混ぜたいなら BattleEmulator::PSYCHE_UP を入れる
};
// 1回の stability check で最大いくつ挿入するか（0なら無効）
static constexpr double STABILITY_EXTRA_ACTION_INSERT_PROB = 0.60;


// この配列に最適化対象の aABILITY_EXTRA_ACTIONS_MAX = 3;
//// 各挿入を行う確率（1.0=必ずction id を並べるだけで追加完了
static constexpr std::array<int, ids> TUNE_IDS = {
    BattleEmulator::MIDHEAL,
    BattleEmulator::SPECIAL_ANTIDOTE,
    BattleEmulator::SPECIAL_MEDICINE,
    BattleEmulator::DOUBLE_UP,
    BattleEmulator::PSYCHE_UP_ALLY,
    BattleEmulator::FLEE_ALLY,
    BattleEmulator::BUFF,
    BattleEmulator::MULTITHRUST,
    BattleEmulator::DEFENCE,
    SimpleParameterOptimizerNode::turnHeignt,
    SimpleParameterOptimizerNode::enemyHpWeight,
    SimpleParameterOptimizerNode::playerHpWeight,
    SimpleParameterOptimizerNode::resourceWeight,
    SimpleParameterOptimizerNode::StatusEffectWeight,
    SimpleParameterOptimizerNode::paralysisWeight,
    SimpleParameterOptimizerNode::sleepWeight,
    SimpleParameterOptimizerNode::poisonWeight,
    SimpleParameterOptimizerNode::inactiveWeight,
    SimpleParameterOptimizerNode::SpHeight,
    SimpleParameterOptimizerNode::ActHeight,
    SimpleParameterOptimizerNode::ResourceHPCost,
    SimpleParameterOptimizerNode::NoResourceCost,
    SimpleParameterOptimizerNode::BuffWeight,
    SimpleParameterOptimizerNode::AtkBuffWeight,
    SimpleParameterOptimizerNode::TensionWeight,
    SimpleParameterOptimizerNode::AntidoteWeight,
};

// action cost テーブル（一次真実源）
static std::array<double, MAX_ACTION_ID> s_actionCosts;

// --- 安定性チェック用: actions をコピーしてランダム挿入する（本体 actions は汚さない） ---
template <size_t N>
static int buildActionsWithRandomInserts(
    const int srcActions[350],
    int dstActions[350],
    std::mt19937 &rng,
    const std::array<int, N> &pool,
    int maxInserts,
    double insertProb,
    std::string *outInsertedSummary // nullptr 可
) {
    // src をコピー & 長さ測定
    int len = 0;
    for (; len < 350; ++len) {
        dstActions[len] = srcActions[len];
        if (srcActions[len] == -1) break;
    }
    if (len == 350) {
        // 念のため終端を保証
        dstActions[349] = -1;
        len = 349;
    }

    if (maxInserts <= 0 || N == 0 || insertProb <= 0.0) {
        if (outInsertedSummary) *outInsertedSummary = "";
        return len;
    }

    std::uniform_real_distribution<double> uni01(0.0, 1.0);
    std::uniform_int_distribution<int> pickAction(0, static_cast<int>(N) - 1);

    std::ostringstream oss;
    int inserted = 0;

    // 「-1」を含まない実データ長（挿入位置計算に使う）
    int dataLen = 0;
    while (dataLen < 350 && dstActions[dataLen] != -1) ++dataLen;

    for (int k = 0; k < maxInserts; ++k) {
        if (uni01(rng) > insertProb) continue;
        if (dataLen >= 349) break; // これ以上挿入すると -1 が置けない

        const int actionToInsert = pool[pickAction(rng)];
        std::uniform_int_distribution<int> pickPos(0, dataLen); // 末尾(=append)も含む
        const int pos = pickPos(rng);

        // 右に1つずらして挿入
        for (int i = std::min(349, dataLen); i > pos; --i) {
            dstActions[i] = dstActions[i - 1];
        }
        dstActions[pos] = actionToInsert;
        ++dataLen;
        dstActions[dataLen] = -1; // 終端更新

        if (inserted > 0) oss << ", ";
        oss << BattleEmulator::getActionName(actionToInsert) << "@idx" << pos;
        ++inserted;
    }

    if (outInsertedSummary) *outInsertedSummary = oss.str();
    return dataLen;
}

// --- ヘルパ関数 ---
static void initActionCostsIfNeeded() {
    static bool inited = false;
    if (inited) return;
    for (int i = 0; i < MAX_ACTION_ID; ++i) s_actionCosts[i] = DEFAULT_ACTION_COST;
    inited = true;
}

static inline double clampDouble(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline void applyActionCostsToCostParams() {
    // ユーザ環境に依存するため、ここで s_actionCosts の各 index を
    // CostParams の静的メンバへ割り当てる実装を追加してください。
    // 例: CostParams::someWeight = s_actionCosts[SomeId];
}

double SimpleParameterOptimizer::getActionCost(int action) {
    if (action < 0 || action >= MAX_ACTION_ID) {
        throw std::invalid_argument("Invalid action ID");
    }
    return s_actionCosts[action];
}

// --- 遺伝的アルゴリズム実装 ---
struct GAGenome {
    std::vector<double> genes; // size = TUNE_IDS.size()
    double fitness; // 小さいほど良い（ターン優先）
    int measuredTurns; // 実測ターン
    double measuredMs; // 実測時間（ms）
};

// 評価: returns fitness (小さい方が良い)
static double evaluateGenome(
    GAGenome &g,
    const Player players[2],
    const std::array<uint64_t, GA_EVAL_SEEDS> &evalSeeds, // ★ baseSeed 単体→5個
    const int actions[350],
    int turnsLimit,
    std::mt19937 &rng,
    int &outTurns,
    double &outMs
) {
    // s_actionCosts を書き換えて評価
    auto backup = s_actionCosts;
    for (size_t i = 0; i < g.genes.size(); ++i) {
        int aid = TUNE_IDS[i];
        if (aid >= 0 && aid < MAX_ACTION_ID) s_actionCosts[aid] = g.genes[i];
    }

    long long totalTurns = 0;
    double totalMs = 0.0;
    double totalFitness = 0.0;

    for (int i = 0; i < GA_EVAL_SEEDS; ++i) {
        const uint64_t seedToUse = evalSeeds[i];

        auto t0 = std::chrono::high_resolution_clock::now();
        int measuredTurn = SimpleParameterOptimizer::testParameters(players, seedToUse, actions, turnsLimit);
        auto t1 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = t1 - t0;

        totalTurns += measuredTurn;
        totalMs += elapsed.count();

        // fitness の作り方（既存ロジックを seed ごとに適用して足し算）
        double fitnessOne;
        if (measuredTurn >= 9999999) {
            fitnessOne = 1e9 + elapsed.count();
        } else {
            fitnessOne = static_cast<double>(measuredTurn) * 1000.0 + static_cast<int>(elapsed.count());
        }
        totalFitness += fitnessOne;
    }

    outTurns = static_cast<int>(totalTurns);
    outMs = totalMs;

    // restore
    s_actionCosts = backup;

    return totalFitness;
}

OptimResult SimpleParameterOptimizer::optimize(const Player players[2], uint64_t seed,
                                               const int actions[350], int maxTests, int turns)
{
    initActionCostsIfNeeded();

    OptimResult result;
    result.bestTurn = 9999;
    result.testCount = 0;
    result.found = false;

    // snapshot
    std::vector<double> originalCosts(MAX_ACTION_ID);
    for (int i = 0; i < MAX_ACTION_ID; ++i) originalCosts[i] = s_actionCosts[i];

    applyActionCostsToCostParams();

    // 初期評価
    int baseTurn = testParameters(players, seed, actions, turns);
    result.bestTurn = baseTurn;
    result.testCount = 1;
    result.found = (baseTurn < 9999);
    std::cout << "[SimpleParameterOptimizer GA] initial turn = " << baseTurn << std::endl;

    if (baseTurn <= 5) {
        applyActionCostsToCostParams();
        result.bestTurn = baseTurn;
        result.found = true;
        return result;
    }

    // GA 初期化
    std::random_device rd;
    std::mt19937 rng(static_cast<uint32_t>(seed ^ rd()));

    const size_t geneCount = TUNE_IDS.size();

    // ★ 追加：評価用 seed 生成関数（世代内で固定に使う）
    auto makeEvalSeeds = [&](uint64_t base) {
        std::array<uint64_t, GA_EVAL_SEEDS> s{};
        s[0] = base;
        for (int i = 1; i < GA_EVAL_SEEDS; ++i) {
            uint64_t r1 = static_cast<uint64_t>(rng());
            uint64_t r2 = static_cast<uint64_t>(rng());
            s[i] = base ^ (r1 << 32) ^ r2 ^ (0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(i));
        }
        return s;
    };

    std::array<uint64_t, GA_EVAL_SEEDS> evalSeeds = makeEvalSeeds(seed);


    // population 初期化: 現状値を中心にランダム化
    std::vector<GAGenome> population;
    population.reserve(GA_POPULATION);

    // get current start values
    std::vector<double> startVals(geneCount);
    for (size_t i = 0; i < geneCount; ++i) {
        int aid = TUNE_IDS[i];
        startVals[i] = (aid >= 0 && aid < MAX_ACTION_ID) ? s_actionCosts[aid] : DEFAULT_ACTION_COST;
    }

    std::normal_distribution<double> normDist(0.0, DEFAULT_STEP * 5.0);
    std::uniform_real_distribution<double> uni01(0.0, 1.0);

    for (int p = 0; p < GA_POPULATION; ++p) {
        GAGenome g;
        g.genes.resize(geneCount);
        for (size_t i = 0; i < geneCount; ++i) {
            // small random perturbation around start
            double v = startVals[i] + normDist(rng);
            if (v < 0.0) v = 0.0;
            g.genes[i] = v;
        }
        g.fitness = std::numeric_limits<double>::infinity();
        population.push_back(std::move(g));
    }

    int evaluations = 1; // already did base evaluation
    const int maxEvaluations = std::max(1, maxTests);
    // --------------------------------------------------------

    // GA loop
    while (evaluations < maxEvaluations) {
        // 世代内で evalSeeds を確率的に変える処理は GA の収束特性を壊すので無効化します。
        // 必要であれば外側ループで GA を複数回回す（各回で seed を変える）方針を推奨します。
        /*
        std::uniform_real_distribution<double> ud(0.0, 1.0);
        if (ud(rng) < GA_SEED_CHANGE_PROB) {
            seed = seed ^ (static_cast<uint64_t>(rng()) << 32) ^ rng();
            evalSeeds = makeEvalSeeds(seed);
            std::cout << "[GA] evalSeeds changed" << std::endl;
        }
        */

        constexpr int MAX_ID = 200;

        std::vector<double> tmp(MAX_ID + 1, 0.0);

        // id → 値 を埋める
        for (size_t i = 0; i < population[0].genes.size(); ++i) {
            int id = TUNE_IDS[i];
            tmp[id] = population[0].genes[i];
        }

        // constexpr 配列リテラルとして出力
        std::cout << "constexpr std::array<double, " << (MAX_ID + 1)
                  << "> GENOME = {\n";

        auto flag = false;
        auto flag1 = false;
        for (int id = 0; id <= MAX_ID; ++id) {
            if (tmp[id] > 0.0 && flag) {
                std::cout << "\n";
                flag1 = true;
            }
            if (tmp[id] > 0.0) {
                std::cout << "    /* " << id << " */ " << tmp[id];
            } else {
                if (flag1) {
                    std::cout << "    ";
                    flag1 = false;
                }
                std::cout << "0.0";
            }
            if (id != MAX_ID)
                std::cout << ",";
            if (tmp[id] > 0.0) {
                std::cout << "\n";
                flag = false;
                flag1 = true;
            } else {
                flag = true;
            }
        }

        std::cout << "\n};\n";

        // evaluate population members that are not yet evaluated
        for (auto &ind : population) {
            if (!std::isfinite(ind.fitness)) {
                int measuredTurn;
                double measuredMs;
                double fit = evaluateGenome(ind, players, evalSeeds, actions, turns, rng, measuredTurn, measuredMs);
                ind.fitness = fit;
                ind.measuredTurns = measuredTurn;
                ind.measuredMs = measuredMs;
                ++evaluations;
                ++result.testCount;

                std::cout << "[GA] eval=" << evaluations << " turn=" << measuredTurn
                          << " ms=" << measuredMs << " fitness=" << fit << std::endl;

                if (measuredTurn < result.bestTurn) {
                    result.bestTurn = measuredTurn;
                    result.found = true;
                    std::cout << "[GA] improvement -> bestTurn=" << result.bestTurn << std::endl;
                    std::cout << std::endl;
                }

                if (evaluations >= maxEvaluations) break;
            }
        }
        if (evaluations >= maxEvaluations) break;

        // sort by fitness (小さい方が良い)
        std::sort(population.begin(), population.end(), [](const GAGenome &a, const GAGenome &b){
            return a.fitness < b.fitness;
        });

        // ---------- Stability feedback: 最良個体の安定性を測って fitness にペナルティを与える ----------
        // NOTE:
        // - 再評価はローカル RNG を使い、main rng の状態を汚しません。
        // - 再評価は評価回数予算を消費します。予算が不足する場合はチェック回数を落とすかスキップします。
        if (!population.empty()) {
            GAGenome bestGenomeCopy = population.front(); // コピー（評価で書き換えられても本体は安全）
            int baselineTurn = bestGenomeCopy.measuredTurns;
            // baseline が未設定なら現在の evalSeeds で評価して基準を作る（評価回数を消費）
            if (baselineTurn == 0 || baselineTurn >= 9999) {
                int mTurn;
                double mMs;
                double baseFit = evaluateGenome(bestGenomeCopy, players, evalSeeds, actions, turns, rng, mTurn, mMs);
                baselineTurn = mTurn;
                // 注意: ここで本来の population.front().fitness は書き換えない（コピーに対して評価）
                ++evaluations;
                ++result.testCount;
            }

            // 予算に応じて実際に何回チェックできるかを決める
            int availableChecks = std::min(STABILITY_CHECKS, maxEvaluations - evaluations);
            double instabilitySum = 0.0;
            int performedChecks = 0;

            for (int i = 0; i < availableChecks; ++i) {
                // 別 seed を作るが、main evalSeeds は変更しない
                uint64_t altBase = seed ^ (0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(i + 1));
                auto altEvalSeeds = makeEvalSeeds(altBase);

                // local RNG: main rng を汚さないため、altBase に基づく固定シードを使う
                uint32_t localSeed32 = static_cast<uint32_t>((altBase >> 32) ^ (altBase & 0xFFFFFFFFu));
                std::mt19937 localRng(localSeed32);

                int mutatedActions[350];
                std::string insertedSummary;
                buildActionsWithRandomInserts(
                    actions,
                    mutatedActions,
                    localRng,
                    STABILITY_RANDOM_ACTION_POOL,
                    STABILITY_EXTRA_ACTIONS_MAX,
                    STABILITY_EXTRA_ACTION_INSERT_PROB,
                    &insertedSummary
                );

                // 評価はコピーで行う（population のデータを書き換えない）
                GAGenome copyForCheck = bestGenomeCopy;
                int mTurn;
                double mMs;
                double f = evaluateGenome(copyForCheck, players, altEvalSeeds, actions, turns, localRng, mTurn, mMs);

                // instability の定義（ここでは baseline より悪ければその差分をペナルティにする）
                if (mTurn > baselineTurn) {
                    instabilitySum += static_cast<double>(mTurn - baselineTurn);
                } else {
                    // もし baseline より良ければペナルティは 0（安定性向上の報酬は現在は与えず）
                    instabilitySum += 0.0;
                }

                ++performedChecks;
                ++evaluations;
                ++result.testCount;

                std::cout << "[GA] stability check " << i << " altTurn=" << mTurn
                          << " baseline=" << baselineTurn << " fit=" << f << std::endl;

                if (evaluations >= maxEvaluations) break;
            }

            if (performedChecks > 0) {
                double instabilityPenalty = (instabilitySum / static_cast<double>(performedChecks));
                double penaltyToApply = GA_INSTABILITY_WEIGHT * instabilityPenalty;

                // population.front() に対してペナルティを追加（GA に「不安定は悪」と学ばせる）
                population.front().fitness += penaltyToApply;

                std::cout << "[GA] applied instability penalty=" << penaltyToApply
                          << " (mean diff=" << (instabilitySum / performedChecks) << ")\n";

                // もう一度ソートして選択が安定するようにする
                std::sort(population.begin(), population.end(), [](const GAGenome &a, const GAGenome &b){
                    return a.fitness < b.fitness;
                });
            }
        }
        // ---------- end stability feedback ----------

        // エリート保存
        int eliteCount = std::max(1, GA_POPULATION / 10);
        std::vector<GAGenome> nextGen;
        nextGen.reserve(GA_POPULATION);
        for (int e = 0; e < eliteCount; ++e) nextGen.push_back(population[e]);

        // ルーレット／トーナメント: シンプルにトーナメント選択
        auto tournamentSelect = [&](int k)->const GAGenome& {
            int best = rng() % GA_POPULATION;
            for (int i = 1; i < k; ++i) {
                int cand = rng() % GA_POPULATION;
                if (population[cand].fitness < population[best].fitness) best = cand;
            }
            return population[best];
        };

        // 子生成
        while ((int)nextGen.size() < GA_POPULATION && evaluations < maxEvaluations) {
            // 選択
            const GAGenome &parentA = tournamentSelect(3);
            const GAGenome &parentB = tournamentSelect(3);

            GAGenome child;
            child.genes.resize(geneCount);
            // 交叉
            if (uni01(rng) < GA_CROSSOVER_PROB) {
                // arithmetic crossover (平均)
                for (size_t i = 0; i < geneCount; ++i) {
                    child.genes[i] = 0.5 * (parentA.genes[i] + parentB.genes[i]);
                }
            } else {
                // クローン
                child.genes = parentA.genes;
            }

            // 変異
            for (size_t i = 0; i < geneCount; ++i) {
                if (uni01(rng) < GA_MUTATION_PROB) {
                    double delta = normDist(rng);
                    child.genes[i] = clampDouble(child.genes[i] + delta, 0.0, 1e6);
                }
            }
            child.fitness = std::numeric_limits<double>::infinity();
            nextGen.push_back(std::move(child));

            // 評価は次ループで一括して行うことで評価回数制御をシンプルにしている
        }

        // 次世代へ
        population.swap(nextGen);
    }

    // 最終的な best を決定
    // population が評価済みであることを仮定するが、保険として評価されていないものは評価する
    for (auto &ind : population) {
        if (!std::isfinite(ind.fitness) && evaluations < maxEvaluations) {
            int measuredTurn;
            double measuredMs;
            double fit = evaluateGenome(ind, players, evalSeeds, actions, turns, rng, measuredTurn, measuredMs);
            ind.fitness = fit;
            ind.measuredTurns = measuredTurn;
            ind.measuredMs = measuredMs;
            ++evaluations;
            ++result.testCount;
        }
    }

    std::sort(population.begin(), population.end(), [](const GAGenome &a, const GAGenome &b){
        return a.fitness < b.fitness;
    });

    if (!population.empty()) {
        const GAGenome &best = population.front();
        // best を s_actionCosts に反映
        for (size_t i = 0; i < best.genes.size(); ++i) {
            int aid = TUNE_IDS[i];
            if (aid >= 0 && aid < MAX_ACTION_ID) s_actionCosts[aid] = best.genes[i];
        }
        applyActionCostsToCostParams();
        result.bestTurn = std::min(result.bestTurn, best.measuredTurns);
    }

    result.testCount = evaluations;
    std::cout << "[SimpleParameterOptimizer GA] done. bestTurn=" << result.bestTurn
              << " evaluations=" << evaluations << std::endl;

    return result;
}

// --- 既存の testParameters の定義（ヘッダ宣言に沿う） ---
int SimpleParameterOptimizer::testParameters(
                                            const Player players[2],
                                            uint64_t seed,
                                            const int actions[350],
                                            int turns)
{
    applyActionCostsToCostParams();

    Player copiedPlayers[2] = { players[0], players[1] };

    int gene[350];
    for (int i = 0; i < 350; ++i) {
        gene[i] = actions[i];
        if (actions[i] == -1) { gene[i] = -1; break; }
    }

    auto genome = ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 6000, gene, 0);

    if (genome.EnemyPlayer.hp <= 0) {
        return genome.turn - 1;
    } else {
        return 9999;
    }
}

#endif // OPTIMIZE_MODE
