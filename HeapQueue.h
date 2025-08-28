#include <vector>
#include <algorithm>
#include "Genome.h"

struct AStarNode {
    Genome genome;   // ← Genomeを持たせる

    double fCost; // g(n) + h(n)
    double gCost; // 実コスト（ターン数）
    double hCost; // ヒューリスティックコスト
    uint64_t stateHash;

    bool operator<(const AStarNode& other) const {
        return fCost > other.fCost;
    }
};

class HeapQueue {
private:
    std::vector<AStarNode> heap;
    size_t maxSize;

public:
    explicit HeapQueue(size_t maxSize) : maxSize(maxSize) {
        heap.reserve(maxSize);
    }

    void push(const AStarNode &node) {
        if (heap.size() < maxSize) {
            heap.push_back(node);
            std::push_heap(heap.begin(), heap.end(), compare);
        } else {
            if (node.fCost < heap.front().fCost) { // fCostで比較
                std::pop_heap(heap.begin(), heap.end(), compare);
                heap.back() = node;
                std::push_heap(heap.begin(), heap.end(), compare);
            }
        }
    }

    void pop() {
        std::pop_heap(heap.begin(), heap.end(), compare);
        heap.pop_back();
    }

    [[nodiscard]] AStarNode top() const {
        return heap.front();
    }

    [[nodiscard]] bool empty() const {
        return heap.empty();
    }

    [[nodiscard]] size_t size() const {
        return heap.size();
    }

private:
    static bool compare(const AStarNode &a, const AStarNode &b) {
        return a.fCost > b.fCost; // fCost小さい順
    }
};
