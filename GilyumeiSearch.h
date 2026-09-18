#pragma once
#include <array>
#include <cstdint>
#include "BattleEmulator.h"

struct GilyumeiState {
    Player players[2]{};
    uint64_t nowState = 0;
    int rngPosition = 1;
    int resultPosition = 0;
    int equipmentChanges = 0;
};

struct GilyumeiLaneResult {
    bool victory = false;
    int position = 0;
    int equipmentChanges = 0;
};

struct GilyumeiResult {
    bool inputValid = false;
    bool victory = false;
    bool replayVerified = false;
    std::array<int32_t, 350> actions{};
    int prefixLength = 0;
    int length = 0;
    GilyumeiState root{}, finalState{};
    BattleResult replay{};
    GilyumeiLaneResult unchanged{}, equipment{};
    uint64_t expanded = 0;
    uint64_t duplicates = 0;
    uint64_t hashCollisions = 0;
    uint64_t rejectedReplay = 0;
    uint64_t unsafeFlee = 0;
    std::array<uint64_t, 69> actionExpansions{};
    uint64_t equipmentBranches = 0;
    int updates = 0;
    int completedWidth = 0;
    double firstVictoryMs = -1;
    double elapsedMs = 0;
};

class GilyumeiSearch {
public:
    // All production callers use these defaults, including WASM and DEBUG3.
    static constexpr int ProductionBudgetMs = 1500;
    static constexpr int ProductionVariant = 0;
    static constexpr int ProductionWidth = 96;
    static GilyumeiResult Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength,
        int budgetMs = ProductionBudgetMs, int variant = ProductionVariant);
    static bool SamePlayer(const Player &, const Player &);
    static bool SameFuture(const GilyumeiState &, const GilyumeiState &);
    static bool SameState(const GilyumeiState &, const GilyumeiState &);
    static bool Legal(const Player &, int encodedAction);
    static bool Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, int prefixLength,
        GilyumeiState &, BattleResult &);
};