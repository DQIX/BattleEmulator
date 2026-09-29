#pragma once

#include <array>
#include <vector>

struct BattleRecord {
    int turn{};
    int actor{};
    int target{};
    int action{};      // Common ID, never a DQ9 ID.
    int damage{};      // Display amount; defend/flee dummy damage is not applied.
    int rawDamage{};
    int rngBefore{};   // Consumed indices, including the guest's AI calculation.
    int rngAfter{};
    bool critical{};
    bool evaded{};
    std::array<int, 5> hp{};
    std::array<int, 5> mp{};
    std::array<bool, 5> escaped{};
};

struct BattleResult {
    std::vector<BattleRecord> records;
    void clear() { records.clear(); }
};
