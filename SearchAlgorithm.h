#ifndef YO2_BE_SEARCH_ALGORITHM_H
#define YO2_BE_SEARCH_ALGORITHM_H

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "BattleEmulator.h"
#include "lcg.h"

namespace SearchAlgorithm {
    struct State {
        Player players[2]{};
        int position = 1;
        std::uint64_t nowState = 0;
    };

    struct Result {
        std::array<int32_t, 350> actions{};
        int actionCount = 0;
        State finalState{};
        std::uint64_t expanded = 0;
        double elapsedMs = 0.0;
        bool timedOut = false;
    };

    namespace detail {
        constexpr std::uint32_t kNoTrace = std::numeric_limits<std::uint32_t>::max();
        constexpr std::size_t kBeamWidth = 55000;
        constexpr int kRngSafetyLimit = 4940;

        struct Trace {
            std::uint32_t parent = kNoTrace;
            int32_t action = -1;
        };

        struct BeamNode {
            State state{};
            std::uint32_t trace = kNoTrace;
        };

        struct Candidate {
            State state{};
            std::uint32_t parentTrace = kNoTrace;
            int32_t action = -1;
            std::int64_t score = std::numeric_limits<std::int64_t>::min();
        };

        [[nodiscard]] inline std::uint64_t Mix64(std::uint64_t x) noexcept {
            x += 0x9e3779b97f4a7c15ULL;
            x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
            x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
            return x ^ (x >> 31);
        }

        inline void HashAdd(std::uint64_t &hash, const std::uint64_t value) noexcept {
            hash ^= Mix64(value + hash + 0x9e3779b97f4a7c15ULL);
            hash = (hash << 17) | (hash >> 47);
            hash *= 0x9ddfea08eb382d69ULL;
        }

        [[nodiscard]] inline std::uint64_t HashPlayer(const Player &p, std::uint64_t hash) noexcept {
#define YO2_HASH_FIELD(field) HashAdd(hash, static_cast<std::uint64_t>(static_cast<std::uint32_t>(p.field)))
            YO2_HASH_FIELD(hp);
            YO2_HASH_FIELD(maxHp);
            YO2_HASH_FIELD(atk);
            YO2_HASH_FIELD(defaultATK);
            YO2_HASH_FIELD(def);
            YO2_HASH_FIELD(defaultDEF);
            YO2_HASH_FIELD(speed);
            YO2_HASH_FIELD(defaultSpeed);
            YO2_HASH_FIELD(HealPower);
            YO2_HASH_FIELD(mp);
            YO2_HASH_FIELD(maxMp);
            YO2_HASH_FIELD(specialCharge);
            YO2_HASH_FIELD(dirtySpecialCharge);
            YO2_HASH_FIELD(specialChargeTurn);
            YO2_HASH_FIELD(paralysis);
            YO2_HASH_FIELD(paralysisLevel);
            YO2_HASH_FIELD(paralysisTurns);
            YO2_HASH_FIELD(SpecialMedicineCount);
            HashAdd(hash, std::bit_cast<std::uint64_t>(p.defence));
            YO2_HASH_FIELD(sleeping);
            YO2_HASH_FIELD(sleepingTurn);
            YO2_HASH_FIELD(BuffLevel);
            YO2_HASH_FIELD(BuffTurns);
            YO2_HASH_FIELD(hasMagicMirror);
            YO2_HASH_FIELD(MagicMirrorTurn);
            YO2_HASH_FIELD(AtkBuffLevel);
            YO2_HASH_FIELD(AtkBuffTurn);
            YO2_HASH_FIELD(TensionLevel);
            YO2_HASH_FIELD(rage);
            YO2_HASH_FIELD(SageElixirCount);
            YO2_HASH_FIELD(ElfinElixirCount);
            YO2_HASH_FIELD(MagicWaterCount);
            YO2_HASH_FIELD(speedTurn);
            YO2_HASH_FIELD(speedLevel);
            YO2_HASH_FIELD(PoisonTurn);
            YO2_HASH_FIELD(PoisonEnable);
            YO2_HASH_FIELD(SpecialAntidoteCount);
            YO2_HASH_FIELD(acrobaticStar);
            YO2_HASH_FIELD(acrobaticStarTurn);
            YO2_HASH_FIELD(rageTurns);
            YO2_HASH_FIELD(medicinal_herbs_count);
            YO2_HASH_FIELD(inactive);
#undef YO2_HASH_FIELD
            return hash;
        }

