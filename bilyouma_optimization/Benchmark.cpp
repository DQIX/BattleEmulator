#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "BilyoumaSearch.h"
#include "BaselineMeasured.h"

// Use the ACTUAL profile and ACTUAL dumpTable, not a reimplementation.
#define main original_program_main
#include "../main.cpp"
#undef main

namespace {
using Clock = std::chrono::steady_clock;
struct Case {
    int id = 0;
    uint64_t seed = 0;
    std::vector<int32_t> prefix;
};

std::array<int32_t, 350> full(const std::vector<int32_t> &v) {
    std::array<int32_t, 350> a;
    a.fill(-1);
    if (v.size() >= a.size()) throw std::runtime_error("Too many actions");
    std::copy(v.begin(), v.end(), a.begin());
    return a;
}

void generate(const std::string &path, int count, uint64_t generatorSeed) {
    std::mt19937_64 rng(generatorSeed);
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot create cases file");
    f << "# generator_seed=" << generatorSeed << " profile_hp=" << BasePlayers[0].maxHp << '\n';
    constexpr int menu[] = {50,55,53,73,26,75,85,25,27,56};
    for (int id = 0; id < count;) {
        Case c; c.id = id; c.seed = (rng() % 0x3fffff) + 1;
        int length = 1 + int(rng() % 8);
        BilyoumaState s; s.players[0] = BasePlayers[0]; s.players[1] = BasePlayers[1];
        lcg::init(c.seed, true);
        for (int t = 0; t < length && s.players[0].hp && s.players[1].hp; ++t) {
            std::vector<int> choices;
            for (int a : menu) if (BilyoumaSearch::Legal(s.players[0], a) && BilyoumaSearch::Safe(s.players[0], a))
                choices.push_back(a);
            int32_t a = choices[rng() % choices.size()];
            c.prefix.push_back(a);
            const int32_t one[2] = {a, -1};
            BattleEmulator::Main(&s.rngPosition, 1, one, s.players, nullptr,
                c.seed, nullptr, nullptr, -2, &s.nowState, true);
        }
        // Only exclude already terminal roots. Never select using search results.
        if (s.players[0].hp == 0 || s.players[1].hp == 0) continue;
        f << id++ << ' ' << c.seed << ' ' << c.prefix.size();
        for (int a : c.prefix) f << ' ' << a;
        f << '\n';
    }
}

std::vector<Case> readCases(const std::string &path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot read cases file");
    std::vector<Case> cases;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        Case c; int n;
        if (!(ss >> c.id >> c.seed >> n) || n < 0 || n > 29) throw std::runtime_error("Invalid case");
        for (int i = 0; i < n; ++i) {
            int a; if (!(ss >> a)) throw std::runtime_error("Missing action");
            c.prefix.push_back(a);
        }
        cases.push_back(c);
    }
    return cases;
}

