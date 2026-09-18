#pragma once

#include <array>
#include <cstdint>
#include <string>
#include "BattleEmulator.h"

// Only emulator output belongs here. Scores are kept separately in search nodes.
struct GadonnkoState {
    Player players[2]{};
    uint64_t nowState = 0;
    int rngPosition = 1;
    int resultPosition = 0;
    int equipmentChanges = 0;
};

struct GadonnkoResult {
    bool inputValid = false;
    bool victory = false;
    bool replayVerified = false;
    std::string error;
    std::array<int32_t, 350> actions{};
    int prefixLength = 0;
    int length = 0;
    GadonnkoState root{}, finalState{};
    BattleResult replay{};
    int zeroChangePosition = -1;
    int zeroChangeLength = 0;
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

class GadonnkoSearch {
public:
    static constexpr int DefaultBudgetMs = 1500;
    static constexpr int DefaultVariant = 0;
    static GadonnkoResult Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength,
        int budgetMs = DefaultBudgetMs, int variant = DefaultVariant);
    static bool SamePlayer(const Player &, const Player &);
    static bool SameFuture(const GadonnkoState &, const GadonnkoState &);
    static bool SameState(const GadonnkoState &, const GadonnkoState &);
    static bool Legal(const Player &, int encodedAction);
    // Historical prefix choices are replayed verbatim, not re-selected by Legal.
    // Whole-sequence and one-turn executions must agree before admission.
    static bool Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, GadonnkoState &, BattleResult &,
        int validateFrom = 0);
    static int VariantCount();
    static const char *VariantName(int variant);
};