#pragma once
#include <array>
#include <cstdint>
#include "BattleEmulator.h"

// These fields are authoritative emulator state, never heuristic estimates.
struct BilyoumaState {
    Player players[2]{};
    uint64_t nowState = 0;
    int rngPosition = 1;
    int resultPosition = 0;
};

struct BilyoumaResult {
    bool victory = false;
    bool replayVerified = false;
    std::array<int32_t, 350> actions{}; // Original prefix, followed by suffix.
    int prefixLength = 0;
    int length = 0;
    BilyoumaState root{};
    BilyoumaState finalState{};
    BattleResult replay{};
    uint64_t expanded = 0;
    uint64_t duplicates = 0;
    uint64_t collisions = 0;
    uint64_t rejectedReplay = 0;
    uint64_t unsafeFleeSkipped = 0;
    int updates = 0;
    double firstVictoryMs = -1;
    double bestVictoryMs = -1;
    double elapsedMs = 0;
    int completedWidth = 0;
};

class BilyoumaSearch {
public:
    // initial + immutable prefix is the ONLY way to create the search root.
    // maxTotalTurns defaults to the original A*'s 30-turn search horizon.
    static BilyoumaResult Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength, int budgetMs,
        int variant = 0, int maxTotalTurns = 30);
    static bool SamePlayer(const Player &, const Player &);
    static bool SameState(const BilyoumaState &, const BilyoumaState &);
    static bool Legal(const Player &, int action);
    static bool Safe(const Player &, int action);
    static bool Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, BilyoumaState &, BattleResult &);
    static const char *VariantName(int variant);
    static int VariantCount();
};
