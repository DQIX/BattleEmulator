#include "ActionSearcher.h"

#include <cassert>
#include <cstring>

#include "lcg.h"

namespace {
    constexpr int kPrefixBuckets =
        ActionBruteForcerConst::ACTION_TABLE_SIZE *
        (ActionBruteForcerConst::ACTION_TABLE_SIZE + 1);

    int ActionIndexFromId(int action) {
        for (int i = 0; i < ActionBruteForcerConst::ACTION_TABLE_SIZE; ++i) {
            if (ACTION_TABLE[i].action == action) {
                return i;
            }
        }
        return -1;
    }

    int PrefixBucket(const Node& node) {
        const int size = ActionBruteForcerConst::ACTION_TABLE_SIZE;
        int first = ActionIndexFromId(node.actions[0]);
        if (first < 0) first = 0;
        int second = size;
        if (node.depth >= 2) {
            int idx = ActionIndexFromId(node.actions[1]);
            if (idx >= 0) second = idx;
        }
        return first * (size + 1) + second;
    }
}

// NOTE:
// - lower score is better
// - beam search / topK は score　昇順で扱う

// inline bool WillPlayer0InitiativeNoTie(
//     int position
// ) {
//     int pos = position;
//
//     double speed0 = setting::ALLY_SPEED * lcg::floatRand051_1(&pos);
//     double speed1 = setting::ENEMY_SPEED * lcg::floatRand051_1(&pos);
//
//     return speed0 > speed1;
// }

void ActionSearcher::expandNode(
    const SearchResult& cur,
    SearchResult* out,
    int maxOut,
    int& outCount,
    int& worst
) {
    brute_->Search(
        cur.players,
        cur.nowState,
        cur.position,
        tmpSearchOutput
    );

    SearchResult prefixBest[kPrefixBuckets];
    bool prefixUsed[kPrefixBuckets] = {};

    for (int i = 0; i < tmpSearchOutput.count; ++i) {
        const Node& node = *tmpSearchOutput.nodes[i];
        if (node.reason == TerminateReason::AllyDead) {
            continue;
        }

        SearchResult cand{};
        cand.depth = cur.depth + node.depth;
        cand.firstAction =
            (cur.depth == 0) ? node.actions[0] : cur.firstAction;
        cand.parentIndex = cur.nodeId;
        cand.fragLen = static_cast<uint8_t>(node.depth);
        cand.fragOffset = -1;
        cand.nodeId = -1;
        const int totalDepth = cur.depth + node.depth;
        cand.score = ActionBruteForcer::EvaluateTerminal(node, totalDepth);
        cand.nowState = node.nowState;
        cand.position = node.position;
        cand.valid = true;
        cand.isWin = (node.reason == TerminateReason::EnemyDead);
        cand.isLose = false;

        auto storeCandidate = [&](SearchResult& dst) {
            dst = cand;
            std::memcpy(dst.players, node.players, sizeof(Player) * 2);
            if (cand.fragLen > 0) {
                std::memcpy(
                    dst.actions,
                    node.actions,
                    sizeof(int) * cand.fragLen
                );
            }
        };

        const int prefixBucket = PrefixBucket(node);
        if (prefixBucket >= 0 && prefixBucket < kPrefixBuckets) {
            if (!prefixUsed[prefixBucket]
                || cand.score < prefixBest[prefixBucket].score) {
                storeCandidate(prefixBest[prefixBucket]);
                prefixUsed[prefixBucket] = true;
            }
        }

        if (outCount < maxOut) {
            SearchResult& dst = out[outCount++];
            storeCandidate(dst);

            if (outCount == maxOut) {
                worst = 0;
                for (int j = 1; j < maxOut; ++j) {
                    if (out[j].score > out[worst].score) {
                        worst = j;
                    }
                }
            }
            continue;
        }

        if (cand.score < out[worst].score) {
            SearchResult& dst = out[worst];
            storeCandidate(dst);

            worst = 0;
            for (int j = 1; j < maxOut; ++j) {
                if (out[j].score > out[worst].score) {
                    worst = j;
                }
            }
        }
    }

    for (int b = 0; b < kPrefixBuckets; ++b) {
        if (!prefixUsed[b]) {
            continue;
        }

        if (outCount < maxOut) {
            out[outCount++] = prefixBest[b];
            if (outCount == maxOut) {
                worst = 0;
                for (int j = 1; j < maxOut; ++j) {
                    if (out[j].score > out[worst].score) {
                        worst = j;
                    }
                }
            }
            continue;
        }

        out[worst] = prefixBest[b];
        worst = 0;
        for (int j = 1; j < maxOut; ++j) {
            if (out[j].score > out[worst].score) {
                worst = j;
            }
        }
    }
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
      bestCount_(0),
      actionPoolUsed_(0),
      nodePoolUsed_(0)
{
    rootPlayers_[0] = rp[0];
    rootPlayers_[1] = rp[1];

    actionPool_ = static_cast<int*>(
        std::malloc(sizeof(int) * ACTION_POOL_SIZE)
    );
    parentPool_ = static_cast<int*>(
        std::malloc(sizeof(int) * NODE_POOL_SIZE)
    );
    fragOffsetPool_ = static_cast<int*>(
        std::malloc(sizeof(int) * NODE_POOL_SIZE)
    );
    fragLenPool_ = static_cast<uint8_t*>(
        std::malloc(sizeof(uint8_t) * NODE_POOL_SIZE)
    );

    brute_ = new ActionBruteForcer();  //  ← これが欲しかったやつ

    assert(actionPool_);
    assert(parentPool_);
    assert(fragOffsetPool_);
    assert(fragLenPool_);
    assert(brute_);
}

int ActionSearcher::beamWidthForDepth(int depth) {
    (void)depth;
    return ActionSearcher::MAX_BEAM;
}

ActionSearcher::~ActionSearcher() {
    delete brute_;
    std::free(actionPool_);
    std::free(parentPool_);
    std::free(fragOffsetPool_);
    std::free(fragLenPool_);
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
        int worst = 0;
        const int beamWidth = beamWidthForDepth(depth);

        for (int i = 0; i < curCount_; ++i) {
            const SearchResult& n = cur_[i];

            // 味方死亡 ↁE枝�EめE
            if (!n.valid) {
                continue;
            }

            // 敵死亡 ↁE成功
            if (n.isWin) {
                buildPlanFromNode(n, best_[bestCount_++]);
                if (bestCount_ == BEST_LIMIT) return;
                continue;
            }

            // 展開
            expandNode(
                n,
                next_,
                beamWidth,
                nextCount_,
                worst
            );
        }

        if (nextCount_ == 0) break;

        SearchResult* tmp = cur_;
        cur_ = next_;
        next_ = tmp;
        curCount_ = nextCount_;
        for (int i = 0; i < curCount_; ++i) {
            if (cur_[i].nodeId < 0) {
                assignNodeId(cur_[i]);
            }
        }
    }
}

int ActionSearcher::getBest(SearchPlan *out) const {
    assert(bestCount_ != 0);
    std::memcpy(out, best_, sizeof(SearchPlan) * BEST_LIMIT);
    return best_[0].depth;
}