// Independent turn-by-turn audit of every action and every recorded log field.
bool audit(const Case &c, const BilyoumaResult &r, std::string &reason) {
    if (!std::equal(c.prefix.begin(), c.prefix.end(), r.actions.begin())) {
        reason = "prefix changed"; return false;
    }
    BilyoumaState split;
    split.players[0] = BasePlayers[0]; split.players[1] = BasePlayers[1];
    BattleResult combined, oneLog;
    lcg::init(c.seed, true);
    for (int t = 0; t < r.length; ++t) {
        if (split.players[1].hp == 0 || split.players[0].hp == 0) {
            reason = "unused post-terminal command"; return false;
        }
        int a = r.actions[t];
        if (!BilyoumaSearch::Legal(split.players[0], a) || !BilyoumaSearch::Safe(split.players[0], a)) {
            reason = "illegal/unsafe action at turn " + std::to_string(t + 1); return false;
        }
        const int32_t one[2] = {a, -1};
        oneLog.clear();
        BattleEmulator::Main(&split.rngPosition, 1, one, split.players,
            &oneLog, c.seed, nullptr, nullptr, -1, &split.nowState, true);
        for (int i = 0; i < oneLog.position; ++i) {
            int j = split.resultPosition + i;
            if (j >= r.replay.position) { reason = "short replay log"; return false; }
#define CHECK_LOG(field) if (oneLog.field[i] != r.replay.field[j]) { reason = "log mismatch: " #field; return false; }
            CHECK_LOG(actions); CHECK_LOG(damages); CHECK_LOG(isEnemy);
            CHECK_LOG(BuffTurnss); CHECK_LOG(PoisonTurns); CHECK_LOG(SpeedTurn);
            CHECK_LOG(turns); CHECK_LOG(initiative); CHECK_LOG(ehp); CHECK_LOG(ahp);
            CHECK_LOG(scTurn); CHECK_LOG(amp); CHECK_LOG(state);
            CHECK_LOG(defenseFlag); CHECK_LOG(sleepFlag);
#undef CHECK_LOG
        }
        split.resultPosition += oneLog.position;
        if (t + 1 == int(c.prefix.size()) && !BilyoumaSearch::SameState(split, r.root)) {
            reason = "wrong current-state root"; return false;
        }
    }
    BilyoumaState replay;
    BilyoumaSearch::Replay(BasePlayers, c.seed, r.actions.data(), r.length, replay, combined);
    if (!BilyoumaSearch::SameState(split, replay) || !BilyoumaSearch::SameState(replay, r.finalState)) {
        reason = "final Player/RNG/nowState/resultPosition mismatch"; return false;
    }
    if (r.victory != (replay.players[1].hp == 0)) { reason = "victory mismatch"; return false; }
    return true;
}

