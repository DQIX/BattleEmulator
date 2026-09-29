#include "BattleEmulator.h"
#include "lcg.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

namespace {
using BE = BattleEmulator;
std::string lastError, lastDump;
std::vector<int> enemyObservations, damageObservations;
std::vector<BE::Command> preparedCommands;
uint64_t foundSeed = 0, lastTurnProcessed = 0;
int foundSeeds = 0;

uint64_t number(std::string_view text) {
    int base = 10;
    if (text.starts_with("0x") || text.starts_with("0X")) { base = 16; text.remove_prefix(2); }
    uint64_t n{};
    const auto [end, ec] = std::from_chars(text.data(), text.data()+text.size(), n, base);
    if (text.empty() || ec != std::errc{} || end != text.data()+text.size()) throw std::invalid_argument("invalid unsigned integer");
    return n;
}

BE::Command command(std::string_view text) {
    const auto colon = text.find(':');
    const auto actionText = text.substr(0, colon);
    int packed = 0;
    if (actionText == "attack") packed = BE::ATTACK_ALLY;
    else if (actionText == "defend") packed = BE::DEFENCE;
    else if (actionText == "flee") packed = BE::FLEE_ALLY;
    else packed = static_cast<int>(number(actionText));
    BE::Command c{BE::HeroActionId(packed), BE::HeroTargetId(packed), BE::HeroBareHands(packed)};
    if (colon != std::string_view::npos) c.target = static_cast<int>(number(text.substr(colon+1)));
    if (c.bareHands || (c.action != BE::ATTACK_ALLY && c.action != BE::DEFENCE && c.action != BE::FLEE_ALLY))
        throw std::invalid_argument("Slime hero commands: attack/25, defend/27, flee/53 only; no equipment changes");
    if (c.target != -1 && (c.target < 1 || c.target > 3)) throw std::invalid_argument("enemy target must be 1..3");
    return c;
}

std::vector<int> integers(std::string text) {
    std::replace(text.begin(), text.end(), ',', ' ');
    std::istringstream input(text);
    std::vector<int> values;
    for (std::string token; input >> token;) values.push_back(static_cast<int>(number(token)));
    if (values.size() > 349) throw std::invalid_argument("at most 349 observations are supported");
    return values;
}

bool prepare(const char* text) {
    preparedCommands.clear(); enemyObservations.clear(); damageObservations.clear(); lastError.clear();
    try {
        if (text == nullptr) throw std::invalid_argument("missing input");
        std::string_view input(text);
        const auto first = input.find('-'), second = first == input.npos ? input.npos : input.find('-', first+1);
        if (first == input.npos || second == input.npos || input.find('-', second+1) != input.npos)
            throw std::invalid_argument("input format: enemyActions-heroActions-damages");
        enemyObservations = integers(std::string(input.substr(0, first)));
        damageObservations = integers(std::string(input.substr(second+1)));
        std::string commands(input.substr(first+1, second-first-1));
        std::replace(commands.begin(), commands.end(), ',', ' ');
        std::istringstream stream(commands);
        for (std::string token; stream >> token;) preparedCommands.push_back(command(token));
        if (preparedCommands.empty()) throw std::invalid_argument("at least one hero command is required");
        for (int a : enemyObservations) if (a != BE::ATTACK_ENEMY && a != BE::FLEE_ENEMY)
            throw std::invalid_argument("enemy observations support common IDs 1 (attack) and 195 (flee)");
        if (enemyObservations.empty() && damageObservations.empty()) throw std::invalid_argument("at least one observed action or damage is required");
        return true;
    } catch (const std::exception& e) { lastError = e.what(); return false; }
}

uint64_t advance(uint64_t seed, int consumed) {
    for (int i = 0; i < consumed; ++i) seed = seed * UINT64_C(0x5d588b656c078965) + UINT64_C(0x269ec3);
    return seed;
}

void dumpRecords(std::ostream& out, const BattleResult& result) {
    for (const auto& r : result.records) {
        out << "TRACE record turn=" << r.turn << " actor=" << r.actor << " rawActor=0x" << std::hex << BE::RawActorId(r.actor)
            << " rawTarget=0x" << BE::RawActorId(r.target) << std::dec << " action=" << r.action
            << " name=" << BE::getActionName(r.action) << " target=" << r.target
            << " damage=" << r.damage << " rawDamage=" << r.rawDamage << " critical=" << r.critical << " evaded=" << r.evaded
            << " rng=" << r.rngBefore << "->" << r.rngAfter << " hp=";
        for (int i = 0; i < 5; ++i) out << (i ? "," : "") << r.hp[i];
        out << " mp=";
        for (int i = 0; i < 5; ++i) out << (i ? "," : "") << r.mp[i];
        out << " escaped=";
        for (int i = 0; i < 5; ++i) out << r.escaped[i];
        out << '\n';
    }
}

void dumpCamera(std::ostream& out) {
#if defined(gerunikku)
    for (std::size_t i = 0; i < camera::DebugEventCount(); ++i) {
        const auto e = camera::DebugEventAt(i);
        out << "TRACE main-camera turn=" << e.turnSerial+1 << " actionIndex=" << e.actionIndex
            << " common=" << e.commonActionId << " dq9=" << e.dq9ActionId
            << " actor=0x" << std::hex << e.actorId << " target=0x" << e.targetId << std::dec
            << " route=" << int(e.actorRouteCount) << " maxRoute=" << int(e.maxRouteCount)
            << " call=" << e.productionCalledFreeCamera << " param5=" << e.runtimeParam5
            << " profile=" << e.membershipProfile << " membership=" << e.actorMembershipCount << " start=";
        for (int j = 0; j < e.presentationActorCount; ++j) out << (j ? "," : "") << int(e.startNodesBefore[j]);
        out << " after=";
        for (int j = 0; j < e.presentationActorCount; ++j) out << (j ? "," : "") << int(e.startNodesAfter[j]);
        out << " goals=";
        for (int j = 0; j < e.presentationActorCount; ++j) out << (j ? "," : "") << int(e.goalNodes[j]);
        out << " row4=";
        for (int j = 0; j < e.presentationActorCount; ++j) out << (e.rosterField4Known[j] ? (e.rosterField4Nonzero[j] ? '1' : '0') : '?');
        out << " aux=";
        for (int j = 0; j < e.presentationActorCount; ++j) out << (j ? "," : "") << int(e.auxiliaryNodes[j]);
        out << " routes=";
        for (int j = 0; j < e.routeActorCount; ++j) {
            out << (j ? ";" : "") << e.routeActorIds[j] << ':';
            for (int k = 0; k < e.routeCounts[j]; ++k) out << (k ? "," : "") << int(e.routeNodes[j][k]);
        }
        out << '\n';
    }
#endif
}

void replay(std::ostream& out, uint64_t seed, int currentPosition, const std::vector<BE::Command>& commands, bool trace, int calibrationHp = -1, bool detailed = true) {
    BE::State state;
    lcg::init(seed);
    if (!BE::InitializeBattle(state, currentPosition+1)) throw std::runtime_error("Slime presentation initialization failed");
    if (calibrationHp >= 0) {
        if (calibrationHp < 1 || calibrationHp > 20) throw std::invalid_argument("calibration HP must be 1..20");
        state.players[0].hp = calibrationHp;
        out << "CALIBRATION heroHp=" << calibrationHp << " (not an unchanged Slime.dst replay)\n";
    }
#if defined(gerunikku)
    camera::SetDebugCapture(trace);
    camera::ClearDebugEvents();
#endif
    for (auto c : commands) {
        if (state.finished) break;
        BattleResult result;
        if (!BE::StepBattle(state, c, &result, trace && detailed)) throw std::runtime_error("command unavailable in current battle state");
        dumpRecords(out, result);
        out << "TRACE turn-end turn=" << state.turn << " consumed=" << state.position-1 << " next=" << state.position
            << " live=0x" << std::hex << std::setw(16) << std::setfill('0') << advance(seed, state.position-1)
            << std::dec << std::setfill(' ') << " finished=" << state.finished << " hp=";
        for (int i = 0; i < 5; ++i) out << (i ? "," : "") << state.players[i].hp;
        out << " escaped=";
        for (const auto& p : state.players) out << p.escaped;
        out << '\n';
    }
    if (trace) dumpCamera(out);
#if defined(gerunikku)
    camera::SetDebugCapture(false);
#endif
}

bool matchSeed(uint64_t seed, BattleResult& result) {
    BE::State state;
    lcg::init(seed);
    if (!BE::InitializeBattle(state)) return false;
    std::size_t enemyIndex = 0, damageIndex = 0;
    for (auto c : preparedCommands) {
        if (state.finished) break;
        result.clear();
        if (!BE::StepBattle(state, c, &result)) return false;
        for (const auto& r : result.records) {
            if (r.actor >= 2 && enemyIndex < enemyObservations.size()) {
                if (r.action != enemyObservations[enemyIndex++]) return false;
            }
            // Same flat chronological damage stream used by the frontend.
            // The guest's visible normal attacks also belong in that stream;
            // defend/flee dummy calculations and healing are not damage.
            if ((r.action == BE::ATTACK_ALLY || r.action == BE::ATTACK_ENEMY) && damageIndex < damageObservations.size()) {
                if (r.damage != damageObservations[damageIndex++]) return false;
            }
            if (enemyIndex == enemyObservations.size() && damageIndex == damageObservations.size()) return true;
        }
    }
    return enemyIndex == enemyObservations.size() && damageIndex == damageObservations.size();
}
}

