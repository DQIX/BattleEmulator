#include "GerunikkuSearch.h"
#include "lcg.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>

namespace gerunikku_search {
namespace {
using BE = BattleEmulator;
using State = BE::SearchState;
using Command = BE::SearchCommand;
using Clock = std::chrono::steady_clock;
// Kept identical to the existing gerunikku kScenarioCommands. The FLEE entry
// remains in the profile, but its known-bad transition is never searched.
constexpr std::array<Command, 36> profile{{
    {BE::MAGIC_MIRROR, -1}, {BE::BUFF, -1}, {BE::PSYCHE_UP_ALLY, -1},
    {BE::DOUBLE_UP, -1}, {BE::MULTITHRUST, 2},
    {BE::ZAKI, 1}, {BE::ZAKI, 3}, {BE::ZARAKI, 1}, {BE::ZARAKI, 3},
    {BE::ATTACK_ALLY, 1}, {BE::ATTACK_ALLY, 2}, {BE::ATTACK_ALLY, 3},
    {BE::THUNDER_THRUST, 1}, {BE::THUNDER_THRUST, 2}, {BE::THUNDER_THRUST, 3},
    {BE::BEAST_THRUST, 1}, {BE::BEAST_THRUST, 2}, {BE::BEAST_THRUST, 3},
    {BE::VITAL_POINT_THRUST, 1}, {BE::VITAL_POINT_THRUST, 2}, {BE::VITAL_POINT_THRUST, 3},
    {BE::MERCURIAL_THRUST, 1}, {BE::MERCURIAL_THRUST, 2}, {BE::MERCURIAL_THRUST, 3},
    {BE::FLEE_ALLY, -1}, {BE::MIDHEAL, -1}, {BE::MORE_HEAL, -1},
    {BE::FULLHEAL, -1}, {BE::SPECIAL_MEDICINE, -1}, {BE::MAGIC_WATER, -1},
    {BE::SAGE_ELIXIR, -1}, {BE::ELFIN_ELIXIR, -1}, {BE::DEFENCE, -1},
    {BE::DEFENDING_CHAMPION, -1}, {BE::INSULATE, -1}, {BE::GOSPEL_SONG, -1}
}};
struct Node {
    State state{};
    std::array<std::uint16_t, 349> path{};
    int depth = 0;
    int records = 0;
    double score = 0;
    std::uint64_t hash = 0;
};
Command unpack(int packed) noexcept {
    return {BE::HeroActionId(packed), BE::HeroTargetId(packed)};
}
// This is only a hash bucket selector. FULL semantic equality, including the
// complete camera snapshot, is required before dropping a duplicate.
std::uint64_t bucketHash(const State& s) noexcept {
    std::uint64_t h = s.nowState ^ (std::uint64_t(s.position) * 0x9e3779b97f4a7c15ULL);
    for (const Player& p : s.players) {
        h ^= std::uint64_t(std::uint32_t(p.hp)) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::uint64_t(std::uint32_t(p.mp)) + (std::uint64_t(p.TensionLevel) << 32)
             + (h << 6) + (h >> 2);
    }
    return h;
}

// Tactical value estimates are not transitions and never modify the real world.
// Estimate the cheapest mix of immediate and charged MULTITHRUST attacks.
double bossDistance(const State& s) noexcept {
    const Player& h = s.players[0];
    const Player& b = s.players[2];
    if (b.hp == 0) return 0;
    constexpr double tension[] = {1, 1.5, 2.5, 4, 6};
    double best = 1000;
    for (int buff = 0; buff != 2; ++buff) {
        const double atk = buff ? std::max(double(h.atk), h.defaultATK * 1.5) : h.atk;
        const double base = std::max(1.0, (atk * .5 - b.def * .25) * .5);
        const int level = std::clamp(h.TensionLevel, 0, 4);
        for (int charge = level; charge <= 4; ++charge) {
            const double damage = (base * tension[charge] + charge * 5) * 3.7 * 1.25;
            const double following = (base * 4 + 15) * 3.7 * 1.25;
            const double turns = (buff ? 1.0 : 0.0) + (charge - level)
                + 1.0 + std::max(0.0, b.hp - damage) / following * 4.0;
            // A soft boundary preserves incremental damage value near a kill.
            const double finishCredit = b.hp < damage ? .45 * (1.0 - b.hp / damage) : 0.0;
            best = std::min(best, turns - finishCredit);
        }
    }
    return best;
}
double estimate(const State& s, int records, int mode) noexcept {
    const Player& h = s.players[0];
    if (h.hp <= 0) return 1e9;
    const int sides = (s.players[1].hp > 0) + (s.players[3].hp > 0);
    const double boss = bossDistance(s);
    // Side removal saves a complete enemy action on every subsequent turn.
    double score = records + boss * 3.0 + sides * (5.0 + boss * .8);
    score += (s.players[1].hp + s.players[3].hp) * .002;
    const double hpRatio = double(h.hp) / std::max(1, h.maxHp);
    score += (1 - hpRatio) * (mode == 1 ? 7.0 : 3.0);
    if (h.hp < 70) score += (70 - h.hp) * .12;
    if (s.players[2].hp > 0 && !h.hasMagicMirror && h.TensionLevel != 4)
        score += mode == 2 ? 1.5 : 4.0;
    if (h.confused || h.paralysis || h.sleeping) score += 7.0;
    if (h.inactive) score += 2.5;
    if (h.mp < 12) score += (12 - h.mp) * .3;
    if (mode == 2) score += boss * .65 - h.TensionLevel * .35;
    return score;
}
// Abstract classes are diversity quotas, never exact equivalence/dominance.
unsigned diversityClass(const State& s) noexcept {
    const Player& h = s.players[0];
    return unsigned((s.players[1].hp > 0) | ((s.players[3].hp > 0) << 1))
        | (unsigned(std::clamp(h.TensionLevel, 0, 4)) << 2)
        | (unsigned(h.hasMagicMirror) << 5)
        | (unsigned(h.AtkBuffLevel > 0) << 6)
        | (unsigned(h.confused || h.paralysis || h.sleeping) << 7);
}

class Engine {
    const Player* initial;
    std::uint64_t seed;
    std::span<const std::int32_t> prefix;
    Limits limits;
    Clock::time_point started = Clock::now();
    Clock::time_point deadline;
    Result out;
    Node root;
    Node best;
    bool haveWin = false;
    BattleResult scratch{};
    std::vector<Node> current;
    std::vector<Node> children;
    std::vector<Node> selected;
    std::vector<std::size_t> order;
    double bestPartial = std::numeric_limits<double>::infinity();
    std::uint64_t noiseSalt = 0;
    double noiseScale = 0;

