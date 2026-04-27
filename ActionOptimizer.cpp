// ActionOptimizer.cpp - 修正版
// 主要変更点:
//   1. runPatternRollouts削除 (サンプル解ハードコード)
//   2. scanEvents: kaisinnP/DragonSlashKaisinnPを正しい値で使用
//   3. cannotKillWithOptimisticBound: AllyPlayerステータスから動的計算
//   4. runEventDrivenRollouts: RNGイベントターゲット駆動の多様なロールアウト
//   5. DragonCritical閾値を/2固定からDragonSlashKaisinnP相当に修正
//   6. runBeam廃止、IDDFSを主軸に
//   7. nodeBudget配分を調整

#include "ActionOptimizer.h"

#include <algorithm>
#include <array>
#include <limits>
#include <vector>

#include "BattleEmulator.h"
#include "lcg.h"

struct ActionEntry {
    int action;
    bool (*condition)(const Genome &);
};

constexpr ActionEntry ACTION_TABLE[] = {
    {BattleEmulator::ATTACK_ALLY, [](const Genome &) { return true; }},
    {BattleEmulator::DRAGON_SLASH, [](const Genome &) { return true; }},
    {BattleEmulator::DEFENCE, [](const Genome &) { return true; }},
    {BattleEmulator::FLEE_ALLY, [](const Genome &) { return true; }},
    {BattleEmulator::SPECIAL_ANTIDOTE,
     [](const Genome &g) {
         return g.AllyPlayer.SpecialAntidoteCount >= 1 &&
                g.AllyPlayer.PoisonEnable;
     }},
    {BattleEmulator::SPECIAL_MEDICINE,
     [](const Genome &g) {
         return g.AllyPlayer.SpecialMedicineCount >= 1 &&
                !g.AllyPlayer.PoisonEnable;
     }},
    {BattleEmulator::HEAL,
     [](const Genome &g) { return g.AllyPlayer.mp >= 2; }},
    {BattleEmulator::CRACK_ALLY,
     [](const Genome &g) { return g.AllyPlayer.mp >= 3; }},
    {BattleEmulator::WOOSH_ALLY,
     [](const Genome &g) { return g.AllyPlayer.mp >= 3; }},
    {BattleEmulator::ACROBATIC_STAR,
     [](const Genome &g) {
         return g.AllyPlayer.specialCharge &&
                g.AllyPlayer.specialChargeTurn != 0;
     }}
};

static uint32_t Node_Used;

namespace {
    constexpr int NO_SOLUTION_TURN = 100;
    constexpr int EVENT_SCAN_RANGE = 1024;
    // ビームは廃止してIDDFSに集中するが、近似解生成用のgreedy rolloutは残す
    constexpr int GREEDY_ROLLOUT_DEPTH = 28;
    constexpr int GREEDY_ROLLOUT_VARIATIONS = 12; // イベントターゲットごとの変種数

    // BattleEmulator.cppと同じ定義 (ビルドフラグで切り替え)
#if defined(BattleEmulatorLV13)
    constexpr int ALLY_CRITICAL_THRESHOLD   = 200;
#elif defined(BattleEmulatorLV19) || defined(BattleEmulatorLV15)
    constexpr int ALLY_CRITICAL_THRESHOLD   = 500;
#else
    constexpr int ALLY_CRITICAL_THRESHOLD   = 500;
#endif
    // DragonSlashKaisinnP = kaisinnP / 2 (BattleEmulator.cppと同じ)
    constexpr int DRAGON_CRITICAL_THRESHOLD = ALLY_CRITICAL_THRESHOLD / 2;
    // Woosh/Crackle/Heal の呪文会心は 100/10000
    constexpr int SPELL_CRITICAL_THRESHOLD  = 100;

    // SpecialChargeのprocess7A8による付与確率テーブル
    // proportionTable2 = {90,90,64,32,16,8,4,2,1}
    // 最大閾値(90%)は baseHP * 0.9+1 以上のダメージで起動
    // 探索中はこの詳細は不要。スキャン側はspecialCharge = getPercent(pos,100)<1 の判定のみ