extern "C" {
EMSCRIPTEN_KEEPALIVE int wasm_prepare_input(const char* input) { return prepare(input) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE const char* wasm_get_last_error() { return lastError.c_str(); }
EMSCRIPTEN_KEEPALIVE uint64_t wasm_bruteforce_range(int resultIndex, uint64_t startSeed, uint64_t endSeed) {
    (void)resultIndex;
    BE::ResetTurnProcessed(); foundSeeds = 0; foundSeed = 0;
    if (preparedCommands.empty() || startSeed > endSeed || endSeed > (UINT64_C(1) << 22)) {
        lastError = "prepare input first; seed interval must be [start,end) within 22 bits"; return 0;
    }
    BattleResult result; result.records.reserve(5);
    for (uint64_t seed = startSeed; seed < endSeed; ++seed) {
        if (matchSeed(seed, result)) { ++foundSeeds; foundSeed = seed; }
    }
    lastTurnProcessed = BE::getTurnProcessed();
    return foundSeeds == 1 ? foundSeed : 0;
}
EMSCRIPTEN_KEEPALIVE uint64_t wasm_get_turn_processed() { return lastTurnProcessed; }
EMSCRIPTEN_KEEPALIVE int wasm_get_found_seeds() { return foundSeeds; }
EMSCRIPTEN_KEEPALIVE const char* wasm_search_dump(int resultIndex, uint64_t seed, int numThreads, int dropbug) {
    (void)numThreads; (void)dropbug;
    try {
        if (resultIndex < 0 || preparedCommands.empty()) throw std::invalid_argument("invalid/unprepared input");
        std::ostringstream output;
        replay(output, seed, 0, preparedCommands, false);
        lastDump = output.str();
    } catch (const std::exception& e) { lastError = e.what(); lastDump.clear(); }
    return lastDump.c_str();
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string_view(argv[1]) == "--help") {
            std::cout << "Slime.dst exact replay (hero + Izayaaru + 3 enemies; full camera)\n"
                         "--trace-main-sequence SEED CURRENT_POSITION COMMAND...\n"
                         "--trace-turn SEED COMMAND [TARGET=-1] [CURRENT_POSITION=0]\n"
                         "--trace-battle SEED TURNS COMMAND [TARGET=-1] [CURRENT_POSITION=0]\n"
                         "--calibrate-hp SEED HERO_HP COMMAND... (explicit modified-state observation)\n"
                         "--frontend-range START END enemyActions-heroActions-damages\n"
                         "Commands: attack/25[:1..3], defend/27, flee/53. Target 1=A,2=cruelcumber,3=B.\n"
                         "No battle-path optimizer is implemented. Existing wasm_search_dump replays the supplied commands.\n";
            return 0;
        }
        const std::string mode(argv[1]);
        if (mode == "--frontend-range") {
            if (argc != 5 || !wasm_prepare_input(argv[4])) throw std::invalid_argument(lastError.empty() ? "expected START END INPUT" : lastError);
            const auto seed = wasm_bruteforce_range(0, number(argv[2]), number(argv[3]));
            std::cout << "found=" << foundSeeds << " uniqueSeed=0x" << std::hex << seed << std::dec << " turns=" << lastTurnProcessed << '\n';
            return 0;
        }
        if (argc < 4) throw std::invalid_argument("missing replay arguments");
        const uint64_t seed = number(argv[2]);
        int currentPosition = 0, hp = -1;
        std::vector<BE::Command> commands;
        if (mode == "--trace-main-sequence" || mode == "--trace-camera-sequence" || mode == "--calibrate-hp") {
            if (argc < 5) throw std::invalid_argument("at least one command is required");
            if (mode == "--calibrate-hp") hp = static_cast<int>(number(argv[3]));
            else currentPosition = static_cast<int>(number(argv[3]));
            for (int i = 4; i < argc; ++i) commands.push_back(command(argv[i]));
        } else if (mode == "--trace-turn" || mode == "--trace-battle") {
            const bool many = mode == "--trace-battle";
            const int actionArg = many ? 4 : 3;
            if (argc <= actionArg) throw std::invalid_argument("missing command");
            auto c = command(argv[actionArg]);
            if (argc > actionArg+1 && std::string_view(argv[actionArg+1]) != "-1") c.target = static_cast<int>(number(argv[actionArg+1]));
            if (argc > actionArg+2) currentPosition = static_cast<int>(number(argv[actionArg+2]));
            const int turns = many ? static_cast<int>(number(argv[3])) : 1;
            if (turns < 1 || turns > 100) throw std::invalid_argument("turns must be 1..100");
            commands.assign(turns, c);
        } else throw std::invalid_argument("unknown mode");
        replay(std::cout, seed, currentPosition, commands, true, hp, mode != "--trace-camera-sequence");
        return 0;
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 2; }
}
