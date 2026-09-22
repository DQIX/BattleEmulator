#pragma once

#include <array>
#include <cstdint>
#include <string>
#include "BattleEmulator.h"

struct GanasadaiState {
    Player players[2]{};
    uint64_t nowState = 0;
    int rngPosition = 1;
    int resultPosition = 0;
    int equipmentChanges = 0;
};

struct GanasadaiSearchResult {
    bool inputValid = false;
    bool victory = false;
    bool replayVerified = false;
    std::string error;
    std::array<int32_t, 350> actions{};
    int prefixLength = 0;
    int length = 0;
    int minimumFinalMp = 0;
    GanasadaiState root{}, finalState{};
    BattleResult replay{};
    uint64_t expanded = 0;
    uint64_t duplicates = 0;
    uint64_t rejectedReplay = 0;
    uint64_t equipmentBranches = 0;
    uint64_t tacticalExpanded = 0;
    int updates = 0;
    int completedWidth = 0;
    double elapsedMs = 0;
};

class GanasadaiSearch {
public:
    static constexpr int RequiredVictoryHp = 200;
    static constexpr int DefaultBudgetMs = 1500;
    static constexpr int DefaultVariant = 12;

    // Legacy ACTION_TABLE admits its damaging action MULTITHRUST at MP >= 10;
    // BattleEmulator consumes 4. Thus those victories always retain >= 6 MP.
    // Preserve that terminal reserve, including for the newly allowed ATTACK
    // finisher. This is NOT an initial-MP equality and NOT an intermediate gate.
    static constexpr int RequiredVictoryMp = 10 - 4;

    static GanasadaiSearchResult Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength,
        int budgetMs = DefaultBudgetMs, int variant = DefaultVariant,
        int maxTotalTurns = 60);
    static bool SameState(const GanasadaiState&, const GanasadaiState&);
    static const char* VariantName(int variant);
    static int VariantCount();
};