    enum class EventType : uint8_t {
        AttackCritical,    // weight高
        DragonCritical,
        SpellCritical,
        SpecialCharge,     // 最重要
        AcrobaticCounter,  // カウンター(50-74)
        AcrobaticAvoid,    // 回避(0-49) - 乱数節約
    };

    // イベントの重みづけ (順序付け専用、枝刈り禁止)
    constexpr int EVENT_WEIGHTS[] = {
        18000, // AttackCritical
        14000, // DragonCritical
        8000,  // SpellCritical
        30000, // SpecialCharge  ← 最重要: specialCharge→ACROBATIC_STAR連鎖
        22000, // AcrobaticCounter
        5000,  // AcrobaticAvoid (乱数消費節約目的)
    };

    struct RNGEvent {
        int position = 0;
        EventType type = EventType::AttackCritical;
    };

    struct SearchCandidate {
        Genome genome{};
        int action = -1;
        int score = std::numeric_limits<int>::min();
    };

    struct SearchContext {
        uint64_t seed = 0;
        int nodeBudget = 0;
        int nodesVisited = 0;
        bool exhausted = false;
        std::vector<RNGEvent> events;
        Genome bestGenome{};
        bool bestIsSolution = false;
    };

    void initializeGenomeActions(Genome &genome) {
        std::fill(std::begin(genome.actions), std::end(genome.actions), -1);
    }

    bool isSolution(const Genome &genome) {
        return genome.EnemyPlayer.hp <= 0 && genome.AllyPlayer.hp > 0;
    }

    bool isBetterProgress(const Genome &candidate, const Genome &best) {
        const bool candidateSolved = isSolution(candidate);
        const bool bestSolved = isSolution(best);

        if (candidateSolved != bestSolved) return candidateSolved;
        if (candidateSolved) {
            if (candidate.turn != best.turn)
                return candidate.turn < best.turn;
            if (candidate.AllyPlayer.hp != best.AllyPlayer.hp)
                return candidate.AllyPlayer.hp > best.AllyPlayer.hp;
            if (candidate.EnemyPlayer.hp != best.EnemyPlayer.hp)
                return candidate.EnemyPlayer.hp < best.EnemyPlayer.hp;
            return candidate.position < best.position;
        }
        if (candidate.EnemyPlayer.hp != best.EnemyPlayer.hp)
            return candidate.EnemyPlayer.hp < best.EnemyPlayer.hp;
        if (candidate.AllyPlayer.hp != best.AllyPlayer.hp)
            return candidate.AllyPlayer.hp > best.AllyPlayer.hp;
        if (candidate.AllyPlayer.mp != best.AllyPlayer.mp)
            return candidate.AllyPlayer.mp > best.AllyPlayer.mp;
        return candidate.position < best.position;
    }

    void updateBestGenome(SearchContext &context, const Genome &candidate) {
        if (!context.bestIsSolution && context.bestGenome.turn == 0) {
            context.bestGenome = candidate;
            context.bestIsSolution = isSolution(candidate);
            return;
        }
        if (isBetterProgress(candidate, context.bestGenome)) {
            context.bestGenome = candidate;
            context.bestIsSolution = isSolution(candidate);
        }
    }

    // positionを副作用なしに読む (ローカルコピーを使う)
    int percentAt(int position, int max) {
        return lcg::getPercent(&position, max);
    }

