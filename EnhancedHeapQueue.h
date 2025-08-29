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
    Genome genome;
    double fCost; // g(n) + h(n)
    double gCost; // Actual cost (turn number + action costs)
    double hCost; // Heuristic cost
    uint64_t stateHash;
    
    // Enhanced comparison with proper tie-breaking
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
        if (genome.EnemyPlayer.hp != other.genome.EnemyPlayer.hp) {
            return genome.EnemyPlayer.hp > other.genome.EnemyPlayer.hp;
        }
        
        // Third tie-breaker: prefer higher player HP (safer states)
        if (genome.AllyPlayer.hp != other.genome.AllyPlayer.hp) {
            return genome.AllyPlayer.hp < other.genome.AllyPlayer.hp;
        }
        
        // Final tie-breaker: use state hash for consistent ordering
        return stateHash > other.stateHash;
    }
};

class EnhancedHeapQueue {
private:
    std::vector<EnhancedAStarNode> heap;
    size_t maxSize;

public:
    explicit EnhancedHeapQueue(size_t maxSize) : maxSize(maxSize) {
        heap.reserve(maxSize);
    }

    void push(const EnhancedAStarNode &node);
    void pop();
    EnhancedAStarNode top() const;
    bool empty() const;
    size_t size() const;

private:
    static bool compare(const EnhancedAStarNode &a, const EnhancedAStarNode &b);
};

#endif // ENHANCED_HEAP_QUEUE_H