        [[nodiscard]] inline std::uint64_t HashState(const State &state) noexcept {
            std::uint64_t hash = Mix64(static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.position)));
            HashAdd(hash, state.nowState);
            hash = HashPlayer(state.players[0], hash);
            hash = HashPlayer(state.players[1], hash);
            return Mix64(hash);
        }

        class FlatSeenSet {
        public:
            void reset(const std::size_t expected) {
                std::size_t size = 16;
                const std::size_t target = std::max<std::size_t>(16, expected * 2);
                while (size < target) size <<= 1;
                if (table_.size() != size) table_.assign(size, 0);
                else std::fill(table_.begin(), table_.end(), 0);
                mask_ = size - 1;
            }

            [[nodiscard]] bool insert(std::uint64_t hash) noexcept {
                std::uint64_t key = Mix64(hash);
                if (key == 0) key = 1;
                std::size_t slot = static_cast<std::size_t>(key) & mask_;
                for (;;) {
                    if (table_[slot] == 0) {
                        table_[slot] = key;
                        return true;
                    }
                    if (table_[slot] == key) return false;
                    slot = (slot + 1) & mask_;
                }
            }

        private:
            std::vector<std::uint64_t> table_{};
            std::size_t mask_ = 0;
        };

        [[nodiscard]] inline std::size_t BuildSelectableActions(
            const State &state, std::array<int32_t, 8> &out) noexcept {
            const Player &hero = state.players[0];
            std::size_t count = 0;
            if (hero.paralysis || hero.inactive) {
                out[count++] = BattleEmulator::ATTACK_ALLY;
                return count;
            }
            out[count++] = BattleEmulator::ATTACK_ALLY;
            out[count++] = BattleEmulator::DRAGON_SLASH;
            out[count++] = BattleEmulator::DEFENCE;
            out[count++] = BattleEmulator::FLEE_ALLY;
            if (hero.medicinal_herbs_count >= 1) out[count++] = BattleEmulator::MEDICINAL_HERBS;
            if (hero.mp >= 2) out[count++] = BattleEmulator::HEAL;
            if (hero.mp >= 3) out[count++] = BattleEmulator::CRACK_ALLY;
            if (hero.specialCharge && hero.specialChargeTurn > 0 && !hero.acrobaticStar) {
                out[count++] = BattleEmulator::ACROBATIC_STAR;
            }
            return count;
        }

        inline void RunOneTurn(
            State &state, const int32_t action, const std::uint64_t seed,
            BattleResult *result = nullptr) {
            // logicalTurnStart + RunCount=1 consumes exactly Gene[0]. Avoid
            // clearing 350 integers for every node expansion.
            int32_t gene[1] = {action};
            BattleEmulator::Main(
                &state.position, 1, gene, state.players, result, seed,
                nullptr, nullptr, result == nullptr ? -2 : -1, &state.nowState, true);
        }

        [[nodiscard]] inline bool TryStep(
            const State &parent, const int32_t action, const std::uint64_t seed,
            State &child) {
            if (action != BattleEmulator::FLEE_ALLY) {
                child = parent;
                RunOneTurn(child, action, seed);
                return true;
            }

            // FLEE is selected before initiative is resolved, but current Main
            // intentionally asserts if an enemy-first status effect makes the
            // ally unable to act before its slot. Probe the same authoritative
            // turn with an ordinary command. If that ally slot is replaced by a
            // status action, prepared FLEE is not an executable transition and
            // must not enter the search frontier. If the ally never gets a slot
            // because the enemy kills it, the probe state is already the exact
            // FLEE state (the selected ally command is never executed).
            State probe = parent;
            static thread_local BattleResult probeResult;
            probeResult.clear();
            RunOneTurn(probe, BattleEmulator::ATTACK_ALLY, seed, &probeResult);

            int executedAllyAction = -1;
            for (int i = 0; i < probeResult.position; ++i) {
                if (!probeResult.isEnemy[i]) {
                    executedAllyAction = probeResult.actions[i];
                    break;
                }
            }
            if (executedAllyAction == BattleEmulator::PARALYSIS ||
                executedAllyAction == BattleEmulator::CURE_PARALYSIS ||
                executedAllyAction == BattleEmulator::INACTIVE_ALLY) {
                return false;
            }
            if (executedAllyAction == -1) {
                child = probe;
                return true;
            }

            child = parent;
            RunOneTurn(child, action, seed);
            return true;
        }

        [[nodiscard]] inline int SteeringCredit(const int32_t action) noexcept {
            switch (action) {
                case BattleEmulator::FLEE_ALLY: return 30;
                case BattleEmulator::DEFENCE: return 11;
                case BattleEmulator::HEAL:
                case BattleEmulator::MEDICINAL_HERBS: return 11;
                case BattleEmulator::ACROBATIC_STAR: return 14;
                default: return 0;
            }
        }

        [[nodiscard]] inline std::int64_t RankCandidate(
            const State &state, const int32_t action, const int rootEnemyHp,
            const std::array<std::int64_t, 5000> &criticalLaneBonus) noexcept {
            const int virtualEnemyHp = std::max(0, state.players[1].hp - SteeringCredit(action));
            const int virtualDamage = rootEnemyHp - virtualEnemyHp;
            std::int64_t score = static_cast<std::int64_t>(virtualDamage) * 1000000LL;
            score += static_cast<std::int64_t>(state.players[0].hp) * 20000LL;
            score += static_cast<std::int64_t>(std::max(0, state.players[0].mp)) * 8000LL;
            score += static_cast<std::int64_t>(std::max(0, state.players[0].medicinal_herbs_count)) * 12000LL;
            if (state.players[0].specialCharge) score += 1200000LL;
            if (state.players[0].acrobaticStar) score += 900000LL;
            if (state.players[0].paralysis) score -= 1800000LL;
            if (state.players[0].inactive) score -= 1200000LL;
            const std::uint64_t diversity = Mix64(
                static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.position)) ^
                (state.nowState * 0x9e3779b97f4a7c15ULL));
            score += static_cast<std::int64_t>(diversity & 0x7ffffULL);

            // A normal hero critical check is 2% (<200 / 10000). Depending on
            // initiative and the enemy action, the next hero critical check sits
            // several RNG cells after the turn boundary. Keep states whose nearby
            // tape contains such a cell instead of forcing the beam to rediscover
            // this sparse opportunity only after it has already happened.
            if (state.position >= 0 && state.position < static_cast<int>(criticalLaneBonus.size())) {
                score += criticalLaneBonus[state.position];
            }
            return score;
        }

        [[nodiscard]] inline bool BetterFallback(
            const State &a, const int aDepth, const State &b, const int bDepth) noexcept {
            if (a.players[1].hp != b.players[1].hp) return a.players[1].hp < b.players[1].hp;
            if (a.players[0].hp != b.players[0].hp) return a.players[0].hp > b.players[0].hp;
            if (aDepth != bDepth) return aDepth < bDepth;
            if (a.players[0].mp != b.players[0].mp) return a.players[0].mp > b.players[0].mp;
            return a.players[0].medicinal_herbs_count > b.players[0].medicinal_herbs_count;
        }

        inline void MaterializeResult(
            Result &result, const std::vector<Trace> &traces,
            std::uint32_t parentTrace, const int32_t finalAction,
            const int depth, const State &finalState) {
            result.actionCount = depth;
            result.finalState = finalState;
            result.actions.fill(0);
            if (depth <= 0) return;
            int write = depth - 1;
            result.actions[write--] = finalAction;
            std::uint32_t trace = parentTrace;
            while (write >= 0 && trace != kNoTrace) {
                result.actions[write--] = traces[trace].action;
                trace = traces[trace].parent;
            }
        }

        [[nodiscard]] inline bool SamePlayer(const Player &a, const Player &b) noexcept {
#define YO2_EQ_FIELD(field) if (a.field != b.field) return false
            YO2_EQ_FIELD(hp);
            YO2_EQ_FIELD(maxHp);
            YO2_EQ_FIELD(atk);
            YO2_EQ_FIELD(defaultATK);
            YO2_EQ_FIELD(def);
            YO2_EQ_FIELD(defaultDEF);
            YO2_EQ_FIELD(speed);
            YO2_EQ_FIELD(defaultSpeed);
            YO2_EQ_FIELD(HealPower);
            YO2_EQ_FIELD(mp);
            YO2_EQ_FIELD(maxMp);
            YO2_EQ_FIELD(specialCharge);
            YO2_EQ_FIELD(dirtySpecialCharge);
            YO2_EQ_FIELD(specialChargeTurn);
            YO2_EQ_FIELD(paralysis);
            YO2_EQ_FIELD(paralysisLevel);
            YO2_EQ_FIELD(paralysisTurns);
            YO2_EQ_FIELD(SpecialMedicineCount);
            YO2_EQ_FIELD(defence);
            YO2_EQ_FIELD(sleeping);
            YO2_EQ_FIELD(sleepingTurn);
            YO2_EQ_FIELD(BuffLevel);
            YO2_EQ_FIELD(BuffTurns);
            YO2_EQ_FIELD(hasMagicMirror);
            YO2_EQ_FIELD(MagicMirrorTurn);
            YO2_EQ_FIELD(AtkBuffLevel);
            YO2_EQ_FIELD(AtkBuffTurn);
            YO2_EQ_FIELD(TensionLevel);
            YO2_EQ_FIELD(rage);
            YO2_EQ_FIELD(SageElixirCount);
            YO2_EQ_FIELD(ElfinElixirCount);
            YO2_EQ_FIELD(MagicWaterCount);
            YO2_EQ_FIELD(speedTurn);
            YO2_EQ_FIELD(speedLevel);
            YO2_EQ_FIELD(PoisonTurn);
            YO2_EQ_FIELD(PoisonEnable);
            YO2_EQ_FIELD(SpecialAntidoteCount);
            YO2_EQ_FIELD(acrobaticStar);
            YO2_EQ_FIELD(acrobaticStarTurn);
            YO2_EQ_FIELD(rageTurns);
            YO2_EQ_FIELD(medicinal_herbs_count);
            YO2_EQ_FIELD(inactive);
#undef YO2_EQ_FIELD
            return true;
        }
    }

    [[nodiscard]] inline bool SameState(const State &a, const State &b) noexcept {
        return a.position == b.position && a.nowState == b.nowState &&
               detail::SamePlayer(a.players[0], b.players[0]) &&
               detail::SamePlayer(a.players[1], b.players[1]);
    }

    [[nodiscard]] inline Result Run(
        const State &start, const std::uint64_t seed,
        const int prefixTurns, const int timeBudgetMs) {
        using Clock = std::chrono::steady_clock;
        const auto started = Clock::now();
        const auto deadline = started + std::chrono::milliseconds(std::max(0, timeBudgetMs));
        Result result{};
        result.finalState = start;
        if (start.players[0].hp <= 0 || start.players[1].hp <= 0 ||
            prefixTurns < 0 || prefixTurns >= 350 || timeBudgetMs <= 0) {
            result.elapsedMs = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
            result.timedOut = timeBudgetMs <= 0;
            return result;
        }

        lcg::init(seed, true);
        const int maxDepth = 349 - prefixTurns;
        const int rootEnemyHp = start.players[1].hp;

        std::array<std::uint16_t, 5000> criticalTape{};
        for (int i = 1; i < static_cast<int>(criticalTape.size()); ++i) {
            int p = i;
            criticalTape[i] = static_cast<std::uint16_t>(lcg::getPercent(&p, 10000));
        }
        std::array<std::int64_t, 5000> criticalLaneBonus{};
        for (int position = 0; position + 34 < static_cast<int>(criticalTape.size()); ++position) {
            std::int64_t best = 0;
            for (int offset = 7; offset <= 34; ++offset) {
                const int value = criticalTape[position + offset];
                if (value >= 200) continue;
                std::int64_t bonus = 3000000LL;
                bonus += static_cast<std::int64_t>(200 - value) * 30000LL;
                bonus += static_cast<std::int64_t>(35 - offset) * 50000LL;
                if (value < 100) bonus += 1000000LL;
                best = std::max(best, bonus);
            }
            criticalLaneBonus[position] = best;
        }

        std::vector<detail::Trace> traces;
        traces.reserve(detail::kBeamWidth * 16);
        std::vector<detail::BeamNode> beam;
        std::vector<detail::BeamNode> nextBeam;
        std::vector<detail::Candidate> candidates;
        beam.reserve(detail::kBeamWidth);
        nextBeam.reserve(detail::kBeamWidth);
        candidates.reserve(detail::kBeamWidth * 8);
        beam.push_back({start, detail::kNoTrace});

        detail::FlatSeenSet seen;
        State bestFallback = start;
        int bestFallbackDepth = 0;
        std::uint32_t bestFallbackParent = detail::kNoTrace;
        int32_t bestFallbackAction = -1;
        bool foundVictory = false;

        for (int depth = 1; depth <= maxDepth && !beam.empty(); ++depth) {
            candidates.clear();
            const std::size_t expected = std::min<std::size_t>(
                detail::kBeamWidth * 8,
                std::max<std::size_t>(16, beam.size() * 7));
            seen.reset(expected);

            for (std::size_t nodeIndex = 0; nodeIndex < beam.size(); ++nodeIndex) {
                if ((result.expanded & 0x7ffULL) == 0 && Clock::now() >= deadline) {
                    result.timedOut = true;
                    break;
                }
                const auto &node = beam[nodeIndex];
                if (node.state.players[0].hp <= 0 || node.state.players[1].hp <= 0) continue;
                if (node.state.position >= detail::kRngSafetyLimit) continue;

                std::array<int32_t, 8> actions{};
                const std::size_t actionCount = detail::BuildSelectableActions(node.state, actions);
                for (std::size_t actionIndex = 0; actionIndex < actionCount; ++actionIndex) {
                    const int32_t action = actions[actionIndex];
                    State child{};
                    if (!detail::TryStep(node.state, action, seed, child)) continue;
                    ++result.expanded;
                    if (child.players[0].hp <= 0) continue;
                    if (child.players[1].hp <= 0) {
                        if (!foundVictory || child.players[0].hp > result.finalState.players[0].hp) {
                            detail::MaterializeResult(result, traces, node.trace, action, depth, child);
                            foundVictory = true;
                        }
                        continue;
                    }
                    if (detail::BetterFallback(child, depth, bestFallback, bestFallbackDepth)) {
                        bestFallback = child;
                        bestFallbackDepth = depth;
                        bestFallbackParent = node.trace;
                        bestFallbackAction = action;
                    }
                    if (!seen.insert(detail::HashState(child))) continue;
                    candidates.push_back({child, node.trace, action,
                                          detail::RankCandidate(child, action, rootEnemyHp, criticalLaneBonus)});
                }
            }

            if (foundVictory || result.timedOut || candidates.empty()) break;
            if (candidates.size() > detail::kBeamWidth) {
                const auto middle = candidates.begin() + static_cast<std::ptrdiff_t>(detail::kBeamWidth);
                std::nth_element(candidates.begin(), middle, candidates.end(),
                                 [](const detail::Candidate &a, const detail::Candidate &b) {
                                     return a.score > b.score;
                                 });
                candidates.resize(detail::kBeamWidth);
            }

            nextBeam.clear();
            for (auto &candidate : candidates) {
                const std::uint32_t traceIndex = static_cast<std::uint32_t>(traces.size());
                traces.push_back({candidate.parentTrace, candidate.action});
                nextBeam.push_back({candidate.state, traceIndex});
            }
            beam.swap(nextBeam);
        }

        if (!foundVictory && bestFallbackDepth > 0) {
            detail::MaterializeResult(result, traces, bestFallbackParent, bestFallbackAction,
                                      bestFallbackDepth, bestFallback);
        }

        const auto finished = Clock::now();
        result.elapsedMs = std::chrono::duration<double, std::milli>(finished - started).count();
        if (!foundVictory && finished >= deadline) result.timedOut = true;
        return result;
    }
}

#endif
