#include "RngFlowPlanner.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <unordered_set>
#include <vector>

#include "lcg.h"

#if defined(_MSC_VER)
#define RNGFLOW_FORCE_INLINE __forceinline
#elif defined(__EMSCRIPTEN__) || defined(__GNUC__)
#define RNGFLOW_FORCE_INLINE inline __attribute__((always_inline))
#else
#define RNGFLOW_FORCE_INLINE inline
#endif

namespace rngflow {
    namespace {
        constexpr int kAttackCriticalThreshold = 200;
        constexpr int kDragonCriticalThreshold = 100;
        constexpr int kSpellCriticalThreshold = 100;
        constexpr int kRngPercentScale = 0x2710;


        struct ActionResult {
            int damage = 0;
            int criticalGain = 0;
        };

        [[nodiscard]] RNGFLOW_FORCE_INLINE std::uint32_t ConsumeTop32(State &state) {
            return lcg::peekTop32(state.position++);
        }

        [[nodiscard]] RNGFLOW_FORCE_INLINE int ConsumePercent(State &state, const int max) {
            const auto top = static_cast<std::uint64_t>(ConsumeTop32(state));
            return static_cast<int>((top * static_cast<std::uint64_t>(max)) >> 32);
        }

        [[nodiscard]] RNGFLOW_FORCE_INLINE double ConsumeUnit(State &state) {
            return static_cast<double>(ConsumeTop32(state)) * (1.0 / 4294967296.0);
        }

        [[nodiscard]] RNGFLOW_FORCE_INLINE double ConsumeFloat(State &state, const double min, const double max) {
            return min + ConsumeUnit(state) * (max - min);
        }

        [[nodiscard]] RNGFLOW_FORCE_INLINE double ConsumeFloatAttack(State &state) {
            return -1.0 + static_cast<double>(ConsumeTop32(state)) * (1.0 / 2147483648.0);
        }

        [[nodiscard]] RNGFLOW_FORCE_INLINE int ConsumeIntRange(State &state, const int min, const int max) {
            return min + ConsumePercent(state, max - min + 1);
        }

        [[nodiscard]] RNGFLOW_FORCE_INLINE bool ConsumeTurnOrder(State &state,
                                                                 const int heroSpeed,
                                                                 const int enemySpeed) {
            struct Cache {
                std::uint64_t generation = 0;
                int heroSpeed = 0;
                int enemySpeed = 0;
                std::array<std::uint8_t, 5000> order{};
                std::array<std::uint8_t, 5000> valid{};
            };
            static thread_local Cache cache{};
            const int position = state.position;
            assert(position >= 1 && position + 1 < 5000);
            state.position += 3; // two speed rolls plus the fixed post-order consume

            const std::uint64_t generation = lcg::generation();
            if (cache.generation != generation || cache.heroSpeed != heroSpeed ||
                cache.enemySpeed != enemySpeed) {
                cache.generation = generation;
                cache.heroSpeed = heroSpeed;
                cache.enemySpeed = enemySpeed;
                cache.valid.fill(0);
            }
            if (cache.valid[position]) return cache.order[position] != 0;

            // Preserve ConsumeFloat(0.51, 1.0)'s arithmetic order exactly; this cache
            // removes repeated floating work, not any RNG/state distinction.
            const double unit0 = static_cast<double>(lcg::peekTop32(position)) *
                                 (1.0 / 4294967296.0);
            const double unit1 = static_cast<double>(lcg::peekTop32(position + 1)) *
                                 (1.0 / 4294967296.0);
            const double speed0 = heroSpeed * (0.51 + unit0 * (1.0 - 0.51));
            const double speed1 = enemySpeed * (0.51 + unit1 * (1.0 - 0.51));
            const bool heroFirst = speed0 > speed1;
            cache.order[position] = heroFirst ? 1 : 0;
            cache.valid[position] = 1;
            return heroFirst;
        }

        // yo2 early-boss specialization: the supported raw ATK/DEF pairs remain in
        // FUN_0207564c's common two-random-number branch.  This is intentionally the
        // same specialization as BattleEmulator::FUN_0207564c in this worktree.
        [[nodiscard]] int PhysicalDamage2Rng(State &state, const int atk, const int def) {
            struct Cache {
                std::uint64_t generation = 0;
                int atk = 0;
                int def = 0;
                std::array<int, 5000> damage{};
                std::array<std::uint8_t, 5000> valid{};
            };
            static thread_local std::array<Cache, 4> caches{};

            const int position = state.position;
            assert(position >= 1 && position + 1 < 5000);
            state.position += 2;

            const std::uint64_t generation = lcg::generation();
            Cache *cache = nullptr;
            for (Cache &candidate: caches) {
                if (candidate.generation == generation && candidate.atk == atk && candidate.def == def) {
                    cache = &candidate;
                    break;
                }
            }
            if (cache == nullptr) {
                for (Cache &candidate: caches) {
                    if (candidate.generation != generation) {
                        candidate.generation = generation;
                        candidate.atk = atk;
                        candidate.def = def;
                        candidate.valid.fill(0);
                        cache = &candidate;
                        break;
                    }
                }
            }
            assert(cache != nullptr);
            if (cache->valid[position]) return cache->damage[position];

            const double atk1 = atk * 0.5 - def * 0.25;
            const double atk2 = atk * 0.0625;
            assert(atk1 > 0.0 && atk1 > atk2);
            const double spread = atk1 * 0.0625;
            const double first = static_cast<double>(lcg::peekTop32(position)) * (1.0 / 4294967296.0);
            const double second = static_cast<double>(lcg::peekTop32(position + 1)) * (1.0 / 2147483648.0);
            double result = atk1 + (-spread + first * (spread + spread));
            result += -1.0 + second;
            const int damage = result <= 0.0 ? 0 : static_cast<int>(result);
            cache->damage[position] = damage;
            cache->valid[position] = 1;
            return damage;
        }

        [[nodiscard]] int TypeCDamage(State &state, const double min, const double max, const double base) {
            const double result = ConsumeFloat(state, min, max) + ConsumeFloat(state, -base, base);
            return static_cast<int>(result);
        }

        [[nodiscard]] int TypeDDamage(State &state, const double difference, const double base) {
            return static_cast<int>(ConsumeFloat(state, -difference, difference) + base);
        }

        void ProcessSpecialCharge(State &state, const int baseDamage, const int defender) {
            RngPlayer &target = state.players[defender];
            if (target.paralysis || target.sleeping || target.inactive) return;
            if (target.hp <= baseDamage || target.specialCharge) return;
            if (baseDamage == 0) {
                ++state.position;
                return;
            }

            const int percent = ConsumePercent(state, 100);
            static constexpr int probabilities[9] = {90, 90, 64, 32, 16, 8, 4, 2, 1};
            for (int i = 9; i >= 1; --i) {
                const int threshold = static_cast<int>(target.maxHp * (static_cast<double>(i) / 10.0)) + 1;
                const int index = 9 - i;
                if (baseDamage >= threshold) {
                    if (percent < probabilities[index]) {
                        target.specialCharge = true;
                        target.specialChargeTurn = 6;
                    }
                    break;
                }
            }
        }

        void ProcessRage(State &state, const int baseDamage) {
            RngPlayer &enemy = state.players[1];
            const int hpBefore = enemy.hp;
            const int hpAfter = std::max(0, enemy.hp - baseDamage);
            const int maxHp = enemy.maxHp;

            if (hpAfter * 2 < maxHp) {
                if (hpBefore * 2 >= maxHp) {
                    if (!enemy.rage) {
                        ++state.position;
                        enemy.rage = true;
                        enemy.rageTurns = ConsumeIntRange(state, 2, 4);
                    } else {
                        ++state.position;
                    }
                } else if (hpAfter * 4 < maxHp && hpBefore * 4 >= maxHp) {
                    state.position += enemy.rage ? 1 : 2;
                }
            }
        }

        [[nodiscard]] int SelectEnemyAction(State &state) {
            // FUN_02158dfc always consumes the charm check even though mitoreP is
            // negative in this yo2 profile and therefore can never trigger.
            (void) ConsumePercent(state, 100);

            const int rnd = ConsumePercent(state, 0x100) + 1;
            int action;
            if (rnd <= 43) action = BattleEmulator::VICTIMISER;
            else if (rnd <= 43 + 42) action = BattleEmulator::HP_HOOVER;
            else if (rnd <= 43 + 42 + 43) action = BattleEmulator::CRACK_ENEMY;
            else if (rnd <= 43 + 42 + 43 + 43) action = BattleEmulator::ATTACK_ENEMY;
            else if (rnd <= 43 + 42 + 43 + 43 + 42) action = BattleEmulator::MANAZASHI;
            else action = BattleEmulator::PUFF_PUFF;

            RngPlayer &hero = state.players[0];
            RngPlayer &enemy = state.players[1];
            if (action == BattleEmulator::PUFF_PUFF && hero.inactive) {
                action = BattleEmulator::MANAZASHI;
            }
            if (action == BattleEmulator::MANAZASHI) {
                state.position += 2;
            } else if (!enemy.rage) {
                ++state.position;
            }
            if (action == BattleEmulator::VICTIMISER && !hero.paralysis) {
                action = BattleEmulator::HP_HOOVER;
            }
            ++state.position;
            return action;
        }