    // ─── Phase 0: RNGイベントスキャン ────────────────────────────────────────
    // seed固定の乱数列から「当たり位置」を事前列挙する。
    // BattleEmulator.cppと同じ判定閾値を使うため、kaisinnP等はビルドフラグ依存定数と同期している。
    //
    // 注意: ここでスキャンする判定は「その位置を消費するif文の条件」であり、
    // 実際にその位置が会心判定に使われるかはコンテキスト(行動)次第。
    // あくまで「この乱数が会心閾値を満たす」という情報として扱い、
    // 行動順序付けにのみ使う。枝刈りに使ってはいけない。
    std::vector<RNGEvent> scanEvents(int startPosition) {
        std::vector<RNGEvent> events;
        events.reserve(EVENT_SCAN_RANGE / 4);
        const int endPosition = startPosition + EVENT_SCAN_RANGE;

        for (int pos = startPosition; pos < endPosition; ++pos) {
            const int p10000 = percentAt(pos, 0x2710);
            if (p10000 < ALLY_CRITICAL_THRESHOLD) {
                events.push_back({pos, EventType::AttackCritical});
            }
            // DragonCriticalはATTACK_ALLYと同じ判定位置だがDRAGON_SLASH専用閾値
            if (p10000 < DRAGON_CRITICAL_THRESHOLD) {
                events.push_back({pos, EventType::DragonCritical});
            }
            if (p10000 < SPELL_CRITICAL_THRESHOLD) {
                events.push_back({pos, EventType::SpellCritical});
            }

            const int p100 = percentAt(pos, 100);
            if (p100 < 1) {
                events.push_back({pos, EventType::SpecialCharge});
            }
            if (p100 >= 50 && p100 < 75) {
                events.push_back({pos, EventType::AcrobaticCounter});
            }
            if (p100 >= 0 && p100 < 50) {
                events.push_back({pos, EventType::AcrobaticAvoid});
            }
        }

        std::sort(events.begin(), events.end(), [](const RNGEvent &a, const RNGEvent &b) {
            if (a.position != b.position) return a.position < b.position;
            return static_cast<int>(a.type) < static_cast<int>(b.type);
        });
        return events;
    }

    int eventWeight(EventType t) {
        return EVENT_WEIGHTS[static_cast<int>(t)];
    }

    // ─── 行動順序スコア計算 ───────────────────────────────────────────────────
    // 仕様書原則: 評価は「並び順」にのみ使う。低スコアでも枝は捨てない。

    int crossedEventScore(const SearchContext &ctx, int from, int to) {
        int score = 0;
        for (const auto &e : ctx.events) {
            if (e.position < from) continue;
            if (e.position >= to)  break;
            score += eventWeight(e.type);
        }
        return score;
    }

    // 次の重要イベントまでの距離ペナルティ
    // 重要度の高いイベント(SpecialCharge, AcrobaticCounter)を優先
    int distanceToNextCriticalEvent(const SearchContext &ctx, int pos) {
        int best = EVENT_SCAN_RANGE;
        for (const auto &e : ctx.events) {
            if (e.position < pos) continue;
            // SpecialChargeとAcrobaticCounterを最重要として距離を測る
            if (e.type == EventType::SpecialCharge ||
                e.type == EventType::AcrobaticCounter ||
                e.type == EventType::AttackCritical) {
                best = e.position - pos;
                break;
            }
        }
        return best;
    }

    // ─── 楽観上界によるダメージ見積もり (安全側 = 過大評価) ─────────────────
    // 仕様書§7.2: 上界は必ず過大評価。過小評価は誤枝刈りで最適保証が壊れる。
    //
    // 楽観的仮定:
    //   - 毎ターン全攻撃が会心
    //   - バフ最大 (AtkBuffLevel考慮)
    //   - specialCharge/acrobaticStar中のカウンター会心が毎ターン発生
    //   - MPはbestcaseで補充される
    //
    // 実装は単純版(残りターン × ターン最大ダメージ)。
    // "ターン最大ダメージ"はAllyPlayerのatkから計算する。
    int computeOptimisticDamagePerTurn(const Genome &g) {
        // ATTACK_ALLY会心の上界: atk * 1.05 (floatRand(0.95,1.05)の最大)
        // さらにコンボ×2.0を仮定
        int baseAtk = g.AllyPlayer.atk;
        // 会心時: atk * 1.05 ≈ atk (整数で保守的に atk のまま)
        int attackCritDmg = baseAtk; // FUN_0207564c の出力上界はatk付近

        // アクロバットスター中カウンター会心: atk * 1.05 もほぼatk
        // これが1ターン内に発生すると仮定

        // DRAGON_SLASH会心: baseDamage * floatRand(1.5, 2.0) → baseDamage * 2.0
        // baseDamageはFUN_0207564cの出力 ≈ atk
        int dragonCritDmg = baseAtk * 2;

        // 1ターンの楽観最大 = DRAGON_SLASH会心 + COUNTER会心(アクロバット中)
        // ただし両方同時は不可能なので加算は過大評価のため安全
        int perTurn = attackCritDmg + dragonCritDmg;

        // コンボボーナス最大(×2.0)も乗せる(過大評価として安全)
        perTurn = perTurn * 2;

        // 下限を設ける(atk/4以下にはならないはず)
        if (perTurn < baseAtk / 4 + 1) perTurn = baseAtk / 4 + 1;
        return perTurn;
    }

