#include "ActionSearcher.h"

#include <cassert>
#include <cstring>

#include "lcg.h"

// NOTE:
// - lower score is better
// - beam search / topK は score 昇順で扱う
constexpr int NODE_PER_ACTIONS = 5;

static SearchOutput tmpSearchOutput;

int ActionSearcher::selectTopK(
    SearchResult* src,
    int srcCount,
    SearchResult* dst,
    int K
) {
    if (srcCount <= K) {
        memcpy(dst, src, sizeof(SearchResult) * srcCount);
        return srcCount;
    }

    int n = 0;
    int worst = 0; // dst 内で score 最大のインデックス

    for (int i = 0; i < srcCount; ++i) {
        const SearchResult& r = src[i];

        if (n < K) {
            dst[n++] = r;

            if (n == K) {
                // 初回だけ worst を確定
                worst = 0;
                for (int j = 1; j < K; ++j) {
                    if (dst[j].score > dst[worst].score) {
                        worst = j;
                    }
                }
            }
            continue;
        }

        // expandNode と同じワースト判定
        if (r.score < dst[worst].score) {
            dst[worst] = r;

            // worst を再計算
            worst = 0;
            for (int j = 1; j < K; ++j) {
                if (dst[j].score > dst[worst].score) {
                    worst = j;
                }
            }
        }
    }

    return K;
}

inline bool WillPlayer0InitiativeNoTie(
    const Player players[2],
    int position
) {
    int pos = position;

    double speed0 = players[0].speed * lcg::floatRand(&pos, 0.51, 1.0);
    double speed1 = players[1].speed * lcg::floatRand(&pos, 0.51, 1.0);

    return speed0 > speed1;
}


int ActionSearcher::expandNode(
    const SearchResult& cur,
    SearchResult* out,
    int maxOut,
    int depth
) {
    ActionBruteForcer::Search(
        cur.players,
        cur.nowState,
        cur.position,
        tmpSearchOutput
    );

    int n = 0;
    int worst = 0;

    for (int i = 0; i < tmpSearchOutput.count; ++i) {
        const SearchResult& r = tmpSearchOutput.results[i];
        if (!r.valid) continue;

        if (r.isLose) {
            continue;
        }

        // if (r.players[0].hp <= 15) {
        //     // 先制できないなら即弾く
        //     if (WillPlayer0InitiativeNoTie(r.players, r.position)) {
        //         continue;
        //     }
        // }

        if (n < maxOut) {
            out[n++] = r;

            if (n == maxOut) {
                worst = 0;
                for (int j = 1; j < maxOut; ++j) {
                    if (out[j].score > out[worst].score) {
                        worst = j; // 最大 = 最悪
                    }
                }
            }
            continue;
        }

        // ★ ここが唯一の修正点
        if (r.score < out[worst].score) {
            out[worst] = r;

            worst = 0;
            for (int j = 1; j < maxOut; ++j) {
                if (out[j].score > out[worst].score) {
                    worst = j;
                }
            }
        }
    }

    return n;
}



ActionSearcher::ActionSearcher(
    const Player* rp,
    uint64_t ns,
    int pos,
    int maxDepth
)
    : rootNowState_(ns),
      rootPosition_(pos),
      maxDepth_(maxDepth),
      cur_(bufA_),
      next_(bufB_),
      curCount_(0),
      nextCount_(0),
      bestCount_(0)
{
    rootPlayers_[0] = rp[0];
    rootPlayers_[1] = rp[1];
}


int ActionSearcher::beamWidthForDepth(int depth) {
    (void)depth;
    return 64;
}

void ActionSearcher::Run() {
    // ---- root result 構築 ----
    SearchResult root{};
    root.depth = 0;
    root.firstAction = -1;
    root.valid = true;
    root.players[0] = rootPlayers_[0];
    root.players[1] = rootPlayers_[1];
    root.nowState = rootNowState_;
    root.position = rootPosition_;

    cur_[0] = root;
    curCount_ = 1;

    // ---- 探索 ----
    for (int depth = 0; depth < maxDepth_; ++depth) {
        nextCount_ = 0;

        for (int i = 0; i < curCount_; ++i) {
            const SearchResult& n = cur_[i];

            // 味方死亡 → 枝切り
            if (!n.valid) {
                continue;
            }

            // 敵死亡 → 成功
            if (n.isWin) {
                best_[bestCount_++] = {
                    n.depth,
                    {}
                };
                std::memcpy(
                    best_[bestCount_ - 1].actions,
                    n.actions,
                    sizeof(int) * n.depth
                );
                if (bestCount_ == BEST_LIMIT) return;
                continue;
            }

            // 展開
            nextCount_ += expandNode(
                n,
                next_ + nextCount_,
                NODE_EXPAND_LIMIT,
                depth
            );
        }

        // ビーム選択
        curCount_ = selectTopK(
            next_,
            nextCount_,
            cur_,
            beamWidthForDepth(depth)
        );

        if (curCount_ == 0) break;

        // ポインタ入替（次ループ用）
        SearchResult* tmp = cur_;
        cur_ = next_;
        next_ = tmp;
    }
}

int ActionSearcher::getBest(SearchPlan *out) const {
    assert(bestCount_ != 0);
    std::memcpy(out, best_, sizeof(SearchPlan) * BEST_LIMIT);
    return best_[0].depth;
}
