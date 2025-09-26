//
// Enhanced Heap Queue Implementation
//

#include "EnhancedHeapQueue.h"

void EnhancedHeapQueue::push(const EnhancedAStarNode &node) {
    if (heap.size() < maxSize) {
        heap.push_back(node);
        std::push_heap(heap.begin(), heap.end(), compare);
    } else {
        // Only replace if the new node has better f-cost
        if (node.fCost < heap.front().fCost) {
            std::pop_heap(heap.begin(), heap.end(), compare);
            heap.back() = node;
            std::push_heap(heap.begin(), heap.end(), compare);
        }
    }
}

void EnhancedHeapQueue::pop() {
    std::pop_heap(heap.begin(), heap.end(), compare);
    heap.pop_back();
}

EnhancedAStarNode EnhancedHeapQueue::top() const {
    return heap.front();
}

bool EnhancedHeapQueue::empty() const {
    return heap.empty();
}

size_t EnhancedHeapQueue::size() const {
    return heap.size();
}

bool EnhancedHeapQueue::compare(const EnhancedAStarNode &a, const EnhancedAStarNode &b) {
    return a < b; // Use the enhanced comparison operator
}