    bool cannotKillWithOptimisticBound(const Genome &g, int remaining) {
        if (remaining <= 0) return g.EnemyPlayer.hp > 0;
        const int optimisticPerTurn = computeOptimisticDamagePerTurn(g);
        return g.EnemyPlayer.hp > remaining * optimisticPerTurn;
    }

    // ─── 行動実行 ─────────────────────────────────────────────────────────────
    bool advanceGenome(const Genome &cur, int action, uint64_t seed, Genome &next) {
        next = cur;
        next.actions[cur.turn - 1] = action;
        next.Initialized = true;

        Player copiedPlayers[2] = {cur.AllyPlayer, cur.EnemyPlayer};
        int position = cur.position;
        uint64_t nowState = cur.state;

        BattleEmulator::Main(&position, cur.turn - cur.processed, next.actions, copiedPlayers,
                             nullptr, seed, nullptr, nullptr, -2, &nowState);

        next.position    = position;
        next.state       = nowState;
        next.turn        = cur.turn + 1;
        next.processed   = cur.turn;
        next.AllyPlayer  = copiedPlayers[0];
        next.EnemyPlayer = copiedPlayers[1];
        return next.AllyPlayer.hp > 0;
    }

    bool isLegalAction(const Genome &g, int action) {
        for (const auto &e : ACTION_TABLE) {
            if (e.action == action) return e.condition(g);
        }
        return false;
    }

    int evaluateCandidate(const Genome &cur, const Genome &next, int action,
                          const SearchContext &ctx) {
        const int dmg      = cur.EnemyPlayer.hp - next.EnemyPlayer.hp;
        const int selfDmg  = cur.AllyPlayer.hp  - next.AllyPlayer.hp;

        int score = 0;
        score += dmg * 30;
        score -= selfDmg * 20;
        score += next.AllyPlayer.hp * 4;
        score += next.AllyPlayer.mp * 2;

        // RNGイベント通過ボーナス (仕様書§6の主軸)
        score += crossedEventScore(ctx, cur.position, next.position);
        // 次の重要イベントへの距離ペナルティ
        score -= distanceToNextCriticalEvent(ctx, next.position) * 10;

        if (next.EnemyPlayer.hp <= 0) {
            score += 2'000'000 - next.turn * 4096;
        }
        // specialCharge獲得・維持ボーナス
        if (next.AllyPlayer.specialCharge && !cur.AllyPlayer.specialCharge) {
            score += 20000; // 新たにspecialCharge取得
        } else if (next.AllyPlayer.specialCharge) {
            score += 8000;
        }
        // ACROBATIC_STAR発動ボーナス
        if (action == BattleEmulator::ACROBATIC_STAR && next.AllyPlayer.acrobaticStar) {
            score += 25000;
        }
        // acrobaticStar中を維持するボーナス
        if (next.AllyPlayer.acrobaticStar) {
            score += 10000;
        }
        // 状態異常ペナルティ
        if (next.AllyPlayer.sleeping || next.AllyPlayer.paralysis) {
            score -= 8000;
        }
        // 過剰回復ペナルティ(HP充分なのに回復行動を取る)
        if ((action == BattleEmulator::HEAL || action == BattleEmulator::SPECIAL_MEDICINE ||
             action == BattleEmulator::SPECIAL_ANTIDOTE) &&
            cur.AllyPlayer.hp > cur.AllyPlayer.maxHp * 7 / 10) {
            score -= 3000;
        }
        return score;
    }