BilyoumaResult baseline(const Case &c, int budgetMs, bool stock) {
    const auto start = Clock::now();
    const double usable = std::max(0.0, double(budgetMs) - std::min(20.0, budgetMs * .05));
    const auto globalEnd = stock ? Clock::time_point::max()
        : start + std::chrono::microseconds(int64_t(usable * 1000));
    auto elapsed = [&]() { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
    BilyoumaResult out;
    out.actions = full(c.prefix); out.prefixLength = out.length = int(c.prefix.size());
    BilyoumaSearch::Replay(BasePlayers, c.seed, out.actions.data(), out.length, out.root, out.replay);
    out.finalState = out.root;
    auto consider = [&](const Genome &g, bool allowDefaultTail) {
        if (Clock::now() >= globalEnd || g.turn <= 0 || g.turn > 350) return;
        int n = g.turn - 1;
        if (n < int(c.prefix.size())) return;
        auto actions = full({});
        std::copy_n(g.actions, n, actions.begin());
        if (!std::equal(c.prefix.begin(), c.prefix.end(), actions.begin())) { ++out.rejectedReplay; return; }
        BilyoumaState actual;
        BattleResult log;
        BilyoumaSearch::Replay(BasePlayers, c.seed, actions.data(), n, actual, log);
        if (actual.rngPosition != g.position || actual.nowState != g.state ||
            !BilyoumaSearch::SamePlayer(actual.players[0], g.AllyPlayer) ||
            !BilyoumaSearch::SamePlayer(actual.players[1], g.EnemyPlayer)) {
            ++out.rejectedReplay; return;
        }
        // Original SearchRequest runs Main(...,100), implicitly attacking after
        // the explicit sequence. Materialize that default tail, not a new search.
        if (allowDefaultTail && actual.players[0].hp && actual.players[1].hp) {
            for (int i = n; i < 100; ++i) actions[i] = BattleEmulator::ATTACK_ALLY;
            actions[100] = -1;
            BilyoumaSearch::Replay(BasePlayers, c.seed, actions.data(), 100, actual, log);
            n = int((actual.nowState >> 12) & 0xfffff);
            actions[n] = -1;
        }
        if (actual.players[1].hp != 0 || Clock::now() >= globalEnd) return;
        BilyoumaResult candidate;
        candidate.actions = actions; candidate.length = n; candidate.prefixLength = out.prefixLength;
        candidate.root = out.root; candidate.finalState = actual; candidate.replay = log; candidate.victory = true;
        std::string why;
        if (!audit(c, candidate, why)) { ++out.rejectedReplay; return; }
        if (Clock::now() >= globalEnd) return;
        if (!out.victory) out.firstVictoryMs = elapsed();
        if (!out.victory || actual.resultPosition < out.finalState.resultPosition ||
            (actual.resultPosition == out.finalState.resultPosition && actual.players[0].hp > out.finalState.players[0].hp)) {
            out.victory = out.replayVerified = true;
            out.actions = actions; out.length = n; out.replay = log; out.finalState = actual;
            out.bestVictoryMs = elapsed(); ++out.updates;
        }
    };
    BaselineMeasured::observe = [&](const Genome &g) { consider(g, false); };
    BaselineMeasured::expanded = 0;
    const int generations[] = {30000,20000,30000,30000,30000,30000};
    double cumulative = 0;
    for (int i = 0; i < 6 && Clock::now() < globalEnd; ++i) {
        cumulative += generations[i];
        BaselineMeasured::deadline = stock ? globalEnd : start + std::chrono::microseconds(
            int64_t(usable * 1000 * cumulative / 170000.0));
        EnhancedCostCalculator::setCostTable(static_cast<EnhancedCostCalculator::CostTable>(i));
        auto a = full(c.prefix);
        const auto g = BaselineMeasured::RunAlgorithm(BasePlayers, c.seed, int(c.prefix.size()), generations[i], a.data(), 0);
        consider(g, true);
    }
    BaselineMeasured::observe = {};
    BaselineMeasured::deadline = Clock::time_point::max();
    out.expanded = BaselineMeasured::expanded;
    out.elapsedMs = elapsed();
    return out;
}

void saveReplay(const Case &c, const BilyoumaResult &r, const std::string &path, const std::string &name) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot save replay");
    // dumpTable always comes BEFORE diagnostics.
    f << dumpTable(r.replay, r.actions.data(), int(c.prefix.size())) << '\n';
    f << "algorithm=" << name << " seed=0x" << std::hex << c.seed << std::dec
      << " prefix_length=" << c.prefix.size() << " suffix_length=" << r.length - int(c.prefix.size())
      << " victory=" << r.victory << " BattleResult.position=" << r.finalState.resultPosition
      << " RNG.position=" << r.finalState.rngPosition << " nowState=" << r.finalState.nowState
      << " enemyHP=" << r.finalState.players[1].hp << " allyHP=" << r.finalState.players[0].hp
      << " MP=" << r.finalState.players[0].mp << '\n';
    f << "actions:";
    for (int i = 0; i < r.length; ++i) f << ' ' << r.actions[i];
    f << '\n' << "prefix:";
    for (int a : c.prefix) f << ' ' << a;
    f << '\n' << "root: allyHP=" << r.root.players[0].hp << " enemyHP=" << r.root.players[1].hp
      << " MP=" << r.root.players[0].mp << " sleeping=" << r.root.players[0].sleeping
      << " poison=" << r.root.players[0].PoisonEnable << " charge=" << r.root.players[0].specialCharge
      << " star=" << r.root.players[0].acrobaticStar << " rng=" << r.root.rngPosition
      << " nowState=" << r.root.nowState << " logPosition=" << r.root.resultPosition << '\n';
    f << "first_ms=" << r.firstVictoryMs << " best_ms=" << r.bestVictoryMs << " wall_ms=" << r.elapsedMs
      << " expanded=" << r.expanded << " rejected_replay=" << r.rejectedReplay << '\n';
    std::string why;
    f << "independent_split_and_full_replay=" << audit(c, r, why) << " " << why << '\n';
}

