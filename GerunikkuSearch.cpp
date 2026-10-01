#include "GerunikkuSearch.h"
#include "lcg.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>
#include <vector>
#include <memory>
#include <tuple>
#include <sstream>


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
    std::array<std::int32_t, 349> path{};
    int depth = 0;
    int rootIndex = 0;
    int records = 0;
    int equipmentChanges = 0;
    double score = 0;
    std::uint64_t hash = 0;
    std::shared_ptr<const std::vector<State>> alternatives;
    int firstUnconfirmedTurn = -1;
    int cameraTurns = 0;
    int riskyTurns = 0;
    int fatalOutcomes = 0;
    bool coverageComplete = true;
};
Command unpack(int packed) noexcept {
    return {BE::HeroActionId(packed) | (packed & BE::HERO_CAMERA_BITS), BE::HeroTargetId(packed), BE::HeroBareHands(packed)};
}
struct Outcome {
    State state;
    int packed;
    int records;
    bool cameraChoice;
    int equipmentChanges;
};

// Safety is a feasibility tier, not a turn/camera-count objective. Among paths
// in the same tier, only actual BattleResult records and equipment changes
// rank wins. Unknown branch probabilities are never treated as uniform odds.
int safetyTier(const Node& n) noexcept {
    return n.fatalOutcomes != 0 ? 2 : (n.coverageComplete ? 0 : 1);
}
auto winningQuality(const Node& n) noexcept {
    return std::tuple{safetyTier(n), n.records, n.equipmentChanges};
}
auto queueQuality(const Node& n) noexcept {
    return std::tuple{safetyTier(n), n.score, n.records, n.equipmentChanges};
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
    int foundTurn = 0;
    std::span<const std::int32_t> prefix;
    Limits limits;
    Clock::time_point started = Clock::now();
    Clock::time_point deadline;
    Result out;
    Node root;
    Node best;
    bool haveWin = false;
    std::vector<Outcome> variantScratch;
    std::vector<Outcome> alternativeScratch;
    std::vector<Node> current;
    std::vector<Node> children;
    std::vector<Node> selected;
    std::vector<Node> roots;
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
    bool candidates(const State& parent, const Command cmd, std::vector<Outcome>& outcomes) {
        outcomes.clear();
        const int packed = BE::PackHeroAction(cmd.action, cmd.target, cmd.bareHands);
        State prepared, child;
        BE::CameraContinuation continuation;
        ++out.stats.battlePreparations;
        if (!BE::PrepareSearchTurn(parent, cmd, &prepared, &continuation)) return false;
        const auto finish = [&](const int choice) {
            child = prepared;
            ++out.stats.transitions;
            out.stats.cameraReplays += continuation.ready;
            return BE::CompleteSearchCamera(&child, continuation, choice);
        };
        if (finish(packed)) {
            outcomes.push_back({child, packed, continuation.records,
                                camera::UsedBranchChoice(), continuation.equipmentChanges});
            return true;
        }
        if (!camera::BranchPending() || BE::HeroCameraActor(packed) >= 0) return false;
        bool complete = true;
        for (int actor = 0; actor < parent.cameraRuntime.presentationActorCount; ++actor) {
            for (bool param5 : {false, true}) {
                const int choice = BE::WithCameraChoice(packed, actor, param5);
                if (!finish(choice)) { complete = false; continue; }
                bool duplicate = false;
                for (const auto& old : outcomes) if (sameState(old.state, child)) { duplicate = true; break; }
                if (!duplicate) outcomes.push_back({child, choice, continuation.records,
                                                    true, continuation.equipmentChanges});
            }
        }
        return complete && !outcomes.empty();
    }
    bool matchesObservation(const Node& n, int throughTurns = -1) {
        if (limits.observedActions.empty() && limits.observedDamages.empty()) return true;
        std::array<int, 350> actions, damages;
        actions.fill(-1);
        damages.fill(-1);
        std::copy(limits.observedActions.begin(), limits.observedActions.end(), actions.begin());
        std::copy(limits.observedDamages.begin(), limits.observedDamages.end(), damages.begin());
        State check;
        BE::InitializeSearchState(&check, initial, limits.initialPosition);
        auto gene = out.gene;
        std::copy_n(n.path.begin(), prefix.size(), gene.begin());
        const int turns = throughTurns < 0 ? int(prefix.size()) + 1 : throughTurns;
        const bool matched = BE::Main(&check.position, turns, gene.data(), check.players,
                                     nullptr, seed, actions.data(), damages.data(), 350, &check.nowState,
                                      -1, false, -1, false);
        return matched;
    }
    bool replay(const Node& candidate, bool keep) {
        ++out.stats.replayChecks;
        std::array<std::int32_t, 350> gene;
        gene.fill(-1);
        std::copy_n(candidate.path.begin(), prefix.size() + candidate.depth, gene.begin());
        State checked{};
        lcg::init(seed, true);
        if (!BE::InitializeSearchState(&checked, initial, limits.initialPosition)) return false;
        BattleResult result{};
        const int turns = int(prefix.size()) + candidate.depth;
        if (turns > 0) {
            BE::Main(&checked.position, turns, gene.data(), checked.players, &result,
                     seed, nullptr, nullptr, -1, &checked.nowState, -1, false, -1, false);
            checked.cameraRuntime = camera::CaptureRuntimeState();
            if (camera::BranchPending()) {
                ++out.stats.replayFailures;
                return false;
            }
        }
        if (!sameState(checked, candidate.state) || result.position != candidate.records
            || result.equipmentChanges != candidate.equipmentChanges) {
            ++out.stats.replayFailures;
            return false;
        }
        if (keep) {
            out.gene = gene;
            out.battle = result;
            out.finalState = checked;
            out.totalTurns = turns;
            out.won = allEnemiesDead(checked) && checked.players[0].hp > 0;
            out.verified = true;
            out.firstUnconfirmedTurn = candidate.firstUnconfirmedTurn;
            out.cameraTurns = candidate.cameraTurns;
            out.riskyTurns = candidate.riskyTurns;
            out.equipmentChanges = candidate.equipmentChanges;
            out.fatalOutcomes = candidate.fatalOutcomes;
            out.cameraCoverageComplete = candidate.coverageComplete;
            out.publishedTurns = candidate.firstUnconfirmedTurn < 0 ? turns
                : std::min(turns, candidate.firstUnconfirmedTurn);
            out.battle.publicTurnLimit = out.publishedTurns;
            out.publicationState = checked;
            if (out.publishedTurns < turns) {
                State boundary;
                BE::InitializeSearchState(&boundary, initial, limits.initialPosition);
                BE::Main(&boundary.position, out.publishedTurns, gene.data(), boundary.players,
                         nullptr, seed, nullptr, nullptr, -1, &boundary.nowState, -1, false, -1, false);
                boundary.cameraRuntime = camera::CaptureRuntimeState();
                out.publicationState = boundary;
            }
        }
        return true;
    }
    void consider(const Node& n) {
        const bool win = allEnemiesDead(n.state);
        if (win) {
            if (n.state.players[0].hp <= 0) return;
            if (haveWin && winningQuality(n) >= winningQuality(best)) return;
            if (!replay(n, true)) return;
            if (!haveWin) out.stats.firstWinMs = elapsed();
            haveWin = true;
            best = n;
            ++out.stats.updates;
        } else if (!haveWin) {
            const double value = estimate(n.state, n.records, 0);
            const int risk = safetyTier(n);
            const int bestRisk = safetyTier(best);
            if (risk < bestRisk || (risk == bestRisk && value < bestPartial)) {
                bestPartial = value;
                best = n;
            }
        }
    }
    void select(int width, bool diverse) {
        order.resize(children.size());
        std::iota(order.begin(), order.end(), std::size_t(0));
        std::sort(order.begin(), order.end(), [&](auto a, auto b) {
            if (queueQuality(children[a]) != queueQuality(children[b]))
                return queueQuality(children[a]) < queueQuality(children[b]);
            return a < b;
        });
        selected.clear();
        auto accept = [&](std::size_t index) {
            Node& c = children[index];
            for (const Node& n : selected) {
                if (n.hash == c.hash && sameState(n.state, c.state)
                    && n.alternatives == c.alternatives
                    && safetyTier(n) <= safetyTier(c)
                    && (!c.coverageComplete || n.coverageComplete)
                    && std::tie(n.records, n.equipmentChanges) <= std::tie(c.records, c.equipmentChanges)
                    && n.firstUnconfirmedTurn == c.firstUnconfirmedTurn) {
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
    bool costBound(const Node& n) const {
        if (!haveWin) return false;
        if (safetyTier(n) != safetyTier(best)) return safetyTier(n) > safetyTier(best);
        // Record/equipment costs are monotone. Turn count is NOT a bound.
        return std::tie(n.records, n.equipmentChanges) >= std::tie(best.records, best.equipmentChanges);
    }

    bool expandCommand(const Node& parent, const Command cmd, const int mode,
                       std::vector<Node>& destination) {
        bool complete = candidates(parent.state, cmd, variantScratch);
        int deaths = 0;
        const auto countDeaths = [&](const std::vector<Outcome>& outcomes) {
            for (const auto& v : outcomes) deaths += v.state.players[0].hp <= 0;
        };
        countDeaths(variantScratch);
        // Unresolved observed roots form the current information set. Check
        // the next actual input against every root, not just the nominal one.
        if (parent.alternatives) for (const auto& possible : *parent.alternatives) {
            if (expired()) { complete = false; break; }
            if (sameState(possible, parent.state)) continue;
            if (possible.players[0].hp <= 0) { ++deaths; continue; }
            if (allEnemiesDead(possible)) continue;
            if (!canSearchCommand(possible, cmd)) { complete = false; continue; }
            complete = candidates(possible, cmd, alternativeScratch) && complete;
            countDeaths(alternativeScratch);
        }
        const bool safe = deaths == 0 && complete && parent.coverageComplete;
        for (const auto& v : variantScratch) {
            if (v.state.players[0].hp <= 0) continue;
            Node child = parent;
            child.state = v.state;
            child.path[prefix.size() + parent.depth] = v.packed;
            ++child.depth;
            child.records += v.records;
            child.equipmentChanges += v.equipmentChanges;
            child.coverageComplete = parent.coverageComplete && complete;
            child.riskyTurns += !safe;
            child.fatalOutcomes += deaths;
            child.cameraTurns += v.cameraChoice;
            const int turn = int(prefix.size()) + parent.depth;
            if ((v.cameraChoice || parent.alternatives) && foundTurn < turn + 1
                && child.firstUnconfirmedTurn < 0) child.firstUnconfirmedTurn = turn;
            // Each camera outcome is a new observation/replanning boundary.
            // Suffix inputs are conditional on observing this child's COMPLETE
            // state. Do not carry sibling hypotheses through a future fixed
            // input sequence; the normal dump cannot issue that suffix.
            child.alternatives.reset();
            child.hash = bucketHash(child.state);
            child.score = estimate(child.state, child.records, mode);
            if (noiseScale != 0) {
                std::uint64_t x = child.hash + noiseSalt;
                x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
                x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
                x ^= x >> 31;
                child.score += noiseScale * (double(x >> 11) * 0x1p-53 - .5);
            }
            destination.push_back(std::move(child));
        }
        return safe && !variantScratch.empty();
    }

    void beam(const Node& start, int width, int mode, bool diverse, bool allRoots = true) {
        ++out.stats.passes;
        current.clear();
        if (allRoots) current = roots;
        else current.push_back(start);
        while (!current.empty() && !expired()) {
            children.clear();
            for (const Node& parent : current) {
                if (!room(parent)) continue;
                if (costBound(parent)) { ++out.stats.boundPruned; continue; }
                ++out.stats.expanded;
                const std::size_t begin = children.size();
                bool haveSafeCommand = false;
                bool sweepComplete = true;
                for (const Command base : profile) {
                    for (const bool bare : {false, true}) {
                        if (expired()) { sweepComplete = false; break; }
                        const Command cmd{base.action, base.target, bare};
                        if (!canSearchCommand(parent.state, cmd)) continue;
                        haveSafeCommand = expandCommand(parent, cmd, mode, children) || haveSafeCommand;
                        if (expired()) { sweepComplete = false; break; }
                    }
                    if (!sweepComplete) break;
                }
                // An unsafe fallback is admissible only after all other inputs
                // were checked. Keep completed SAFE work even on deadline.
                if (haveSafeCommand || !sweepComplete)
                    children.erase(std::remove_if(children.begin() + begin, children.end(),
                        [&](const Node& n) { return n.riskyTurns > parent.riskyTurns; }), children.end());
                for (std::size_t i = begin; i < children.size(); ++i) consider(children[i]);
                if (!sweepComplete) return;
                const std::size_t capacity = std::max(32, width * 4);
                if (children.size() > capacity) {
                    std::nth_element(children.begin(), children.begin() + capacity, children.end(),
                        [](const Node& a, const Node& b) { return queueQuality(a) < queueQuality(b); });
                    children.resize(capacity);
                }
            }
            children.erase(std::remove_if(children.begin(), children.end(),
                [&](const Node& n) { return !room(n) || costBound(n); }), children.end());
            if (children.empty()) return;
            select(width, diverse);
        }
    }

    bool bestPrefix(const int depth, Node& n) {
        n = roots[best.rootIndex];
        std::vector<Node> atCut;
        for (int i = 0; i < depth; ++i) {
            if (expired()) return false;
            const int packed = best.path[prefix.size() + i];
            const Command input{BE::HeroActionId(packed), BE::HeroTargetId(packed), BE::HeroBareHands(packed)};
            atCut.clear();
            expandCommand(n, input, 0, atCut);
            const auto matching = std::find_if(atCut.begin(), atCut.end(),
                [&](const Node& x) { return x.path[prefix.size() + i] == packed; });
            if (matching == atCut.end()) return false;
            n = *matching;
        }
        return true;
    }

    void neighborhood(const int trial, const int width) {
        if (!haveWin || best.depth < 2) return;
        // Rebuild a principal-variation checkpoint, then replan ALL subsequent
        // inputs using exactly the same camera/safety-aware expansion. Sweep
        // cuts through the suffix and continue broad root passes in between.
        const int cut = 1 + trial % (best.depth - 1);
        Node start;
        if (!bestPrefix(cut, start)) return;
        noiseSalt = std::uint64_t(trial + 1) * 0x9e3779b97f4a7c15ULL;
        noiseScale = 1.0 + trial % 4;
        beam(start, width, trial % 3, true, false);
        noiseScale = 0;
    }
public:
    Engine(const Player* p, std::uint64_t s, std::span<const std::int32_t> past, Limits l)
        : initial(p), seed(s), prefix(past), limits(l) {
        foundTurn = std::max(0, l.foundTurn);
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
        if (limits.observedActions.size() > 349 || limits.observedDamages.size() > 349) {
            out.validInput = false;
            out.error = "observation exceeds supplied history";
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
        std::copy(prefix.begin(), prefix.end(), root.path.begin());
        struct PendingPrefix { Node node; int turn; };
        std::vector<PendingPrefix> deferred;
        deferred.push_back({root, 0});
        std::vector<Outcome> prefixOutcomes;
        bool rootCoverageComplete = true;
        while (!deferred.empty()) {
            // A zero budget is the existing DEBUG2 root-only inspection mode.
            if (limits.milliseconds > 0 && expired()) {
                rootCoverageComplete = false;
                break;
            }
            PendingPrefix work = std::move(deferred.back());
            deferred.pop_back();
            Node& parent = work.node;
            const int i = work.turn;
            if (i == int(prefix.size())) {
                if (!matchesObservation(parent)) continue;
                auto duplicate = std::find_if(roots.begin(), roots.end(), [&](const Node& old) {
                    return sameState(old.state, parent.state);
                });
                if (duplicate != roots.end()) {
                    if (winningQuality(parent) < winningQuality(*duplicate)) *duplicate = std::move(parent);
                    continue;
                }
                // Limit only the already-observation-matched SEARCH roots.
                // This is never a seed rejection; incomplete coverage is explicit.
                if (roots.size() >= static_cast<std::size_t>(limits.maxBeamWidth)) {
                    rootCoverageComplete = false;
                    out.capacityLimited = true;
                    break;
                }
                roots.push_back(std::move(parent));
                continue;
            }
            if (parent.state.position >= 6400 || parent.records > 990) {
                out.capacityLimited = true;
                rootCoverageComplete = false;
                continue;
            }
            if (allEnemiesDead(parent.state) || parent.state.players[0].hp <= 0) continue;
            const bool complete = candidates(parent.state, unpack(prefix[i]), prefixOutcomes);
            rootCoverageComplete = rootCoverageComplete && complete;
            for (auto it = prefixOutcomes.rbegin(); it != prefixOutcomes.rend(); ++it) {
                const auto& v = *it;
                Node n = parent;
                n.state = v.state;
                n.records += v.records;
                n.equipmentChanges += v.equipmentChanges;
                n.path[i] = v.packed;
                if (v.cameraChoice) {
                    ++n.cameraTurns;
                    if (foundTurn < i + 1 && n.firstUnconfirmedTurn < 0) n.firstUnconfirmedTurn = i;
                }
                // Prune against the measured history before adding more camera
                // decisions. No future Cartesian product is ever constructed.
                if (matchesObservation(n, i + 1)) deferred.push_back({std::move(n), i + 1});
            }
        }
        for (auto& n : roots) n.coverageComplete = n.coverageComplete && rootCoverageComplete;
        if (roots.empty()) {
            out.validInput = !rootCoverageComplete;
            out.cameraCoverageComplete = rootCoverageComplete;
            out.error = rootCoverageComplete ? "no camera candidate matches the measured history/state"
                : "root enumeration incomplete; no measured-state candidate verified within the budget";
            out.stats.elapsedMs = elapsed();
            return std::move(out);
        }
        std::shared_ptr<std::vector<State>> possibilities;
        if (roots.size() > 1) {
            possibilities = std::make_shared<std::vector<State>>();
            for (const auto& n : roots) possibilities->push_back(n.state);
        }
        for (auto& n : roots) {
            n.rootIndex = int(&n - roots.data());
            n.alternatives = possibilities;
            // Prefix is observed, but only subsequent observations disambiguate
            // camera state. Surviving roots remain separate possibilities.
        }
        if (limits.debugCameraCandidates) {
            for (std::size_t index = 0; index < roots.size(); ++index) {
                const auto& n = roots[index];
                std::ostringstream row;
                row << "DEBUG_CAMERA_CANDIDATE index=" << index << " rngPosition=" << n.state.position
                    << " hp=" << n.state.players[0].hp << " row4=";
                for (int actor = 0; actor < n.state.cameraRuntime.presentationActorCount; ++actor)
                    row << (n.state.cameraRuntime.rosterField4Known[actor]
                        ? (n.state.cameraRuntime.rosterField4Nonzero[actor] ? '1' : '0') : '?');
                row << " packed";
                for (std::size_t i = 0; i < prefix.size(); ++i) row << ' ' << n.path[i];
                out.cameraDebugRows.push_back(row.str());
            }
        }
        root = roots.front();
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
        const auto anyRootCanContinue = [&] {
            return std::any_of(roots.begin(), roots.end(), [&](const Node& n) { return room(n); });
        };
        if (rootCoverageComplete && !expired())
            for (const auto& n : roots) if (allEnemiesDead(n.state)) consider(n);
        if (rootCoverageComplete && anyRootCanContinue() && !expired()) {
            current.reserve(limits.maxBeamWidth);
            selected.reserve(limits.maxBeamWidth);
            children.reserve(std::max(32, limits.maxBeamWidth * 4));
            int width = std::min(6, limits.maxBeamWidth);
            int round = 0;
            int repair = 0;
            while (!expired()) {
                // Start with a small, deep search to secure an incumbent, then
                // widen and alternate value estimates. Later suffix repair keeps
                // useful discovered setup but never locks it across root passes.
                Node start = root;

                if (limits.variant == 0 && round >= 6) {
                    noiseSalt = std::uint64_t(round + 1) * 0x9e3779b97f4a7c15ULL;
                    noiseScale = 2.0 + (round % 5);
                }
                if (limits.variant == 0 && haveWin && best.depth >= 2 && (round & 1))
                    neighborhood(repair++, width);
                else
                    beam(start, width, round % 3, limits.variant != 1);
                noiseScale = 0;
                ++round;
                width = std::min(limits.maxBeamWidth, width * 2);
                if (!anyRootCanContinue()) break;
            }
        }
        // Fresh original-world replay is the source of every output field.
        if (!replay(best, true)) {
            // The last verified incumbent is deliberately retained on mismatch.
            out.error = "candidate mismatch; retained the last verified incumbent";
        }
        out.stats.elapsedMs = elapsed();
        if (!rootCoverageComplete)
            out.error = "root camera coverage incomplete; acquire more observations before selecting another input";
        return std::move(out);
    }
};
} // namespace

std::span<const Command> commandProfile() noexcept { return profile; }
bool canSearchCommand(const State& s, Command c) noexcept {
    c.action = BE::HeroActionId(c.action);
    if (c.action == BE::FLEE_ALLY) return false; // Known unfaithful pre-action skip, NOT a legal-set edit.
    if (c.bareHands && (c.action == BE::MULTITHRUST || c.action == BE::THUNDER_THRUST
        || c.action == BE::BEAST_THRUST || c.action == BE::VITAL_POINT_THRUST
        || c.action == BE::MERCURIAL_THRUST)) return false;
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