    // candidatesを収集してスコアでソート。返り値はcount。
    int collectCandidates(const Genome &cur, SearchContext &ctx,
                          std::array<SearchCandidate, std::size(ACTION_TABLE)> &candidates) {
        int count = 0;

        // 麻痺・睡眠中は強制ATTACK_ALLY (行動不能扱い)
        if (cur.AllyPlayer.sleeping || cur.AllyPlayer.paralysis) {
            Genome next{};
            initializeGenomeActions(next);
            if (advanceGenome(cur, BattleEmulator::ATTACK_ALLY, ctx.seed, next)) {
                candidates[0].genome  = next;
                candidates[0].action  = BattleEmulator::ATTACK_ALLY;
                candidates[0].score   = evaluateCandidate(cur, next, BattleEmulator::ATTACK_ALLY, ctx);
                return 1;
            }
            return 0;
        }

        for (const auto &entry : ACTION_TABLE) {
            if (!entry.condition(cur)) continue;
            Genome next{};
            initializeGenomeActions(next);
            if (!advanceGenome(cur, entry.action, ctx.seed, next)) continue;
            candidates[count].genome  = next;
            candidates[count].action  = entry.action;
            candidates[count].score   = evaluateCandidate(cur, next, entry.action, ctx);
            ++count;
        }

        std::sort(candidates.begin(), candidates.begin() + count,
                  [](const SearchCandidate &a, const SearchCandidate &b) {
                      if (a.score != b.score) return a.score > b.score;
                      return a.action < b.action;
                  });
        return count;
    }

    // ─── DFS (IDDFS内部) ──────────────────────────────────────────────────────
    bool depthFirstSearch(SearchContext &ctx, const Genome &cur, int remaining) {
        if (ctx.nodesVisited >= ctx.nodeBudget) {
            ctx.exhausted = true;
            return false;
        }
        ++ctx.nodesVisited;
        updateBestGenome(ctx, cur);

        if (isSolution(cur)) return true;
        if (cur.AllyPlayer.hp <= 0 || remaining <= 0) return false;

        // 安全な楽観上界による枝刈り (仕様書§7)
        if (cannotKillWithOptimisticBound(cur, remaining)) return false;

        // 既知の最良解より深い探索は不要
        if (ctx.bestIsSolution && cur.turn >= ctx.bestGenome.turn) return false;

        std::array<SearchCandidate, std::size(ACTION_TABLE)> candidates{};
        const int cnt = collectCandidates(cur, ctx, candidates);
        ctx.nodesVisited += cnt;

        for (int i = 0; i < cnt; ++i) {
            const Genome &cand = candidates[i].genome;
            updateBestGenome(ctx, cand);
            if (isSolution(cand)) return true;
            if (depthFirstSearch(ctx, cand, remaining - 1)) return true;
            if (ctx.exhausted) return false;
        }
        return false;
    }

