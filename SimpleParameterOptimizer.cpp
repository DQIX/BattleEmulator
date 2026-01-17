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
static constexpr int ids = 25;
static constexpr double DEFAULT_ACTION_COST = 1.0;
static constexpr double DEFAULT_STEP = 0.5; // 変異の基本スケール

// GA パラメータ（必要なら調整）
static constexpr int GA_POPULATION = 50;
static constexpr int GA_OFFSPRING = 30; // 1世代あたり生成する子の数
static constexpr double GA_MUTATION_PROB = 0.15; // 各遺伝子が変異する確率
static constexpr double GA_CROSSOVER_PROB = 0.9; // 親から交叉する確率
static constexpr double GA_SEED_CHANGE_PROB = 0.2; // 評価時に初期シードを変える確率

// この配列に最適化対象の action id を並べるだけで追加完了
static constexpr std::array<int, ids> TUNE_IDS = {
    BattleEmulator::MIDHEAL,
    BattleEmulator::SPECIAL_ANTIDOTE,
    BattleEmulator::SPECIAL_MEDICINE,
    BattleEmulator::DOUBLE_UP,
    BattleEmulator::PSYCHE_UP_ALLY,
    BattleEmulator::FLEE_ALLY,
    BattleEmulator::BUFF,
    BattleEmulator::MULTITHRUST,
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
    uint64_t baseSeed,
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

    // たまに baseSeed を変えて多様性を評価
    uint64_t seedToUse = baseSeed;


    auto t0 = std::chrono::high_resolution_clock::now();
    int measuredTurn = SimpleParameterOptimizer::testParameters(players, seedToUse, actions, turnsLimit);
    auto t1 = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = t1 - t0;

    outTurns = measuredTurn;
    outMs = elapsed.count();

    // restore
    s_actionCosts = backup;

    // fitness の作り方: ターン数を優先し、ミリ秒は二次的に効くように組み合わせ
    // (turns * 1000) + time_ms。ターン数が小さい方を優先するため重みを大きめにする。
    double fitness;
    if (measuredTurn >= 99999) {
        // 失敗は非常に悪いスコアにする
        fitness = 1e9 + outMs;
    } else {
        fitness = static_cast<double>(measuredTurn) * 10000.0 + static_cast<int>(outMs);
    }
    return fitness;
}

OptimResult SimpleParameterOptimizer::optimize(const Player players[2], uint64_t seed,
                                               const int actions[350], int maxTests, int turns)
{
    initActionCostsIfNeeded();

    OptimResult result;
    result.bestTurn = 999;
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
    result.found = (baseTurn < 999);
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

    // GA loop
    while (evaluations < maxEvaluations) {
        std::uniform_real_distribution<double> ud(0.0, 1.0);
        if (ud(rng) < GA_SEED_CHANGE_PROB) {
            // small perturbation but deterministic given rng
            seed = seed ^ (static_cast<uint64_t>(rng()) << 32) ^ rng();
            std::cout << "[GA] seed change" << std::endl;
        }

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
            }else {
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
            }else {
                flag = true;
            }
        }

        std::cout << "\n};\n";



        // evaluate population members that are not yet evaluated
        for (auto &ind : population) {
            if (!std::isfinite(ind.fitness)) {
                int measuredTurn;
                double measuredMs;
                double fit = evaluateGenome(ind, players, seed, actions, turns, rng, measuredTurn, measuredMs);
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
                    std::cout << "[GA] improvement -> bestTurn=" << baseTurn << std::endl;

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
            double fit = evaluateGenome(ind, players, seed, actions, turns, rng, measuredTurn, measuredMs);
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
        return 9999999;
    }
}

#endif // OPTIMIZE_MODE
