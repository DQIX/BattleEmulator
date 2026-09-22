#pragma once

#include <cstdint>
#include <string>
#include "BattleResult.h"
#include "Genome.h"

// The budget covers prefix replay, all search passes and candidate admission.
struct ErusionnSearchOptions {
    int budgetMs = 1500;
    int variant = 12;
    int maxTotalTurns = 133; // Existing 400-record buffer, at most 3 records/turn.
};

struct ErusionnSearchResult {
    Genome genome{};             // position remains the legacy RNG cursor.
    BattleResult replay{};       // The objective is THIS position, not genome.position.
    bool inputValid = false;
    bool victory = false;
    bool replayVerified = false;
    int prefixLength = 0;
    int length = 0;
    int equipmentChanges = 0;   // Counted from ACTION_EQUIPMENT_CHANGED in replay.
    int variant = 0;
    uint64_t expanded = 0;
    uint64_t duplicates = 0;
    uint64_t rejectedReplay = 0;
    int updates = 0;
    double firstVictoryMs = -1;
    double bestVictoryMs = -1;
    double elapsedMs = 0;
    std::string error;
};

class ErusionnSearch {
public:
    // All entries preceding -1 are immutable, regardless of prefix length.
    static ErusionnSearchResult Run(const Player initial[2], uint64_t seed,
        const int prefix[350], const ErusionnSearchOptions &options = {});
    static const char *VariantName(int variant);
    static int VariantCount();
};
