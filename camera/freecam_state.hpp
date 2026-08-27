#pragma once

#include <cstdint>
#include <span>

#include "freecam_metadata.hpp"
#include "freecam_route.hpp"

namespace dq9::freecam {

enum class BattleActorSide : std::uint8_t {
    ally,
    enemy,
};

struct BattleActorRef {
    BattleActorSide side{};
    std::uint8_t index{};
};

namespace detail {

enum class TriggerSource : std::uint8_t {
    none,
    actor_membership,
    action_bact,
    fallback_membership,
    reset_only,
};

struct TriggerInput {
    std::uint16_t actionId{};
    std::uint16_t turnActionIndex{};

    // main:020498D4(actor, actionId) returned non-null.
    bool actorMembership{};

    // Resolved from the immutable metadata loaded once by main.cpp.
    bool actionHasBact{};

    // controller+0x5914 membership fallback in overlay_d_25:021DCF8C.
    bool fallbackMembership{};

    // FUN_0204A264(actor), written by main:0204A230, in the range 0..15.
    std::uint8_t actorPresentationRouteCount{1};

    // Compact ordered projection consumed by 021DC1D4. It contains only the
    // first 0x0C/0x3D/0x07 relationship, first 0x0C value, and 0x20/0x22 bits.
    std::uint32_t selectorProjection{};

    // Later-action exceptions that force param5=1. The first field is the
    // participant actor presentation route count (+0x34), not an opaque
    // camera mode.
    bool anyParticipantCameraStateAbove4{};
    bool targetIsCurrentActor{};
    bool actorsOverlap{};

    // overlay_d_25:021E03AC or another action-specific reset-only path.
    bool resetOverride{};
};

inline constexpr std::uint16_t kInvalidBattleActor = 0xffff;

// DQ9's managed battle-actor IDs use ally slots as 0x00+index and enemy
// slots as 0xC0+index. This is independent of how many allies/enemies the
// current BattleEmulator configuration happens to use.
[[nodiscard]] constexpr std::uint16_t Dq9ActorId(
    const BattleActorRef actor
) noexcept {
    return static_cast<std::uint16_t>(
        actor.index
        + (actor.side == BattleActorSide::enemy ? UINT16_C(0x00c0) : UINT16_C(0))
    );
}

static_assert(Dq9ActorId({BattleActorSide::ally, 0}) == 0x0000);
static_assert(Dq9ActorId({BattleActorSide::ally, 3}) == 0x0003);
static_assert(Dq9ActorId({BattleActorSide::enemy, 0}) == 0x00c0);
static_assert(Dq9ActorId({BattleActorSide::enemy, 3}) == 0x00c3);

struct TriggerActionState {
    std::uint16_t actionId{};
    std::uint16_t actorId{kInvalidBattleActor};
    std::uint16_t targetId{kInvalidBattleActor};
};

struct PresentationMembershipGroup {
    std::span<const std::uint16_t> actionIds{};
    std::uint32_t selectorProjection{};
};

struct TriggerDerivationInput {
    TriggerActionState current{};
    TriggerActionState previous{};
    std::uint16_t turnActionIndex{};
    std::uint16_t currentActorId{kInvalidBattleActor};
    const FreeCameraFixedMetadata* fixedMetadata{};
    const FreeCameraActionMetadata* actionMetadata{};
    const FreeCameraMembershipMetadata* membershipMetadata{};
    std::uint32_t actorMembershipProfile{kInvalidMembershipProfile};

    std::span<const PresentationMembershipGroup> actorMembershipGroups{};
    std::span<const PresentationMembershipGroup> fallbackMembershipGroups{};

    // Raw actor presentation state used by 021E03AC and 021DC1D4.
    bool actorPresentationFlag80{};
    std::uint8_t targetPresentationSlot{0xff};

