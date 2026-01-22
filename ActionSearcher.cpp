#include "ActionSearcher.h"

#include <cassert>
#include <cstring>

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

    bool used[512] = {}; // srcCount 上限想定

    int out = 0;
    for (; out < K; ++out) {
        int best = -1;
        for (int i = 0; i < srcCount; ++i) {
            if (used[i]) continue;
            if (best == -1 || src[i].score > src[best].score) {
                best = i;
            }
        }
        used[best] = true;
        dst[out] = src[best];
    }
    return K;
}

int ActionSearcher::expandNode(
    const SearchResult& cur,
    SearchResult* out,
    int maxOut,
    int depth
) {
    // 味方死亡 → 枝切り
    if (cur.players[0].hp <= 0) {
        return 0;
    }

    // 敵死亡 → 成功（展開しない）
    if (cur.players[1].hp <= 0) {
        return 0;
    }

    // ActionBruteForcer で次の候補を生成

    ActionBruteForcer::Search(
        cur.players,
        cur.nowState,
        cur.position,
        ///*isFirstExec=*/(depth == 0),
        tmpSearchOutput
    );

    int n = 0;
    for (int i = 0; i < tmpSearchOutput.count && n < maxOut; ++i) {
        if (!tmpSearchOutput.results[i].valid) continue;

        out[n] = tmpSearchOutput.results[i];
        ++n;
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
    return 32;
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
            if (n.players[0].hp <= 0) {
                continue;
            }

            // 敵死亡 → 成功
            if (n.players[1].hp <= 0) {
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
                MAX_LAYER - nextCount_,
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
    assert(bestCount_ > 0);
    std::memcpy(out, best_, sizeof(SearchPlan) * BEST_LIMIT);
    return best_[0].depth;
}