    double elapsed() const { return std::chrono::duration<double, std::milli>(Clock::now() - started).count(); }
    bool expired() const { return Clock::now() >= deadline; }
    bool room(const Node& n) {
        // The emulator owns fixed RNG/result buffers. Stop BEFORE capacity, not
        // by changing RNG position or continuing beyond the supplied gene.
        if (n.depth >= limits.maxSuffixTurns || prefix.size() + n.depth >= 349) return false;
        if (n.state.position >= 6400 || n.records > 990) {
            out.capacityLimited = true;
            return false;
        }
        return n.state.players[0].hp > 0 && !allEnemiesDead(n.state);
    }
    bool replay(const Node& candidate, bool keep) {
        ++out.stats.replayChecks;
        std::array<std::int32_t, 350> gene;
        gene.fill(-1);
        std::copy(prefix.begin(), prefix.end(), gene.begin());
        for (int i = 0; i < candidate.depth; ++i) gene[prefix.size() + i] = candidate.path[i];
        State checked{};
        lcg::init(seed, true);
        if (!BE::InitializeSearchState(&checked, initial, limits.initialPosition)) return false;
        BattleResult result{};
        const int turns = int(prefix.size()) + candidate.depth;
        if (turns > 0) {
            BE::Main(&checked.position, turns, gene.data(), checked.players, &result,
                     seed, nullptr, nullptr, -1, &checked.nowState);
            checked.cameraRuntime = camera::CaptureRuntimeState();
        }
        if (!sameState(checked, candidate.state) || result.position != candidate.records) {
            ++out.stats.replayFailures;
            return false;
        }
        if (keep) {
            out.gene = gene;
            out.battle = result;
            out.finalState = checked;
            out.totalTurns = turns;
            out.won = allEnemiesDead(checked);
            out.verified = true;
        }
        return true;
    }
    void consider(const Node& n) {
        const bool win = allEnemiesDead(n.state);
        if (win) {
            if (haveWin && (n.records > best.records ||
                (n.records == best.records && n.state.players[0].hp <= best.state.players[0].hp))) return;
            if (!replay(n, true)) return;
            if (!haveWin) out.stats.firstWinMs = elapsed();
            haveWin = true;
            best = n;
            ++out.stats.updates;
        } else if (!haveWin) {
            const double value = estimate(n.state, n.records, 0);
            if (value < bestPartial) {
                bestPartial = value;
                best = n;
            }
        }
    }
    void select(int width, bool diverse) {
        order.resize(children.size());
        std::iota(order.begin(), order.end(), std::size_t(0));
        std::sort(order.begin(), order.end(), [&](auto a, auto b) {
            if (children[a].score != children[b].score) return children[a].score < children[b].score;
            return a < b;
        });
        selected.clear();
        auto accept = [&](std::size_t index) {
            Node& c = children[index];
            for (const Node& n : selected) {
                if (n.hash == c.hash && sameState(n.state, c.state)) {
                    ++out.stats.exactDuplicates;
                    return false;
                }
            }
            selected.push_back(std::move(c));
            return true;
        };
        std::vector<bool> used(children.size());
        const int elite = diverse ? std::max(1, width / 2) : width;
        for (std::size_t i : order) {
            if (int(selected.size()) >= elite) break;
            used[i] = accept(i);
        }
        if (diverse) {
            std::array<int, 256> counts{};
            for (const Node& n : selected) ++counts[diversityClass(n.state)];
            const int quota = std::max(1, width / 12);
            for (std::size_t i : order) {
                if (int(selected.size()) >= width) break;
                if (used[i]) continue;
                const unsigned group = diversityClass(children[i].state);
                if (counts[group] >= quota) continue;
                if (accept(i)) { used[i] = true; ++counts[group]; }
            }
        }
        for (std::size_t i : order) {
            if (int(selected.size()) >= width) break;
            if (!used[i]) accept(i);
        }
        current.swap(selected);
    }
    void beam(const Node& start, int width, int mode, bool diverse) {
        ++out.stats.passes;
        current.clear();
        current.push_back(start);
        while (!current.empty() && !expired()) {
            children.clear();
            for (const Node& parent : current) {
                if (!room(parent)) continue;
                if (haveWin && parent.records >= best.records) {
                    ++out.stats.boundPruned;
                    continue;
                }
                ++out.stats.expanded;
                for (const Command cmd : profile) {
                    if (expired()) return;
                    if (!canSearchCommand(parent.state, cmd)) continue;
                    Node child;
                    if (!BE::StepSearchState(parent.state, cmd, &child.state, &scratch)) continue;
                    ++out.stats.transitions;
                    child.depth = parent.depth + 1;
                    child.path = parent.path;
                    child.path[parent.depth] = std::uint16_t(BE::PackHeroAction(cmd.action, cmd.target));
                    child.records = parent.records + scratch.position;
                    consider(child);
                    if (!room(child)) continue;
                    if (haveWin && child.records >= best.records) {
                        ++out.stats.boundPruned;
                        continue;
                    }
                    child.hash = bucketHash(child.state);
                    child.score = estimate(child.state, child.records, mode);
                    if (noiseScale != 0) {
                        // Search-order noise only. Never touches emulator RNG.
                        std::uint64_t x = child.hash + noiseSalt;
                        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
                        x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
                        x ^= x >> 31;
                        child.score += noiseScale * (double(x >> 11) * 0x1p-53 - .5);
                    }
                    children.push_back(std::move(child));
                }
            }
            if (children.empty()) return;
            select(width, diverse);
        }
    }
    Node bestPrefix(int depth) {
        Node n = root;
        for (int i = 0; i < depth; ++i) {
            const Command cmd = unpack(best.path[i]);
            BE::StepSearchStateInPlace(&n.state, cmd, &scratch);
            ++out.stats.transitions;
            n.records += scratch.position;
            n.path[i] = best.path[i];
            ++n.depth;
        }
        return n;
    }
    void neighborhood(int trial) {
        if (!haveWin || best.depth == 0) return;
        // Change one actual command, then replan ALL following commands. Unlike
        // replaying a fixed suffix this can repair the resulting RNG divergence.
        // The cut is relative to the searched suffix, never to the input prefix.
        const int cut = trial % best.depth;
        Node start = bestPrefix(cut);
        const int originalCommand = best.path[cut];
        std::vector<Node> alternatives;
        alternatives.reserve(profile.size());
        for (Command cmd : profile) {
            if (expired()) return;
            const int packed = BE::PackHeroAction(cmd.action, cmd.target);
            if (packed == originalCommand || !canSearchCommand(start.state, cmd)) continue;
            Node child = start;
            BE::StepSearchStateInPlace(&child.state, cmd, &scratch);
            ++out.stats.transitions;
            child.path[child.depth++] = std::uint16_t(packed);
            child.records += scratch.position;
            consider(child);
            if (!room(child) || (haveWin && child.records >= best.records)) continue;
            child.score = estimate(child.state, child.records, trial % 3);
            alternatives.push_back(std::move(child));
        }
        if (alternatives.empty()) return;
        std::stable_sort(alternatives.begin(), alternatives.end(), [](const Node& a, const Node& b) {
            return a.score < b.score;
        });
        const std::size_t rank = std::size_t(trial / std::max(1, best.depth)) % alternatives.size();
        noiseSalt = std::uint64_t(trial + 1) * 0x9e3779b97f4a7c15ULL;
        noiseScale = 1.0 + (trial % 4);
        beam(alternatives[rank], std::min(12, limits.maxBeamWidth), trial % 3, true);
        noiseScale = 0;
    }
public:
    Engine(const Player* p, std::uint64_t s, std::span<const std::int32_t> past, Limits l)
        : initial(p), seed(s), prefix(past), limits(l) {
        const double budget = std::isfinite(l.milliseconds) && l.milliseconds >= 0
            ? std::min(l.milliseconds, 86400000.0) : 0.0;
        const double reserve = std::min(2.0, std::max(.05, budget * .02));
        deadline = started + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double, std::milli>(std::max(0.0, budget - reserve)));
        out.gene.fill(-1);
        out.pastTurns = int(prefix.size());
    }
    Result run() {
        if (!initial || prefix.size() > 349 || !std::isfinite(limits.milliseconds)
            || limits.milliseconds < 0 || limits.initialPosition < 1 || limits.initialPosition >= 6400
            || limits.maxSuffixTurns < 0 || limits.maxBeamWidth < 1 || limits.maxBeamWidth > 4096
            || limits.variant < 0 || limits.variant > 2) {
            out.validInput = false;
            out.error = "invalid search input or limits";
            return std::move(out);
        }
        for (int a : prefix) {
            if (a <= 0) {
                out.validInput = false;
                out.error = "fixed prefix must contain commands, not a terminator";
                return std::move(out);
            }
        }
        lcg::init(seed, true);
        if (!BE::InitializeSearchState(&root.state, initial, limits.initialPosition)) {
            out.validInput = false;
            out.error = "emulator could not initialize input world";
            return std::move(out);
        }
        // Replay exactly the fixed prefix, using Main's original command/history
        // semantics (including status-display actions). Never optimize this part.
        std::copy(prefix.begin(), prefix.end(), out.gene.begin());
        if (!prefix.empty()) {
            // Check fixed emulator capacities before replaying each supplied turn.
            for (int i = 0; i < int(prefix.size()); ++i) {
                if (root.state.position >= 6400 || root.records > 990) {
                    out.validInput = false;
                    out.capacityLimited = true;
                    out.error = "fixed prefix exceeds emulator buffer capacity";
                    return std::move(out);
                }
                scratch.clear();
                BE::Main(&root.state.position, 1, out.gene.data(), root.state.players, &scratch,
                         seed, nullptr, nullptr, -1, &root.state.nowState, -1, false, -1, i == 0);
                root.records += scratch.position;
                if (allEnemiesDead(root.state) || root.state.players[0].hp <= 0) {
                    if (i + 1 < int(prefix.size())) {
                        out.validInput = false;
                        out.error = "fixed prefix continues after the battle has ended";
                        return std::move(out);
                    }
                    break;
                }
            }
            root.state.cameraRuntime = camera::CaptureRuntimeState();
        }
        out.root = root.state;
        best = root;
        bestPartial = estimate(root.state, root.records, 0);
        if (!replay(root, true)) {
            out.validInput = false;
            out.error = "fixed-prefix replay did not reproduce the current state";
            return std::move(out);
        }
        haveWin = out.won;
        if (haveWin) out.stats.firstWinMs = elapsed();
        if (room(root) && !expired()) {
            current.reserve(limits.maxBeamWidth);
            selected.reserve(limits.maxBeamWidth);
            children.reserve(std::size_t(limits.maxBeamWidth) * profile.size());
            int width = std::min(6, limits.maxBeamWidth);
            int round = 0;
            int repairTrial = 0;
            while (!expired()) {
                // Start with a small, deep search to secure an incumbent, then
                // widen and alternate value estimates. Later suffix repair keeps
                // useful discovered setup but never locks it across root passes.
                if (limits.variant == 0 && haveWin && round >= 6 && round % 5 != 4) {
                    neighborhood(repairTrial++);
                    ++round;
                    continue;
                }
                Node start = root;
                if ((limits.variant != 0 || round < 6) && haveWin && round % 3 == 2 && best.depth > 3) {
                    const int cut = std::max(1, best.depth - 3 - (round / 3) % (best.depth - 2));
                    start = bestPrefix(cut);
                }
                if (limits.variant == 0 && round >= 6) {
                    noiseSalt = std::uint64_t(round + 1) * 0x9e3779b97f4a7c15ULL;
                    noiseScale = 2.0 + (round % 5);
                }
                beam(start, width, round % 3, limits.variant != 1);
                noiseScale = 0;
                ++round;
                width = std::min(limits.maxBeamWidth, width * 2);
                if (!room(root)) break;
            }
        }
        // Fresh original-world replay is the source of every output field.
        if (!replay(best, true)) {
            // The last verified incumbent is deliberately retained on mismatch.
            out.error = "candidate mismatch; retained the last verified incumbent";
        }
        out.stats.elapsedMs = elapsed();
        return std::move(out);
    }
};
} // namespace

