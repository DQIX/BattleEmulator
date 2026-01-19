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
#include <future>
#include <limits>
#include <string>
#include <cstdint>

#include "lcg.h"
#include "setting.h"

// --- 設定 ---
// この配列に最適化対象の aABILITY_EXTRA_ACTIONS_MAX = 3;
//// 各挿入を行う確率（1.0=必ずction id を並べるだけで追加完了
static constexpr std::array<int, ids> TUNE_IDS = {
    BattleEmulator::ATTACK_ALLY,
    BattleEmulator::DRAGON_SLASH,
    BattleEmulator::DEFENCE,
    BattleEmulator::FLEE_ALLY,
    BattleEmulator::MEDICINAL_HERBS,
    BattleEmulator::HEAL,
    BattleEmulator::CRACK_ALLY,
    BattleEmulator::ACROBATIC_STAR,
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
    SimpleParameterOptimizerNode::SpecialMedicineCount,
};

static_assert(TUNE_IDS[ids - 1] != 0, "TUNE_IDS mismatch");

// action cost テーブル（一次真実源）
static thread_local std::array<double, MAX_ACTION_ID> s_actionCosts;

// --- 安定性チェック用: actions をコピーしてランダム挿入する（本体 actions は汚さない） ---
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
    std::fill_n(dstActions, 350, -1);

    // ★修正: 元 actions を dst にコピーして dataLen を正しく算出
    int dataLen = 0;
    for (; dataLen < 350; ++dataLen) {
        dstActions[dataLen] = srcActions[dataLen];
        if (srcActions[dataLen] == -1) break;
    }
    if (dataLen >= 350) dataLen = 349; // 念のため
    len = dataLen;

    if (maxInserts <= 0 || N == 0 || insertProb <= 0.0) {
        if (outInsertedSummary) *outInsertedSummary = "";
        return len;
    }

    std::uniform_real_distribution<double> uni01(0.0, 1.0);
    std::uniform_int_distribution<int> pickAction(0, static_cast<int>(N) - 1);

    std::ostringstream oss;
    int inserted = 0;

    // ... existing code ...
    // 「-1」を含まない実データ長（挿入位置計算に使う）
    // int dataLen = 0;
    // while (dataLen < 350 && dstActions[dataLen] != -1) ++dataLen;

    for (int k = 0; k < maxInserts; ++k) {
        if (uni01(rng) > insertProb) continue;
        if (dataLen >= 349) break; // これ以上挿入すると -1 が置けない

        const int actionToInsert = pool[pickAction(rng)];
        std::uniform_int_distribution<int> pickPos(0, dataLen);
        const int pos = pickPos(rng);

        // 右に1つずらして挿入
        for (int i = std::min(349, dataLen); i > pos; --i) {
            dstActions[i] = dstActions[i - 1];
        }
        dstActions[pos] = actionToInsert;
        ++dataLen;
        dstActions[dataLen] = -1;

        if (inserted > 0) oss << ", ";
        oss << BattleEmulator::getActionName(actionToInsert) << "@idx" << pos;
        ++inserted;
    }

    if (dstActions[0] == -1) {
        const int actionToInsert = pool[pickAction(rng)];
        dstActions[0] = actionToInsert;
        dstActions[1] = -1;
        len = 1;

        if (outInsertedSummary) {
            *outInsertedSummary =
                BattleEmulator::getActionName(actionToInsert) + std::string("@idx0");
        }
    }

    if (outInsertedSummary) *outInsertedSummary = oss.str();
    return dataLen;
}

// ★追加: thread_local を各スレッドで確実に初期化する
static inline void ensureActionCostsInitializedForThisThread() {
    static thread_local bool tlsInited = false;
    if (tlsInited) return;
    for (int i = 0; i < MAX_ACTION_ID; ++i) s_actionCosts[i] = DEFAULT_ACTION_COST;
    tlsInited = true;
}

