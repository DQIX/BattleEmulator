#include "GerunikkuSearchCli.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

std::string dumpTable(const BattleResult&, const int32_t[350], int);
namespace gerunikku_search {
namespace {
std::int32_t parsePackedCommand(const std::string_view token) {
    const auto firstColon = token.find(':');
    const std::string_view actionText = token.substr(0, firstColon);
    const int action = std::stoi(std::string(actionText), nullptr, 0);
    int target = -1;
    bool bareHands = false;
    if (firstColon != token.npos) {
        const std::string_view remainder = token.substr(firstColon + 1);
        const auto secondColon = remainder.find(':');
        const std::string_view second = remainder.substr(0, secondColon);
        if (second == "sude" || second == "on") {
            bareHands = second == "sude";
        } else if (!second.empty()) {
            target = std::stoi(std::string(second), nullptr, 0);
        }
        if (secondColon != remainder.npos) {
            const std::string_view equipment = remainder.substr(secondColon + 1);
            if (equipment == "sude") bareHands = true;
            else if (equipment == "on") bareHands = false;
            else throw std::invalid_argument("equipment must be on or sude");
        }
    }
    if (action <= 0 || action > BattleEmulator::HERO_ACTION_MASK
        || target < -1 || target == 0 || target > 3) {
        throw std::invalid_argument("invalid fixed prefix command");
    }
    return BattleEmulator::PackHeroAction(action, target, bareHands);
}
}

void printResult(const Result& r, std::uint64_t seed, std::ostream& os) {
    os << dumpTable(r.battle, r.gene.data(), r.pastTurns - 1);
    os << std::fixed << std::setprecision(3)
       << "GERUNIKKU_SEARCH seed=0x" << std::hex << seed << std::dec
       << " win=" << r.won << " verified=" << r.verified
       << " pastTurns=" << r.pastTurns << " turns=" << r.totalTurns
       << " resultPosition=" << r.battle.position
       << " rngPosition=" << r.finalState.position
       << " hp=" << r.finalState.players[0].hp << ',' << r.finalState.players[1].hp
       << ',' << r.finalState.players[2].hp << ',' << r.finalState.players[3].hp
       << " mp=" << r.finalState.players[0].mp
       << " elapsedMs=" << r.stats.elapsedMs << " firstWinMs=" << r.stats.firstWinMs
       << " transitions=" << r.stats.transitions << " expanded=" << r.stats.expanded
       << " passes=" << r.stats.passes << " updates=" << r.stats.updates
       << " replayFailures=" << r.stats.replayFailures
       << " capacityLimited=" << r.capacityLimited << '\n';
    os << "GENE";
    for (int i = 0; i < r.totalTurns; ++i) {
        os << ' ' << BattleEmulator::HeroActionId(r.gene[i]);
        int target = BattleEmulator::HeroTargetId(r.gene[i]);
        if (target != -1) os << ':' << target;
        if (BattleEmulator::HeroBareHands(r.gene[i])) os << ":sude";
    }
    os << '\n';
    if (!r.error.empty()) os << "SEARCH_ERROR " << r.error << '\n';
}

Result runRequest(const Player players[4], const std::uint64_t seed,
                  const std::span<const std::int32_t> prefix, const Limits& limits,
                  std::ostream& os) {
    Result result = search(players, seed, prefix, limits);
    printResult(result, seed, os);
    return result;
}

int runCli(int argc, char* argv[], const Player players[4]) {
    try {
        if (argc < 4) throw std::invalid_argument(
            "usage: --search <seed> <milliseconds> [--initial-position=N] [--beam=N] [--depth=N] [--variant=0|1|2] [past action[:target][:sude|on] ...]");
        const std::uint64_t seed = std::stoull(argv[2], nullptr, 0);
        Limits limits;
        limits.milliseconds = std::stod(argv[3]);
        std::vector<std::int32_t> prefix;
        for (int i = 4; i < argc; ++i) {
            const std::string_view token(argv[i]);
            if (token.starts_with("--initial-position=")) limits.initialPosition = std::stoi(std::string(token.substr(19)));
            else if (token.starts_with("--beam=")) limits.maxBeamWidth = std::stoi(std::string(token.substr(7)));
            else if (token.starts_with("--depth=")) limits.maxSuffixTurns = std::stoi(std::string(token.substr(8)));
            else if (token.starts_with("--variant=")) limits.variant = std::stoi(std::string(token.substr(10)));
            else {
                prefix.push_back(parsePackedCommand(token));
            }
        }
        const Result result = runRequest(players, seed, prefix, limits, std::cout);
        return !result.validInput || !result.verified ? 1 : result.won ? 0 : 2;
    } catch (const std::exception& ex) {
        std::cerr << "SEARCH_ERROR " << ex.what() << '\n';
        return 1;
    }
}
}