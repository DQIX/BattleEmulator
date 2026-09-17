#pragma once
#include <array>
#include <cstdint>
#include <string>
#include "BattleEmulator.h"

// Authoritative emulator state. Ordering estimates never get written here.
struct ErugiosuState {
    Player players[2]{};
    uint64_t nowState = 0;
    int rngPosition = 1;
    int resultPosition = 0;
    int equipmentChanges = 0;
};

struct ErugiosuLaneResult {
    bool victory = false;
    int position = -1;
    int equipmentChanges = -1; // Changes AFTER the immutable prefix.
    int length = 0;
    double firstVictoryMs = -1;
    double bestVictoryMs = -1;
};

struct ErugiosuResult {
    bool inputValid = false;
    bool victory = false;
    bool replayVerified = false;
    std::string error;
    std::array<int32_t, 350> actions{};
    int prefixLength = 0;
    int length = 0;
    ErugiosuState root{}, finalState{};
    BattleResult replay{};
    ErugiosuLaneResult zeroChange{}, equipmentAware{};
    uint64_t expanded = 0;
    uint64_t duplicates = 0;
    uint64_t hashCollisions = 0;
    uint64_t rejectedReplay = 0;
    uint64_t unsafeFleeSkipped = 0;
    uint64_t equipmentBranches = 0;
    std::array<uint64_t, 64> actionExpansions{};
    int updates = 0;
    int completedWidth = 0;
    double firstVictoryMs = -1;
    double bestVictoryMs = -1;
    double elapsedMs = 0;
};

class ErugiosuSearch {
public:
    static constexpr int DefaultBudgetMs = 2000;
    static constexpr int DefaultVariant = 0;
    // Fixed prefix is copied verbatim. Zero-change means keeping its END state.
    static ErugiosuResult Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength,
        int budgetMs = DefaultBudgetMs, int variant = DefaultVariant,
        int maxTotalTurns = 60);
    static bool SamePlayer(const Player &, const Player &);
    static bool SameFuture(const ErugiosuState &, const ErugiosuState &);
    static bool SameState(const ErugiosuState &, const ErugiosuState &);
    static bool Legal(const Player &, int action);
    // Whole-sequence Main plus independent one-turn replay; both must agree.
    // validateFrom leaves historical prefix choices alone, validates the suffix.
    static bool Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, ErugiosuState &, BattleResult &,
        int validateFrom = 0);
    static int VariantCount();
    static const char *VariantName(int variant);
};