static uint64_t evaluateGenome(
    GAGenome &g,
    const Player players[2],
    const std::array<uint64_t, GA_EVAL_SEEDS> &evalSeeds,
    const int actions[350],
    uint64_t turnsLimit,
    uint64_t &outTurns,
    double &outMs,
    uint64_t &outtotalHP,
    uint64_t &outfaultCount,
    uint64_t &outStabilityGap,
    uint64_t &outMaxDeviation
) {
    ensureActionCostsInitializedForThisThread();
    BattleEmulator::ResetTurnProcessed();

    // --- genes を action cost に適用 ---
    auto backup = s_actionCosts;
    for (size_t i = 0; i < g.genes.size(); ++i) {
        int aid = TUNE_IDS[i];
        if (aid >= 0 && aid < MAX_ACTION_ID) {
            s_actionCosts[aid] = g.genes[i];
        }
    }

    constexpr uint64_t kFailedTurnSentinel = 9999;

    // --- 評価用集計 ---
    uint64_t bestTurn = kFailedTurnSentinel;   // ★最重要
    double   successTurnSum = 0.0;
    uint64_t successCount = 0;

    uint64_t totalMs10 = 0;
    int totalHP = 0;
    uint64_t totalHerb = 0;
    uint64_t faultCount = 0;

    std::vector<double> successTurns{};

    for (int i = 0; i < GA_EVAL_SEEDS; ++i) {
        const uint64_t seedToUse = evalSeeds[i];

        auto t0 = std::chrono::high_resolution_clock::now();

        uint64_t turn = 0;
        int enemyHp = 0;
        int herbcount = 0;

        SimpleParameterOptimizer::testParameters(
            players, seedToUse, actions,
            static_cast<int>(turnsLimit),
            turn, enemyHp, herbcount
        );

        auto t1 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = t1 - t0;

        totalMs10 += static_cast<uint64_t>(elapsed.count() * 10.0);
        totalHP += enemyHp;
        totalHerb += setting::herbcount - herbcount;

        if (enemyHp == 0) {
            // ★成功 seed のみ評価に使う
            bestTurn = std::min(bestTurn, turn);
            successTurnSum += static_cast<double>(turn);
            ++successCount;
            successTurns.push_back(static_cast<double>(turn));
        } else {
            ++faultCount;
        }
    }

    // --- 成功 seed のみで平均 ---
    const double avgTurn =
        (successCount > 0)
            ? (successTurnSum / static_cast<double>(successCount))
            : static_cast<double>(kFailedTurnSentinel);

    const double avgHP =
        (successCount > 0)
            ? static_cast<double>(totalHP) / static_cast<double>(successCount)
            : static_cast<double>(totalHP);

    const double avgherb =
        (successCount > 0)
            ? static_cast<double>(totalHerb) / static_cast<double>(successCount)
            : static_cast<double>(totalHerb);

    const double avgMs =
        static_cast<double>(totalMs10) / (GA_EVAL_SEEDS * 10.0);

    // --- 出力 ---
    outTurns = (bestTurn < kFailedTurnSentinel)
        ? bestTurn
        : kFailedTurnSentinel;

    outMs = avgMs;
    outtotalHP = static_cast<uint64_t>(avgHP);
    outfaultCount = faultCount;

    if (successCount >= 2) {
        double worstTurn = 0.0;
        for (double t : successTurns) {
            worstTurn = std::max(worstTurn, t);
        }

        double stabilityGap = worstTurn - avgTurn;

        // median-based deviation
        std::sort(successTurns.begin(), successTurns.end());
        double median = successTurns[successTurns.size() / 2];
        double maxDeviation = 0.0;
        for (double t : successTurns) {
            maxDeviation = std::max(maxDeviation, std::abs(t - median));
        }

        // out 用（整数化）
        outStabilityGap = static_cast<uint64_t>(stabilityGap * 100.0);
        outMaxDeviation = static_cast<uint64_t>(maxDeviation * 100.0);
    }else {
        outStabilityGap = 0;
        outMaxDeviation = 0;
    }

    // --- fitness 合成 ---
    // ★ OR 禁止。必ず +
    // ★ bestTurn を主、avgTurn を副
    constexpr int HERB_SHIFT      = 0;   // 8bit
    constexpr int FAULT_SHIFT     = 8;   // 3bit
    constexpr int DEVIATION_SHIFT = 11;  // 11bit
    constexpr int STABILITY_SHIFT = 22;  // 11bit
    constexpr int SUCCESS_SHIFT   = 33;  // 2bit
    constexpr int AVG_SHIFT       = 35;  // 12bit
    constexpr int BEST_SHIFT      = 47;  // 12bit


    // ---- 共通で整数化 ----
    const auto best100 = static_cast<uint64_t>(bestTurn * 100);
    const auto avg100  = static_cast<uint64_t>(avgTurn  * 100.0);
    const auto herb100 = static_cast<uint64_t>(avgherb  * 100.0);

    // ---- 失敗フェーズ（全 seed 討伐できていない）----
    if (bestTurn >= kFailedTurnSentinel) {
        const uint64_t f =
              (best100 << BEST_SHIFT)
            + (avg100  << AVG_SHIFT)
            + (successCount << SUCCESS_SHIFT)
            + (faultCount << FAULT_SHIFT)
            + (herb100 << HERB_SHIFT);

        s_actionCosts = backup;
        return f;
    }

    // ---- 成功フェーズ ----
    uint64_t f =
          (best100 << BEST_SHIFT)            // 絶対主軸
        + (avg100  << AVG_SHIFT)             // 平均性能
        + (outStabilityGap << STABILITY_SHIFT)
        + (successCount << SUCCESS_SHIFT)
        + (outMaxDeviation << DEVIATION_SHIFT)
        + (faultCount << FAULT_SHIFT)
        + (herb100 << HERB_SHIFT);

    s_actionCosts = backup;
    return f;
}

