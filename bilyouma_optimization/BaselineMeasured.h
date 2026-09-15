#pragma once
#include "ActionOptimizer.h"
#include <chrono>
#include <functional>

// This is an instrumented COPY. Original ActionOptimizer.cpp remains untouched.
class BaselineMeasured {
public:
    static Genome RunAlgorithm(const Player[2], uint64_t, int, int, int[350], int);
    static std::pair<int, Genome> RunAlgorithmAsync(const Player[2], uint64_t, int, int, int[350], int, bool);
    static void updateCompromiseScore(Genome &);
    static uint32_t getNodesUsed();
    static inline std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
    static inline std::function<void(const Genome &)> observe;
    static inline uint64_t expanded = 0;
};