std::span<const Command> commandProfile() noexcept { return profile; }
bool canSearchCommand(const State& s, Command c) noexcept {
    if (c.action == BE::FLEE_ALLY) return false; // Known unfaithful pre-action skip, NOT a legal-set edit.
    if ((s.players[0].confused || s.players[0].paralysis) && c.action != BE::ATTACK_ALLY) return false;
    bool member = false;
    for (Command p : profile) if (p.action == c.action && p.target == c.target) { member = true; break; }
    if (!member || !BE::IsHeroCommandSelectable(s, c)) return false;
    // The legacy selector's default=true omits these execution costs. Do not
    // exploit negative MP; no changes to the emulator or action ownership.
    if (c.action == BE::THUNDER_THRUST && s.players[0].mp < 8) return false;
    if (c.action == BE::DEFENDING_CHAMPION && s.players[0].mp < 3) return false;
    return true;
}
bool allEnemiesDead(const State& s) noexcept {
    return s.players[1].hp == 0 && s.players[2].hp == 0 && s.players[3].hp == 0;
}
bool sameState(const State& a, const State& b) noexcept {
    return std::equal(std::begin(a.players), std::end(a.players), std::begin(b.players))
        && a.position == b.position && a.nowState == b.nowState && a.cameraRuntime == b.cameraRuntime;
}
Result search(const Player initial[4], std::uint64_t seed,
              std::span<const std::int32_t> prefix, const Limits& limits) {
    return Engine(initial, seed, prefix, limits).run();
}
} // namespace gerunikku_search
