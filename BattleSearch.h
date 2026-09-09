#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include "BattleEmulator.h"

namespace battle_search {

// A snapshot at a logical turn boundary, supplied by SearchRequest unchanged.
struct State {
    Player players[2];
    int position = 1;
    uint64_t nowState = 0;
};

struct Options {
    std::chrono::milliseconds budget{5000};
    int maxTurns = 350;
    int threads = 1;
    // Optional per-worker setup for the emulator's existing random tables.
    // All workers finish initialization before any worker starts transitions.
    std::function<void()> initializeWorker;
};

struct Result {
    std::array<int32_t, 350> actions{};
    State finalState{};
    int length = 0;
    bool won = false;
    uint64_t transitions = 0;
    uint64_t duplicates = 0;
    uint64_t passes = 0;
    double elapsedMs = 0;
};

// Legal command generation and the emulator transition are shared by search
// and replay; no synthetic enemy actions, random draws, or known solutions.
int legalActions(const State& state, std::array<int32_t, 8>& actions);
bool advance(const State& state, int32_t action, uint64_t seed,
             State& next, BattleResult* trace = nullptr);
bool sameState(const State& left, const State& right);
Result search(const State& start, uint64_t seed, const Options& options = {});

// Replays the selected suffix with tracing enabled and verifies every final
// state field, including random position and packed emulator state.
bool replay(const State& start, uint64_t seed, const Result& result,
            BattleResult& trace, State& finalState);
Options environmentOptions(int threads, int maxTurns);
using TableFormatter = std::string (*)(BattleResult&, int32_t[350], int);
void printResult(const State& start, uint64_t seed, const Result& result,
                 const int32_t prefix[350], int pastTurns, TableFormatter table);

} // namespace battle_search
