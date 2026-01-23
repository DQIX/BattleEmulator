#include "ActionSearcher.h"

#include <cassert>
#include <cstring>

#include "lcg.h"

// NOTE:
// - lower score is better
// - beam search / topK は score 昇順で扱う
constexpr int NODE_PER_ACTIONS = 5;

static SearchOutput tmpSearchOutput;

inline bool WillPlayer0InitiativeNoTie(
    const Player players[2],
    int position
) {
    int pos = position;

    double speed0 = players[0].speed * lcg::floatRand(&pos, 0.51, 1.0);
    double speed1 = players[1].speed * lcg::floatRand(&pos, 0.51, 1.0);

    return speed0 > speed1;
}

// score: lower is better
// dst は「最大ヒープ」（最悪 = score 最大が root）

static inline void siftUp(SearchResult* heap, int idx) {
    while (idx > 0) {
        int p = (idx - 1) >> 1;
        if (heap[p].score >= heap[idx].score) {
            break;
        }
        std::swap(heap[p], heap[idx]);
        idx = p;
    }
}

static inline void siftDown(SearchResult* heap, int size, int idx) {
    while (true) {
        int l = (idx << 1) + 1;
        if (l >= size) {
            break;
        }
        int r = l + 1;
        int m = (r < size && heap[r].score > heap[l].score) ? r : l;

        if (heap[idx].score >= heap[m].score) {
            break;
        }
        std::swap(heap[idx], heap[m]);
        idx = m;
    }
}


static inline int selectTopKHeap(
    const SearchResult* src,
    int srcCount,
    SearchResult* dst,
    int K
) {
    if (srcCount <= K) {
        memcpy(dst, src, sizeof(SearchResult) * srcCount);
        return srcCount;
    }

    int size = 0;

    for (int i = 0; i < srcCount; ++i) {
        const SearchResult& r = src[i];

        if (size < K) {
            dst[size] = r;
            siftUp(dst, size);
            ++size;
            continue;
        }

        // root = 最悪
        if (r.score < dst[0].score) {
            dst[0] = r;
            siftDown(dst, size, 0);
        }
    }

    return K;
}

int inline ActionSearcher::selectTopK(
    SearchResult* src,
    int srcCount,
    SearchResult* dst,
    int K
) {
    return selectTopKHeap(src, srcCount, dst, K);
}




int inline ActionSearcher::expandNode(
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

    int size = 0;

    for (int i = 0; i < tmpSearchOutput.count; ++i) {
        const SearchResult& r = tmpSearchOutput.results[i];
        if (!r.valid) continue;
        if (r.isLose) continue;

        if (size < maxOut) {
            out[size] = r;
            siftUp(out, size);
            ++size;
            continue;
        }

        // root が最悪
        if (r.score < out[0].score) {
            out[0] = r;
            siftDown(out, size, 0);
        }
    }

    return size;
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
        // ActionSearcher.cpp:216-228
        curCount_ = selectTopK(next_, nextCount_, cur_, beamWidthForDepth(depth));
        if (curCount_ == 0) break;

        // swapを削除して、cur_ にある上位Kを次ループで使う
    }
}

int ActionSearcher::getBest(SearchPlan *out) const {
    assert(bestCount_ != 0);
    std::memcpy(out, best_, sizeof(SearchPlan) * BEST_LIMIT);
    return best_[0].depth;
}
