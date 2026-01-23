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

int inline ActionSearcher::selectTopK(
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

        if (r.players[0].hp <= 15) {
            // 先制できないなら即弾く
            if (!WillPlayer0InitiativeNoTie(r.players, r.position)) {
                continue;
            }
        }

        // ... existing code ...
        if (n < maxOut) {
            out[n] = r;
            out[n].depth = cur.depth + r.depth;
            out[n].firstAction = (cur.depth == 0) ? r.actions[0] : cur.firstAction;
            out[n].parentIndex = cur.nodeId;
            out[n].fragLen = static_cast<uint8_t>(r.depth);
            out[n].fragOffset = -1;
            out[n].nodeId = -1;
            ++n;

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
            out[worst].depth = cur.depth + r.depth;
            out[worst].firstAction = (cur.depth == 0) ? r.actions[0] : cur.firstAction;
            out[worst].parentIndex = cur.nodeId;
            out[worst].fragLen = static_cast<uint8_t>(r.depth);
            out[worst].fragOffset = -1;
            out[worst].nodeId = -1;

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
