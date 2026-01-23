#include "ActionSearcher.h"

#include <cassert>
#include <cstring>

#include "lcg.h"

// NOTE:
// - lower score is better
// - beam search / topK は score 昇順で扱う
constexpr int NODE_PER_ACTIONS = 5;

static SearchOutput tmpSearchOutput;

int ActionSearcher::actionPool_[ActionSearcher::ACTION_POOL_SIZE];
int ActionSearcher::actionPoolUsed_ = 0;
int ActionSearcher::parentPool_[ActionSearcher::NODE_POOL_SIZE];
int ActionSearcher::fragOffsetPool_[ActionSearcher::NODE_POOL_SIZE];
uint8_t ActionSearcher::fragLenPool_[ActionSearcher::NODE_POOL_SIZE];
int ActionSearcher::nodePoolUsed_ = 0;

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
    (void)depth;
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
            out[size].depth = cur.depth + r.depth;
            out[size].firstAction = (cur.depth == 0) ? r.actions[0] : cur.firstAction;
            out[size].parentIndex = cur.nodeId;
            out[size].fragLen = static_cast<uint8_t>(r.depth);
            out[size].fragOffset = -1;
            out[size].nodeId = -1;
            siftUp(out, size);
            ++size;
            continue;
        }

        // root が最悪
        if (r.score < out[0].score) {
            out[0] = r;
            out[0].depth = cur.depth + r.depth;
            out[0].firstAction = (cur.depth == 0) ? r.actions[0] : cur.firstAction;
            out[0].parentIndex = cur.nodeId;
            out[0].fragLen = static_cast<uint8_t>(r.depth);
            out[0].fragOffset = -1;
            out[0].nodeId = -1;
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

void ActionSearcher::assignNodeId(SearchResult& node) {
    assert(nodePoolUsed_ < NODE_POOL_SIZE);
    assert(actionPoolUsed_ + node.fragLen <= ACTION_POOL_SIZE);

    const int id = nodePoolUsed_++;
    node.nodeId = id;
    parentPool_[id] = node.parentIndex;
    fragLenPool_[id] = node.fragLen;
    fragOffsetPool_[id] = actionPoolUsed_;

    if (node.fragLen > 0) {
        std::memcpy(
            actionPool_ + actionPoolUsed_,
            node.actions,
            sizeof(int) * node.fragLen
        );
        actionPoolUsed_ += node.fragLen;
    }
}

void ActionSearcher::buildPlanFromNode(const SearchResult& node, SearchPlan& plan) {
    plan.depth = node.depth;
    int pos = node.depth;
    int id = node.nodeId;

    while (id >= 0) {
        const int fragLen = fragLenPool_[id];
        if (fragLen > 0) {
            pos -= fragLen;
            std::memcpy(
                plan.actions + pos,
                actionPool_ + fragOffsetPool_[id],
                sizeof(int) * fragLen
            );
        }
        id = parentPool_[id];
    }
}

void ActionSearcher::Run() {
    actionPoolUsed_ = 0;
    nodePoolUsed_ = 0;
    // ---- root result 構築 ----
    SearchResult root{};
    root.depth = 0;
    root.firstAction = -1;
    root.valid = true;
    root.isWin = false;
    root.isLose = false;
    root.players[0] = rootPlayers_[0];
    root.players[1] = rootPlayers_[1];
    root.nowState = rootNowState_;
    root.position = rootPosition_;
    root.parentIndex = -1;
    root.fragLen = 0;
    root.fragOffset = 0;
    assignNodeId(root);

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
                buildPlanFromNode(n, best_[bestCount_++]);
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
        for (int i = 0; i < curCount_; ++i) {
            if (cur_[i].nodeId < 0) {
                assignNodeId(cur_[i]);
            }
        }

        // swapを削除して、cur_ にある上位Kを次ループで使う
    }
}

int ActionSearcher::getBest(SearchPlan *out) const {
    assert(bestCount_ != 0);
    std::memcpy(out, best_, sizeof(SearchPlan) * BEST_LIMIT);
    return best_[0].depth;
}
