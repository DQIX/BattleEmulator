#pragma once
#include <array>
#include <cstdint>
#include "BattleEmulator.h"

struct NusisamaState {
    Player players[2]{};
    uint64_t nowState = 0;
    int rngPosition = 1;
    int resultPosition = 0; // BattleResult records, NOT the RNG cursor.
};

struct NusisamaResult {
    bool inputValid = false;
    bool victory = false;
    bool replayVerified = false;
    std::array<int32_t, 350> actions{}; // Original prefix, then searched suffix.
    int prefixLength = 0;
    int length = 0;
    NusisamaState root{}, finalState{};
    BattleResult replay{};
    uint64_t expanded = 0;
    uint64_t duplicates = 0;
    uint64_t rejectedReplay = 0;
    uint64_t unsafeFleeSkipped = 0;
    int updates = 0;
    int completedWidth = 0;
    double firstVictoryMs = -1;
    double bestVictoryMs = -1;
    double elapsedMs = 0;
};

class NusisamaSearch {
public:
    static constexpr int DefaultVariant = 6;
    static constexpr int DefaultBudgetMs = 1000;
    // maxTotalTurns is an absolute turn horizon; -1 means prefix + 40 (<= 90).
    static NusisamaResult Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength,
        int budgetMs = DefaultBudgetMs, int variant = DefaultVariant,
        int maxTotalTurns = -1);
    static bool Legal(const NusisamaState &, int action);
    static bool Safe(const Player &, int action);
    static bool SamePlayer(const Player &, const Player &);
    static bool SameState(const NusisamaState &, const NusisamaState &);
    static bool Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, NusisamaState &, BattleResult &);
    static bool VerifySuffix(uint64_t seed, const NusisamaState &root,
        const int32_t actions[350], int prefixLength, int length,
        const NusisamaState &expected, const BattleResult &replay);
    static const char *VariantName(int variant);
    static int VariantCount();
};