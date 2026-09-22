#pragma once

#include <array>
#include <cstdint>
#include "BattleEmulator.h"

struct ZilyadamaState {
    Player players[2]{};
    uint64_t nowState = 0;
    int rngPosition = 1;
    int resultPosition = 0;
};

struct ZilyadamaSearchOptions {
    // A deterministic sequence of completed beam widths, with a wall-clock
    // safety deadline. Production and DEBUG3 use exactly these defaults.
    int budgetMs = 1500;
    int variant = 1;
    int maxWidth = 16384;
};

struct ZilyadamaSearchResult {
    bool inputValid = false;
    bool victory = false;
    bool replayVerified = false;
    bool deadlineReached = false;
    std::array<int32_t, 350> actions{};
    int prefixLength = 0;
    int length = 0;
    int variant = 0;
    int completedWidth = 0;
    ZilyadamaState root{}, finalState{};
    BattleResult replay{};
    uint64_t transitions = 0;
    uint64_t duplicates = 0;
    uint64_t rejectedReplay = 0;
    uint64_t unsafeFleeSkipped = 0;
    int improvements = 0;
    double firstVictoryMs = -1;
    double elapsedMs = 0;
};

class ZilyadamaSearch {
public:
    static ZilyadamaSearchResult Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], const ZilyadamaSearchOptions &options = {});
    static bool Legal(const Player &player, int action);
    static bool Safe(const Player &player, int action);
    static bool SamePlayer(const Player &a, const Player &b);
    static bool SameState(const ZilyadamaState &a, const ZilyadamaState &b);
    static bool Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, ZilyadamaState &state, BattleResult &log);
    static const char *VariantName(int variant);
    static int VariantCount();
};