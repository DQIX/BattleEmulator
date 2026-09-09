// Compile the existing table formatter and SearchRequest in this translation
// unit without replacing their implementations or inspecting other app APIs.
#define main battle_search_application_main
#include "main.cpp"
#undef main

#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

battle_search::State sample(uint64_t seed, int variant) {
    lcg::init(seed, true);
    battle_search::State s{};
    Player& a = s.players[0];
    Player& e = s.players[1];
    a.hp = a.maxHp = 65 + variant % 3 * 10;
    a.atk = a.defaultATK = 63 + variant % 4 * 5;
    a.def = a.defaultDEF = 58;
    a.speed = a.defaultSpeed = 40 + variant % 2 * 25;
    a.mp = a.maxMp = 22;
    a.HealPower = 20;
    a.medicinal_herbs_count = 8;
    e.hp = e.maxHp = 456 + variant % 3 * 80;
    e.atk = e.defaultATK = 85;
    e.def = e.defaultDEF = 62;
    e.speed = e.defaultSpeed = 54;
    e.HealPower = 0;
    if (variant % 5 == 1) {
        a.hp = 28;
        a.specialCharge = true;
        a.specialChargeTurn = 3;
    } else if (variant % 5 == 2) {
        a.acrobaticStar = true;
        a.acrobaticStarTurn = 2;
    } else if (variant % 5 == 3) {
        a.mp = 0;
        a.medicinal_herbs_count = 0;
    } else if (variant % 5 == 4) {
        a.paralysis = true;
        a.paralysisTurns = 2;
        a.paralysisLevel = 1;
    }
    return s;
}

int32_t baselineAction(const battle_search::State& s) {
    const Player& a = s.players[0];
    if (!a.acrobaticStar && a.specialCharge && a.specialChargeTurn > 0)
        return BattleEmulator::ACROBATIC_STAR;
    if (a.hp < 32) {
        if (a.mp >= 2) return BattleEmulator::HEAL;
        if (a.medicinal_herbs_count > 0) return BattleEmulator::MEDICINAL_HERBS;
    }
    return BattleEmulator::ATTACK_ALLY;
}

int baseline(battle_search::State s, uint64_t seed) {
    for (int turn = 1; turn <= 80; ++turn) {
        battle_search::State next;
        if (!battle_search::advance(s, baselineAction(s), seed, next)) return 999;
        s = next;
        if (s.players[0].hp <= 0) return 999;
        if (s.players[1].hp == 0) return turn;
    }
    return 999;
}