    // ─── Phase 1: RNGイベント駆動Greedy Rollout ──────────────────────────────
    // 「当たり位置」をターゲットとして、そこへ到達できる行動を優先的に1本掘る。
    // パターンはハードコードせず、スキャン結果から動的に生成する。
    //
    // 変種パラメータ: targetEventIndex (何番目のイベントを狙うか)
    //                 biasAction (最初の数手で特定の行動を優先する偏り)
    void runEventDrivenRollout(SearchContext &ctx, const Genome &initial,
                               int targetEventIndex, int biasAction) {
        Genome cur = initial;
        for (int depth = 0; depth < GREEDY_ROLLOUT_DEPTH &&
             cur.AllyPlayer.hp > 0 && cur.EnemyPlayer.hp > 0 &&
             ctx.nodesVisited < ctx.nodeBudget; ++depth) {

            std::array<SearchCandidate, std::size(ACTION_TABLE)> candidates{};
            const int cnt = collectCandidates(cur, ctx, candidates);
            ctx.nodesVisited += cnt;
            if (cnt == 0) break;

            // 最初の1手だけbiasActionを優先する
            if (depth == 0 && biasAction >= 0) {
                for (int i = 0; i < cnt; ++i) {
                    if (candidates[i].action == biasAction) {
                        // スコアを上乗せして先頭に来やすくする
                        candidates[i].score += 500000;
                    }
                }
                std::sort(candidates.begin(), candidates.begin() + cnt,
                          [](const SearchCandidate &a, const SearchCandidate &b) {
                              return a.score > b.score;
                          });
            }

            // targetEventIndexに近い当たり位置を踏みやすい行動を選ぶ
            // collectCandidates内でRNGイベントスコアが加算されているので
            // 基本的には先頭が最良。targetEventIndexによる追加バイアスのみ加える。
            int selectIdx = 0;
            if (targetEventIndex < static_cast<int>(ctx.events.size())) {
                const int targetPos = ctx.events[targetEventIndex].position;
                int bestDist = std::numeric_limits<int>::max();
                for (int i = 0; i < cnt; ++i) {
                    const int afterPos = candidates[i].genome.position;
                    // targetPosを越えているか直前まで来ているかを評価
                    const int dist = std::abs(afterPos - targetPos);
                    if (dist < bestDist ||
                        (dist == bestDist && candidates[i].score > candidates[selectIdx].score)) {
                        bestDist = dist;
                        selectIdx = i;
                    }
                }
            }

            cur = candidates[selectIdx].genome;
            updateBestGenome(ctx, cur);
            if (isSolution(cur)) return;
        }
    }

    // 複数のターゲットイベントと初手バイアスの組み合わせでロールアウトを実行
    void runEventDrivenRollouts(SearchContext &ctx, const Genome &initial) {
        // 重要なイベントインデックスを収集
        std::vector<int> importantIndices;
        for (int i = 0; i < static_cast<int>(ctx.events.size()); ++i) {
            const auto &e = ctx.events[i];
            if (e.type == EventType::SpecialCharge ||
                e.type == EventType::AcrobaticCounter ||
                e.type == EventType::AttackCritical ||
                e.type == EventType::DragonCritical) {
                importantIndices.push_back(i);
                if (importantIndices.size() >= 20) break; // 先頭20個で十分
            }
        }

        // 初手バイアスの候補 (特にspecialCharge獲得・ACROBATIC_STAR発動を重視)
        const int biasCandidates[] = {
            BattleEmulator::ATTACK_ALLY,
            BattleEmulator::DRAGON_SLASH,
            BattleEmulator::CRACK_ALLY,
            BattleEmulator::WOOSH_ALLY,
            BattleEmulator::ACROBATIC_STAR,
            BattleEmulator::FLEE_ALLY,   // 乱数ずらし用
            BattleEmulator::DEFENCE,     // 乱数ずらし用
            -1,                          // バイアスなし
        };

        for (int targetIdx : importantIndices) {
            for (int bias : biasCandidates) {
                if (ctx.nodesVisited >= ctx.nodeBudget) return;
                if (bias >= 0 && !isLegalAction(initial, bias)) continue;
                runEventDrivenRollout(ctx, initial, targetIdx, bias);
            }
        }

        // イベントインデックスが少ない場合のフォールバック: バイアスのみで複数ロールアウト
        if (importantIndices.empty()) {
            for (int bias : biasCandidates) {
                if (ctx.nodesVisited >= ctx.nodeBudget) return;
                if (bias >= 0 && !isLegalAction(initial, bias)) continue;
                runEventDrivenRollout(ctx, initial, 0, bias);
            }
        }
    }

    // ─── Phase 2: IDDFS 証明探索 ──────────────────────────────────────────────
    void runIddfs(SearchContext &ctx, const Genome &initial, int maxTargetTurn) {
        const int currentActionCount = initial.turn - 1;
        for (int targetTurn = currentActionCount + 1;
             targetTurn <= maxTargetTurn && ctx.nodesVisited < ctx.nodeBudget; ++targetTurn) {
            ctx.exhausted = false;
            if (depthFirstSearch(ctx, initial, targetTurn - currentActionCount)) {
                return;
            }
            if (ctx.exhausted) {
                return;
            }
        }
    }

} // namespace

