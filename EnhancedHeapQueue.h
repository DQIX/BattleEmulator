//
// Enhanced Heap Queue with improved tie-breaking
// Fixes the issue where nodes with identical f-costs have arbitrary ordering
//

#ifndef ENHANCED_HEAP_QUEUE_H
#define ENHANCED_HEAP_QUEUE_H

#include <vector>
#include <algorithm>
#include <cmath>
#include "Genome.h"

struct EnhancedAStarNode {
    double fCost; // g(n) + h(n)
    double gCost; // Actual cost (turn number + action costs)
    double hCost; // Heuristic cost
    int enemyHP;
    int allyHP;
    uint32_t nodeId;
    //uint64_t stateHash;

    bool operator<(const EnhancedAStarNode& other) const {
        // Primary comparison: f-cost (with epsilon for floating point)
        if (std::abs(fCost - other.fCost) > 1e-9) {
            return fCost > other.fCost; // Min-heap (smaller f-cost has higher priority)
        }

        // First tie-breaker: prefer higher g-cost (deeper nodes, closer to goal)
        if (std::abs(gCost - other.gCost) > 1e-9) {
            return gCost < other.gCost;
        }

        // Second tie-breaker: prefer lower enemy HP (closer to victory)
        if (enemyHP != other.enemyHP) {
            return enemyHP > other.enemyHP;
        }

        // Third tie-breaker: prefer higher player HP (safer states)
        if (allyHP != other.allyHP) {
            return allyHP < other.allyHP;
        }

        // Final tie-breaker: use state hash for consistent ordering
        return nodeId > other.nodeId;
    }
};

class EnhancedHeapQueue {
public:
    void push(const EnhancedAStarNode& x) {
        heap_.push_back(x);
        std::push_heap(heap_.begin(), heap_.end());
    }
    void pop() {
        std::pop_heap(heap_.begin(), heap_.end());
        heap_.pop_back();
    }

    [[nodiscard]] const EnhancedAStarNode& top() const { return heap_.front(); }
    [[nodiscard]] bool empty() const { return heap_.empty(); }
    [[nodiscard]] size_t size() const { return heap_.size(); }

    [[nodiscard]] uint32_t topId() const { return heap_.front().nodeId; } // これが便利

private:
    std::vector<EnhancedAStarNode> heap_;
};

#endif // ENHANCED_HEAP_QUEUE_H