        ActionResult CallAction(State &state, const int id, const int attacker, const int defender) {
            RngPlayer &hero = state.players[0];
            RngPlayer &enemy = state.players[1];
            RngPlayer &actor = state.players[attacker];
            RngPlayer &target = state.players[defender];
            ActionResult result{};
            bool critical = false;
            bool evaded = false;
            bool shield = false;

            switch (id & 0xffff) {
                case BattleEmulator::MEDICINAL_HERBS:
                    {
                        --hero.medicinal_herbs_count;
                        state.position += 5; // +2, unrelated, crit slot, evade slot
                        result.damage = TypeCDamage(state, 35.0, 35.0, 5.0);
                        ++state.position;
                        if (!actor.specialCharge) {
                            ++state.position;
                            if (ConsumePercent(state, 100) < 1) {
                                actor.specialCharge = true;
                                actor.specialChargeTurn = 6;
                            }
                        }
                        break;
                    }

                case BattleEmulator::ACROBATSTAR_KAIHI:
                    critical = ConsumePercent(state, kRngPercentScale) < kAttackCriticalThreshold;
                    ++state.position;
                    state.position += 2;
                    if (!target.specialCharge) ++state.position;
                    // The critical here belongs to the enemy animation path and does
                    // not count toward the player's critical-hacking reward.
                    return result;

                case BattleEmulator::COUNTER:
                    ++state.position;
                    critical = ConsumePercent(state, kRngPercentScale) < kAttackCriticalThreshold;
                    evaded = ConsumePercent(state, 100) < 2;
                    if (!evaded) ++state.position;
                    ++state.position;
                    result.damage = PhysicalDamage2Rng(state, actor.atk, target.def);
                    if (critical) {
                        result.damage = static_cast<int>(actor.atk * ConsumeFloat(state, 0.95, 1.05));
                        if (attacker == 0) result.criticalGain = 1;
                    }
                    if (!evaded) {
                        ProcessRage(state, result.damage);
                        RngPlayer::reduceHp(target, result.damage);
                    }
                    // Full emulator returns 0 because counter damage was applied here.
                    result.damage = 0;
                    return result;

                case BattleEmulator::ACROBATIC_STAR:
                    hero.acrobaticStar = true;
                    hero.acrobaticStarTurn = 6;
                    hero.specialCharge = false;
                    hero.specialChargeTurn = 0;
                    state.position += 5;
                    state.position += 2;
                    ++state.position;
                    break;

                case BattleEmulator::DEFENCE:
                case BattleEmulator::CURE_PARALYSIS:
                    state.position += 5;
                    state.position += 2;
                    ++state.position;
                    if (!actor.specialCharge) {
                        ++state.position;
                        if (ConsumePercent(state, 100) < 1) {
                            actor.specialCharge = true;
                            actor.specialChargeTurn = 6;
                        }
                    }
                    break;

                case BattleEmulator::VICTIMISER:
                    state.position += 4;
                    if (!hero.paralysis && !hero.inactive) {
                        if (!hero.acrobaticStar && ConsumePercent(state, 100) < 2) evaded = true;
                        if (!evaded && ConsumePercent(state, 100) < 0.5) shield = true;
                    }
                    ++state.position;
                    result.damage = static_cast<int>(PhysicalDamage2Rng(state, actor.atk, target.def) * 1.5);
                    if (evaded || shield) result.damage = 0;
                    else state.position += 2;
                    result.damage = static_cast<int>(result.damage * target.defence);
                    ProcessSpecialCharge(state, result.damage, defender);
                    break;

                case BattleEmulator::ATTACK_ENEMY:
                    {
                        state.position += 2;
                        if (hero.acrobaticStar && !hero.paralysis && !hero.inactive) {
                            const int acrobat = ConsumePercent(state, 100);
                            if (acrobat < 50) {
                                return CallAction(state, BattleEmulator::ACROBATSTAR_KAIHI, attacker, defender);
                            }
                            if (acrobat < 75) {
                                return CallAction(state, BattleEmulator::COUNTER, defender, attacker);
                            }
                        } else {
                            ++state.position;
                        }
                        ++state.position;
                        if (!hero.paralysis && !hero.inactive) {
                            if (!hero.acrobaticStar && ConsumePercent(state, 100) < 2) evaded = true;
                            if (!evaded && ConsumePercent(state, 100) < 0.5) shield = true;
                        }
                        ++state.position;
                        result.damage = PhysicalDamage2Rng(state, actor.atk, target.def);
                        if (evaded || shield) result.damage = 0;
                        else state.position += 2;
                        result.damage = static_cast<int>(result.damage * target.defence);
                        ProcessSpecialCharge(state, result.damage, defender);
                        break;
                    }

                case BattleEmulator::INACTIVE_ALLY:
                case BattleEmulator::PARALYSIS:
                case BattleEmulator::INACTIVE_ENEMY:
                    state.position += 5;
                    state.position += 2;
                    ++state.position;
                    break;

                case BattleEmulator::PUFF_PUFF:
                    state.position += 4;
                    if (ConsumePercent(state, 100) < 50) target.inactive = true;
                    if (!target.inactive && target.paralysis) return result;
                    if (target.inactive) {
                        state.position += 2;
                        ++state.position;
                    } else if (!target.specialCharge) {
                        ++state.position;
                    }
                    break;

                case BattleEmulator::HEAL:
                    state.position += 3;
                    critical = ConsumePercent(state, kRngPercentScale) < kSpellCriticalThreshold;
                    ++state.position;
                    result.damage = TypeDDamage(state, 5.0, 35.0);
                    if (critical) {
                        result.damage = static_cast<int>(result.damage * ConsumeFloat(state, 1.5, 2.0));
                        if (attacker == 0) result.criticalGain = 1;
                    }
                    ++state.position;
                    if (!actor.specialCharge) ++state.position;
                    if (!enemy.rage) ++state.position;
                    ++state.position;
                    if (critical) state.position += enemy.rage ? 1 : 2;
                    if (!hero.paralysis && !hero.inactive && !actor.specialCharge) {
                        if (ConsumePercent(state, 100) < 1) {
                            actor.specialCharge = true;
                            actor.specialChargeTurn = 6;
                        }
                    }
                    actor.mp -= 2;
                    break;

                case BattleEmulator::MANAZASHI:
                    state.position += 5;
                    result.damage = TypeDDamage(state, 4.0, 12.0);
                    if (ConsumePercent(state, 100) < 25) {
                        target.paralysis = true;
                        target.paralysisTurns = 4;
                        ++target.paralysisLevel;
                    }
                    ++state.position;
                    result.damage = static_cast<int>(result.damage * target.defence);
                    ProcessSpecialCharge(state, result.damage, defender);
                    break;

                case BattleEmulator::CRACK_ENEMY:
                    state.position += 4;
                    if (!hero.paralysis && !hero.inactive && ConsumePercent(state, 100) < 0.5) shield = true;
                    ++state.position;
                    result.damage = TypeDDamage(state, 5.0, 17.0);
                    if (shield) result.damage = 0;
                    else ++state.position;
                    result.damage = static_cast<int>(result.damage * target.defence);
                    ProcessSpecialCharge(state, result.damage, defender);
                    break;

                case BattleEmulator::HP_HOOVER:
                    {
                        state.position += 2;
                        if (hero.acrobaticStar && !hero.paralysis && !hero.inactive) {
                            const int acrobat = ConsumePercent(state, 100);
                            if (acrobat < 50) {
                                return CallAction(state, BattleEmulator::ACROBATSTAR_KAIHI, attacker, defender);
                            }
                            if (acrobat < 75) {
                                return CallAction(state, BattleEmulator::COUNTER, defender, attacker);
                            }
                        } else {
                            ++state.position;
                        }
                        ++state.position;
                        if (!hero.paralysis && !hero.inactive) {
                            if (!hero.acrobaticStar && ConsumePercent(state, 100) < 2) evaded = true;
                            if (!evaded && ConsumePercent(state, 100) < 0.5) shield = true;
                        }
                        ++state.position;
                        result.damage = PhysicalDamage2Rng(state, actor.atk, target.def);
                        if (evaded || shield) {
                            result.damage = 0;
                        } else {
                            state.position += 2;
                            result.damage = static_cast<int>(result.damage * target.defence);
                            RngPlayer::heal(actor, result.damage >> 2);
                        }
                        ProcessSpecialCharge(state, result.damage, defender);
                        break;
                    }

                case BattleEmulator::CRACK_ALLY:
                    actor.mp -= 3;
                    state.position += 3;
                    critical = ConsumePercent(state, kRngPercentScale) < kSpellCriticalThreshold;
                    state.position += 2;
                    result.damage = TypeDDamage(state, 5.0, 30.0);
                    if (critical) {
                        result.damage = static_cast<int>(result.damage * ConsumeFloat(state, 1.5, 2.0));
                        if (attacker == 0) result.criticalGain = 1;
                    }
                    result.damage = static_cast<int>(result.damage * 0.5);
                    ProcessRage(state, result.damage);
                    if (critical) state.position += enemy.rage ? 1 : 2;
                    ++state.position;
                    if (!actor.specialCharge && ConsumePercent(state, 100) < 1) {
                        actor.specialCharge = true;
                        actor.specialChargeTurn = 6;
                    }
                    break;

                case BattleEmulator::ATTACK_ALLY:
                case BattleEmulator::DRAGON_SLASH:
                    {
                        state.position += 3;
                        const int criticalRoll = ConsumePercent(state, kRngPercentScale);
                        const int threshold = id == BattleEmulator::ATTACK_ALLY
                                                  ? kAttackCriticalThreshold
                                                  : kDragonCriticalThreshold;
                        critical = criticalRoll < threshold;
                        if (!hero.paralysis && !hero.inactive) {
                            if (ConsumePercent(state, 100) < 2) evaded = true;
                            if (!evaded) ++state.position;
                        }
                        ++state.position;
                        result.damage = PhysicalDamage2Rng(state, actor.atk, target.def);
                        if (critical) {
                            if (id == BattleEmulator::DRAGON_SLASH) {
                                result.damage = static_cast<int>(result.damage * ConsumeFloat(state, 1.5, 2.0));
                            } else {
                                result.damage = static_cast<int>(actor.atk * ConsumeFloat(state, 0.95, 1.05));
                            }
                            if (attacker == 0) result.criticalGain = 1;
                        }
                        if (!evaded) {
                            ProcessRage(state, result.damage);
                            state.position += 2;
                        } else {
                            result.damage = 0;
                        }
                        if (critical) state.position += enemy.rage ? 1 : 2;
                        if (target.hp - result.damage >= 0 && !actor.specialCharge) {
                            if (ConsumePercent(state, 100) < 1) {
                                actor.specialCharge = true;
                                actor.specialChargeTurn = 6;
                            }
                        }
                        break;
                    }

                default:
                    break;
            }
            return result;
        }

        void CameraStep(State &state, const std::array<int, 2> &actions) {
            bool preemptive = true;
            for (const int action: actions) {
                if (action == BattleEmulator::ATTACK_ALLY) {
                    if (preemptive) {
                        ++state.position;
                        if (state.cameraCounter == 0) {
                            ++state.position;
                            state.cameraCounter = 0;
                        } else {
                            state.position += 2;
                            state.cameraCounter = 0;
                        }
                    } else {
                        ++state.position;
                        if (state.cameraCounter == 0) {
                            ++state.cameraCounter;
                        } else {
                            const int ret = ConsumePercent(state, 5 - state.cameraCounter);
                            if (ret == 0 || state.cameraCounter == 5) {
                                state.cameraCounter = 0;
                                ++state.position;
                            } else {
                                ++state.cameraCounter;
                            }
                        }
                    }
                } else if (action == BattleEmulator::ATTACK_ENEMY ||
                           action == BattleEmulator::MIRACLE_SLASH ||
                           action == BattleEmulator::DRAGON_SLASH) {
                    ++state.position;
                }
                if (action != BattleEmulator::ATTACK_ALLY) preemptive = false;
            }
        }

        [[nodiscard]] bool IsHealingAction(const int action) {
            return action == BattleEmulator::HEAL || action == BattleEmulator::MEDICINAL_HERBS;
        }

        [[nodiscard]] std::size_t BuildCandidates(const State &state, std::array<int, 8> &out) {
            const RngPlayer &hero = state.players[0];
            std::size_t count = 0;
            out[count++] = BattleEmulator::ATTACK_ALLY;
            out[count++] = BattleEmulator::DRAGON_SLASH;
            out[count++] = BattleEmulator::DEFENCE;
            out[count++] = BattleEmulator::FLEE_ALLY;
            if (hero.medicinal_herbs_count >= 1) out[count++] = BattleEmulator::MEDICINAL_HERBS;
            if (hero.mp >= 2) out[count++] = BattleEmulator::HEAL;
            if (hero.mp >= 3) out[count++] = BattleEmulator::CRACK_ALLY;
            if (hero.specialCharge && hero.specialChargeTurn != 0 && !hero.acrobaticStar) {
                out[count++] = BattleEmulator::ACROBATIC_STAR;
            }
            return count;
        }

        constexpr int kMaxProjectedRngPerTurn = 64;
        // Static-tail relaxation used by optimistic planning.  For a completed yo2
        // turn where both actors survive, Step() consumes at least 12 RNG positions:
        //   3 turn-order + 3 enemy selection + 4 shortest enemy action + 1 post-enemy
        //   + 1 turn tail.  FLEE makes the ally half and camera contribution zero.
        // A branch-by-branch upper count is below 45 positions; keep 48 as deliberate
        // slack.  Allowing every delta in [12,48] is a relaxation (many are not really
        // reachable), so the DP below can only overestimate the real critical reward.
        constexpr int kStaticMinRngPerCompletedTurn = 12;
        constexpr int kStaticMaxRngPerCompletedTurn = 48;
        constexpr int kStaticMinPlayerCriticalOffset = 7;
        constexpr int kStaticMaxPlayerCriticalOffset = 30;

        [[nodiscard]] RNGFLOW_FORCE_INLINE constexpr std::uint64_t BitMask(const int bits) noexcept {
            return (std::uint64_t{1} << bits) - 1;
        }

        [[nodiscard]] RNGFLOW_FORCE_INLINE std::uint64_t PackSigned(const int value, const int bias,
                                                                    const int bits) noexcept {
            const int encoded = value + bias;
            assert(encoded >= 0 && encoded <= static_cast<int>(BitMask(bits)));
            return static_cast<std::uint64_t>(encoded) & BitMask(bits);
        }

        [[nodiscard]] RNGFLOW_FORCE_INLINE int ExactEnemyHp(const RngPlayer &enemy) noexcept {
            return std::clamp(enemy.hp, 0, enemy.maxHp);
        }

        [[nodiscard]] RNGFLOW_FORCE_INLINE State CanonicalizePlannerState(State state) noexcept {
            RngPlayer &hero = state.players[0];
            RngPlayer &enemy = state.players[1];

            // Ally HP is not part of the early critical/RNG objective and is forgotten
            // between projected turns. MP is different: it limits how many HEAL/CRACK
            // RNG-adjustment actions can still be issued, so remaining MP stays exact.
            hero.hp = hero.maxHp;
            // Enemy HP stays exact.  Unlike ally HP/MP it can change future RNG
            // consumption by moving the exact turn on which rage thresholds are
            // crossed.  Coarse bands are useful only as a heuristic ordering signal,
            // never as the identity of the exact critical solver.
            enemy.hp = ExactEnemyHp(enemy);

            // Timer values behind disabled flags are dead state.  Letting them drift
            // negative creates fake planner states (and previously even wrapped the
            // packed key) although they cannot affect any future RNG branch.
            if (!hero.specialCharge) hero.specialChargeTurn = 0;
            if (!hero.paralysis) {
                hero.paralysisTurns = -1;
                hero.paralysisLevel = 0;
            }
            if (!hero.acrobaticStar) hero.acrobaticStarTurn = 0;
            if (!enemy.rage) enemy.rageTurns = -1;
            return state;
        }

        // Relax ally HP to max HP at every optimistic-window memo boundary.  The only
        // current-HP RNG branch is ProcessSpecialCharge's `hp <= damage` early return;
        // that condition means the same enemy hit is lethal when Step later applies
        // the damage.  Every real continuation that survives therefore takes the same
        // RNG-consuming branch as max HP.  Reviving branches that would have died only
        // adds impossible continuations, so this is a genuine optimistic superset and
        // lets all surviving HP values share one memo entry.
        [[nodiscard]] RNGFLOW_FORCE_INLINE State CanonicalizeOptimisticWindowState(State state) noexcept {
            return CanonicalizePlannerState(state);
        }

        [[nodiscard]] RNGFLOW_FORCE_INLINE std::uint64_t MakeKey(const State &state, const int turnsLeft) noexcept {
            const RngPlayer &hero = state.players[0];
            const RngPlayer &enemy = state.players[1];
            std::uint16_t flags = 0;
            if (hero.specialCharge) flags |= 1u << 0;
            if (hero.paralysis) flags |= 1u << 1;
            if (hero.inactive) flags |= 1u << 2;
            if (hero.acrobaticStar) flags |= 1u << 3;
            if (enemy.rage) flags |= 1u << 4;
            // 57-bit packed projected state.  This is deliberately the state of the
            // critical/RNG-flow problem, not a BattleEmulator transposition hash.
            //
            // turns 5 | position 13 | enemy HP 9 | ally MP 5 |
            // special turn 4 | paralysis turn 5 |
            // acrobat turn 4 | rage turn 4 | flags 5 | camera 3
            std::uint64_t key = 0;
            int shift = 0;
            const auto put = [&key, &shift](const std::uint64_t value, const int bits) {
                assert((value & ~BitMask(bits)) == 0);
                key |= value << shift;
                shift += bits;
            };

            put(static_cast<std::uint64_t>(turnsLeft), 5);
            put(static_cast<std::uint64_t>(state.position), 13);
            put(static_cast<std::uint64_t>(ExactEnemyHp(enemy)), 9);
            // MP above the maximum amount that can possibly be spent in the remaining
            // turns is future-equivalent. CRACK is the largest supported MP consumer
            // at 3 MP/turn, so this canonicalization is exact rather than heuristic.
            put(static_cast<std::uint64_t>(std::clamp(hero.mp, 0, std::min(31, turnsLeft * 3))), 5);
            put(PackSigned(hero.specialChargeTurn, 2, 4), 4);
            put(PackSigned(hero.paralysisTurns, 16, 5), 5);
            put(PackSigned(hero.acrobaticStarTurn, 2, 4), 4);
            put(PackSigned(enemy.rageTurns, 2, 4), 4);
            put(flags, 5);
            put(state.cameraCounter, 3);
            assert(shift == 57);
            return key;
        }

        struct Value {
            int criticals = 0;
            int bestAction = -1;
        };

        [[nodiscard]] bool Better(const Value &a, const Value &b) {
            return a.criticals > b.criticals;
        }

        class FlatMemo {
        public:
            using PackedValues = std::array<std::uint16_t, 8>;

            FlatMemo() { Reset(1u << 15); }

            [[nodiscard]] bool Find(const std::uint64_t key, PackedValues &value) const noexcept {
                const std::uint64_t storedKey = key + 1;
                std::size_t index = Hash(storedKey) & mask_;
                for (;;) {
                    const Slot &slot = slots_[index];
                    if (slot.key == 0) return false;
                    if (slot.key == storedKey) {
                        value = slot.value;
                        return true;
                    }
                    index = (index + 1) & mask_;
                }
            }

