#include "ProofTypes.h"

#include <algorithm>
#include <limits>

namespace d20proof {
    namespace {
        bool samePlayer(const Player &a, const Player &b) noexcept {
            return a.hp == b.hp &&
                   a.maxHp == b.maxHp &&
                   a.atk == b.atk &&
                   a.defaultATK == b.defaultATK &&
                   a.def == b.def &&
                   a.defaultDEF == b.defaultDEF &&
                   a.speed == b.speed &&
                   a.defaultSpeed == b.defaultSpeed &&
                   a.HealPower == b.HealPower &&
                   a.mp == b.mp &&
                   a.maxMp == b.maxMp &&
                   a.specialCharge == b.specialCharge &&
                   a.dirtySpecialCharge == b.dirtySpecialCharge &&
                   a.specialChargeTurn == b.specialChargeTurn &&
                   a.paralysis == b.paralysis &&
                   a.paralysisLevel == b.paralysisLevel &&
                   a.paralysisTurns == b.paralysisTurns &&
                   a.SpecialMedicineCount == b.SpecialMedicineCount &&
                   a.defence == b.defence &&
                   a.sleeping == b.sleeping &&
                   a.sleepingTurn == b.sleepingTurn &&
                   a.BuffLevel == b.BuffLevel &&
                   a.BuffTurns == b.BuffTurns &&
                   a.hasMagicMirror == b.hasMagicMirror &&
                   a.MagicMirrorTurn == b.MagicMirrorTurn &&
                   a.AtkBuffLevel == b.AtkBuffLevel &&
                   a.AtkBuffTurn == b.AtkBuffTurn &&
                   a.TensionLevel == b.TensionLevel &&
                   a.rage == b.rage &&
                   a.SageElixirCount == b.SageElixirCount &&
                   a.ElfinElixirCount == b.ElfinElixirCount &&
                   a.MagicWaterCount == b.MagicWaterCount &&
                   a.speedTurn == b.speedTurn &&
                   a.speedLevel == b.speedLevel &&
                   a.PoisonTurn == b.PoisonTurn &&
                   a.PoisonEnable == b.PoisonEnable &&
                   a.SpecialAntidoteCount == b.SpecialAntidoteCount &&
                   a.acrobaticStar == b.acrobaticStar &&
                   a.acrobaticStarTurn == b.acrobaticStarTurn &&
                   a.rageTurns == b.rageTurns &&
                   a.medicinal_herbs_count == b.medicinal_herbs_count &&
                   a.inactive == b.inactive;
        }
    } // namespace

    bool sameRawState(const RawState &a, const RawState &b) noexcept {
        return a.position == b.position &&
               a.nowState == b.nowState &&
               samePlayer(a.players[0], b.players[0]) &&
               samePlayer(a.players[1], b.players[1]);
    }