void verify(const battle_search::State& start, uint64_t seed,
            const battle_search::Result& result) {
    BattleResult stepTrace, wholeTrace;
    battle_search::State finalState;
    require(battle_search::replay(start, seed, result, stepTrace, finalState),
        "Exact per-turn replay mismatch");
    auto whole = start;
    BattleEmulator::Main(&whole.position, result.length, result.actions.data(),
        whole.players, &wholeTrace, seed, nullptr, nullptr, -1, &whole.nowState, true);
    require(battle_search::sameState(whole, result.finalState), "Whole-suffix replay mismatch");
    require(stepTrace.position == wholeTrace.position, "Trace length mismatch");
    for (int i = 0; i < wholeTrace.position; ++i) {
        require(stepTrace.actions[i] == wholeTrace.actions[i] &&
            stepTrace.damages[i] == wholeTrace.damages[i] &&
            stepTrace.turns[i] == wholeTrace.turns[i] &&
            stepTrace.state[i] == wholeTrace.state[i] &&
            stepTrace.isEnemy[i] == wholeTrace.isEnemy[i] &&
            stepTrace.initiative[i] == wholeTrace.initiative[i] &&
            stepTrace.ahp[i] == wholeTrace.ahp[i] && stepTrace.ehp[i] == wholeTrace.ehp[i] &&
            stepTrace.amp[i] == wholeTrace.amp[i] &&
            stepTrace.scTurn[i] == wholeTrace.scTurn[i] &&
            stepTrace.Paralysis1[i] == wholeTrace.Paralysis1[i] &&
            stepTrace.Inactive1[i] == wholeTrace.Inactive1[i], "Trace contents mismatch");
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const int milliseconds = argc > 1 ? std::max(0, std::atoi(argv[1])) : 150;
        const int threads = argc > 2 ? std::max(1, std::atoi(argv[2])) : 1;
        const int samples = argc > 3 ? std::max(1, std::atoi(argv[3])) : 12;
        const uint64_t seedBase = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 8123417ULL;
        int wins = 0, baseWins = 0, faster = 0;
        double totalMs = 0;
        uint64_t transitions = 0;
        std::ostringstream metrics;
        metrics << "case,seed,turns,won,ally_hp,enemy_hp,baseline_turns,ms,transitions\n";
        for (int i = 0; i < samples; ++i) {
            // Independent generated seeds/stats; no supplied solution sequences.
            const uint64_t seed = seedBase + uint64_t(i) * 104729;
            auto start = sample(seed, i);
            const auto original = start;
            const int base = baseline(start, seed);
            baseWins += base != 999;
            battle_search::Options options;
            options.budget = std::chrono::milliseconds(milliseconds);
            options.threads = threads;
            options.maxTurns = 80;
            options.initializeWorker = [seed] { lcg::init(seed, true); };
            const auto result = battle_search::search(start, seed, options);
            require(battle_search::sameState(start, original), "Search changed input state");
            verify(start, seed, result);
            wins += result.won;
            faster += result.won && result.length < base;
            totalMs += result.elapsedMs;
            transitions += result.transitions;
            metrics << i << ',' << seed << ',' << result.length << ',' << result.won
                << ',' << result.finalState.players[0].hp << ',' << result.finalState.players[1].hp
                << ',' << base << ',' << result.elapsedMs << ',' << result.transitions << '\n';
            if (i == 0) {
                int32_t prefix[350]{};
                battle_search::printResult(start, seed, result, prefix, 0, dumpTable);
            }
            // Check a nonzero absolute turn/RNG boundary against one uninterrupted
            // replay from the original snapshot, using the same supplied prefix.
            if (i == 0 && result.length >= 4) {
                auto suffixStart = start;
                BattleEmulator::Main(&suffixStart.position, 3, result.actions.data(),
                    suffixStart.players, nullptr, seed, nullptr, nullptr, -2, &suffixStart.nowState);
                auto suffix = result;
                suffix.length -= 3;
                std::copy_n(result.actions.begin() + 3, suffix.length, suffix.actions.begin());
                verify(suffixStart, seed, suffix);
                options.budget = std::chrono::milliseconds(30);
                const auto searchedSuffix = battle_search::search(suffixStart, seed, options);
                verify(suffixStart, seed, searchedSuffix);
                // Exercise the real SearchRequest boundary and table-first output.
                // Supply only a three-turn observed prefix, never a future solution.
                int observed[350]{};
                std::copy_n(result.actions.begin(), 3, observed);
                observed[3] = -1;
#ifdef _WIN32
                _putenv_s("BATTLE_SEARCH_MS", "30");
#else
                setenv("BATTLE_SEARCH_MS", "30", 1);
#endif
                Player copied[2] = {start.players[0], start.players[1]};
                std::ostringstream printed;
                auto* previous = std::cout.rdbuf(printed.rdbuf());
                SearchRequest(copied, seed, observed, 3, threads);
                std::cout.rdbuf(previous);
                require(printed.str().find("turn") < printed.str().find("Search:"),
                    "SearchRequest must print its table first");
                require(printed.str().find("replay=exact") != std::string::npos,
                    "SearchRequest integration failed");
            }
        }
        std::cout << metrics.str() << "Verified " << samples << " inputs; wins=" << wins
            << ", baseline_wins=" << baseWins << ", faster=" << faster
            << ", transitions_per_second=" << (totalMs > 0 ? transitions * 1000.0 / totalMs : 0)
            << '\n';

        auto start = sample(3571, 0);
        battle_search::Options zero;
        zero.budget = std::chrono::milliseconds(0);
        const auto empty = battle_search::search(start, 3571, zero);
        require(empty.length == 0, "Zero budget must return immediately");
        verify(start, 3571, empty);
        start.nowState = uint64_t(349) << 12;
        zero.budget = std::chrono::milliseconds(10);
        const auto last = battle_search::search(start, 3571, zero);
        require(last.length <= 1, "Action array horizon overflow");
        verify(start, 3571, last);
        start.players[1].hp = 0;
        const auto won = battle_search::search(start, 3571, zero);
        require(won.won && won.length == 0, "Terminal input mishandled");
        std::cout << "Replay, input immutability, prefix, zero-budget, horizon, terminal checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CHECK FAILED: " << error.what() << '\n';
        return 1;
    }
}