            void Insert(const std::uint64_t key, const PackedValues &value) {
                if ((size_ + 1) * 10 >= slots_.size() * 7) Grow();
                InsertNoGrow(key + 1, value);
            }

        private:
            struct Slot {
                std::uint64_t key = 0;
                PackedValues value{};
            };

            [[nodiscard]] static std::uint64_t Hash(std::uint64_t x) noexcept {
                // SplitMix64 finalizer: 64-bit only, portable to WebAssembly.
                x ^= x >> 30;
                x *= UINT64_C(0xbf58476d1ce4e5b9);
                x ^= x >> 27;
                x *= UINT64_C(0x94d049bb133111eb);
                x ^= x >> 31;
                return x;
            }

            void Reset(const std::size_t capacity) {
                slots_.assign(capacity, {});
                mask_ = capacity - 1;
                size_ = 0;
            }

            void Grow() {
                std::vector<Slot> old = std::move(slots_);
                Reset(old.size() * 2);
                for (const Slot &slot: old) {
                    if (slot.key != 0) InsertNoGrow(slot.key, slot.value);
                }
            }

            void InsertNoGrow(const std::uint64_t storedKey, const PackedValues &value) noexcept {
                std::size_t index = Hash(storedKey) & mask_;
                for (;;) {
                    Slot &slot = slots_[index];
                    if (slot.key == 0) {
                        slot.key = storedKey;
                        slot.value = value;
                        ++size_;
                        return;
                    }
                    if (slot.key == storedKey) {
                        slot.value = value;
                        return;
                    }
                    index = (index + 1) & mask_;
                }
            }

            std::vector<Slot> slots_;
            std::size_t mask_ = 0;
            std::size_t size_ = 0;
        };

        // OptimisticWindowSolver keeps enemy special-charge state beside the compact
        // key because it changes ProcessSpecialCharge RNG consumption. Ally HP is
        // deliberately relaxed to max HP at each memo boundary (see
        // CanonicalizeOptimisticWindowState), so it is not part of this identity.
        class OptimisticFlatMemo {
        public:
            using PackedValues = std::array<std::uint16_t, 8>;

            OptimisticFlatMemo() { Reset(1u << 15); }

            [[nodiscard]] RNGFLOW_FORCE_INLINE bool Find(const std::uint64_t baseKey, const std::uint8_t extra,
                                                         PackedValues &value) const noexcept {
                const std::uint64_t storedKey = EncodeStoredKey(baseKey, extra);
                std::size_t index = Hash(storedKey) & mask_;
                for (;;) {
                    const std::uint64_t key = keys_[index];
                    if (key == 0) return false;
                    if (key == storedKey) {
                        value = values_[index];
                        return true;
                    }
                    index = (index + 1) & mask_;
                }
            }

            void Insert(const std::uint64_t baseKey, const std::uint8_t extra,
                        const PackedValues &value) {
                if ((size_ + 1) * 10 >= keys_.size() * 7) Grow();
                InsertNoGrow(EncodeStoredKey(baseKey, extra), value);
            }

        private:
            // MakeKey occupies bits 0..56. Fold the one-bit enemy-special-charge
            // discriminator into bit 57, then reserve zero as the empty sentinel.
            // Failed linear probes only touch keys_; the 16-byte value payload is
            // fetched after a key match. This directly targets the hottest memo path.
            [[nodiscard]] RNGFLOW_FORCE_INLINE static std::uint64_t EncodeStoredKey(
                const std::uint64_t baseKey, const std::uint8_t extra) noexcept {
                assert((baseKey >> 57) == 0);
                assert(extra <= 1);
                return (baseKey | (static_cast<std::uint64_t>(extra) << 57)) + 1;
            }

            [[nodiscard]] RNGFLOW_FORCE_INLINE static std::uint64_t Hash(std::uint64_t x) noexcept {
                x ^= x >> 30;
                x *= UINT64_C(0xbf58476d1ce4e5b9);
                x ^= x >> 27;
                x *= UINT64_C(0x94d049bb133111eb);
                x ^= x >> 31;
                return x;
            }

            void Reset(const std::size_t capacity) {
                keys_.assign(capacity, 0);
                values_.resize(capacity);
                mask_ = capacity - 1;
                size_ = 0;
            }

            void Grow() {
                std::vector<std::uint64_t> oldKeys = std::move(keys_);
                std::vector<PackedValues> oldValues = std::move(values_);
                Reset(oldKeys.size() * 2);
                for (std::size_t i = 0; i < oldKeys.size(); ++i) {
                    if (oldKeys[i] != 0) InsertNoGrow(oldKeys[i], oldValues[i]);
                }
            }

            void InsertNoGrow(const std::uint64_t storedKey,
                              const PackedValues &value) noexcept {
                std::size_t index = Hash(storedKey) & mask_;
                for (;;) {
                    std::uint64_t &key = keys_[index];
                    if (key == 0) {
                        key = storedKey;
                        values_[index] = value;
                        ++size_;
                        return;
                    }
                    if (key == storedKey) {
                        values_[index] = value;
                        return;
                    }
                    index = (index + 1) & mask_;
                }
            }

            std::vector<std::uint64_t> keys_;
            std::vector<PackedValues> values_;
            std::size_t mask_ = 0;
            std::size_t size_ = 0;
        };

        [[nodiscard]] RNGFLOW_FORCE_INLINE std::uint8_t OptimisticMemoExtra(const State &state) noexcept {
            return static_cast<std::uint8_t>(state.players[1].specialCharge ? 1 : 0);
        }

        class Solver {
        public:
            using Values = std::array<Value, 8>;

            Solver() {
                for (int position = 0; position < static_cast<int>(criticalPrefix_.size()) - 1; ++position) {
                    criticalPrefix_[position + 1] = criticalPrefix_[position]
                                                    + (lcg::peekPercent(std::max(1, position), kRngPercentScale) <
                                                       kAttackCriticalThreshold
                                                           ? 1
                                                           : 0);
                }
            }

            Value Solve(const State &rawState, const int turnsLeft) {
                const int herbs = std::clamp(rawState.players[0].medicinal_herbs_count, 0, 7);
                return SolveAll(rawState, turnsLeft)[herbs];
            }

            Values SolveAll(const State &rawState, const int turnsLeft) {
                State state = CanonicalizePlannerState(rawState);
                if (turnsLeft <= 0 || state.players[1].hp <= 0) {
                    Values terminal{};
                    for (Value &value: terminal) value = {0, -1};
                    return terminal;
                }

                // Herb count is a pure resource dimension.  Compute all 0..7 resource
                // levels together for this RNG-flow state so non-herb transitions are
                // shared instead of re-expanded eight times.
                state.players[0].medicinal_herbs_count = 7;

                const std::uint64_t key = MakeKey(state, turnsLeft);
                FlatMemo::PackedValues packed{};
                if (memo_.Find(key, packed)) return UnpackValues(packed);
                ++expanded_;

                std::array<int, 8> actions{};
                const std::size_t count = BuildCandidates(state, actions);
                Values best{};
                for (Value &value: best) value = {std::numeric_limits<int>::min(), -1};
                std::array<StepResult, 8> steps{};
                std::array<int, 8> optimistic{};
                std::array<std::size_t, 8> order{};
                for (std::size_t i = 0; i < count; ++i) {
                    steps[i] = Step(state, actions[i]);
                    const int delta = steps[i].state.position - state.position;
                    assert(delta >= 0 && delta <= kMaxProjectedRngPerTurn);
                    optimistic[i] = steps[i].criticalGain
                                    + CriticalSlotUpperBound(steps[i].state.position, turnsLeft - 1);
                    order[i] = i;
                }
                std::sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(count),
                          [&steps, &optimistic](const std::size_t lhs, const std::size_t rhs) {
                              if (optimistic[lhs] != optimistic[rhs]) {
                                  return optimistic[lhs] > optimistic[rhs];
                              }
                              if (steps[lhs].criticalGain != steps[rhs].criticalGain) {
                                  return steps[lhs].criticalGain > steps[rhs].criticalGain;
                              }
                              return steps[lhs].state.position < steps[rhs].state.position;
                          });

                for (std::size_t oi = 0; oi < count; ++oi) {
                    const std::size_t i = order[oi];
                    const StepResult &step = steps[i];
                    const bool herbAction = actions[i] == BattleEmulator::MEDICINAL_HERBS;
                    bool canImprove = false;
                    for (int herbs = 0; herbs <= 7; ++herbs) {
                        if (herbAction && herbs == 0) continue;
                        if (best[herbs].criticals < optimistic[i]) {
                            canImprove = true;
                            break;
                        }
                    }
                    if (!canImprove) continue;

                    const Values tail = SolveAll(step.state, turnsLeft - 1);
                    for (int herbs = 0; herbs <= 7; ++herbs) {
                        if (herbAction && herbs == 0) continue;
                        if (best[herbs].criticals >= optimistic[i]) continue;
                        const int tailHerbs = herbAction ? herbs - 1 : herbs;
                        Value candidate{
                            step.criticalGain + tail[tailHerbs].criticals,
                            actions[i]
                        };
                        if (Better(candidate, best[herbs])) best[herbs] = candidate;
                    }
                }
                memo_.Insert(key, PackValues(best));
                return best;
            }

            [[nodiscard]] int BestAction(const State &rawState, const int turnsLeft) const {
                const State state = CanonicalizePlannerState(rawState);
                const int herbs = std::clamp(rawState.players[0].medicinal_herbs_count, 0, 7);
                FlatMemo::PackedValues packed{};
                return memo_.Find(MakeKey(state, turnsLeft), packed) ? UnpackValue(packed[herbs]).bestAction : -1;
            }

            [[nodiscard]] std::uint64_t Expanded() const noexcept { return expanded_; }

            bool ExtractFeasible(const State &state, const int turnsLeft, const int criticalsNeeded,
                                 std::array<int, kMaxPlanTurns> &actions, int &actionCount,
                                 std::uint64_t &visited) {
                ++visited;
                if (state.players[0].hp <= 0) return false;
                if (state.players[1].hp <= 0) return criticalsNeeded <= 0;
                if (turnsLeft <= 0) return criticalsNeeded <= 0;

                std::array<int, 8> candidates{};
                const std::size_t count = BuildCandidates(state, candidates);
                struct Candidate {
                    int action = -1;
                    int upper = -1;
                    int idlePriority = 100;
                    StepResult step{};
                };
                std::array<Candidate, 8> ranked{};
                std::size_t rankedCount = 0;
                for (std::size_t i = 0; i < count; ++i) {
                    StepResult step = Step(state, candidates[i]);
                    if (!step.heroAlive) continue;
                    int tailUpper = 0;
                    if (criticalsNeeded > 0 && turnsLeft > 1 && step.enemyAlive) {
                        tailUpper = Solve(step.state, turnsLeft - 1).criticals;
                    }
                    const int idlePriority = [&]() {
                        switch (candidates[i]) {
                            case BattleEmulator::DEFENCE: return 0;
                            case BattleEmulator::ATTACK_ALLY: return 1;
                            case BattleEmulator::FLEE_ALLY: return 2;
                            case BattleEmulator::DRAGON_SLASH: return 3;
                            case BattleEmulator::ACROBATIC_STAR: return 4;
                            case BattleEmulator::MEDICINAL_HERBS: return 5;
                            case BattleEmulator::HEAL: return 6;
                            case BattleEmulator::CRACK_ALLY: return 7;
                            default: return 8;
                        }
                    }();
                    ranked[rankedCount++] = {
                        candidates[i], step.criticalGain + tailUpper, idlePriority, step
                    };
                }
                std::sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(rankedCount),
                          [criticalsNeeded](const Candidate &a, const Candidate &b) {
                              if (criticalsNeeded <= 0 && a.idlePriority != b.idlePriority) {
                                  return a.idlePriority < b.idlePriority;
                              }
                              if (a.upper != b.upper) return a.upper > b.upper;
                              if (a.step.criticalGain != b.step.criticalGain) {
                                  return a.step.criticalGain > b.step.criticalGain;
                              }
                              return a.step.state.position < b.step.state.position;
                          });

                for (std::size_t i = 0; i < rankedCount; ++i) {
                    const Candidate &candidate = ranked[i];
                    if (criticalsNeeded > 0 && candidate.upper < criticalsNeeded) break;
                    const int savedCount = actionCount;
                    actions[actionCount++] = candidate.action;
                    const int remaining = std::max(0, criticalsNeeded - candidate.step.criticalGain);
                    if ((!candidate.step.enemyAlive && remaining == 0) ||
                        (candidate.step.enemyAlive &&
                         ExtractFeasible(candidate.step.state, turnsLeft - 1, remaining,
                                         actions, actionCount, visited))) {
                        return true;
                    }
                    actionCount = savedCount;
                }
                return false;
            }

        private:
            [[nodiscard]] int CriticalSlotUpperBound(const int position, const int turnsLeft) const noexcept {
                if (turnsLeft <= 0) return 0;
                if (position <= 0 || position >= 4998) return turnsLeft * 2;
                const int end = position + turnsLeft * kMaxProjectedRngPerTurn + 1;
                if (end >= 4998) return turnsLeft * 2;
                const int slots = criticalPrefix_[end] - criticalPrefix_[position];
                return std::min(turnsLeft * 2, slots);
            }

            [[nodiscard]] static std::uint16_t PackValue(const Value value) noexcept {
                assert(value.criticals >= 0 && value.criticals <= 31);
                assert(value.bestAction >= 0 && value.bestAction <= 0x3ff);
                return static_cast<std::uint16_t>((value.criticals << 10) | value.bestAction);
            }

            [[nodiscard]] static Value UnpackValue(const std::uint16_t packed) noexcept {
                return {static_cast<int>(packed >> 10), static_cast<int>(packed & 0x3ff)};
            }

            [[nodiscard]] static FlatMemo::PackedValues PackValues(const Values &values) noexcept {
                FlatMemo::PackedValues packed{};
                for (int herbs = 0; herbs <= 7; ++herbs) packed[herbs] = PackValue(values[herbs]);
                return packed;
            }