    namespace {
        bool sameConstraints(const ProblemConstraints &a, const ProblemConstraints &b) noexcept {
            if (a.actions != b.actions || a.observations.size() != b.observations.size()) {
                return false;
            }
            for (std::size_t index = 0; index < a.observations.size(); ++index) {
                if (a.observations[index].absoluteTurn != b.observations[index].absoluteTurn ||
                    !sameRawState(
                        a.observations[index].expectedState,
                        b.observations[index].expectedState)) {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    bool sameProblemKey(const Problem &a, const Problem &b) noexcept {
        return a.ruleId == b.ruleId &&
               a.seed == b.seed &&
               a.startTurn == b.startTurn &&
               sameRawState(a.s0, b.s0) &&
               sameConstraints(a.constraints, b.constraints);
    }

    bool commandAllowedByConstraints(
        const Problem &problem,
        int absoluteTurn,
        int command) noexcept {
        const auto found = std::lower_bound(
            problem.constraints.actions.begin(),
            problem.constraints.actions.end(),
            absoluteTurn,
            [](const ActionConstraint &constraint, int turn) {
                return constraint.absoluteTurn < turn;
            });
        if (found == problem.constraints.actions.end() || found->absoluteTurn != absoluteTurn) {
            return true;
        }
        return std::binary_search(found->allowedCommands.begin(), found->allowedCommands.end(), command);
    }

    const BoundaryObservation *boundaryObservationAt(
        const Problem &problem,
        int absoluteTurn) noexcept {
        const auto found = std::lower_bound(
            problem.constraints.observations.begin(),
            problem.constraints.observations.end(),
            absoluteTurn,
            [](const BoundaryObservation &observation, int turn) {
                return observation.absoluteTurn < turn;
            });
        if (found == problem.constraints.observations.end() || found->absoluteTurn != absoluteTurn) {
            return nullptr;
        }
        return &*found;
    }

    namespace {
        bool intervalContains(const Interval &outer, const Interval &inner) noexcept {
            return outer.lo <= inner.lo && outer.hi >= inner.hi;
        }
    } // namespace

    bool Box::empty() const noexcept {
        return enemyHp.empty() || heroHp.empty() || mp.empty() || herb.empty() || chargeMask == 0 ||
               paralysisMask == 0 || acroMask == 0 || rageMask == 0 || inactiveMask == 0 || cameraMask == 0;
    }

    std::uint16_t allMask(ModeAxis axis) noexcept {
        switch (axis) {
            case ModeAxis::Charge:
                return kChargeMaskAll;
            case ModeAxis::Paralysis:
                return kParalysisMaskAll;
            case ModeAxis::Acro:
                return kAcroMaskAll;
            case ModeAxis::Rage:
                return kRageMaskAll;
            case ModeAxis::Inactive:
                return kInactiveMaskAll;
            case ModeAxis::Camera:
                return kCameraMaskAll;
        }
        return 0;
    }

    const Interval &intervalOf(const Box &box, ResourceAxis axis) noexcept {
        switch (axis) {
            case ResourceAxis::EnemyHp:
                return box.enemyHp;
            case ResourceAxis::HeroHp:
                return box.heroHp;
            case ResourceAxis::Mp:
                return box.mp;
            case ResourceAxis::Herb:
                return box.herb;
        }
        return box.enemyHp;
    }

    Interval &intervalOf(Box &box, ResourceAxis axis) noexcept {
        return const_cast<Interval &>(intervalOf(static_cast<const Box &>(box), axis));
    }

    std::uint16_t modeMaskOf(const Box &box, ModeAxis axis) noexcept {
        switch (axis) {
            case ModeAxis::Charge:
                return box.chargeMask;
            case ModeAxis::Paralysis:
                return box.paralysisMask;
            case ModeAxis::Acro:
                return box.acroMask;
            case ModeAxis::Rage:
                return box.rageMask;
            case ModeAxis::Inactive:
                return box.inactiveMask;
            case ModeAxis::Camera:
                return box.cameraMask;
        }
        return 0;
    }

    std::uint16_t &modeMaskOf(Box &box, ModeAxis axis) noexcept {
        switch (axis) {
            case ModeAxis::Charge:
                return box.chargeMask;
            case ModeAxis::Paralysis:
                return box.paralysisMask;
            case ModeAxis::Acro:
                return box.acroMask;
            case ModeAxis::Rage:
                return box.rageMask;
            case ModeAxis::Inactive:
                return box.inactiveMask;
            case ModeAxis::Camera:
                return box.cameraMask;
        }
        return box.chargeMask;
    }

    Box intersect(const Box &a, const Box &b) noexcept {
        Box result;
        result.enemyHp = {std::max(a.enemyHp.lo, b.enemyHp.lo), std::min(a.enemyHp.hi, b.enemyHp.hi)};
        result.heroHp = {std::max(a.heroHp.lo, b.heroHp.lo), std::min(a.heroHp.hi, b.heroHp.hi)};
        result.mp = {std::max(a.mp.lo, b.mp.lo), std::min(a.mp.hi, b.mp.hi)};
        result.herb = {std::max(a.herb.lo, b.herb.lo), std::min(a.herb.hi, b.herb.hi)};
        result.chargeMask = a.chargeMask & b.chargeMask;
        result.paralysisMask = a.paralysisMask & b.paralysisMask;
        result.acroMask = a.acroMask & b.acroMask;
        result.rageMask = a.rageMask & b.rageMask;
        result.inactiveMask = a.inactiveMask & b.inactiveMask;
        result.cameraMask = a.cameraMask & b.cameraMask;
        return result;
    }

    bool contains(const Box &outer, const Box &inner) noexcept {
        if (inner.empty()) {
            return true;
        }
        if (outer.empty()) {
            return false;
        }

        return intervalContains(outer.enemyHp, inner.enemyHp) && intervalContains(outer.heroHp, inner.heroHp) &&
               intervalContains(outer.mp, inner.mp) && intervalContains(outer.herb, inner.herb) &&
               (inner.chargeMask & ~outer.chargeMask) == 0 && (inner.paralysisMask & ~outer.paralysisMask) == 0 &&
               (inner.acroMask & ~outer.acroMask) == 0 && (inner.rageMask & ~outer.rageMask) == 0 &&
               (inner.inactiveMask & ~outer.inactiveMask) == 0 && (inner.cameraMask & ~outer.cameraMask) == 0;
    }

    std::pair<Box, Box> split(const Box &box, const Predicate &predicate) {
        Box trueSide = box;
        Box falseSide = box;

        if (predicate.kind == PredicateKind::ResourceLe) {
            auto &trueInterval = intervalOf(trueSide, predicate.resource);
            auto &falseInterval = intervalOf(falseSide, predicate.resource);

            trueInterval.hi = std::min(trueInterval.hi, predicate.threshold);
            if (predicate.threshold == std::numeric_limits<std::int64_t>::max()) {
                falseInterval = {1, 0};
            } else {
                falseInterval.lo = std::max(falseInterval.lo, predicate.threshold + 1);
            }
            return {trueSide, falseSide};
        }

        const auto allowedMask = static_cast<std::uint16_t>(predicate.mask & allMask(predicate.mode));
        auto &trueMask = modeMaskOf(trueSide, predicate.mode);
        auto &falseMask = modeMaskOf(falseSide, predicate.mode);
        trueMask &= allowedMask;
        falseMask &= static_cast<std::uint16_t>(allMask(predicate.mode) & ~allowedMask);
        return {trueSide, falseSide};
    }
} // namespace d20proof
