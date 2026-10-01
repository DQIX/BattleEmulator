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
    if (firstColon == token.npos && action > BattleEmulator::HERO_ACTION_MASK) {
        constexpr int validBits = BattleEmulator::HERO_ACTION_MASK
            | (BattleEmulator::HERO_TARGET_MASK << BattleEmulator::HERO_TARGET_SHIFT)
            | BattleEmulator::HERO_BARE_HANDS_BIT | BattleEmulator::HERO_CAMERA_BITS;
        if ((action & ~validBits) != 0 || BattleEmulator::HeroActionId(action) == 0
            || BattleEmulator::HeroCameraActor(action) >= 12) throw std::invalid_argument("invalid packed command");
        return action;
    }
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
    if (r.cancelled) { os << "SEARCH_CANCELLED\n"; return; }
    os << dumpTable(r.battle, r.gene.data(), r.pastTurns - 1);
    os << std::fixed << std::setprecision(3)
       << "GERUNIKKU_SEARCH seed=0x" << std::hex << seed << std::dec
       << " win=" << r.won << " verified=" << r.verified
       << " pastTurns=" << r.pastTurns << " turns=" << r.totalTurns
       << " resultPosition=" << r.battle.position
       << " equipmentChanges=" << r.equipmentChanges
       << " rngPosition=" << r.publicationState.position
       << " hp=" << r.publicationState.players[0].hp << ',' << r.publicationState.players[1].hp
       << ',' << r.publicationState.players[2].hp << ',' << r.publicationState.players[3].hp
       << " mp=" << r.publicationState.players[0].mp
       << " elapsedMs=" << r.stats.elapsedMs << " firstWinMs=" << r.stats.firstWinMs
       << " transitions=" << r.stats.transitions << " expanded=" << r.stats.expanded
       << " battlePreparations=" << r.stats.battlePreparations
       << " cameraReplays=" << r.stats.cameraReplays
       << " passes=" << r.stats.passes << " updates=" << r.stats.updates
       << " replayFailures=" << r.stats.replayFailures
       << " capacityLimited=" << r.capacityLimited
       << " cameraTurns=" << r.cameraTurns << " riskyTurns=" << r.riskyTurns
       << " fatalOutcomes=" << r.fatalOutcomes
       << " cameraCoverageComplete=" << r.cameraCoverageComplete
       << " publishedTurns=" << r.publishedTurns << '\n';
    if (r.firstUnconfirmedTurn >= 0) {
        os << "CAMERA_BOUNDARY turn=" << r.firstUnconfirmedTurn + 1
           << " beforePosition=" << r.publicationState.position << " recheck=1\n";
        // One executable INPUT, not a predicted outcome or a speculative suffix.
        // No input is issued until it follows all already-observed commands.
        if (r.verified && r.cameraCoverageComplete && r.firstUnconfirmedTurn >= r.pastTurns
            && r.firstUnconfirmedTurn < r.totalTurns) {
            const int packed = r.gene[r.firstUnconfirmedTurn] & ~BattleEmulator::HERO_CAMERA_BITS;
            os << "NEXT_INPUT turn=" << r.firstUnconfirmedTurn + 1
               << " commonId=" << BattleEmulator::HeroActionId(packed)
               << " target=" << BattleEmulator::HeroTargetId(packed)
               << " bareHands=" << BattleEmulator::HeroBareHands(packed)
               << " packed=" << packed << " observeBeforeContinuing=1"
               << " knownFatalCandidate=" << (r.fatalOutcomes != 0) << '\n';
        }
    }
    for (int i = r.pastTurns; i < r.publishedTurns; ++i) {
        os << "SEARCH_COMMAND turn=" << i + 1
           << " commonId=" << BattleEmulator::HeroActionId(r.gene[i])
           << " target=" << BattleEmulator::HeroTargetId(r.gene[i])
           << " bareHands=" << BattleEmulator::HeroBareHands(r.gene[i])
           << " cameraActor=" << BattleEmulator::HeroCameraActor(r.gene[i])
           << " cameraParam5=" << BattleEmulator::HeroCameraParam5(r.gene[i])
           << " packed=" << r.gene[i] << '\n';
    }
    os << "GENE";
    for (int i = 0; i < r.publishedTurns; ++i) {
        os << ' ' << BattleEmulator::HeroActionId(r.gene[i]);
        int target = BattleEmulator::HeroTargetId(r.gene[i]);
        if (target != -1) os << ':' << target;
        if (BattleEmulator::HeroBareHands(r.gene[i])) os << ":sude";
    }
    os << std::endl << "raw";
    for (int i = 0; i < r.publishedTurns; ++i) {
         os << ", " << r.gene[i];
    }
    os << '\n';
    if (!r.error.empty()) os << "SEARCH_ERROR " << r.error << '\n';
    for (const auto& row : r.cameraDebugRows) os << row << '\n';
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
        const bool debug2 = argc > 1 && std::string_view(argv[1]) == "--debug2";
        const bool debug3 = argc > 1 && std::string_view(argv[1]) == "--debug3";
        if (argc < (debug2 ? 3 : 4)) throw std::invalid_argument(
            "usage: --search|--debug3 <seed> <milliseconds> [--initial-position=N] [--beam=N] [--depth=N] [--variant=0|1|2] [past action[:target][:sude|on] or packed Gene ...]; --debug2 <seed> [packed Gene ...]");
        const std::uint64_t seed = std::stoull(argv[2], nullptr, 0);
        Limits limits;
        limits.milliseconds = debug2 ? 0.0 : std::stod(argv[3]);
        limits.debugCameraCandidates = debug2 || debug3;
        std::vector<std::int32_t> prefix;
        for (int i = debug2 ? 3 : 4; i < argc; ++i) {
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
        return !result.validInput || !result.verified ? 1 : debug2 || result.won ? 0 : 2;
    } catch (const std::exception& ex) {
        std::cerr << "SEARCH_ERROR " << ex.what() << '\n';
        return 1;
    }
}
}