// --- testParameters の定義（参照版） ---
void SimpleParameterOptimizer::testParameters(
    const Player players[2],
    uint64_t seed,
    const int actions[350],
    int turns,
    uint64_t &outTurn,
    int &outEnemyHp,
    int &outherb
){
    lcg::init(seed);
    Player copiedPlayers[2] = { players[0], players[1] };

    int gene[350];
    for (int i = 0; i < 350; ++i) {
        gene[i] = actions[i];
        if (actions[i] == -1) { gene[i] = -1; break; }
    }

    auto genome = ActionOptimizer::RunAlgorithm(copiedPlayers, seed, turns, 5000, gene, 0);

    outTurn = static_cast<uint64_t>(genome.turn);
    outEnemyHp = genome.EnemyPlayer.hp;
    outherb = copiedPlayers[0].medicinal_herbs_count;
}

// ... existing code ...

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
    ensureActionCostsInitializedForThisThread();

    if (action < 0 || action >= MAX_ACTION_ID) {
        throw std::invalid_argument("Invalid action ID");
    }
    return s_actionCosts[action];
}

OptimResult SimpleParameterOptimizer::optimize(const Player players[2], uint64_t seed,
                                               const int actions[350], int maxTests, int turns)
{
    initActionCostsIfNeeded();
    ensureActionCostsInitializedForThisThread();

    OptimResult result;
    result.bestTurn = 9999;
    result.testCount = 0;
    result.found = false;

    // snapshot
    std::vector<double> originalCosts(MAX_ACTION_ID);
    for (int i = 0; i < MAX_ACTION_ID; ++i) originalCosts[i] = s_actionCosts[i];

    applyActionCostsToCostParams();

    std::random_device rd;
    std::mt19937 rng(static_cast<uint32_t>(seed ^ rd()));

    const size_t geneCount = TUNE_IDS.size();

    auto makeEvalSeeds = [&](uint64_t base) {
        std::array<uint64_t, GA_EVAL_SEEDS> s{};
        s[0] = base;
        for (int i = 1; i < GA_EVAL_SEEDS; ++i) {
            auto r1 = static_cast<uint64_t>(rng());
            auto r2 = static_cast<uint64_t>(rng());
            s[i] = base ^ (r1 << 32) ^ r2 ^ (0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(i));
        }
        return s;
    };

    std::array<uint64_t, GA_EVAL_SEEDS> evalSeeds = makeEvalSeeds(seed);

    std::vector<double> startVals(geneCount);
    for (size_t i = 0; i < geneCount; ++i) {
        int aid = TUNE_IDS[i];
        startVals[i] = (aid >= 0 && aid < MAX_ACTION_ID) ? s_actionCosts[aid] : DEFAULT_ACTION_COST;
    }

    {
        GAGenome baseline;
        baseline.genes = startVals;

        uint64_t baselineTurn = 0;
        uint64_t outtotalHP = 0;
        uint64_t outfaultCount = 0;
        uint64_t outStabilityGap = 0;
        uint64_t outMaxDeviation = 0;
        double baselineMs = 0.0;
        (void)evaluateGenome(baseline, players, evalSeeds, actions, turns, baselineTurn, baselineMs, outtotalHP, outfaultCount, outStabilityGap, outMaxDeviation);

        result.bestTurn = baselineTurn;
        result.testCount = 1;
        result.found = (baselineTurn < 9999);

        std::cout << "[SimpleParameterOptimizer GA] baseline(avg) turn = " << baselineTurn
                  << " ms=" << baselineMs << std::endl;
    }

    // population 初期化: 現状値を中心にランダム化
    std::vector<GAGenome> population;
    population.reserve(GA_POPULATION);

    std::normal_distribution<double> normDist(0.0, DEFAULT_STEP * 5.0);
    std::uniform_real_distribution<double> uni01(0.0, 1.0);

    for (int p = 0; p < GA_POPULATION; ++p) {
        GAGenome g;
        g.genes.resize(geneCount);
        for (size_t i = 0; i < geneCount; ++i) {
            double v = startVals[i] + normDist(rng);
            g.genes[i] = v;
        }
        g.fitness = kUnevaluatedFitness;
        g.measuredTurns = 0;
        g.measuredMs = 0.0;
        population.push_back(std::move(g));
    }

    uint64_t evaluations = 1; // baseline 評価1回消費済み
    const uint64_t maxEvaluations = std::max(1, maxTests);

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

        // --- ここから並列評価ブロック ---
        // 未評価 index を収集（予算も考慮）
        std::vector<int> pending;
        pending.reserve(population.size());

        const auto budget = maxEvaluations - evaluations;
        if (budget > 0) {
            for (int i = 0; i < static_cast<int>(population.size()) && static_cast<int>(pending.size()) < budget; ++i) {
                // ★修正: 未評価だけを pending に入れる（条件逆転バグ修正）
                if (population[i].fitness == kUnevaluatedFitness) {
                    pending.push_back(i);
                }
            }
        }

        if (!pending.empty()) {
            const auto numThreads = std::min(kNumThreads, pending.size());
            const auto chunkSize = (static_cast<uint64_t>(pending.size()) + numThreads - 1) / numThreads;

            std::vector<std::future<std::vector<EvalResult>>> futures;
            futures.reserve(numThreads);

            for (int t = 0; t < numThreads; ++t) {
                const auto start = t * chunkSize;
                const auto end = std::min(static_cast<uint64_t>(pending.size()), start + chunkSize);
                if (start >= end) break;

                const uint64_t threadSeed = seed ^ (0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(t + 1));

                futures.push_back(std::async(
                    std::launch::async,
                    evaluateGenomeRange,
                    &population,
                    &pending,
                    start,
                    end,
                    players,
                    std::cref(evalSeeds),
                    actions,
                    turns,
                    threadSeed
                ));
            }

            for (auto &fut : futures) {
                auto results = fut.get();
                for (const auto &r : results) {
                    auto &ind = population[r.index];

                    // r.fitness は uint64_t。population[].fitness は double のまま想定のため明示キャスト
                    ind.fitness = r.fitness;
                    ind.measuredTurns = r.measuredTurns;
                    ind.measuredMs = r.measuredMs;
                    ind.totalHP = r.totalHP;
                    ind.faultCount = r.faultCount;
                    ind.stabilityGap = r.stabilityGap;
                    ind.maxDeviation = r.maxDeviation;
                    ++evaluations;
                    ++result.testCount;

                    std::cout << "[GA] eval=" << evaluations << " turn=" << r.measuredTurns
                              << " ms=" << r.measuredMs << " fitness=" << r.fitness << " totalHP=" << r.totalHP << " faultCount=" << r.faultCount <<  " stabilityGap=" << r.stabilityGap << " maxDeviation=" << r.maxDeviation <<" best=" << result.bestTurn << std::endl;

                    bool improved = false;

                    if (r.measuredTurns < result.bestTurn) {
                        improved = true;
                    }
                    else if (r.measuredTurns == result.bestTurn) {
                        // 0 は「未定義」なので無視
                        if (r.stabilityGap > 0 &&
                            r.stabilityGap < result.bestStableGap) {
                            improved = true;
                            }
                        else if (r.stabilityGap == result.bestStableGap &&
                                 r.maxDeviation > 0 &&
                                 r.maxDeviation < result.bestStableDeviation) {
                            improved = true;
                                 }
                    }
                    if (improved){
                        result.bestTurn = r.measuredTurns;
                        result.bestStableGap = r.stabilityGap;
                        result.bestStableDeviation = r.maxDeviation;
                        result.found = true;
                        std::cout << "[GA] improvement -> bestTurn=" << result.bestTurn << std::endl;
                        std::cout << std::endl;

                        // constexpr 配列リテラルとして出力
                        std::cout << "constexpr std::array<double, " << (MAX_ID + 1)
                                  << "> GENOME = {\n";

                        std::vector<double> tmp(MAX_ID + 1, 0.0);

                        // id → 値 を埋める
                        for (size_t i = 0; i < population[r.idx].genes.size(); ++i) {
                            int id = TUNE_IDS[i];
                            tmp[id] = population[r.idx].genes[i];
                        }

                        auto flag = false;
                        auto flag1 = false;
                        for (int id = 0; id <= MAX_ID; ++id) {
                            if (tmp[id] != 0.0 && flag) {
                                std::cout << "\n";
                                flag1 = true;
                            }
                            if (tmp[id] != 0.0) {
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
                    }

                    if (evaluations >= maxEvaluations) break;
                }
                if (evaluations >= maxEvaluations) break;
            }
        }

        // sort by fitness (小さい方が良い)
        std::sort(population.begin(), population.end(), [](const GAGenome &a, const GAGenome &b){
            return a.fitness < b.fitness;
        });

        // ---------- Stability feedback ----------
        if (false && !population.empty()) {
            GAGenome bestGenomeCopy = population.front();
            auto baselineTurn = bestGenomeCopy.measuredTurns;
            if (baselineTurn == 0 || baselineTurn >= 9999) {
                uint64_t mTurn;
                double mMs;
                uint64_t outtotalHP = 0;
                uint64_t outfaultCount = 0;
                uint64_t outStabilityGap = 0;
                uint64_t outMaxDeviation = 0;
                uint64_t baseFit = evaluateGenome(bestGenomeCopy, players, evalSeeds, actions, turns, mTurn, mMs, outtotalHP, outfaultCount, outStabilityGap, outMaxDeviation);
                baselineTurn = mTurn;
                ++evaluations;
                ++result.testCount;
            }

            auto availableChecks = std::min(STABILITY_CHECKS, maxEvaluations - evaluations);
            if (availableChecks > 0) {
                const auto numThreads = std::min(kNumThreads, availableChecks);
                const auto chunkSize = (availableChecks + numThreads - 1) / numThreads;

                std::vector<std::future<StabilityChunkResult>> futures;
                futures.reserve(numThreads);

                for (int t = 0; t < numThreads; ++t) {
                    const auto beginIdx = t * chunkSize;
                    const auto endIdx = std::min(availableChecks, beginIdx + chunkSize);
                    if (beginIdx >= endIdx) break;

                    futures.push_back(std::async(
                        std::launch::async,
                        stabilityCheckRange,
                        &bestGenomeCopy,
                        baselineTurn,
                        beginIdx,
                        endIdx,
                        seed,
                        players,
                        actions,
                        turns
                    ));
                }

                uint64_t instabilitySum = 0;
                uint64_t performedChecks = 0;

                for (auto &f : futures) {
                    auto r = f.get();
                    instabilitySum += r.instabilitySum;
                    performedChecks += r.performed;
                    std::cout << "[GA] stability check=" << r.performed << " instability=" << r.instabilitySum << " turns=" << r.turns <<  " turn=" << ((r.turns / r.performed) / GA_EVAL_SEEDS) << std::endl;
                }

                evaluations += performedChecks;
                result.testCount += performedChecks;

                if (performedChecks > 0) {
                    uint64_t instabilityPenalty = (instabilitySum / static_cast<uint64_t>(performedChecks));
                    uint64_t penaltyToApply = GA_INSTABILITY_WEIGHT * instabilityPenalty;
                    population.front().fitness += penaltyToApply;

                    std::cout << "[GA] stability checks(performed)=" << performedChecks
                              << " baseline=" << baselineTurn
                              << " applied penalty=" << penaltyToApply
                              << " (mean diff=" << (instabilitySum / static_cast<uint64_t>(performedChecks)) << ")\n";

                    std::sort(population.begin(), population.end(), [](const GAGenome &a, const GAGenome &b){
                        return a.fitness < b.fitness;
                    });
                }
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
            int best = static_cast<int>(rng() % GA_POPULATION);
            for (int i = 1; i < k; ++i) {
                int cand = static_cast<int>(rng() % GA_POPULATION);
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
                    child.genes[i] = clampDouble(child.genes[i] + delta, -1e6-1, 1e6);
                }
            }
            // ★修正: 整数に infinity() は使わない
            child.fitness = kUnevaluatedFitness;
            child.measuredTurns = 0;
            child.measuredMs = 0.0;
            nextGen.push_back(std::move(child));

            // 評価は次ループで一括して行うことで評価回数制御をシンプルにしている
        }

        // 次世代へ
        population.swap(nextGen);
    }

    // 最終的な best を決定
    // population が評価済みであることを仮定するが、保険として評価されていないものは評価する
    for (auto &ind : population) {
        if (ind.fitness == kUnevaluatedFitness && evaluations < maxEvaluations) {
            uint64_t measuredTurn;
            double measuredMs;
            uint64_t outtotalHP = 0;
            uint64_t outfaultCount = 0;
            uint64_t outStabilityGap = 0;
            uint64_t outMaxDeviation = 0;
            uint64_t fit = evaluateGenome(ind, players, evalSeeds, actions, turns, measuredTurn, measuredMs, outtotalHP, outfaultCount, outStabilityGap, outMaxDeviation);
            ind.fitness = fit;
            ind.measuredTurns = measuredTurn;
            ind.measuredMs = measuredMs;
            ind.totalHP = outtotalHP;
            ind.faultCount = outfaultCount;
            ++evaluations;
            ++result.testCount;
        }
    }

    std::sort(population.begin(), population.end(), [](const GAGenome &a, const GAGenome &b){
        return a.fitness < b.fitness;
    });

    std::cout << "done" << std::endl;

    return result;
}
// --- evaluateGenomeRange: EvalResult::fitness は uint64_t に準拠 ---
std::vector<EvalResult> SimpleParameterOptimizer::evaluateGenomeRange(std::vector<GAGenome> *population,
    const std::vector<int> *pendingIndices, int start, int end, const Player players[2],
    const std::array<uint64_t, GA_EVAL_SEEDS> &evalSeeds, const int actions[350], int turnsLimit,
    uint64_t seedForThread) {

    s_actionCosts.fill(-1.0);

    std::vector<EvalResult> out;
    out.reserve(static_cast<size_t>(std::max(0, end - start)));

    for (int k = start; k < end; ++k) {
        const int idx = (*pendingIndices)[k];
        auto &ind = (*population)[idx];

        uint64_t measuredTurn = 0;
        double measuredMs = 0.0;

        uint64_t outtotalHP = 0;
        uint64_t outfaultCount = 0;
        uint64_t outStabilityGap = 0;
        uint64_t outMaxDeviation = 0;

        uint64_t fit = evaluateGenome(ind, players, evalSeeds, actions, turnsLimit, measuredTurn, measuredMs, outtotalHP, outfaultCount, outStabilityGap, outMaxDeviation);

        EvalResult r;
        r.index = idx;
        r.fitness = fit; // uint64_t
        r.measuredTurns = measuredTurn;
        r.measuredMs = measuredMs;
        r.totalHP = outtotalHP;
        r.faultCount = outfaultCount;
        r.stabilityGap = outStabilityGap;
        r.maxDeviation = outMaxDeviation;
        r.idx = idx;
        out.push_back(r);
    }

    return out;
}

// --- 追加: stability check 用の決定的 seed 生成（スレッドセーフ） ---
static inline uint64_t splitmix64(uint64_t &x) {
    uint64_t z = (x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static std::array<uint64_t, GA_EVAL_SEEDS> makeEvalSeedsDeterministic(uint64_t base) {
    std::array<uint64_t, GA_EVAL_SEEDS> s{};
    uint64_t x = base;
    for (int i = 0; i < GA_EVAL_SEEDS; ++i) {
        s[i] = splitmix64(x);
    }
    return s;
}

StabilityChunkResult SimpleParameterOptimizer::stabilityCheckRange(const GAGenome *bestGenomeCopy, int baselineTurn,
    int beginIdx, int endIdx, uint64_t baseSeed, const Player players[2], const int actions[350], int turnsLimit) {
    StabilityChunkResult out{};
    uint64_t turns = 0;

    for (int i = beginIdx; i < endIdx; ++i) {
        uint64_t altBase = baseSeed ^ (0x9E3779B97F4A7C15ULL * static_cast<uint64_t>(i + 1));
        auto altEvalSeeds = makeEvalSeedsDeterministic(altBase);

        auto localSeed32 = static_cast<uint32_t>((altBase >> 32) ^ (altBase & 0xFFFFFFFFu));
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

        GAGenome copyForCheck = *bestGenomeCopy;
        uint64_t mTurn = 0;
        double mMs = 0.0;
        uint64_t outtotalHP = 0;
        uint64_t outfaultCount = 0;
        uint64_t outStabilityGap = 0;
        uint64_t outMaxDeviation = 0;

        // NOTE: mutatedActions を使う（安定性チェックの意図どおり）
        (void)evaluateGenome(copyForCheck, players, altEvalSeeds, mutatedActions, turnsLimit, mTurn, mMs, outtotalHP, outfaultCount, outStabilityGap, outMaxDeviation);

        turns += mTurn;

        if (mTurn > baselineTurn) {
            out.instabilitySum += static_cast<uint64_t>(mTurn - baselineTurn);
        }
        ++out.performed;
    }

    out.turns = turns;

    return out;
}

#endif // OPTIMIZE_MODE
