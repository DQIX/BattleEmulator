#ifndef GERUNIKKU_SEARCH_H
#define GERUNIKKU_SEARCH_H

#include "BattleEmulator.h"
#include <array>
#include <cstdint>
#include <span>
#include <string>

// The original Player world and fixed command prefix are input, never search variables.
namespace gerunikku_search {
struct Limits {
    double milliseconds = 250.0;
    int initialPosition = 2; // Same next-RNG-entry convention as the existing search.
    int maxSuffixTurns = 64;
    int maxBeamWidth = 96;
    // 0: diverse beam + directed neighborhood repair; 1: plain beam;
    // 2: checkpoint-01 diverse beam (both retained evaluation baselines).
    int variant = 0;
};
struct Statistics {
    std::uint64_t transitions = 0;
    std::uint64_t expanded = 0;
    std::uint64_t exactDuplicates = 0;
    std::uint64_t boundPruned = 0;
    std::uint64_t updates = 0;
    std::uint64_t replayChecks = 0;
    std::uint64_t replayFailures = 0;
    int passes = 0;
    double firstWinMs = -1.0;
    double elapsedMs = 0.0;
};
struct Result {
    std::array<std::int32_t, 350> gene{};
    BattleResult battle{}; // Always from a fresh whole-prefix + suffix Main replay.
    BattleEmulator::SearchState root{};
    BattleEmulator::SearchState finalState{};
    Statistics stats{};
    int pastTurns = 0;
    int totalTurns = 0;
    bool won = false;
    bool verified = false;
    bool validInput = true;
    bool capacityLimited = false;
    std::string error;
};

// The existing encounter profile, not all internal emulator action IDs.
std::span<const BattleEmulator::SearchCommand> commandProfile() noexcept;
bool canSearchCommand(const BattleEmulator::SearchState& state,
                      BattleEmulator::SearchCommand command) noexcept;
bool allEnemiesDead(const BattleEmulator::SearchState& state) noexcept;
bool sameState(const BattleEmulator::SearchState& a,
               const BattleEmulator::SearchState& b) noexcept;
Result search(const Player initialPlayers[4], std::uint64_t seed,
              std::span<const std::int32_t> fixedPrefix, const Limits& limits = {});
} // namespace gerunikku_search
#endif