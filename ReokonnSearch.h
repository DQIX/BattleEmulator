#pragma once
#include <array>
#include <cstdint>
#include "BattleEmulator.h"

struct ReokonnState {
    Player players[2]{};
    uint64_t nowState = 0;
    int rngPosition = 1;
    int resultPosition = 0; // BattleResult event count, never the RNG cursor.
};

struct ReokonnResult {
    bool victory = false;
    bool replayVerified = false;
    bool inputValid = false;
    std::array<int32_t, 350> actions{};
    int prefixLength = 0;
    int length = 0;
    ReokonnState root{}, finalState{};
    BattleResult replay{};
    uint64_t expanded = 0;
    uint64_t duplicates = 0;
    uint64_t collisions = 0;
    uint64_t rejectedReplay = 0;
    uint64_t unsafeFleeSkipped = 0;
    int updates = 0;
    int completedWidth = 0;
    double firstVictoryMs = -1;
    double bestVictoryMs = -1;
    double elapsedMs = 0;
};

class ReokonnSearch {
public:
    static constexpr int DefaultVariant = 16;
    static constexpr int DefaultBudgetMs = 1000;
    // Replay the immutable prefix first; only the subsequent suffix is searched.
    // The default horizon is the baseline's prefixLength + 40 (at most 99).
    static ReokonnResult Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength, int budgetMs = DefaultBudgetMs,
        int variant = DefaultVariant, int maxTotalTurns = -1);
    static bool SamePlayer(const Player &, const Player &);
    static bool SameState(const ReokonnState &, const ReokonnState &);
    static bool Legal(const Player &, int action);
    static bool Safe(const Player &, int action);
    static bool Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, ReokonnState &, BattleResult &);
    static const char *VariantName(int variant);
    static int VariantCount();
};