    // Raw geometry used by the overlap exception in 021DC1D4.
    bool actorAndTargetHaveGeometry{};
    std::int32_t actorTargetDistance{};
    std::int32_t actorRadius{};
    std::int32_t targetRadius{};
};

[[nodiscard]] constexpr bool ContainsAction(
    const std::span<const std::uint16_t> actions,
    const std::uint16_t actionId
) noexcept {
    for (const std::uint16_t candidate : actions) {
        if (candidate == actionId) return true;
    }
    return false;
}

[[nodiscard]] constexpr const PresentationMembershipGroup* FindMembershipGroup(
    const std::span<const PresentationMembershipGroup> groups,
    const std::uint16_t actionId
) noexcept {
    for (const PresentationMembershipGroup& group : groups) {
        if (ContainsAction(group.actionIds, actionId)) return &group;
    }
    return nullptr;
}

[[nodiscard]] constexpr std::uint8_t CountMembershipGroups(
    const std::span<const PresentationMembershipGroup> groups,
    const std::uint16_t actionId,
    const std::uint8_t limit = 0xff
) noexcept {
    std::uint8_t count = 0;
    for (const PresentationMembershipGroup& group : groups) {
        if (ContainsAction(group.actionIds, actionId) && count < limit) ++count;
    }
    return count;
}

[[nodiscard]] constexpr FreeCameraMembershipCell ActorMembershipCell(
    const TriggerDerivationInput& input,
    const std::uint16_t actionId
) noexcept {
    if (input.membershipMetadata != nullptr
        && input.actorMembershipProfile != kInvalidMembershipProfile) {
        return input.membershipMetadata->ActorMembership(input.actorMembershipProfile, actionId);
    }
    const PresentationMembershipGroup* group = FindMembershipGroup(input.actorMembershipGroups, actionId);
    return group == nullptr
        ? FreeCameraMembershipCell{}
        : FreeCameraMembershipCell{group->selectorProjection, CountMembershipGroups(input.actorMembershipGroups, actionId)};
}

[[nodiscard]] constexpr FreeCameraMembershipCell FallbackMembershipCell(
    const TriggerDerivationInput& input,
    const std::uint16_t actionId
) noexcept {
    if (input.membershipMetadata != nullptr) {
        return input.membershipMetadata->FallbackMembership(actionId);
    }
    const PresentationMembershipGroup* group = FindMembershipGroup(input.fallbackMembershipGroups, actionId);
    return group == nullptr
        ? FreeCameraMembershipCell{}
        : FreeCameraMembershipCell{group->selectorProjection, CountMembershipGroups(input.fallbackMembershipGroups, actionId)};
}

// Exact ordered-list predicate at 021DC268..021DC32C, encoded by the ROM
// generator as: bit0 has0C, bit1 first0C is at/after first3D or first07,
// bit2 has20, bit3 has22, bits16..31 first0C value.
[[nodiscard]] constexpr bool ComputeSelectorSuppression(
    const std::uint32_t projection
) noexcept {
    const bool has0c = (projection & 1) != 0;
    if (has0c && (projection & 2) != 0) return false;
    if (has0c && static_cast<std::uint16_t>(projection >> 16) != 10) return true;
    return (projection & (4 | 8)) != 0;
}

// Exact boolean use of overlay_d_25:021E03AC. It is a consecutive normal-
// attack/same-actor/same-target rule, not the unverified 0-damage -> Merazoma
// correction described by the old handoff.
[[nodiscard]] constexpr bool ComputeConsecutiveAttackReset(
    const TriggerDerivationInput& input,
    const std::uint16_t targetRecord02161720ActorId
) noexcept {
    return input.turnActionIndex > 0
        && input.current.actionId == 1
        && input.previous.actionId == 1
        && input.current.actorId != kInvalidBattleActor
        && input.current.actorId == input.previous.actorId
        && input.current.targetId != kInvalidBattleActor
        && input.current.targetId == input.previous.targetId
        && targetRecord02161720ActorId == input.previous.targetId
        && input.actorPresentationFlag80
        && ActorMembershipCell(input, 1).count > 1;
}

[[nodiscard]] constexpr TriggerInput DeriveTriggerInput(
    const TriggerDerivationInput& input,
    const PresentationTurnRoutes& routes,
    const std::uint16_t targetRecord02161720ActorId = kInvalidBattleActor
) noexcept {
    const PresentationActorRoute* actorRoute = FindPresentationRoute(routes, input.current.actorId);
    const FreeCameraMembershipCell actorMembership = ActorMembershipCell(input, input.current.actionId);
    const std::uint16_t fallbackLookupActionId = input.actionMetadata == nullptr
        ? kInvalidFreeCameraActionId
        : input.actionMetadata->FallbackLookupActionId(input.current.actionId);
    const FreeCameraMembershipCell fallbackMembership = fallbackLookupActionId == kInvalidFreeCameraActionId
        ? FreeCameraMembershipCell{}
        : FallbackMembershipCell(input, fallbackLookupActionId);
    const bool actionHasBact = input.fixedMetadata != nullptr
        && input.fixedMetadata->HasBact(input.current.actionId);
    std::uint32_t selectorProjection = 0;
    if (actorMembership.Present()) {
        selectorProjection = actorMembership.selectorProjection;
    } else if (actionHasBact) {
        selectorProjection = input.fixedMetadata->SelectorProjection(input.current.actionId);
    } else if (fallbackMembership.Present()) {
        selectorProjection = fallbackMembership.selectorProjection;
    }
    bool anyRouteAbove4 = false;
    for (std::size_t index = 0; index < routes.actorCount; ++index) {
        if (routes.actors[index].count > 4) {
            anyRouteAbove4 = true;
            break;
        }
    }
    const bool targetIsCurrentActor = input.targetPresentationSlot != 0xff
        && input.current.targetId == input.currentActorId;
    const bool actorsOverlap = input.actorAndTargetHaveGeometry
        && input.actorTargetDistance < ((input.actorRadius + input.targetRadius) >> 1);
    return {
        input.current.actionId,
        input.turnActionIndex,
        actorMembership.Present(),
        actionHasBact,
        fallbackMembership.Present(),
        actorRoute == nullptr ? std::uint8_t{0} : actorRoute->count,
        selectorProjection,
        anyRouteAbove4,
        targetIsCurrentActor,
        actorsOverlap,
        ComputeConsecutiveAttackReset(input, targetRecord02161720ActorId),
    };
}

struct TriggerDecision {
    TriggerSource source{TriggerSource::none};
    bool callFreeCamera{};
    bool param5{};
    bool resetOnly{};
};

[[nodiscard]] constexpr TriggerDecision DecideFreeCamera(const TriggerInput input) noexcept {
    if (input.resetOverride) {
        return {TriggerSource::reset_only, false, false, true};
    }

    TriggerSource source = TriggerSource::none;
    if (input.actorMembership) {
        source = TriggerSource::actor_membership;
    } else if (input.actionHasBact) {
        source = TriggerSource::action_bact;
    } else if (input.fallbackMembership) {
        source = TriggerSource::fallback_membership;
    }

    if (source == TriggerSource::none) {
        return {};
    }

    if (input.actorPresentationRouteCount == 0 && ComputeSelectorSuppression(input.selectorProjection)) {
        return {};
    }

    const bool forceMode1Exception = input.anyParticipantCameraStateAbove4
        || input.targetIsCurrentActor
        || input.actorsOverlap;
    const bool preemptiveMode = input.turnActionIndex == 0 || forceMode1Exception;
    return {source, true, preemptiveMode, false};
}

static_assert(!ComputeSelectorSuppression(UINT32_C(0x00050003))); // first 0C is hidden by 3D/07
static_assert(!ComputeSelectorSuppression(UINT32_C(0x000a0001))); // visible 0C value 10
static_assert(ComputeSelectorSuppression(UINT32_C(0x000a0005)));  // value 10 plus type 20
static_assert(DecideFreeCamera({1, 0, true}).callFreeCamera);
static_assert(DecideFreeCamera({1, 0, true}).param5);
static_assert(!DecideFreeCamera({236, 0}).callFreeCamera);
static_assert(DecideFreeCamera({72, 0, false, true}).callFreeCamera);
static_assert(DecideFreeCamera({72, 1, false, true}).param5 == false);

} // namespace detail
} // namespace dq9::freecam