void evaluate(const std::string &casesPath, const std::string &csvPath,
              const std::string &directory, int budgetMs, int variant, const std::string &mode) {
    const auto cases = readCases(casesPath);
    std::filesystem::create_directories(directory);
    std::ofstream f(csvPath);
    if (!f) throw std::runtime_error("Cannot create csv");
    f << "case,profile,seed,prefix,algorithm,variant,budget_ms,victory,result_position,suffix_turns,first_ms,best_ms,expanded,wall_ms,replay_ok,rejected_replay,duplicates,root_hp,root_enemy_hp,root_mp,root_sleep,root_poison,root_charge,root_star,root_rng,root_nowstate,final_hp,final_mp,final_rng,final_nowstate\n";
    f << std::fixed << std::setprecision(3);
    for (const auto &c : cases) {
        std::array<std::string,2> order = c.id % 2 ? std::array<std::string,2>{"optimized","baseline"}
                                                : std::array<std::string,2>{"baseline","optimized"};
        for (const auto &algo : order) {
            if ((mode == "opt" && algo != "optimized") ||
                ((mode == "base" || mode == "stock") && algo != "baseline")) continue;
            const auto a = full(c.prefix);
            auto r = algo == "baseline" ? baseline(c, budgetMs, mode == "stock")
                : BilyoumaSearch::Run(BasePlayers, c.seed, a.data(), int(c.prefix.size()), budgetMs, variant);
            std::string reason;
            const bool ok = audit(c, r, reason);
            if (!ok) throw std::runtime_error("Case " + std::to_string(c.id) + " " + algo + " " + reason);
            f << c.id << ',' << BasePlayers[0].maxHp << ',' << c.seed << ",\"";
            for (int x : c.prefix) f << x << ' ';
            f << "\"," << algo << ',' << variant << ',' << budgetMs << ',' << r.victory << ','
              << r.finalState.resultPosition << ',' << r.length - int(c.prefix.size()) << ','
              << r.firstVictoryMs << ',' << r.bestVictoryMs << ',' << r.expanded << ',' << r.elapsedMs
              << ',' << ok << ',' << r.rejectedReplay << ',' << r.duplicates << ','
              << r.root.players[0].hp << ',' << r.root.players[1].hp << ',' << r.root.players[0].mp << ','
              << r.root.players[0].sleeping << ',' << r.root.players[0].PoisonEnable << ','
              << r.root.players[0].specialCharge << ',' << r.root.players[0].acrobaticStar << ','
              << r.root.rngPosition << ',' << r.root.nowState << ',' << r.finalState.players[0].hp << ','
              << r.finalState.players[0].mp << ',' << r.finalState.rngPosition << ',' << r.finalState.nowState << '\n';
            f.flush();
            saveReplay(c, r, directory + "/case-" + std::to_string(c.id) + "-" + algo + ".txt",
                algo == "baseline" ? (mode == "stock" ? "stock-six-tables" : "timed-six-tables") : BilyoumaSearch::VariantName(variant));
            std::cout << "case=" << c.id << " " << algo << " win=" << r.victory
                << " position=" << r.finalState.resultPosition << " first=" << r.firstVictoryMs
                << " wall=" << r.elapsedMs << " rejected=" << r.rejectedReplay << std::endl;
        }
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 5 && std::string(argv[1]) == "--generate") {
            generate(argv[2], std::stoi(argv[3]), std::stoull(argv[4], nullptr, 0)); return 0;
        }
        if (argc == 8 && std::string(argv[1]) == "--evaluate") {
            evaluate(argv[2], argv[3], argv[4], std::stoi(argv[5]), std::stoi(argv[6]), argv[7]); return 0;
        }
        std::cerr << "--generate FILE COUNT GENERATOR_SEED\n"
                  << "--evaluate CASES CSV REPLAY_DIR BUDGET_MS VARIANT pair|opt|base|stock\n";
        return 2;
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