            [[nodiscard]] static Values UnpackValues(const FlatMemo::PackedValues &packed) noexcept {
                Values values{};
                for (int herbs = 0; herbs <= 7; ++herbs) values[herbs] = UnpackValue(packed[herbs]);
                return values;
            }

            FlatMemo memo_;
            std::array<std::uint16_t, 5000> criticalPrefix_{};
            std::uint64_t expanded_ = 0;
        };
    } // namespace

    State FromSearchState(const BattleEmulator::SearchState &state) noexcept {
        State result{};
        const auto copyPlayer = [](const Player &src) {
            RngPlayer dst{};
            dst.hp = src.hp;
            dst.maxHp = src.maxHp;
            dst.atk = src.atk;
            dst.defaultATK = src.defaultATK;
            dst.def = src.def;
            dst.speed = src.speed;
            dst.mp = src.mp;
            dst.maxMp = src.maxMp;
            dst.specialCharge = src.specialCharge;
            dst.specialChargeTurn = src.specialChargeTurn;
            dst.paralysis = src.paralysis;
            dst.paralysisLevel = src.paralysisLevel;
            dst.paralysisTurns = src.paralysisTurns;
            dst.sleeping = src.sleeping;
            dst.defence = src.defence;
            dst.rage = src.rage;
            dst.acrobaticStar = src.acrobaticStar;
            dst.acrobaticStarTurn = src.acrobaticStarTurn;
            dst.rageTurns = src.rageTurns;
            dst.medicinal_herbs_count = src.medicinal_herbs_count;
            dst.inactive = src.inactive;
            return dst;
        };
        result.players[0] = copyPlayer(state.players[0]);
        result.players[1] = copyPlayer(state.players[1]);
        result.position = state.position;
        result.cameraCounter = static_cast<std::uint8_t>((state.nowState >> 8) & 0x0f);
        return result;
    }

    std::size_t LegalActions(const State &state, std::array<int, 8> &actions) noexcept {
        return BuildCandidates(state, actions);
    }

    StepResult Step(const State &source, const int heroAction) {
        StepResult result{};
        result.state = source;
        State &state = result.state;
        RngPlayer &hero = state.players[0];
        RngPlayer &enemy = state.players[1];
        if (hero.hp <= 0 || enemy.hp <= 0) {
            result.heroAlive = hero.hp > 0;
            result.enemyAlive = enemy.hp > 0;
            return result;
        }

        --hero.specialChargeTurn;
        if (hero.specialChargeTurn == -1) hero.specialCharge = false;
        hero.defence = 1.0;

        const bool heroFirst = ConsumeTurnOrder(state, hero.speed, enemy.speed);

        int selectedHeroAction = heroAction;
        if (selectedHeroAction == BattleEmulator::HEAL && hero.mp <= 0) {
            selectedHeroAction = hero.medicinal_herbs_count >= 1
                                     ? BattleEmulator::MEDICINAL_HERBS
                                     : BattleEmulator::ATTACK_ALLY;
        }
        if (selectedHeroAction == BattleEmulator::INACTIVE_ALLY ||
            selectedHeroAction == BattleEmulator::PARALYSIS) {
            selectedHeroAction = BattleEmulator::ATTACK_ALLY;
        }
        if (selectedHeroAction == BattleEmulator::DEFENCE) hero.defence = 0.5;

        std::array<int, 2> executionActions{};
        int executionIndex = 0;
        for (int slot = 0; slot < 2; ++slot) {
            if (hero.hp <= 0 || enemy.hp <= 0) break;
            const bool enemyTurn = (slot == 0 && !heroFirst) || (slot == 1 && heroFirst);
            if (enemyTurn) {
                const int action = SelectEnemyAction(state);
                const ActionResult actionResult = CallAction(state, action, 1, 0);
                executionActions[executionIndex++] = action;
                RngPlayer::reduceHp(hero, actionResult.damage);
                result.criticalGain += actionResult.criticalGain;
                if (hero.hp > 0 && enemy.hp > 0) ++state.position;
                else break;
                if (enemy.rage) {
                    --enemy.rageTurns;
                    if (enemy.rageTurns <= 0) enemy.rage = false;
                }
            } else {
                int action = selectedHeroAction;
                const bool skipTurn = action == BattleEmulator::FLEE_ALLY;
                if (!skipTurn) {
                    if (!hero.paralysis) {
                        ++state.position;
                    } else {
                        action = BattleEmulator::PARALYSIS;
                        --hero.paralysisTurns;
                        if (hero.paralysisTurns <= 0) {
                            static constexpr double table[4] = {0.6250, 0.7500, 0.8750, 1.0000};
                            const int index = std::min(3, std::abs(hero.paralysisTurns));
                            const double probability = ConsumePercent(state, 100) * 0.01;
                            if (table[index] >= probability) {
                                hero.paralysis = false;
                                hero.paralysisLevel = 0;
                                action = BattleEmulator::CURE_PARALYSIS;
                            }
                        } else {
                            ++state.position;
                        }
                    }
                    if (hero.inactive) {
                        hero.inactive = false;
                        if (action != BattleEmulator::CURE_PARALYSIS && action != BattleEmulator::PARALYSIS) {
                            action = BattleEmulator::INACTIVE_ALLY;
                        }
                    }

                    const ActionResult actionResult = CallAction(state, action, 0, 1);
                    executionActions[executionIndex++] = action;
                    result.criticalGain += actionResult.criticalGain;
                    if (IsHealingAction(action)) RngPlayer::heal(hero, actionResult.damage);
                    else RngPlayer::reduceHp(enemy, actionResult.damage);

                    if (hero.hp > 0 && enemy.hp > 0) {
                        ++state.position;
                        --hero.acrobaticStarTurn;
                        if (hero.acrobaticStar && hero.acrobaticStarTurn == 0) {
                            hero.acrobaticStar = false;
                            ++state.position;
                        }
                    }
                }
            }
        }

        if (hero.hp > 0 && enemy.hp > 0) ++state.position;
        CameraStep(state, executionActions);
        result.heroAlive = hero.hp > 0;
        result.enemyAlive = enemy.hp > 0;
        return result;
    }

    TapeSummary AnalyzeTape(const int beginPosition, const int endPosition) {
        TapeSummary summary{};
        int previousAttack = -1;
        int previousDragon = -1;
        for (int position = beginPosition; position < endPosition; ++position) {
            const int roll = lcg::peekPercent(position, kRngPercentScale);
            if (roll < kAttackCriticalThreshold) {
                ++summary.attackCriticalSlots;
                if (previousAttack >= 0) {
                    const int gap = position - previousAttack;
                    summary.attackGapGcd = summary.attackGapGcd == 0
                                               ? gap
                                               : std::gcd(summary.attackGapGcd, gap);
                }
                previousAttack = position;
            }
            if (roll < kDragonCriticalThreshold) {
                ++summary.dragonCriticalSlots;
                if (previousDragon >= 0) {
                    const int gap = position - previousDragon;
                    summary.dragonGapGcd = summary.dragonGapGcd == 0
                                               ? gap
                                               : std::gcd(summary.dragonGapGcd, gap);
                }
                previousDragon = position;
            }
        }
        return summary;
    }

    Plan FindMaxCriticalPlan(const State &root, const int maxTurns) {
        Plan plan{};
        const int turns = std::clamp(maxTurns, 0, kMaxPlanTurns);
        const int tapeEnd = std::min(4997, root.position + std::max(64, turns * 96));
        plan.tape = AnalyzeTape(root.position, tapeEnd);
        if (turns == 0 || root.players[0].hp <= 0 || root.players[1].hp <= 0) return plan;

        Solver solver;
        const Value best = solver.Solve(root, turns);
        plan.optimisticCriticals = best.criticals;
        plan.expandedStates = solver.Expanded();

        // Ally HP is deliberately absent from the expensive memo.  Use that DP as
        // an admissible critical-count upper bound, then extract an actually
        // executable path with real ally HP.  A branch is rejected only on the
        // turn it really dies; HP never becomes a memo-state explosion dimension.
        for (int target = best.criticals; target >= 0; --target) {
            plan.actionCount = 0;
            if (solver.ExtractFeasible(root, turns, target,
                                       plan.actions, plan.actionCount,
                                       plan.feasibilityStates)) {
                plan.maxCriticals = target;
                break;
            }
        }
        return plan;
    }

    int ExactCriticalUpperBound(const State &root, const int maxTurns,
                                std::uint64_t *const newlyExpanded) {
        const int turns = std::clamp(maxTurns, 0, kMaxPlanTurns);
        if (newlyExpanded != nullptr) *newlyExpanded = 0;
        if (turns <= 0 || root.players[0].hp <= 0 || root.players[1].hp <= 0) return 0;

        static thread_local std::uint64_t cachedGeneration = 0;
        static thread_local std::unique_ptr<Solver> solver;
        const std::uint64_t generation = lcg::generation();
        if (!solver || cachedGeneration != generation) {
            solver = std::make_unique<Solver>();
            cachedGeneration = generation;
        }

        const std::uint64_t before = solver->Expanded();
        const int bound = solver->Solve(root, turns).criticals;
        if (newlyExpanded != nullptr) *newlyExpanded = solver->Expanded() - before;
        return bound;
    }

    int StaticCriticalSlotUpperBound(const int position, const int maxTurns) {
        const int turns = std::clamp(maxTurns, 0, kMaxPlanTurns);
        if (turns <= 0) return 0;
        if (position <= 0 || position >= 4999) return turns * 2;

        static thread_local std::uint64_t cachedGeneration = 0;
        static thread_local std::array<std::uint16_t, 5000> prefix{};
        static thread_local std::array<std::array<std::uint8_t, 5000>, kMaxPlanTurns + 1> upper{};
        const std::uint64_t generation = lcg::generation();
        if (cachedGeneration != generation) {
            prefix.fill(0);
            for (int p = 0; p < 4999; ++p) {
                prefix[p + 1] = static_cast<std::uint16_t>(
                    prefix[p] +
                    (lcg::peekPercent(std::max(1, p), kRngPercentScale) < kAttackCriticalThreshold ? 1 : 0));
            }

            for (auto &row: upper) row.fill(0);
            for (int t = 1; t <= kMaxPlanTurns; ++t) {
                for (int p = 4998; p >= 1; --p) {
                    int best = 0;
                    bool crossedPrecalcLimit = false;
                    for (int delta = kStaticMinRngPerCompletedTurn;
                         delta <= kStaticMaxRngPerCompletedTurn; ++delta) {
                        const int q = p + delta;
                        if (q >= 4999) {
                            crossedPrecalcLimit = true;
                            break;
                        }

                        // Every player critical consumes a roll below the normal
                        // attack 2% threshold.  In Step(), player-owned critical
                        // checks can only occur at offsets 7..30 from the turn
                        // start: own Attack/Dragon/Heal/Crack checks, plus the
                        // latest possible Acrobatic counter after a hero-first
                        // long action.  State-dependent feasibility is deliberately
                        // ignored, but unrelated camera/rage tail slots are no
                        // longer counted as if they could be critical checks.
                        const int criticalBegin = p + kStaticMinPlayerCriticalOffset;
                        const int criticalEnd = std::min(q, p + kStaticMaxPlayerCriticalOffset + 1);
                        const int slots = criticalEnd <= criticalBegin
                                              ? 0
                                              : static_cast<int>(prefix[criticalEnd]) - static_cast<int>(prefix[
                                                    criticalBegin]);
                        const int thisTurn = std::min(2, slots);
                        best = std::max(best, thisTurn + static_cast<int>(upper[t - 1][q]));
                    }

                    // The precomputed LCG tape ends at 4999.  If even one relaxed
                    // transition can leave it, fall back to the trivial 2/turn
                    // bound instead of risking an underestimate.
                    upper[t][p] = static_cast<std::uint8_t>(
                        crossedPrecalcLimit ? t * 2 : best);
                }
            }
            cachedGeneration = generation;
        }
        return upper[turns][position];
    }

    namespace {
        // Full-horizon optimistic damage DP over the fixed LCG tape.  Unlike the
        // old hit-count + critical-count envelope, each virtual turn keeps the
        // damage allowance attached to the same relaxed next RNG position q.
        //
        // star=0 means Acrobatic Star is inactive.  star=1..6 is the exact
        // turn-boundary timer when it is active.  The relaxation deliberately
        // assumes a special charge is always obtainable when Star is inactive,
        // every active-Star enemy action can counter, and every RNG delta in
        // [12,48] is reachable.  Those additions only create fake continuations;
        // no real continuation is removed.
        struct StaticDamageDpCache {
            std::uint64_t generation = 0;
            int nonCriticalHitUpper = -1;
            int criticalHitUpper = -1;
            int computedTurns = 0;
            std::array<std::uint16_t, 5000> criticalPrefix{};
            std::array<std::array<std::array<std::uint16_t, 5000>, 7>, kMaxPlanTurns + 1> upper{};
        };

        [[nodiscard]] int StaticTapedDamageUpper(const int position,
                                                 const int maxTurns,
                                                 const int starTurns,
                                                 const int nonCriticalHitUpper,
                                                 const int criticalHitUpper) {
            const int turns = std::clamp(maxTurns, 0, kMaxPlanTurns);
            if (turns <= 0) return 0;
            if (position <= 0 || position >= 4999) return turns * 2 * criticalHitUpper;
            assert(starTurns >= 0 && starTurns <= 6);
            assert(nonCriticalHitUpper >= 0 && criticalHitUpper >= nonCriticalHitUpper);

            static thread_local StaticDamageDpCache cache{};
            const std::uint64_t generation = lcg::generation();
            if (cache.generation != generation ||
                cache.nonCriticalHitUpper != nonCriticalHitUpper ||
                cache.criticalHitUpper != criticalHitUpper) {
                cache.generation = generation;
                cache.nonCriticalHitUpper = nonCriticalHitUpper;
                cache.criticalHitUpper = criticalHitUpper;
                cache.computedTurns = 0;
                cache.criticalPrefix.fill(0);
                for (int p = 0; p < 4999; ++p) {
                    cache.criticalPrefix[p + 1] = static_cast<std::uint16_t>(
                        cache.criticalPrefix[p] +
                        (lcg::peekPercent(std::max(1, p), kRngPercentScale) <
                             kAttackCriticalThreshold
                             ? 1
                             : 0));
                }
                for (auto &starRow: cache.upper[0]) starRow.fill(0);
            }

            const int criticalBonusUpper = criticalHitUpper - nonCriticalHitUpper;
            while (cache.computedTurns < turns) {
                const int t = ++cache.computedTurns;
                const int trivialUpper = t * 2 * criticalHitUpper;
                for (int p = 4998; p >= 1; --p) {
                    // If even one relaxed completed-turn transition can leave the
                    // precomputed tape, keep the bound deliberately trivial.  The
                    // exact solver would otherwise have to report UNKNOWN when it
                    // reaches such a live state, so the bound must never prune it.
                    if (p + kStaticMaxRngPerCompletedTurn >= 4999) {
                        for (int star = 0; star <= 6; ++star) {
                            cache.upper[t][star][p] = static_cast<std::uint16_t>(trivialUpper);
                        }
                        continue;
                    }

                    std::array<int, 7> best{};
                    for (int delta = kStaticMinRngPerCompletedTurn;
                         delta <= kStaticMaxRngPerCompletedTurn; ++delta) {
                        const int q = p + delta;
                        const int criticalBegin = p + kStaticMinPlayerCriticalOffset;
                        const int criticalEnd = std::min(q, p + kStaticMaxPlayerCriticalOffset + 1);
                        const int slots = criticalEnd <= criticalBegin
                                              ? 0
                                              : static_cast<int>(cache.criticalPrefix[criticalEnd]) -
                                                    static_cast<int>(cache.criticalPrefix[criticalBegin]);

                        const int oneHitDamage = nonCriticalHitUpper +
                                                 criticalBonusUpper * std::min(1, slots);
                        const int twoHitDamage = nonCriticalHitUpper * 2 +
                                                 criticalBonusUpper * std::min(2, slots);

                        // Star inactive: either use a one-hit hero command and
                        // stay inactive, or spend the hero command activating Star.
                        // Activation itself deals no damage, but optimistically allow
                        // one same-turn counter; CallAction then leaves timer=5 at
                        // the next turn boundary.
                        best[0] = std::max(
                            best[0],
                            oneHitDamage + std::max(static_cast<int>(cache.upper[t - 1][0][q]),
                                                    static_cast<int>(cache.upper[t - 1][5][q])));

                        for (int star = 1; star <= 6; ++star) {
                            // A damaging hero command can coexist with one optimistic
                            // counter and advances the Star timer.  FLEE can preserve
                            // the timer in the real transition, so keep a separate
                            // one-counter preserve option as well.
                            const int damageAndAdvance =
                                twoHitDamage + static_cast<int>(cache.upper[t - 1][star - 1][q]);
                            const int fleeAndPreserve =
                                oneHitDamage + static_cast<int>(cache.upper[t - 1][star][q]);
                            best[star] = std::max(best[star],
                                                  std::max(damageAndAdvance, fleeAndPreserve));
                        }
                    }
                    for (int star = 0; star <= 6; ++star) {
                        assert(best[star] <= std::numeric_limits<std::uint16_t>::max());
                        cache.upper[t][star][p] = static_cast<std::uint16_t>(best[star]);
                    }
                }
            }
            return cache.upper[turns][starTurns][position];
        }
    } // namespace

    int StaticOptimisticKillTurns(const State &root, const int maxTurns) {
        const int turns = std::clamp(maxTurns, 0, kMaxPlanTurns);
        const int enemyHp = std::max(0, root.players[1].hp);
        if (enemyHp <= 0) return 0;
        if (turns <= 0) return 1;

        // Safe damage envelope for the supported yo2 player actions.
        // PhysicalDamage2Rng is strictly below atk1 + spread + 1.  Round upward
        // here on purpose; this is an upper bound, not a damage prediction.
        const double atk1 = root.players[0].atk * 0.5 - root.players[1].def * 0.25;
        const double spread = std::max(0.0, atk1) * 0.0625;
        const double physicalUpperExclusive = std::max(0.0, atk1) + spread + 1.0;
        const int physicalNonCriticalUpper = std::max(
            0, static_cast<int>(std::ceil(physicalUpperExclusive)) - 1);
        // CRACK's non-critical damage is at most floor(34 * 0.5) = 17.
        const int nonCriticalHitUpper = std::max(17, physicalNonCriticalUpper);
        // Normal ATTACK / COUNTER criticals are strictly below atk*1.05; ceil is
        // deliberately one-sided and therefore safe. Dragon/Crack criticals are
        // smaller for this supported profile.
        const double criticalUpperExclusive = std::max(0, root.players[0].atk) * 1.05;
        const int criticalHitUpper = std::max(
            nonCriticalHitUpper,
            std::max(0, static_cast<int>(std::ceil(criticalUpperExclusive)) - 1));
        const RngPlayer &hero = root.players[0];
        if (hero.acrobaticStar &&
            (hero.acrobaticStarTurn <= 0 || hero.acrobaticStarTurn > 6)) {
            // A malformed/out-of-domain active timer is not allowed to become an
            // UNSAT shortcut.  Fall back to the trivial two-critical-hits/turn
            // envelope; the reversible codec will separately reject unsupported
            // live states as UNKNOWN.
            for (int t = 1; t <= turns; ++t) {
                if (t * 2 * criticalHitUpper >= enemyHp) return t;
            }
            return turns + 1;
        }
        const int starTurns = hero.acrobaticStar ? hero.acrobaticStarTurn : 0;

        for (int t = 1; t <= turns; ++t) {
            const int damageUpper = StaticTapedDamageUpper(
                root.position, t, starTurns, nonCriticalHitUpper, criticalHitUpper);
            if (damageUpper >= enemyHp) return t;
        }
        return turns + 1;
    }

    // Tighten the position-only static tail by keeping the first tail turn on the
    // exact projected state.  Every legal projected action is retained, and only
    // after that exact transition do we relax to StaticCriticalSlotUpperBound().
    // Therefore this can only lower the old static upper bound without removing a
    // real continuation from the optimistic set.
    int OneStepCriticalUpperBound(const State &root, const int maxTurns) {
        const int turns = std::clamp(maxTurns, 0, kMaxPlanTurns);
        if (turns <= 0 || root.players[0].hp <= 0 || root.players[1].hp <= 0) return 0;

        std::array<int, 8> actions{};
        const std::size_t count = BuildCandidates(root, actions);
        int best = 0;
        for (std::size_t i = 0; i < count; ++i) {
            const StepResult step = Step(root, actions[i]);
            int candidate = step.criticalGain;
            if (turns > 1 && step.heroAlive && step.enemyAlive) {
                candidate += StaticCriticalSlotUpperBound(step.state.position, turns - 1);
            }
            best = std::max(best, candidate);
        }
        return best;
    }

    namespace {
        class OptimisticWindowSolver {
        public:
            explicit OptimisticWindowSolver(const int tailTurns) : tailTurns_(tailTurns) {
            }

            OptimisticBoundResult Solve(const State &rawState, const int windowTurns) {
                const int herbs = std::clamp(rawState.players[0].medicinal_herbs_count, 0, 7);
                return UnpackBound(SolveAll(rawState, windowTurns)[herbs]);
            }

            [[nodiscard]] std::uint64_t Expanded() const noexcept { return expanded_; }

            bool TryCached(const State &rawState, const int windowTurns,
                           OptimisticBoundResult &result) const {
                const int herbs = std::clamp(rawState.players[0].medicinal_herbs_count, 0, 7);
                State state = CanonicalizeOptimisticWindowState(rawState);
                if (state.players[1].hp <= 0) {
                    result = {0, 0};
                    return true;
                }
                if (windowTurns <= 0) {
                    result = {
                        StaticCriticalSlotUpperBound(state.position, tailTurns_),
                        StaticOptimisticKillTurns(state, tailTurns_)
                    };
                    return true;
                }
                state.players[0].medicinal_herbs_count = 7;
                OptimisticFlatMemo::PackedValues packed{};
                if (!memo_.Find(MakeKey(state, windowTurns), OptimisticMemoExtra(state), packed)) return false;
                result = UnpackBound(packed[herbs]);
                return true;
            }

        private:
            using Bounds = std::array<std::uint16_t, 8>;

            [[nodiscard]] RNGFLOW_FORCE_INLINE static std::uint16_t PackBound(
                const OptimisticBoundResult value) noexcept {
                assert(value.criticalUpper >= 0 && value.criticalUpper <= 63);
                assert(value.optimisticKillTurns >= 0 && value.optimisticKillTurns <= 63);
                return static_cast<std::uint16_t>((value.criticalUpper << 6) | value.optimisticKillTurns);
            }

            [[nodiscard]] RNGFLOW_FORCE_INLINE static OptimisticBoundResult UnpackBound(
                const std::uint16_t packed) noexcept {
                return {static_cast<int>(packed >> 6), static_cast<int>(packed & 63)};
            }

            [[nodiscard]] RNGFLOW_FORCE_INLINE static OptimisticBoundResult MergeBound(
                const OptimisticBoundResult &candidate,
                const OptimisticBoundResult &current) noexcept {
                return {
                    std::max(candidate.criticalUpper, current.criticalUpper),
                    std::min(candidate.optimisticKillTurns, current.optimisticKillTurns)
                };
            }

            static constexpr std::size_t kLeafCacheSize = 1u << 15;

            [[nodiscard]] RNGFLOW_FORCE_INLINE static std::uint64_t LeafHash(std::uint64_t x) noexcept {
                x ^= x >> 30;
                x *= UINT64_C(0xbf58476d1ce4e5b9);
                x ^= x >> 27;
                x *= UINT64_C(0x94d049bb133111eb);
                x ^= x >> 31;
                return x;
            }

            [[nodiscard]] std::uint16_t CachedStaticTail(const State &state) {
                // MakeKey contains every field used by StaticOptimisticKillTurns and
                // StaticCriticalSlotUpperBound (plus harmless extra state). tailTurns_
                // is fixed for this solver instance, so an exact key match can safely
                // reuse the packed leaf bound. Direct-map collisions are misses only.
                const std::uint64_t storedKey = MakeKey(state, 0) + 1;
                const std::size_t index = LeafHash(storedKey) & (kLeafCacheSize - 1);
                if (leafKeys_[index] == storedKey) return leafValues_[index];
                const int critical = StaticCriticalSlotUpperBound(state.position, tailTurns_);
                const std::uint16_t packed = PackBound({
                    critical, StaticOptimisticKillTurns(state, tailTurns_)
                });
                leafKeys_[index] = storedKey;
                leafValues_[index] = packed;
                return packed;
            }

            Bounds SolveAll(const State &rawState, const int windowTurns) {
                State state = CanonicalizeOptimisticWindowState(rawState);
                Bounds terminal{};
                if (state.players[1].hp <= 0) {
                    terminal.fill(PackBound({0, 0}));
                    return terminal;
                }
                if (windowTurns <= 0) {
                    terminal.fill(CachedStaticTail(state));
                    return terminal;
                }

                state.players[0].medicinal_herbs_count = 7;
                const std::uint64_t key = MakeKey(state, windowTurns);
                const std::uint8_t extra = OptimisticMemoExtra(state);
                OptimisticFlatMemo::PackedValues packed{};
                if (memo_.Find(key, extra, packed)) return packed;
                ++expanded_;

                Bounds best{};
                best.fill(PackBound({0, windowTurns + tailTurns_ + 1}));
                std::array<int, 8> actions{};
                const std::size_t count = BuildCandidates(state, actions);
                for (std::size_t i = 0; i < count; ++i) {
                    const bool herbAction = actions[i] == BattleEmulator::MEDICINAL_HERBS;
                    const StepResult step = Step(state, actions[i]);
                    Bounds tail{};
                    if (step.enemyAlive) {
                        tail = SolveAll(step.state, windowTurns - 1);
                    } else {
                        tail.fill(PackBound({0, 0}));
                    }
                    for (int herbs = 0; herbs <= 7; ++herbs) {
                        if (herbAction && herbs == 0) continue;
                        const int tailHerbs = herbAction ? herbs - 1 : herbs;
                        const OptimisticBoundResult tailBound = UnpackBound(tail[tailHerbs]);
                        OptimisticBoundResult candidate{
                            step.criticalGain + tailBound.criticalUpper,
                            !step.enemyAlive
                                ? 1
                                : std::min(windowTurns + tailTurns_ + 1,
                                           1 + tailBound.optimisticKillTurns)
                        };
                        const OptimisticBoundResult current = UnpackBound(best[herbs]);
                        best[herbs] = PackBound(MergeBound(candidate, current));
                    }
                }
                memo_.Insert(key, extra, best);
                return best;
            }

            int tailTurns_ = 0;
            OptimisticFlatMemo memo_;
            std::array<std::uint64_t, kLeafCacheSize> leafKeys_{};
            std::array<std::uint16_t, kLeafCacheSize> leafValues_{};
            std::uint64_t expanded_ = 0;
        };

        struct OptimisticSolverCache {
            std::uint64_t generation = 0;
            std::array<std::unique_ptr<OptimisticWindowSolver>, kMaxPlanTurns + 1> solvers{};
        };

        OptimisticSolverCache &GetOptimisticSolverCache() {
            static thread_local OptimisticSolverCache cache{};
            const std::uint64_t generation = lcg::generation();
            if (cache.generation != generation) {
                for (auto &solver: cache.solvers) solver.reset();
                cache.generation = generation;
            }
            return cache;
        }

        OptimisticWindowSolver &GetOptimisticWindowSolver(const int tailTurns) {
            OptimisticSolverCache &cache = GetOptimisticSolverCache();
            if (!cache.solvers[tailTurns]) {
                cache.solvers[tailTurns] = std::make_unique<OptimisticWindowSolver>(tailTurns);
            }
            return *cache.solvers[tailTurns];
        }
    } // namespace

    OptimisticBoundResult OptimisticCriticalUpperBoundDetailed(
        const State &root, const int windowTurns, const int remainingTurns,
        std::uint64_t *const newlyExpanded) {
        const int remaining = std::clamp(remainingTurns, 0, kMaxPlanTurns);
        const int window = std::clamp(windowTurns, 0, remaining);
        if (newlyExpanded != nullptr) *newlyExpanded = 0;
        if (remaining <= 0 || root.players[0].hp <= 0 || root.players[1].hp <= 0) {
            return {0, root.players[1].hp <= 0 ? 0 : remaining + 1};
        }

        const int tailTurns = remaining - window;
        OptimisticWindowSolver &solver = GetOptimisticWindowSolver(tailTurns);
        const std::uint64_t before = solver.Expanded();
        const OptimisticBoundResult bound = solver.Solve(root, window);
        if (newlyExpanded != nullptr) *newlyExpanded = solver.Expanded() - before;
        return bound;
    }

    bool TryCachedOptimisticCriticalUpperBoundDetailed(
        const State &root, const int windowTurns, const int remainingTurns,
        OptimisticBoundResult &result) {
        const int remaining = std::clamp(remainingTurns, 0, kMaxPlanTurns);
        const int window = std::clamp(windowTurns, 0, remaining);
        if (remaining <= 0 || root.players[0].hp <= 0 || root.players[1].hp <= 0) {
            result = {0, root.players[1].hp <= 0 ? 0 : remaining + 1};
            return true;
        }
        const int tailTurns = remaining - window;
        OptimisticSolverCache &cache = GetOptimisticSolverCache();
        if (!cache.solvers[tailTurns]) return false;
        return cache.solvers[tailTurns]->TryCached(root, window, result);
    }

    int OptimisticCriticalUpperBound(const State &root,
                                     const int windowTurns,
                                     const int remainingTurns,
                                     std::uint64_t *const newlyExpanded) {
        return OptimisticCriticalUpperBoundDetailed(
            root, windowTurns, remainingTurns, newlyExpanded).criticalUpper;
    }

    namespace {
        // Reversible turn-boundary encoding for the supported yo2 projected model.
        // This is not a hash: equality of the 64-bit values means equality of every
        // dynamic field that can affect a later rngflow::Step() in this profile.
        // Fields that are deliberately omitted are either immutable (copied from the
        // root template) or dead while their enable flag is false.
        class ExactStateCodec {
        public:
            explicit ExactStateCodec(State root, const bool relaxHeroHp)
                : base_(root), relaxHeroHp_(relaxHeroHp) {
                CanonicalizeDeadFields(base_);
                if (relaxHeroHp_) base_.players[0].hp = base_.players[0].maxHp;
            }

            [[nodiscard]] bool Pack(State state, std::uint64_t &key) const noexcept {
                CanonicalizeDeadFields(state);
                if (relaxHeroHp_) state.players[0].hp = state.players[0].maxHp;
                const RngPlayer &hero = state.players[0];
                const RngPlayer &enemy = state.players[1];

                if (state.position < 1 || state.position >= 5000) return false;
                if (hero.hp < 0 || hero.hp > 65) return false;
                if (enemy.hp < 0 || enemy.hp > 456) return false;
                if (hero.mp < 0 || hero.mp > 31) return false;
                if (hero.medicinal_herbs_count < 0 || hero.medicinal_herbs_count > 7) return false;
                if (hero.specialCharge && (hero.specialChargeTurn < 0 || hero.specialChargeTurn > 6)) return false;
                if (hero.paralysis && (hero.paralysisTurns < -2 || hero.paralysisTurns > 4)) return false;
                if (hero.paralysisLevel < 0 || hero.paralysisLevel > 31) return false;
                if (hero.acrobaticStar && (hero.acrobaticStarTurn < 0 || hero.acrobaticStarTurn > 6)) return false;
                if (enemy.rage && (enemy.rageTurns < 1 || enemy.rageTurns > 4)) return false;
                if (state.cameraCounter > 5) return false;

                key = 0;
                int shift = 0;
                const auto put = [&key, &shift](const std::uint64_t value, const int bits) {
                    assert(bits > 0 && shift + bits <= 64);
                    assert((value & ~BitMask(bits)) == 0);
                    key |= value << shift;
                    shift += bits;
                };

                // Keep all three monotone resources at the low end.  This is still a
                // reversible state encoding.  Making HP the most-significant member of
                // this 15-bit suffix lets a descending scan see every possible
                // HP/MP/herb dominator before the state it can dominate.
                put(static_cast<std::uint64_t>(hero.mp), 5); // 0..4
                put(static_cast<std::uint64_t>(hero.medicinal_herbs_count), 3); // 5..7
                put(static_cast<std::uint64_t>(hero.hp), 7); // 8..14
                put(static_cast<std::uint64_t>(state.position), 13); // 15..27
                put(static_cast<std::uint64_t>(enemy.hp), 9); // 28..36
                put(static_cast<std::uint64_t>(hero.specialCharge ? hero.specialChargeTurn : 0), 3);
                put(static_cast<std::uint64_t>(hero.paralysis ? hero.paralysisTurns + 2 : 0), 3);
                put(static_cast<std::uint64_t>(hero.acrobaticStar ? hero.acrobaticStarTurn : 0), 3);
                put(static_cast<std::uint64_t>(enemy.rage ? enemy.rageTurns : 0), 3);
                put(static_cast<std::uint64_t>(state.cameraCounter), 3);
                put(hero.specialCharge ? 1u : 0u, 1);
                put(hero.paralysis ? 1u : 0u, 1);
                put(hero.inactive ? 1u : 0u, 1);
                put(hero.acrobaticStar ? 1u : 0u, 1);
                put(enemy.rage ? 1u : 0u, 1);
                put(hero.sleeping ? 1u : 0u, 1);
                put(static_cast<std::uint64_t>(hero.paralysisLevel), 5);
                put(enemy.specialCharge ? 1u : 0u, 1);
                assert(shift == 64);
                return true;
            }

            [[nodiscard]] State Unpack(const std::uint64_t key) const noexcept {
                State state = base_;
                RngPlayer &hero = state.players[0];
                RngPlayer &enemy = state.players[1];
                int shift = 0;
                const auto take = [&key, &shift](const int bits) {
                    const std::uint64_t value = (key >> shift) & BitMask(bits);
                    shift += bits;
                    return value;
                };

                hero.mp = static_cast<int>(take(5));
                hero.medicinal_herbs_count = static_cast<int>(take(3));
                hero.hp = static_cast<int>(take(7));
                state.position = static_cast<int>(take(13));
                enemy.hp = static_cast<int>(take(9));
                const int specialTurn = static_cast<int>(take(3));
                const int paralysisTurn = static_cast<int>(take(3)) - 2;
                const int starTurn = static_cast<int>(take(3));
                const int rageTurn = static_cast<int>(take(3));
                state.cameraCounter = static_cast<std::uint8_t>(take(3));
                hero.specialCharge = take(1) != 0;
                hero.paralysis = take(1) != 0;
                hero.inactive = take(1) != 0;
                hero.acrobaticStar = take(1) != 0;
                enemy.rage = take(1) != 0;
                hero.sleeping = take(1) != 0;
                hero.paralysisLevel = static_cast<int>(take(5));
                enemy.specialCharge = take(1) != 0;
                assert(shift == 64);

                if (relaxHeroHp_) hero.hp = hero.maxHp;

                hero.specialChargeTurn = hero.specialCharge ? specialTurn : 0;
                if (hero.paralysis) {
                    hero.paralysisTurns = paralysisTurn;
                } else {
                    hero.paralysisTurns = -1;
                    hero.paralysisLevel = 0;
                }
                hero.acrobaticStarTurn = hero.acrobaticStar ? starTurn : 0;
                enemy.rageTurns = enemy.rage ? rageTurn : -1;
                // This field is not consulted by Step() for the enemy in the supported
                // profile; keep one canonical value so equal future states have one key.
                enemy.specialChargeTurn = enemy.specialCharge ? 0 : 0;
                return state;
            }

        private:
            static void CanonicalizeDeadFields(State &state) noexcept {
                RngPlayer &hero = state.players[0];
                RngPlayer &enemy = state.players[1];
                if (!hero.specialCharge) hero.specialChargeTurn = 0;
                if (!hero.paralysis) {
                    hero.paralysisTurns = -1;
                    hero.paralysisLevel = 0;
                }
                if (!hero.acrobaticStar) hero.acrobaticStarTurn = 0;
                if (!enemy.rage) enemy.rageTurns = -1;
                // defence is reset to 1.0 at the beginning of every Step(), so its
                // previous turn-boundary value is future-dead.
                hero.defence = 1.0;
            }

            State base_{};
            bool relaxHeroHp_ = false;
        };

        class ExactBattleStateCodec {
        public:
            explicit ExactBattleStateCodec(BattleEmulator::SearchState root,
                                           const bool relaxHeroHp)
                : base_(root), lightCodec_(FromSearchState(root), relaxHeroHp),
                  relaxHeroHp_(relaxHeroHp) {
                CanonicalizeBoundary(base_);
            }

            [[nodiscard]] bool Pack(BattleEmulator::SearchState state,
                                    std::uint64_t &key) const noexcept {
                CanonicalizeBoundary(state);
                if (!OmittedFieldsMatchBase(state)) return false;
                return lightCodec_.Pack(FromSearchState(state), key);
            }

            [[nodiscard]] BattleEmulator::SearchState Unpack(const std::uint64_t key) const noexcept {
                BattleEmulator::SearchState state = base_;
                const State light = lightCodec_.Unpack(key);
                Player &hero = state.players[0];
                Player &enemy = state.players[1];
                const RngPlayer &lh = light.players[0];
                const RngPlayer &le = light.players[1];

                state.position = light.position;
                state.nowState = static_cast<std::uint64_t>(light.cameraCounter) << 8;

                hero.hp = relaxHeroHp_ ? hero.maxHp : lh.hp;
                hero.mp = lh.mp;
                hero.specialCharge = lh.specialCharge;
                hero.specialChargeTurn = lh.specialCharge ? lh.specialChargeTurn : 0;
                hero.paralysis = lh.paralysis;
                hero.paralysisLevel = lh.paralysis ? lh.paralysisLevel : 0;
                hero.paralysisTurns = lh.paralysis ? lh.paralysisTurns : -1;
                hero.sleeping = lh.sleeping;
                hero.acrobaticStar = lh.acrobaticStar;
                hero.acrobaticStarTurn = lh.acrobaticStar ? lh.acrobaticStarTurn : 0;
                hero.medicinal_herbs_count = lh.medicinal_herbs_count;
                hero.inactive = lh.inactive;
                hero.defence = 1.0;

                enemy.hp = le.hp;
                enemy.rage = le.rage;
                enemy.rageTurns = le.rage ? le.rageTurns : -1;
                enemy.specialCharge = le.specialCharge;
                enemy.specialChargeTurn = 0;
                return state;
            }

        private:
            static void CanonicalizeBoundary(BattleEmulator::SearchState &state) noexcept {
                Player &hero = state.players[0];
                Player &enemy = state.players[1];
                hero.defence = 1.0;
                if (!hero.specialCharge) hero.specialChargeTurn = 0;
                if (!hero.paralysis) {
                    hero.paralysisLevel = 0;
                    hero.paralysisTurns = -1;
                }
                if (!hero.acrobaticStar) hero.acrobaticStarTurn = 0;
                if (!enemy.rage) enemy.rageTurns = -1;
                enemy.specialChargeTurn = 0;
                state.nowState &= UINT64_C(0x0000000000000f00);
            }

            [[nodiscard]] static bool SameOmittedPlayerFields(const Player &a,
                                                              const Player &b,
                                                              const bool hero) noexcept {
                if (a.maxHp != b.maxHp || a.atk != b.atk || a.defaultATK != b.defaultATK ||
                    a.def != b.def || a.defaultDEF != b.defaultDEF || a.speed != b.speed ||
                    a.defaultSpeed != b.defaultSpeed || a.HealPower != b.HealPower ||
                    a.maxMp != b.maxMp || a.dirtySpecialCharge != b.dirtySpecialCharge ||
                    a.SpecialMedicineCount != b.SpecialMedicineCount ||
                    a.sleepingTurn != b.sleepingTurn || a.BuffLevel != b.BuffLevel ||
                    a.BuffTurns != b.BuffTurns || a.hasMagicMirror != b.hasMagicMirror ||
                    a.MagicMirrorTurn != b.MagicMirrorTurn || a.AtkBuffLevel != b.AtkBuffLevel ||
                    a.AtkBuffTurn != b.AtkBuffTurn || a.TensionLevel != b.TensionLevel ||
                    a.SageElixirCount != b.SageElixirCount || a.ElfinElixirCount != b.ElfinElixirCount ||
                    a.MagicWaterCount != b.MagicWaterCount || a.speedTurn != b.speedTurn ||
                    a.speedLevel != b.speedLevel || a.PoisonTurn != b.PoisonTurn ||
                    a.PoisonEnable != b.PoisonEnable || a.SpecialAntidoteCount != b.SpecialAntidoteCount) {
                    return false;
                }
                if (hero) return a.rage == b.rage && a.rageTurns == b.rageTurns;
                return a.mp == b.mp && a.medicinal_herbs_count == b.medicinal_herbs_count &&
                       a.paralysis == b.paralysis && a.paralysisLevel == b.paralysisLevel &&
                       a.paralysisTurns == b.paralysisTurns && a.sleeping == b.sleeping &&
                       a.inactive == b.inactive && a.acrobaticStar == b.acrobaticStar &&
                       a.acrobaticStarTurn == b.acrobaticStarTurn;
            }

            [[nodiscard]] bool OmittedFieldsMatchBase(const BattleEmulator::SearchState &state) const noexcept {
                return SameOmittedPlayerFields(state.players[0], base_.players[0], true) &&
                       SameOmittedPlayerFields(state.players[1], base_.players[1], false);
            }

            BattleEmulator::SearchState base_{};
            ExactStateCodec lightCodec_;
            bool relaxHeroHp_ = false;
        };

        [[nodiscard]] std::size_t BuildBattleProofCandidates(
            const BattleEmulator::SearchState &state, std::array<int, 8> &out) noexcept {
            static constexpr std::array<int, 8> candidates{
                {
                    BattleEmulator::ATTACK_ALLY,
                    BattleEmulator::DRAGON_SLASH,
                    BattleEmulator::DEFENCE,
                    BattleEmulator::FLEE_ALLY,
                    BattleEmulator::MEDICINAL_HERBS,
                    BattleEmulator::HEAL,
                    BattleEmulator::CRACK_ALLY,
                    BattleEmulator::ACROBATIC_STAR,
                }
            };
            std::size_t count = 0;
            for (const int action: candidates) {
                if (BattleEmulator::IsHeroCommandSelectable(state, {action})) {
                    out[count++] = action;
                }
            }
            return count;
        }

        struct BattleWitnessCandidate {
            BattleEmulator::SearchState state{};
            int action = -1;
            int optimisticKillTurns = kMaxPlanTurns + 1;
        };

        bool FindBattleWitnessDfs(
            const BattleEmulator::SearchState &state,
            const int remainingTurns,
            const int depth,
            const std::chrono::steady_clock::time_point deadline,
            ExactKillDecisionResult &result,
            const ExactBattleStateCodec &codec,
            std::array<std::unordered_set<std::uint64_t>, kMaxPlanTurns + 1> &deadMemo) {
            if (state.players[1].hp <= 0) {
                result.killReachable = true;
                result.firstKillTurn = depth;
                result.actionCount = depth;
                return true;
            }
            if (remainingTurns <= 0 || state.players[0].hp <= 0) return false;
            if (StaticOptimisticKillTurns(FromSearchState(state), remainingTurns) > remainingTurns) {
                return false;
            }
            if ((result.generatedStates & UINT64_C(0x0fff)) == 0 &&
                std::chrono::steady_clock::now() >= deadline) {
                result.complete = false;
                return false;
            }

            std::uint64_t memoKey = 0;
            const bool memoizable = codec.Pack(state, memoKey);
            if (memoizable && deadMemo[remainingTurns].contains(memoKey)) return false;

            ++result.expandedStates;
            std::array<int, 8> actions{};
            const std::size_t actionCount = BuildBattleProofCandidates(state, actions);
            std::array<BattleWitnessCandidate, 8> candidates{};
            std::size_t candidateCount = 0;

            for (std::size_t i = 0; i < actionCount; ++i) {
                BattleEmulator::SearchState child{};
                if (!BattleEmulator::StepSearchState(
                    state, {actions[i]}, &child, remainingTurns <= 1)) {
                    result.complete = false;
                    return false;
                }
                ++result.generatedStates;
                if (child.players[0].hp <= 0) continue;
                if (child.players[1].hp <= 0) {
                    result.actions[depth] = actions[i];
                    result.actionCount = depth + 1;
                    result.firstKillTurn = depth + 1;
                    result.killReachable = true;
                    return true;
                }
                if (remainingTurns <= 1) continue;

                const int childOptimisticTurns =
                        StaticOptimisticKillTurns(FromSearchState(child), remainingTurns - 1);
                if (childOptimisticTurns > remainingTurns - 1) continue;
                candidates[candidateCount++] = {child, actions[i], childOptimisticTurns};
            }

            // G changes only visitation order. It never removes a branch and therefore
            // cannot turn a real SAT branch into UNSAT. Lower admissible kill-turn bound
            // first, then lower exact enemy HP; remaining ties prefer more hero HP.
            std::sort(candidates.begin(), candidates.begin() + candidateCount,
                      [](const BattleWitnessCandidate &a, const BattleWitnessCandidate &b) {
                          if (a.optimisticKillTurns != b.optimisticKillTurns) {
                              return a.optimisticKillTurns < b.optimisticKillTurns;
                          }
                          if (a.state.players[1].hp != b.state.players[1].hp) {
                              return a.state.players[1].hp < b.state.players[1].hp;
                          }
                          if (a.state.players[0].hp != b.state.players[0].hp) {
                              return a.state.players[0].hp > b.state.players[0].hp;
                          }
                          return a.action < b.action;
                      });

            for (std::size_t i = 0; i < candidateCount; ++i) {
                result.actions[depth] = candidates[i].action;
                if (FindBattleWitnessDfs(candidates[i].state, remainingTurns - 1, depth + 1,
                                         deadline, result, codec, deadMemo)) {
                    return true;
                }
                if (!result.complete) return false;
            }
            if (memoizable) deadMemo[remainingTurns].insert(memoKey);
            return false;
        }

        void RadixSortPackedStates(std::vector<std::uint64_t> &values,
                                   std::vector<std::uint64_t> &scratch) {
            if (values.size() < 2) return;
            scratch.resize(values.size());
            static thread_local std::array<std::uint32_t, 1u << 16> counts{};

            const auto pass = [&](const std::vector<std::uint64_t> &src,
                                  std::vector<std::uint64_t> &dst,
                                  const int shift) {
                counts.fill(0);
                for (const std::uint64_t value: src) {
                    ++counts[static_cast<std::uint16_t>(value >> shift)];
                }
                std::size_t offset = 0;
                for (std::uint32_t &count: counts) {
                    const std::uint32_t bucketSize = count;
                    count = static_cast<std::uint32_t>(offset);
                    offset += bucketSize;
                }
                for (const std::uint64_t value: src) {
                    const auto bucket = static_cast<std::uint16_t>(value >> shift);
                    dst[counts[bucket]++] = value;
                }
            };

            pass(values, scratch, 0);
            pass(scratch, values, 16);
            pass(values, scratch, 32);
            pass(scratch, values, 48);
        }

        void ParetoPruneHpMpHerbs(std::vector<std::uint64_t> &states,
                                  ExactKillDecisionResult &result) {
            if (states.empty()) return;

            // Low 15 bits are [mp:5, herbs:3, hp:7].  For an otherwise identical
            // reachable turn-boundary state, larger HP/MP/herb counts can execute every
            // continuation available to a smaller triple with exactly the same RNG:
            // forced search commands do not branch on current HP, incoming damage is
            // HP-independent, capped healing is monotone, and MP/herbs only gate or
            // consume commands.  Thus removing a component-wise dominated triple
            // preserves E(t) exactly; this is not approximate state merging.
            //
            // HP is the most-significant part of the suffix, so a descending scan sees
            // every possible HP dominator first.  For each herb threshold keep a
            // 32-bit set of MP values already seen with at least that many herbs.
            std::size_t write = 0;
            std::size_t begin = 0;
            while (begin < states.size()) {
                const std::uint64_t base = states[begin] >> 15;
                std::size_t end = begin + 1;
                while (end < states.size() && (states[end] >> 15) == base) ++end;

                std::array<std::uint32_t, 8> seenMpAtLeastHerbs{};
                for (std::size_t i = end; i-- > begin;) {
                    const std::uint64_t key = states[i];
                    const unsigned mp = static_cast<unsigned>(key & 31u);
                    const unsigned herbs = static_cast<unsigned>((key >> 5) & 7u);
                    const std::uint32_t mpAtLeastMask = UINT32_MAX << mp;
                    if ((seenMpAtLeastHerbs[herbs] & mpAtLeastMask) != 0) {
                        ++result.dominatedStates;
                        continue;
                    }

                    states[write++] = key;
                    const std::uint32_t bit = UINT32_C(1) << mp;
                    for (unsigned h = 0; h <= herbs; ++h) {
                        seenMpAtLeastHerbs[h] |= bit;
                    }
                }
                begin = end;
            }
            states.resize(write);
        }
    } // namespace

    static ExactKillDecisionResult DecideKillWithinImpl(
        const State &root, const int maxTurns, const bool relaxHeroHp,
        const int timeLimitMs) {
        ExactKillDecisionResult result{};
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = timeLimitMs > 0
                                  ? started + std::chrono::milliseconds(timeLimitMs)
                                  : std::chrono::steady_clock::time_point::max();
        const int turns = std::clamp(maxTurns, 0, kMaxPlanTurns);
        if (root.players[1].hp <= 0) {
            result.killReachable = true;
            result.firstKillTurn = 0;
            return result;
        }
        if (root.players[0].hp <= 0 || turns <= 0) return result;

        ExactStateCodec codec(root, relaxHeroHp);
        std::uint64_t rootKey = 0;
        if (!codec.Pack(root, rootKey)) {
            result.complete = false;
            return result;
        }

        std::vector<std::uint64_t> frontier{rootKey};
        std::vector<std::uint64_t> next;
        result.peakFrontier = 1;

        for (int depth = 0; depth < turns && !frontier.empty(); ++depth) {
            const bool finalLayer = depth + 1 == turns;
            result.peakFrontier = std::max<std::uint64_t>(result.peakFrontier, frontier.size());
            result.expandedStates += frontier.size();
            next.clear();
            if (frontier.size() <= std::numeric_limits<std::size_t>::max() / 4) {
                next.reserve(frontier.size() * 4);
            }

            for (const std::uint64_t packed: frontier) {
                const State state = codec.Unpack(packed);
                std::array<int, 8> actions{};
                const std::size_t count = BuildCandidates(state, actions);
                for (std::size_t i = 0; i < count; ++i) {
                    if ((result.generatedStates & UINT64_C(0x0fff)) == 0 &&
                        std::chrono::steady_clock::now() >= deadline) {
                        result.complete = false;
                        return result;
                    }
                    const StepResult step = Step(state, actions[i]);
                    ++result.generatedStates;
                    if (!step.heroAlive) continue;
                    if (!step.enemyAlive) {
                        result.killReachable = true;
                        result.firstKillTurn = depth + 1;
                        return result;
                    }

                    // On the last allowed turn only kill/non-kill matters.  The
                    // post-horizon state is never expanded, so packing, storing,
                    // sorting, or quotienting it cannot contribute to E(turns).
                    if (finalLayer) continue;

                    // A live state outside the precomputed tape cannot be expanded
                    // safely.  Never convert that condition into a false UNSAT.
                    if (step.state.position >= 4998 && depth + 1 < turns) {
                        result.complete = false;
                        return result;
                    }

                    std::uint64_t childKey = 0;
                    if (!codec.Pack(step.state, childKey)) {
                        result.complete = false;
                        return result;
                    }
                    next.push_back(childKey);
                }
            }

            if (finalLayer) return result;

            // The frontier is already dead after child generation, so reuse its
            // allocation as radix scratch.  Four fixed 16-bit passes preserve the
            // exact same numeric order as std::sort without comparison overhead or
            // another persistent table/hash allocation.
            RadixSortPackedStates(next, frontier);
            const auto uniqueEnd = std::unique(next.begin(), next.end());
            result.duplicateStates += static_cast<std::uint64_t>(next.end() - uniqueEnd);
            next.erase(uniqueEnd, next.end());

            if (relaxHeroHp) ParetoPruneHpMpHerbs(next, result);
            frontier.swap(next);
            result.completedNoKillDepth = depth + 1;
        }

        result.peakFrontier = std::max<std::uint64_t>(result.peakFrontier, frontier.size());
        return result;
    }

    ExactKillDecisionResult DecideExactKillWithin(
        const State &root, const int maxTurns, const int timeLimitMs) {
        return DecideKillWithinImpl(root, maxTurns, false, timeLimitMs);
    }

    ExactKillDecisionResult ProveNoKillWithin(
        const State &root, const int maxTurns, const int timeLimitMs) {
        return DecideKillWithinImpl(root, maxTurns, true, timeLimitMs);
    }

    static ExactKillDecisionResult DecideBattleExactImpl(
        const BattleEmulator::SearchState &root, const int maxTurns,
        const int timeLimitMs, const bool relaxHeroHp) {
        ExactKillDecisionResult result{};
        const auto started = std::chrono::steady_clock::now();
        const auto deadline = timeLimitMs > 0
                                  ? started + std::chrono::milliseconds(timeLimitMs)
                                  : std::chrono::steady_clock::time_point::max();
        const int turns = std::clamp(maxTurns, 0, kMaxPlanTurns);
        if (root.players[1].hp <= 0) {
            result.killReachable = true;
            result.firstKillTurn = 0;
            return result;
        }
        if (root.players[0].hp <= 0 || turns <= 0) return result;

        ExactBattleStateCodec codec(root, relaxHeroHp);
        std::uint64_t rootKey = 0;
        if (!codec.Pack(root, rootKey)) {
            result.complete = false;
            return result;
        }

        std::vector<std::uint64_t> frontier{rootKey};
        std::vector<std::uint64_t> next;
        result.peakFrontier = 1;

        for (int depth = 0; depth < turns && !frontier.empty(); ++depth) {
            const bool finalLayer = depth + 1 == turns;
            result.peakFrontier = std::max<std::uint64_t>(result.peakFrontier, frontier.size());
            result.expandedStates += frontier.size();
            next.clear();
            if (!finalLayer && frontier.size() <= std::numeric_limits<std::size_t>::max() / 4) {
                next.reserve(frontier.size() * 4);
            }

            for (const std::uint64_t packed: frontier) {
                const BattleEmulator::SearchState state = codec.Unpack(packed);
                const int remainingTurns = turns - depth;
                if (StaticOptimisticKillTurns(FromSearchState(state), remainingTurns) > remainingTurns) {
                    continue;
                }
                std::array<int, 8> actions{};
                const std::size_t count = BuildBattleProofCandidates(state, actions);
                for (std::size_t i = 0; i < count; ++i) {
                    if ((result.generatedStates & UINT64_C(0x0fff)) == 0 &&
                        std::chrono::steady_clock::now() >= deadline) {
                        result.complete = false;
                        return result;
                    }
                    BattleEmulator::SearchState child{};
                    if (!BattleEmulator::StepSearchState(state, {actions[i]}, &child, finalLayer)) {
                        result.complete = false;
                        return result;
                    }
                    ++result.generatedStates;
                    if (child.players[0].hp <= 0) continue;
                    if (child.players[1].hp <= 0) {
                        result.killReachable = true;
                        result.firstKillTurn = depth + 1;
                        return result;
                    }
                    if (finalLayer) continue;

                    const int transitionDelta = child.position - state.position;
                    if (transitionDelta >= 0 && transitionDelta < 64) {
                        result.observedLiveTransitionDeltaMask |= UINT64_C(1) << transitionDelta;
                    }

                    std::uint64_t childKey = 0;
                    if (!codec.Pack(child, childKey)) {
                        result.complete = false;
                        return result;
                    }
                    next.push_back(childKey);
                }
            }

            if (finalLayer) {
                result.completedNoKillDepth = depth + 1;
                return result;
            }
            RadixSortPackedStates(next, frontier);
            const auto uniqueEnd = std::unique(next.begin(), next.end());
            result.duplicateStates += static_cast<std::uint64_t>(next.end() - uniqueEnd);
            next.erase(uniqueEnd, next.end());

            ParetoPruneHpMpHerbs(next, result);
            frontier.swap(next);
            result.completedNoKillDepth = depth + 1;
        }
        result.peakFrontier = std::max<std::uint64_t>(result.peakFrontier, frontier.size());
        return result;
    }

    ExactKillDecisionResult ProveNoKillWithinBattleExact(
        const BattleEmulator::SearchState &root, const int maxTurns,
        const int timeLimitMs) {
        return DecideBattleExactImpl(root, maxTurns, timeLimitMs, true);
    }

    ExactKillDecisionResult FindShortestKillBattleExact(
        const BattleEmulator::SearchState &root, const int maxTurns,
        const int timeLimitMs) {
        return DecideBattleExactImpl(root, maxTurns, timeLimitMs, false);
    }

    ExactKillDecisionResult FindKillWitnessBattleExact(
        const BattleEmulator::SearchState &root, const int maxTurns,
        const int timeLimitMs) {
        ExactKillDecisionResult result{};
        const int turns = std::clamp(maxTurns, 0, kMaxPlanTurns);
        const auto deadline = timeLimitMs > 0
                                  ? std::chrono::steady_clock::now() + std::chrono::milliseconds(timeLimitMs)
                                  : std::chrono::steady_clock::time_point::max();
        ExactBattleStateCodec codec(root, false);
        std::array<std::unordered_set<std::uint64_t>, kMaxPlanTurns + 1> deadMemo{};
        FindBattleWitnessDfs(root, turns, 0, deadline, result, codec, deadMemo);
        return result;
    }

    ExactKillDecisionResult SolveShortestKillBattleExact(
        const BattleEmulator::SearchState &root, const int maxTurns,
        const int timeLimitMs) {
        ExactKillDecisionResult result{};
        const auto started = std::chrono::steady_clock::now();
        const auto remainingBudgetMs = [&]() -> int {
            if (timeLimitMs <= 0) return 0;
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= timeLimitMs) return -1;
            return std::max(1, timeLimitMs - static_cast<int>(elapsed));
        };

        int budget = remainingBudgetMs();
        if (budget < 0) {
            result.complete = false;
            return result;
        }

        // Exhaust frontiers strictly by depth.  The first authoritative kill at T
        // is enough to prove E(T-1)=false because every shallower frontier has
        // already been completely consumed.  There is no reason to spend a full
        // witness search first merely to discover an upper bound U.
        ExactKillDecisionResult proof = FindShortestKillBattleExact(root, maxTurns, budget);
        result.complete = proof.complete;
        result.killReachable = proof.killReachable;
        result.firstKillTurn = proof.firstKillTurn;
        result.completedNoKillDepth = proof.completedNoKillDepth;
        result.expandedStates = proof.expandedStates;
        result.generatedStates = proof.generatedStates;
        result.duplicateStates = proof.duplicateStates;
        result.dominatedStates = proof.dominatedStates;
        result.peakFrontier = proof.peakFrontier;
        result.observedLiveTransitionDeltaMask = proof.observedLiveTransitionDeltaMask;
        if (!proof.complete || !proof.killReachable || proof.firstKillTurn <= 0) {
            return result;
        }

        // The proof frontier deliberately stores no parent table.  Recover one
        // executable certificate only after T is known; this DFS changes visit
        // order only, and every transition remains authoritative.
        budget = remainingBudgetMs();
        if (budget < 0) {
            result.complete = false;
            return result;
        }
        ExactKillDecisionResult witness = FindKillWitnessBattleExact(root, proof.firstKillTurn, budget);
        result.witnessExpandedStates = witness.expandedStates;
        result.witnessGeneratedStates = witness.generatedStates;
        if (!witness.complete || !witness.killReachable ||
            witness.firstKillTurn != proof.firstKillTurn) {
            result.complete = false;
            return result;
        }
        result.actionCount = witness.actionCount;
        result.actions = witness.actions;
        return result;
    }


    std::vector<DiversePlan> FindDiverseCriticalPlans(const State &root, const int maxTurns,
                                                      const int maxPlans) {
        std::vector<DiversePlan> result;
        const int turns = std::clamp(maxTurns, 0, kMaxPlanTurns);
        const int limit = std::max(0, maxPlans);
        if (turns == 0 || limit == 0 || root.players[0].hp <= 0 || root.players[1].hp <= 0) {
            return result;
        }

        struct SeedPrefix {
            State state{};
            std::array<int, kMaxPlanTurns> actions{};
            int actionCount = 0;
            int criticals = 0;
        };

        // A single k-best DP tends to return tiny variants of the same LCG path.
        // Force the first few choices instead.  Each forced prefix therefore owns
        // its exact RNG position before the suffix DP is queried.
        const int forcedDepth = std::min(turns, limit >= 128 ? 3 : (limit >= 16 ? 2 : 1));
        std::vector<SeedPrefix> frontier(1);
        frontier[0].state = root;
        for (int depth = 0; depth < forcedDepth; ++depth) {
            std::vector<SeedPrefix> next;
            next.reserve(frontier.size() * 6);
            for (const SeedPrefix &prefix: frontier) {
                if (prefix.state.players[0].hp <= 0) continue;
                if (prefix.state.players[1].hp <= 0) {
                    next.push_back(prefix);
                    continue;
                }
                std::array<int, 8> actions{};
                const std::size_t count = BuildCandidates(prefix.state, actions);
                for (std::size_t i = 0; i < count; ++i) {
                    const StepResult step = Step(prefix.state, actions[i]);
                    if (!step.heroAlive) continue;
                    SeedPrefix child = prefix;
                    child.state = step.state;
                    child.actions[child.actionCount++] = actions[i];
                    child.criticals += step.criticalGain;
                    next.push_back(child);
                }
            }
            frontier.swap(next);
            if (frontier.empty()) return result;
        }

        Solver solver;
        std::vector<DiversePlan> all;
        all.reserve(frontier.size());
        const TapeSummary tape = AnalyzeTape(
            root.position, std::min(4997, root.position + std::max(64, turns * 96)));

        for (const SeedPrefix &prefix: frontier) {
            const int remaining = turns - prefix.actionCount;
            int optimisticTail = 0;
            if (prefix.state.players[1].hp > 0 && remaining > 0) {
                optimisticTail = solver.Solve(prefix.state, remaining).criticals;
            }

            // Keep three qualitatively different suffix requests from the same
            // exact RNG endpoint: critical maximum, one-critical compromise, and
            // a zero-critical survival/idle lane.  The latter two are important at
            // chunk boundaries; otherwise 256 beam slots can all be tiny variants
            // of the same aggressive critical route.
            std::array<int, 3> targets{
                optimisticTail,
                std::max(0, optimisticTail - 1),
                0
            };
            std::array<std::array<int, kMaxPlanTurns>, 3> emittedActions{};
            std::array<int, 3> emittedCounts{};
            int emitted = 0;

            for (const int target: targets) {
                bool duplicateTarget = false;
                for (int i = 0; i < emitted; ++i) {
                    if (targets[i] == target) {
                        duplicateTarget = true;
                        break;
                    }
                }
                if (duplicateTarget) continue;

                DiversePlan candidate{};
                candidate.plan.tape = tape;
                candidate.plan.actionCount = prefix.actionCount;
                for (int i = 0; i < prefix.actionCount; ++i) candidate.plan.actions[i] = prefix.actions[i];

                std::uint64_t feasibilityVisited = 0;
                if (prefix.state.players[1].hp > 0 && remaining > 0) {
                    std::array<int, kMaxPlanTurns> suffix{};
                    int suffixCount = 0;
                    if (!solver.ExtractFeasible(prefix.state, remaining, target,
                                                suffix, suffixCount, feasibilityVisited)) {
                        continue;
                    }
                    for (int i = 0; i < suffixCount; ++i) {
                        candidate.plan.actions[candidate.plan.actionCount++] = suffix[i];
                    }
                }

                bool sameSequence = false;
                for (int e = 0; e < emitted; ++e) {
                    if (emittedCounts[e] != candidate.plan.actionCount) continue;
                    bool equal = true;
                    for (int i = 0; i < candidate.plan.actionCount; ++i) {
                        if (emittedActions[e][i] != candidate.plan.actions[i]) {
                            equal = false;
                            break;
                        }
                    }
                    if (equal) {
                        sameSequence = true;
                        break;
                    }
                }
                if (sameSequence) continue;

                State endpoint = root;
                int realizedCriticals = 0;
                bool executable = true;
                for (int i = 0; i < candidate.plan.actionCount; ++i) {
                    const StepResult step = Step(endpoint, candidate.plan.actions[i]);
                    if (!step.heroAlive) {
                        executable = false;
                        break;
                    }
                    realizedCriticals += step.criticalGain;
                    endpoint = step.state;
                    if (!step.enemyAlive) break;
                }
                if (!executable) continue;

                candidate.endpoint = endpoint;
                candidate.plan.optimisticCriticals = prefix.criticals + optimisticTail;
                candidate.plan.maxCriticals = realizedCriticals;
                candidate.plan.feasibilityStates = feasibilityVisited;
                emittedActions[emitted] = candidate.plan.actions;
                emittedCounts[emitted] = candidate.plan.actionCount;
                ++emitted;
                all.push_back(candidate);
            }
        }

        const auto better = [](const DiversePlan &a, const DiversePlan &b) {
            const bool aWin = a.endpoint.players[1].hp <= 0 && a.endpoint.players[0].hp > 0;
            const bool bWin = b.endpoint.players[1].hp <= 0 && b.endpoint.players[0].hp > 0;
            if (aWin != bWin) return aWin;
            if (a.plan.maxCriticals != b.plan.maxCriticals) {
                return a.plan.maxCriticals > b.plan.maxCriticals;
            }
            if (a.endpoint.players[1].hp != b.endpoint.players[1].hp) {
                return a.endpoint.players[1].hp < b.endpoint.players[1].hp;
            }
            if (a.endpoint.players[0].hp != b.endpoint.players[0].hp) {
                return a.endpoint.players[0].hp > b.endpoint.players[0].hp;
            }
            if (a.endpoint.players[0].mp != b.endpoint.players[0].mp) {
                return a.endpoint.players[0].mp > b.endpoint.players[0].mp;
            }
            return a.plan.actionCount < b.plan.actionCount;
        };
        result.reserve(std::min<std::size_t>(static_cast<std::size_t>(limit), all.size()));
        std::vector<bool> selected(all.size(), false);

        // Select from three lanes instead of allowing one scalar ranking to fill
        // the entire portfolio: critical hunting, damage progress, and survival /
        // resource preservation.  Each lane itself round-robins exact endpoint LCG
        // positions, so near-identical random-tape endpoints cannot monopolize it.
        const auto appendLane = [&](const int quota, const auto &comparator) {
            if (quota <= 0 || static_cast<int>(result.size()) >= limit) return;
            std::vector<std::size_t> order(all.size());
            std::iota(order.begin(), order.end(), std::size_t{0});
            std::stable_sort(order.begin(), order.end(), [&](const std::size_t lhs, const std::size_t rhs) {
                return comparator(all[lhs], all[rhs]);
            });

            struct PositionGroup {
                int position = 0;
                std::vector<std::size_t> indices;
                std::size_t cursor = 0;
            };
            std::vector<PositionGroup> groups;
            for (const std::size_t index: order) {
                if (selected[index]) continue;
                const int position = all[index].endpoint.position;
                auto it = std::find_if(groups.begin(), groups.end(), [position](const PositionGroup &group) {
                    return group.position == position;
                });
                if (it == groups.end()) groups.push_back({position, {index}, 0});
                else it->indices.push_back(index);
            }

            const int targetSize = std::min(limit, static_cast<int>(result.size()) + quota);
            while (static_cast<int>(result.size()) < targetSize) {
                bool added = false;
                for (PositionGroup &group: groups) {
                    while (group.cursor < group.indices.size() && selected[group.indices[group.cursor]]) {
                        ++group.cursor;
                    }
                    if (group.cursor >= group.indices.size()) continue;
                    const std::size_t index = group.indices[group.cursor++];
                    selected[index] = true;
                    result.push_back(all[index]);
                    added = true;
                    if (static_cast<int>(result.size()) >= targetSize) break;
                }
                if (!added) break;
            }
        };

        const auto damageBetter = [](const DiversePlan &a, const DiversePlan &b) {
            const bool aWin = a.endpoint.players[1].hp <= 0 && a.endpoint.players[0].hp > 0;
            const bool bWin = b.endpoint.players[1].hp <= 0 && b.endpoint.players[0].hp > 0;
            if (aWin != bWin) return aWin;
            if (a.endpoint.players[1].hp != b.endpoint.players[1].hp) {
                return a.endpoint.players[1].hp < b.endpoint.players[1].hp;
            }
            if (a.plan.maxCriticals != b.plan.maxCriticals) {
                return a.plan.maxCriticals > b.plan.maxCriticals;
            }
            return a.endpoint.players[0].hp > b.endpoint.players[0].hp;
        };
        const auto survivalBetter = [](const DiversePlan &a, const DiversePlan &b) {
            const bool aWin = a.endpoint.players[1].hp <= 0 && a.endpoint.players[0].hp > 0;
            const bool bWin = b.endpoint.players[1].hp <= 0 && b.endpoint.players[0].hp > 0;
            if (aWin != bWin) return aWin;
            if (a.endpoint.players[0].hp != b.endpoint.players[0].hp) {
                return a.endpoint.players[0].hp > b.endpoint.players[0].hp;
            }
            if (a.endpoint.players[0].mp != b.endpoint.players[0].mp) {
                return a.endpoint.players[0].mp > b.endpoint.players[0].mp;
            }
            if (a.endpoint.players[0].medicinal_herbs_count != b.endpoint.players[0].medicinal_herbs_count) {
                return a.endpoint.players[0].medicinal_herbs_count > b.endpoint.players[0].medicinal_herbs_count;
            }
            return a.endpoint.players[1].hp < b.endpoint.players[1].hp;
        };

        const int criticalQuota = (limit + 1) / 2;
        const int damageQuota = limit / 4;
        const int survivalQuota = limit - criticalQuota - damageQuota;
        appendLane(criticalQuota, better);
        appendLane(damageQuota, damageBetter);
        appendLane(survivalQuota, survivalBetter);
        appendLane(limit, better); // fill any holes left by exhausted lanes

        const std::uint64_t expanded = solver.Expanded();
        for (DiversePlan &candidate: result) candidate.plan.expandedStates = expanded;
        return result;
    }
} // namespace rngflow
