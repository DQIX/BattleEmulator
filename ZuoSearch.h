#pragma once

#include <array>
#include <cstdint>
#include <string>
#include "BattleEmulator.h"
#include "Genome.h"

struct ZuoState {
    Player players[2]{};
    uint64_t state = 0;
    int rng = 1;
    int position = 0; // BattleResult entries, NOT the RNG cursor.
    int changes = 0;  // ACTION_EQUIPMENT_CHANGED entries in actual replay.
};

struct ZuoSearchResult {
    bool inputValid = false;
    bool victory = false;
    bool replayVerified = false;
    std::string error;
    std::array<int32_t, 350> actions{};
    int prefixLength = 0;
    int length = 0;
    ZuoState root{}, finalState{};
    BattleResult replay{};
    uint64_t expanded = 0, duplicates = 0, rejectedReplay = 0;
    int updates = 0;
    double firstVictoryMs = -1, bestVictoryMs = -1, elapsedMs = 0;
    Genome toGenome() const;
};

class ZuoSearch {
public:
    static constexpr int DefaultBudgetMs = 1500;
    static constexpr int DefaultVariant = 0;
    static ZuoSearchResult Run(const Player initial[2], uint64_t seed,
        const int32_t prefix[350], int prefixLength,
        int budgetMs = DefaultBudgetMs, int variant = DefaultVariant);
    static int VariantCount();
    static const char *VariantName(int variant);
    static bool Legal(const Player &player, int action);
    static bool SameState(const ZuoState &, const ZuoState &);
    // Prefix is historical input, never rewritten. Legality applies to suffix.
    static bool Replay(const Player initial[2], uint64_t seed,
        const int32_t actions[350], int length, int prefixLength,
        ZuoState &finalState, BattleResult &log);
};