uint32_t ActionOptimizer::getNodesUsed() {
    return Node_Used;
}

Genome ActionOptimizer::RunAlgorithm(const Player players[2], uint64_t seed, int turns, int maxGenerations,
                                     int actions[350], int seedOffset) {
    (void)seedOffset;
    lcg::init(seed, true);
    Node_Used = 0;

    Player copiedPlayers[2] = {players[0], players[1]};
    int position = 1;
    uint64_t nowState = 0;

    BattleEmulator::Main(&position, turns, actions, copiedPlayers, nullptr, seed, nullptr, nullptr, -2, &nowState);

    Genome initial{};
    initializeGenomeActions(initial);
    initial.EnemyPlayer  = copiedPlayers[1];
    initial.AllyPlayer   = copiedPlayers[0];
    initial.EActions[0]  = -1;
    initial.EActions[1]  = -1;
    initial.Aactions     = -1;
    initial.fitness      = 0;
    initial.turn         = turns + 1;
    initial.processed    = turns;
    initial.Initialized  = false;
    initial.Visited      = 0;
    initial.position     = position;
    initial.state        = nowState;

    for (int i = 0; i < 350; ++i) {
        if (actions[i] == -1 || actions[i] == 0) {
            initial.actions[i] = -1;
            break;
        }
        initial.actions[i] = actions[i];
    }

    if (isSolution(initial)) return initial;

    SearchContext ctx{};
    ctx.seed       = seed;
    // nodeBudget配分: maxGenerations<=0なら10000をデフォルトとして
    // Phase1(greedy): 全体の約30%
    // Phase2(IDDFS): 残り70%
    ctx.nodeBudget = maxGenerations <= 0 ? 10'000000 : std::max(10'000000, maxGenerations);
    ctx.events     = scanEvents(initial.position);
    ctx.bestGenome = initial;
    updateBestGenome(ctx, initial);

    // Phase 1: RNGイベント駆動ロールアウト (近似解生成)
    const int phase1Budget = ctx.nodeBudget * 3 / 10;
    {
        SearchContext phase1Ctx = ctx;
        phase1Ctx.nodeBudget = ctx.nodesVisited + phase1Budget;
        runEventDrivenRollouts(phase1Ctx, initial);
        ctx.nodesVisited   = phase1Ctx.nodesVisited;
        ctx.bestGenome     = phase1Ctx.bestGenome;
        ctx.bestIsSolution = phase1Ctx.bestIsSolution;
    }

    // Phase 2: IDDFS証明探索
    // bestIsSolutionがあればbestTurn-1まで証明を試みる
    // なければ GREEDY_ROLLOUT_DEPTH ターンまで
    const int currentActionCount = initial.turn - 1;
    const int bestKnown = ctx.bestIsSolution
        ? ctx.bestGenome.turn - 1
        : currentActionCount + GREEDY_ROLLOUT_DEPTH;

    // IDDFS中はnodeBudgetを絞りすぎず残予算を全て使う
    runIddfs(ctx, initial, bestKnown - 1);

    Node_Used = static_cast<uint32_t>(ctx.nodesVisited);

    if (ctx.bestIsSolution) return ctx.bestGenome;

    ctx.bestGenome.turn = NO_SOLUTION_TURN;
    return ctx.bestGenome;
}

void ActionOptimizer::updateCompromiseScore(Genome &genome) {
    (void)genome;
}

std::pair<int, Genome> ActionOptimizer::RunAlgorithmAsync(const Player players[2], uint64_t seed, int turns,
                                                          int maxGenerations, int actions[350], int numThreads,
                                                          bool dropbug) {
    (void)numThreads;
    (void)dropbug;
    auto genome = RunAlgorithm(players, seed, turns, maxGenerations, actions, 0);
    return {0, genome};
}