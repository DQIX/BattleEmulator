#include "GanasadaiSearch.h"
#include "lcg.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>
#include <vector>

namespace {
using B = BattleEmulator;
using Clock = std::chrono::steady_clock;
constexpr uint32_t NoLink = UINT32_MAX;

// Current actor's candidate set, plus ATTACK as a terminal-only finisher.
// Mirror remains available to the comparison variant; the engine is untouched.
constexpr int Actions[] = {
    B::MULTITHRUST, B::PSYCHE_UP_ALLY, B::DOUBLE_UP,
    B::SPECIAL_MEDICINE, B::MORE_HEAL, B::MIDHEAL, B::FULLHEAL,
    B::MAGIC_WATER, B::BUFF, B::FLEE_ALLY, B::DEFENCE,
    B::DEFENDING_CHAMPION, B::GOSPEL_SONG, B::INSULATE,
    B::ATTACK_ALLY, B::MAGIC_MIRROR
};

auto fields(const Player& p) {
    return std::tie(p.hp, p.maxHp, p.atk, p.defaultATK, p.def, p.defaultDEF,
        p.speed, p.HealPower, p.mp, p.maxMp, p.specialCharge,
        p.dirtySpecialCharge, p.specialChargeTurn, p.paralysis,
        p.paralysisLevel, p.paralysisTurns, p.SpecialMedicineCount, p.defence,
        p.sleeping, p.sleepingTurn, p.BuffLevel, p.BuffTurns,
        p.hasMagicMirror, p.MagicMirrorTurn, p.AtkBuffLevel, p.AtkBuffTurn,
        p.TensionLevel, p.rage, p.SageElixirCount, p.ElfinElixirCount,
        p.MagicWaterCount, p.InsulateLevel, p.InsulateTurns, p.inactive,
        p.rageTurns);
}

bool sameFuture(const GanasadaiState& a, const GanasadaiState& b) {
    return a.rngPosition == b.rngPosition && a.nowState == b.nowState &&
        a.resultPosition == b.resultPosition &&
        fields(a.players[0]) == fields(b.players[0]) &&
        fields(a.players[1]) == fields(b.players[1]);
}

int turnOf(const GanasadaiState& s) { return int((s.nowState >> 12) & 0xfffff); }
bool bare(const Player& p) { return p.defaultATK == B::GANASADAI1_BARE_HANDS_ATK; }
bool canChange(const Player& p) { return !p.paralysis && !p.sleeping && !p.inactive; }

uint64_t mix(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
uint64_t fingerprint(const GanasadaiState& s) {
    const auto& p = s.players[0];
    uint64_t h = mix(s.nowState) ^ mix(uint32_t(s.rngPosition));
    h ^= mix(uint64_t(uint32_t(p.hp)) << 32 | uint32_t(s.players[1].hp));
    h ^= mix(uint64_t(uint32_t(p.defaultATK)) << 32 | uint32_t(p.mp));
    h ^= mix(uint64_t(uint32_t(p.TensionLevel)) << 32 | uint32_t(p.AtkBuffTurn));
    h ^= mix(uint64_t(uint32_t(p.BuffTurns)) << 32 | uint32_t(p.def));
    // Fingerprints only locate candidates. Every merge checks ALL fields.
    return h;
}

int equipmentCount(const BattleResult& r) {
    int n = 0;
    for (int i = 0; i < r.position; ++i)
        if (!r.isEnemy[i] && (r.actions[i] & B::ACTION_EQUIPMENT_CHANGED)) ++n;
    return n;
}

bool legal(const Player& p, int action) {
    const int id = action & B::ACTION_ID_MASK;
    const bool wantBare = (action & B::ACTION_BARE_HANDS) != 0;
    if (wantBare != bare(p) && !canChange(p)) return false;
    if (id == B::FLEE_ALLY && !canChange(p)) return false;
    // Check the equipment requested at THIS turn's menu, not the old equipment.
    if (id == B::MULTITHRUST && wantBare) return false;
    switch (id) {
    case B::MULTITHRUST: case B::MIDHEAL: case B::MAGIC_MIRROR:
    case B::INSULATE: return p.mp >= 4;
    case B::BUFF: case B::DEFENDING_CHAMPION: return p.mp >= 3;
    case B::MORE_HEAL: return p.mp >= 8;
    case B::FULLHEAL: return p.mp >= 24;
    case B::SPECIAL_MEDICINE: return p.SpecialMedicineCount > 0;
    case B::MAGIC_WATER: return p.MagicWaterCount > 0;
    case B::GOSPEL_SONG:
        return p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn > 0;
    case B::PSYCHE_UP_ALLY: case B::DOUBLE_UP: case B::DEFENCE:
    case B::FLEE_ALLY: case B::ATTACK_ALLY: return true;
    default: return false;
    }
}

struct Config {
    const char* name;
    double tension, attackBuff, defence;
    bool quotas, mirror;
    bool preview = false;
    double positionWeight = 0;
    bool diversify = false;
};
constexpr Config Configs[] = {
    {"balanced",  .96, 100, 35, true, false},
    {"burst",    1.08, 130, 20, true, false},
    {"setup",     .85,  85, 50, true, false},
    {"damage",    .58,  65, 25, false, false},
    {"mirror",    .96, 100, 35, true, true},
    {"lookahead", .96, 100, 35, true, false, true},
    {"diverse",   .96, 100,  0, true, false, false, 20, true},
    {"position", 1.08, 130,  0, true, false, false, 65, true}
};
constexpr const char* Names[] = {
    "equipment-portfolio", "balanced-equipment", "burst-equipment",
    "setup-equipment", "damage-global-beam", "mirror-comparison",
    "fixed-equipment-reference", "beam-neighborhood-portfolio",
    "wide-setup-beam", "exact-tactical-lookahead", "diverse-position-beam",
    "position-priority-beam", "burst-schedule-and-beam"
};

double value(const GanasadaiState& s, const Config& c, bool equipment) {
    // HP and MP deliberately do not contribute to beam priority. Alive paths
    // may spend either resource and recover before a terminal victory.
    const auto& p = s.players[0];
    const auto& e = s.players[1];
    constexpr double tension[] = {1, 1.5, 2.5, 4, 6};
    constexpr double attackBuff[] = {.5, .75, 1, 1.25, 1.5};
    double attack = p.atk;
    if (equipment)
        attack = std::max(attack, B::GANASADAI1_EQUIPPED_ATK *
            attackBuff[std::clamp(p.AtkBuffLevel + 2, 0, 4)]);
    const int t = std::clamp(p.TensionLevel, 0, 4);
    const double base = std::max(0.0, attack * .25 - e.def * .125);
    const double ordinary = base * 3.5;
    const double burst = (base * tension[t] + t * 5) * 3.5;
    const double credit = std::max(0.0, std::min(double(e.hp), burst) -
        std::min(double(e.hp), ordinary));
    double score = -e.hp + c.tension * credit;
    score += c.attackBuff * std::max(0, p.AtkBuffLevel) *
        std::clamp(p.AtkBuffTurn + 1, 1, 4) / 4.0;
    score += c.defence * p.BuffLevel * std::clamp(p.BuffTurns + 1, 1, 4) / 4.0;
    if (p.specialCharge && !p.dirtySpecialCharge && p.specialChargeTurn > 0) score += 45;
    if (t == 4) score += 65;
    score -= c.positionWeight * s.resultPosition;
    return score;
}

int bucket(const GanasadaiState& s) {
    const auto& p = s.players[0];
    // Diversity in attack preparation and equipment, not terminal HP/MP tests.
    return (((std::clamp(p.TensionLevel, 0, 4) * 3 +
        std::clamp(p.AtkBuffLevel, 0, 2)) * 2 +
        bare(p)) * 2 + (p.BuffLevel > 0));
}

struct Link { uint32_t parent; int32_t action; };
struct Node {
    GanasadaiState state{};
    double score = 0;
    uint64_t hash = 0;
    uint32_t link = NoLink;
    int32_t action = 0; // Pending edge before promotion to the next layer.
};

class Search {
    const Player* initial;
    uint64_t seed;
    Clock::time_point started, deadline;
    int maxTurns;
    GanasadaiSearchResult out;
    std::array<int32_t, 350> scratchActions{};
    BattleResult scratchLog{};
    std::vector<Link> paths;

    bool expired() const { return Clock::now() >= deadline; }
    bool won(const GanasadaiState& s) const {
        return s.players[1].hp == 0 && s.players[0].hp >= GanasadaiSearch::RequiredVictoryHp &&
            s.players[0].mp >= GanasadaiSearch::RequiredVictoryMp;
    }
    bool better(int position, int changes) const {
        return !out.victory || std::tie(position, changes) <
            std::tie(out.finalState.resultPosition, out.finalState.equipmentChanges);
    }
    bool canImprove(const GanasadaiState& s) const {
        return !out.victory || s.resultPosition + 1 < out.finalState.resultPosition ||
            (s.resultPosition + 1 == out.finalState.resultPosition &&
             s.equipmentChanges < out.finalState.equipmentChanges);
    }

    bool step(const GanasadaiState& before, int action, GanasadaiState& after) {
        const int turn = turnOf(before);
        scratchActions[turn] = action;
        after = before;
        scratchLog.clear();
        B::Main(&after.rngPosition, 1, scratchActions.data(), after.players,
            &scratchLog, seed, nullptr, nullptr, -1, &after.nowState);
        after.resultPosition += scratchLog.position;
        after.equipmentChanges += equipmentCount(scratchLog);
        ++out.expanded;
        if (after.equipmentChanges > before.equipmentChanges) ++out.equipmentBranches;
        if (after.players[0].hp <= 0 || after.players[0].mp < 0) return false;
        // ATTACK may be evaluated, but a non-terminal attack NEVER enters a path.
        if ((action & B::ACTION_ID_MASK) == B::ATTACK_ALLY && after.players[1].hp != 0)
            return false;
        return true;
    }

    bool replay(const std::array<int32_t, 350>& actions, int length,
                const GanasadaiState& expected) {
        // Fresh original world + the unmodified complete prefix + searched suffix.
        lcg::init(seed, true);
        GanasadaiState actual;
        actual.players[0] = initial[0]; actual.players[1] = initial[1];
        BattleResult log;
        B::Main(&actual.rngPosition, length, actions.data(), actual.players,
            &log, seed, nullptr, nullptr, -1, &actual.nowState);
        actual.resultPosition = log.position;
        actual.equipmentChanges = equipmentCount(log);
        if (!GanasadaiSearch::SameState(actual, expected) || !won(actual)) {
            ++out.rejectedReplay;
            return false;
        }
        // Both MP admission and the final reserve are required, not a score.
        for (int i = 0; i < log.position; ++i) {
            if (log.amp[i] < 0) { ++out.rejectedReplay; return false; }
            if (log.turns[i] >= out.prefixLength && !log.isEnemy[i] &&
                (log.actions[i] & B::ACTION_ID_MASK) == B::ATTACK_ALLY &&
                log.turns[i] != length - 1) { ++out.rejectedReplay; return false; }
        }
        if (!better(actual.resultPosition, actual.equipmentChanges)) return false;
        out.victory = true;
        out.replayVerified = true;
        out.actions = actions;
        out.length = length;
        out.finalState = actual;
        out.minimumFinalMp = GanasadaiSearch::RequiredVictoryMp;
        out.replay = log;
        ++out.updates;
        return true;
    }

    void admit(const Node& n) {
        if (!won(n.state) || !better(n.state.resultPosition, n.state.equipmentChanges)) return;
        const int length = turnOf(n.state);
        std::array<int32_t, 350> actions;
        actions.fill(-1);
        std::copy_n(out.actions.begin(), out.prefixLength, actions.begin());
        int at = length - 1;
        actions[at--] = n.action;
        for (uint32_t link = n.link; link != NoLink; link = paths[link].parent)
            actions[at--] = paths[link].action;
        if (at != out.prefixLength - 1) { ++out.rejectedReplay; return; }
        replay(actions, length, n.state);
    }

    bool candidate(const Player& p, int id, const Config& c) const {
        if (!canChange(p)) return id == B::DEFENCE; // No carried-rest equipment/FLEE exploit.
        switch (id) {
        case B::PSYCHE_UP_ALLY: return p.TensionLevel < 4;
        case B::DOUBLE_UP: return p.AtkBuffLevel < 2 || p.AtkBuffTurn <= 1;
        case B::BUFF: return p.BuffLevel < 2 || p.BuffTurns <= 1;
        case B::MAGIC_MIRROR: return c.mirror && (!p.hasMagicMirror || p.MagicMirrorTurn <= 1);
        case B::INSULATE: return p.InsulateLevel < 2 || p.InsulateTurns <= 1;
        default: return true;
        }
    }

    bool beam(int width, const Config& config, bool equipment, uint64_t salt = 0) {
        paths.clear();
        std::vector<Node> beam(1), next;
        beam[0].state = out.root;
        next.reserve(size_t(width) * 32);
        size_t tableSize = 1;
        while (tableSize < size_t(width) * 64) tableSize *= 2;
        std::vector<uint32_t> slots(tableSize, NoLink);
        std::vector<uint32_t> order;
        std::vector<uint8_t> selected;
        for (int depth = out.prefixLength; depth < maxTurns && !beam.empty(); ++depth) {
            if (expired()) return false;
            next.clear();
            std::fill(slots.begin(), slots.end(), NoLink);
            for (const Node& parent : beam) {
                if (!canImprove(parent.state)) continue;
                const auto& p = parent.state.players[0];
                const int equipmentVariants = equipment && canChange(p) ? 2 : 1;
                for (int id : Actions) {
                    if (!candidate(p, id, config)) continue;
                    for (int eq = 0; eq < equipmentVariants; ++eq) {
                        const bool requestedBare = eq == 0 ? bare(p) : !bare(p);
                        const int action = id | (requestedBare ? B::ACTION_BARE_HANDS : 0);
                        if (!legal(p, action)) continue;
                        if ((out.expanded & 127) == 0 && expired()) return false;
                        Node child;
                        if (!step(parent.state, action, child.state)) continue;
                        child.link = parent.link;
                        child.action = action;
                        if (child.state.players[1].hp == 0) { admit(child); continue; }
                        if (!canImprove(child.state)) continue;
                        child.hash = fingerprint(child.state);
                        size_t slot = child.hash & (tableSize - 1);
                        bool duplicate = false;
                        while (slots[slot] != NoLink) {
                            auto& old = next[slots[slot]];
                            if (old.hash == child.hash && sameFuture(old.state, child.state)) {
                                ++out.duplicates;
                                if (child.state.equipmentChanges < old.state.equipmentChanges) {
                                    old.state.equipmentChanges = child.state.equipmentChanges;
                                    old.link = child.link; old.action = child.action;
                                }
                                duplicate = true;
                                break;
                            }
                            slot = (slot + 1) & (tableSize - 1);
                        }
                        if (duplicate) continue;
                        child.score = value(child.state, config, equipment);
                        if (config.preview && depth + 1 < maxTurns) {
                            double future = child.score;
                            const int previews[] = {B::MULTITHRUST, B::PSYCHE_UP_ALLY,
                                B::PSYCHE_UP_ALLY | B::ACTION_BARE_HANDS};
                            for (int preview : previews) {
                                if (!candidate(child.state.players[0], preview & B::ACTION_ID_MASK, config) ||
                                    !legal(child.state.players[0], preview)) continue;
                                Node grandchild;
                                if (!step(child.state, preview, grandchild.state)) continue;
                                if (grandchild.state.players[1].hp == 0) {
                                    paths.push_back({child.link, child.action});
                                    grandchild.link = uint32_t(paths.size() - 1);
                                    grandchild.action = preview;
                                    admit(grandchild);
                                    paths.pop_back();
                                } else {
                                    future = std::max(future, value(grandchild.state, config,
                                        equipment) - 100);
                                }
                            }
                            child.score = child.score * .4 + future * .6;
                        }
                        slots[slot] = uint32_t(next.size());
                        next.push_back(child);
                    }
                }
            }
            if (next.empty()) break;
            order.resize(next.size());
            std::iota(order.begin(), order.end(), 0);
            auto rank = [&](uint32_t a, uint32_t b) {
                if (next[a].score != next[b].score) return next[a].score > next[b].score;
                if (config.diversify) return mix(next[a].hash ^ salt) < mix(next[b].hash ^ salt);
                if (next[a].state.equipmentChanges != next[b].state.equipmentChanges)
                    return next[a].state.equipmentChanges < next[b].state.equipmentChanges;
                return a < b;
            };
            // Select only the needed fronts. Sorting the whole generated layer
            // used to overshoot the deadline on wide passes.
            const size_t front = std::min(size_t(width), order.size());
            if (front < order.size())
                std::nth_element(order.begin(), order.begin() + front, order.end(), rank);
            std::sort(order.begin(), order.begin() + front, rank);
            if (expired()) return false;
            beam.clear();
            beam.reserve(width);
            selected.assign(next.size(), 0);
            auto select = [&](uint32_t i) {
                Node n = next[i];
                paths.push_back({n.link, n.action});
                n.link = uint32_t(paths.size() - 1); n.action = 0;
                beam.push_back(n); selected[i] = 1;
            };
            if (config.quotas) {
                std::array<std::vector<uint32_t>, 60> buckets;
                const int quota = std::max(1, width / 180);
                for (uint32_t i : order) {
                    buckets[bucket(next[i].state)].push_back(i);
                }
                std::vector<uint32_t> diverse;
                for (auto& group : buckets) {
                    const size_t count = std::min(size_t(quota), group.size());
                    if (count < group.size())
                        std::nth_element(group.begin(), group.begin() + count, group.end(), rank);
                    diverse.insert(diverse.end(), group.begin(), group.begin() + count);
                }
                std::sort(diverse.begin(), diverse.end(), rank);
                for (uint32_t i : diverse) {
                    if (int(beam.size()) >= width / 3) break;
                    select(i);
                }
                if (expired()) return false;
            }
            for (size_t at = 0; at < front; ++at) {
                if (int(beam.size()) >= width) break;
                const uint32_t i = order[at];
                if (!selected[i]) select(i);
            }
        }
        out.completedWidth = std::max(out.completedWidth, width);
        return true;
    }

    // A bounded tactical portfolio, not a victory sequence: enumerate order,
    // recovery, equipment and RNG alternatives for a chosen tension target.
    // The general beam also explores repeated MULTITHRUST and other tactics.
    // The schedule bound concerns the chosen plan's remaining commands only;
    // it never uses the hero's HP or MP to rank/prune a live intermediate state.
    void burstSchedules() {
        std::array<int32_t, 350> sequence = out.actions;
        const uint64_t before = out.expanded;
        const auto visit = [&](auto&& self, const GanasadaiState& state,
                               int targetTension, int limit) -> void {
            if ((out.expanded & 127) == 0 && expired()) return;
            const int depth = turnOf(state);
            if (depth >= limit || !canImprove(state)) return;
            const auto& p = state.players[0];
            for (int id : {B::MULTITHRUST, B::ATTACK_ALLY}) {
                for (int eq = 0; eq < (id == B::ATTACK_ALLY ? 2 : 1); ++eq) {
                    const int action = id | (eq ? B::ACTION_BARE_HANDS : 0);
                    if (!legal(p, action)) continue;
                    GanasadaiState child;
                    if (!step(state, action, child)) continue;
                    if (won(child) && better(child.resultPosition, child.equipmentChanges)) {
                        sequence[depth] = action;
                        std::fill(sequence.begin() + depth + 1, sequence.end(), -1);
                        replay(sequence, depth + 1, child);
                    }
                }
            }
            if (depth + 1 >= limit) return;
            const int preparations = std::max(0, targetTension - p.TensionLevel) +
                (p.AtkBuffLevel < 2 ? 1 : 0);
            if (depth + preparations + 1 > limit) return;
            for (int id : Actions) {
                if (id == B::MULTITHRUST || id == B::ATTACK_ALLY || id == B::MAGIC_MIRROR) continue;
                if (id == B::PSYCHE_UP_ALLY && p.TensionLevel >= targetTension) continue;
                if (!candidate(p, id, Configs[0])) continue;
                for (int eq = 0; eq < (canChange(p) ? 2 : 1); ++eq) {
                    const int action = id | ((eq == 0 ? bare(p) : !bare(p)) ? B::ACTION_BARE_HANDS : 0);
                    if (!legal(p, action)) continue;
                    GanasadaiState child;
                    if (!step(state, action, child)) continue;
                    const int left = std::max(0, targetTension - child.players[0].TensionLevel) +
                        (child.players[0].AtkBuffLevel < 2 ? 1 : 0);
                    if (depth + 1 + left + 1 > limit) continue;
                    sequence[depth] = action;
                    self(self, child, targetTension, limit);
                    if (expired()) return;
                }
            }
        };
        for (int extra = 0; extra < 4 && !expired(); ++extra) {
            for (int target : {4, 3, 2}) {
                const auto& p = out.root.players[0];
                const int limit = out.prefixLength + std::max(0, target - p.TensionLevel) +
                    (p.AtkBuffLevel < 2 ? 1 : 0) + 1 + extra;
                if (limit <= maxTurns) visit(visit, out.root, target, limit);
                if (expired()) break;
            }
        }
        out.tacticalExpanded += out.expanded - before;
    }

    // Optimize a solution found DURING THIS request. There are no stored seed,
    // prefix, or action-sequence answers. Every changed suffix is simulated.
    void repair() {
        while (out.victory && !expired()) {
            const auto base = out.actions;
            const int length = out.length;
            std::vector<GanasadaiState> states(length + 1);
            states[out.prefixLength] = out.root;
            bool usable = true;
            for (int i = out.prefixLength; i < length; ++i)
                if (!step(states[i], base[i], states[i + 1])) { usable = false; break; }
            if (!usable) return;
            auto attempt = [&](std::array<int32_t, 350> sequence, int n) {
                if (expired()) return false;
                int start = out.prefixLength;
                while (start < n && start < length && sequence[start] == base[start]) ++start;
                GanasadaiState state = states[start];
                for (int i = start; i < n; ++i) {
                    if (!canImprove(state) || !legal(state.players[0], sequence[i])) return false;
                    GanasadaiState after;
                    if (!step(state, sequence[i], after)) return false;
                    state = after;
                    if (state.players[1].hp == 0) {
                        if (!won(state) || !better(state.resultPosition, state.equipmentChanges)) return false;
                        std::fill(sequence.begin() + i + 1, sequence.end(), -1);
                        return replay(sequence, i + 1, state);
                    }
                }
                return false;
            };
            auto replacement = [&](const std::array<int32_t, 350>& sequence, int n) {
                if (attempt(sequence, n)) return true;
                for (int at = out.prefixLength; at < n && !expired(); ++at) {
                    for (int id : Actions) {
                        if (id == B::MAGIC_MIRROR) continue;
                        for (int eq = 0; eq < 2; ++eq) {
                            if (id == B::MULTITHRUST && eq != 0) continue;
                            if (id == B::ATTACK_ALLY && at != n - 1) continue;
                            auto trial = sequence;
                            trial[at] = id | (eq ? B::ACTION_BARE_HANDS : 0);
                            if (trial[at] != sequence[at] && attempt(trial, n)) return true;
                        }
                    }
                }
                return false;
            };
            bool improved = false;
            // Delete one/two commands, with one simultaneous replacement. Fixed
            // prefix commands are never edited or included in the neighborhood.
            for (int a = out.prefixLength; a < length && !improved && !expired(); ++a) {
                for (int b = a; b < length && !improved && !expired(); ++b) {
                    std::array<int32_t, 350> trial;
                    trial.fill(-1);
                    int n = 0;
                    for (int i = 0; i < length; ++i)
                        if (i != a && (b == a || i != b)) trial[n++] = base[i];
                    improved = replacement(trial, n);
                }
            }
            if (improved) continue;
            // Two substitutions also reach alternate RNG paths without changing
            // the number of turns, and can reduce the exact equipment count.
            for (int at = out.prefixLength; at < length && !improved && !expired(); ++at) {
                for (int id : Actions) {
                    if (id == B::MAGIC_MIRROR) continue;
                    for (int eq = 0; eq < 2 && !improved && !expired(); ++eq) {
                        if (id == B::MULTITHRUST && eq != 0) continue;
                        if (id == B::ATTACK_ALLY && at != length - 1) continue;
                        auto trial = base;
                        trial[at] = id | (eq ? B::ACTION_BARE_HANDS : 0);
                        if (trial[at] != base[at]) improved = replacement(trial, length);
                    }
                    if (improved || expired()) break;
                }
            }
            if (!improved) break;
        }
    }

public:
    Search(const Player p[2], uint64_t s, int budgetMs, int maxTotalTurns)
        : initial(p), seed(s), started(Clock::now()),
          deadline(started + std::chrono::milliseconds(std::max(1, budgetMs - 5))),
          maxTurns(std::clamp(maxTotalTurns, 1, 349)) {
        scratchActions.fill(-1);
        out.actions.fill(-1);
        out.minimumFinalMp = GanasadaiSearch::RequiredVictoryMp;
    }

    GanasadaiSearchResult run(const int32_t prefix[350], int length, int variant) {
        auto finish = [&]() {
            out.elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            return out;
        };
        if (!prefix || length < 0 || length >= 350 || prefix[length] != -1 ||
            initial[0].mp < 0 || initial[0].mp > initial[0].maxMp || seed == 0 ||
            variant < 0 || variant >= GanasadaiSearch::VariantCount()) {
            out.error = "invalid prefix, initial MP, seed, or search variant";
            return finish();
        }
        for (int i = 0; i < length; ++i) {
            if (prefix[i] <= 0) { out.error = "invalid fixed prefix action"; return finish(); }
            out.actions[i] = prefix[i];
        }
        out.prefixLength = out.length = length;
        out.root.players[0] = initial[0]; out.root.players[1] = initial[1];
        lcg::init(seed, true);
        // Validate only the explicit prefix/equipment/MP constraints needed by
        // this search. Invalid fixed commands are rejected, never rewritten.
        GanasadaiState prefixState = out.root;
        for (int i = 0; i < length; ++i) {
            const auto& p = prefixState.players[0];
            const int id = prefix[i] & B::ACTION_ID_MASK;
            const bool requestedBare = (prefix[i] & B::ACTION_BARE_HANDS) != 0;
            if (p.hp <= 0 || prefixState.players[1].hp == 0) {
                out.error = "fixed prefix extends beyond battle end"; return finish();
            }
            if ((!canChange(p) && (requestedBare != bare(p) || id == B::FLEE_ALLY)) ||
                (id == B::MULTITHRUST && requestedBare)) {
                out.error = "fixed prefix violates equipment or carried-rest constraints";
                return finish();
            }
            scratchLog.clear();
            B::Main(&prefixState.rngPosition, 1, out.actions.data(), prefixState.players,
                &scratchLog, seed, nullptr, nullptr, -1, &prefixState.nowState);
            prefixState.resultPosition += scratchLog.position;
            prefixState.equipmentChanges += equipmentCount(scratchLog);
            if (prefixState.players[0].mp < 0) {
                out.error = "fixed prefix exhausts MP"; return finish();
            }
            if (id == B::ATTACK_ALLY && (i != length - 1 || prefixState.players[1].hp != 0)) {
                out.error = "fixed prefix contains a non-final ATTACK_ALLY"; return finish();
            }
        }
        lcg::init(seed, true);
        B::Main(&out.root.rngPosition, length, out.actions.data(), out.root.players,
            &out.replay, seed, nullptr, nullptr, -1, &out.root.nowState);
        out.root.resultPosition = out.replay.position;
        out.root.equipmentChanges = equipmentCount(out.replay);
        out.finalState = out.root;
        if (!GanasadaiSearch::SameState(prefixState, out.root)) {
            out.error = "fixed prefix incremental/full replay mismatch"; return finish();
        }
        // Never replace an impossible fixed prefix with an empty one.
        if (turnOf(out.root) != length || out.root.players[0].hp <= 0) {
            out.error = "fixed prefix extends beyond battle end or defeats the ally";
            return finish();
        }
        for (int i = 0; i < out.replay.position; ++i)
            if (out.replay.amp[i] < 0) { out.error = "fixed prefix exhausts MP"; return finish(); }
        out.inputValid = true;
        if (out.root.players[1].hp == 0) {
            replay(out.actions, length, out.root);
            return finish();
        }
        maxTurns = std::min(349, std::max(maxTurns, length + 1));
        const bool equipment = variant != 6;
        const auto finalDeadline = deadline;
        if (variant == 12) {
            deadline = started + (finalDeadline - started) * 2 / 5;
            burstSchedules();
            deadline = finalDeadline;
        }
        if (variant == 7)
            deadline = started + (finalDeadline - started) * 3 / 4;
        for (int pass = 0; !expired(); ++pass) {
            const bool portfolio = variant == 0 || variant == 7;
            const int ci = variant == 12 ? (pass % 2 ? 6 : 1) : portfolio ? pass % 3 : (variant == 6 ? 0 :
                variant == 8 ? 2 : variant == 9 ? 5 : variant >= 10 ? variant - 4 : variant - 1);
            const int exponent = portfolio ? pass / 3 : pass;
            const int width = variant == 8 ? (256 << std::min(exponent * 2, 7)) :
                (64 << std::min(exponent, 9));
            if (!beam(width, Configs[ci], equipment, mix(pass + 1))) break;
        }
        deadline = finalDeadline;
        if (variant == 7) repair();
        return finish();
    }
};
}

bool GanasadaiSearch::SameState(const GanasadaiState& a, const GanasadaiState& b) {
    return sameFuture(a, b) && a.equipmentChanges == b.equipmentChanges;
}
const char* GanasadaiSearch::VariantName(int variant) {
    return variant >= 0 && variant < VariantCount() ? Names[variant] : "invalid";
}
int GanasadaiSearch::VariantCount() { return int(sizeof(Names) / sizeof(Names[0])); }

GanasadaiSearchResult GanasadaiSearch::Run(const Player initial[2], uint64_t seed,
    const int32_t prefix[350], int prefixLength, int budgetMs, int variant, int maxTotalTurns) {
    return Search(initial, seed, budgetMs, maxTotalTurns).run(prefix, prefixLength, variant);
}