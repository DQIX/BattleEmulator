#include <array>
#include <cstdint>
#ifdef FREECAM_DEBUG_ROUTES
#include <iostream>
#endif

#include "freecam_state.hpp"
#include "freecam_runtime.hpp"
#include "freecam-bact-table.hpp"
#include "freecam-selector-table.hpp"

namespace {
using namespace dq9::freecam;
using namespace dq9::freecam::detail;

constexpr FreeCameraFixedMetadata BuildValidationMetadata() {
    FreeCameraFixedMetadata result{};
    result.selectorProjection = ::dq9::freecam::generated::kSelectorProjection;
    for (std::uint16_t actionId = 0; actionId < 1024; ++actionId) {
        if (::dq9::freecam::generated::HasSpBact(actionId)) {
            result.hasBact[actionId >> 3] |= static_cast<std::uint8_t>(1U << (actionId & 7));
        }
    }
    return result;
}
constexpr FreeCameraFixedMetadata kValidationMetadata = BuildValidationMetadata();
static_assert(kValidationMetadata.HasBact(72));
static_assert(!kValidationMetadata.HasBact(236));
static_assert(ComputeSelectorSuppression(kValidationMetadata.SelectorProjection(72)));
static_assert(ComputeSelectorSuppression(kValidationMetadata.SelectorProjection(255)));


constexpr PresentationActorState kResolvedTargetActor{
    .actorId = Dq9ActorId({BattleActorSide::ally, 0}),
    .startNode = 55,
    .goalNode = 59,
    .cachedTargetId = static_cast<std::uint8_t>(Dq9ActorId({BattleActorSide::ally, 1})),
    .auxiliaryTargetId = kInvalidPresentationTarget,
    .movementEnabled = true,
    .presentationFlags = 0x20,
    .worldX = -31925,
    .worldY = 204,
    .worldZ = 18432,
};
static_assert(ResolvePresentationTarget(
    kResolvedTargetActor,
    Dq9ActorId({BattleActorSide::ally, 3})
) == Dq9ActorId({BattleActorSide::ally, 1}));
static_assert(IsPresentationMovementEligible(
    kResolvedTargetActor,
    Dq9ActorId({BattleActorSide::ally, 1})
));
static_assert(!IsPresentationMovementEligible(
    kResolvedTargetActor,
    Dq9ActorId({BattleActorSide::ally, 0})
));
constexpr auto kFlag80Actor = [] {
    auto actor = kResolvedTargetActor;
    actor.presentationFlags |= kPresentationFlag80;
    return actor;
}();
static_assert(!IsPresentationMovementEligible(kFlag80Actor, 1));
constexpr auto kAuxTargetActor = [] {
    auto actor = kResolvedTargetActor;
    actor.cachedTargetId = kInvalidPresentationTarget;
    actor.auxiliaryTargetId = 3;
    return actor;
}();
static_assert(ResolvePresentationTarget(kAuxTargetActor, 2) == 3);
constexpr auto kPrimaryTargetActor = [] {
    auto actor = kAuxTargetActor;
    actor.auxiliaryTargetId = kInvalidPresentationTarget;
    return actor;
}();
static_assert(ResolvePresentationTarget(kPrimaryTargetActor, 2) == 2);

constexpr std::array kSimpleTurnActors{
    PresentationActorInput{0, 56, 55},
    PresentationActorInput{3, 66, 57},
    PresentationActorInput{2, 67, 67},
    PresentationActorInput{1, 60, 61},
    PresentationActorInput{Dq9ActorId({BattleActorSide::enemy, 0}), 22, 22},
};
constexpr std::array<std::uint16_t, 6> kSimpleTurnActions{
    Dq9ActorId({BattleActorSide::enemy, 0}),
    Dq9ActorId({BattleActorSide::enemy, 0}),
    0, 3, 1, 2,
};
constexpr auto kSimpleTurnRoutes = PlanPresentationRoutes(kSimpleTurnActors, kSimpleTurnActions, 0);
static_assert(kSimpleTurnRoutes.valid);
static_assert(FindPresentationRoute(kSimpleTurnRoutes, 0)->count == 2);
static_assert(FindPresentationRoute(kSimpleTurnRoutes, 0)->nodes[0] == 56);
static_assert(FindPresentationRoute(kSimpleTurnRoutes, 0)->nodes[1] == 55);
static_assert(FindPresentationRoute(kSimpleTurnRoutes, 2)->count == 0);

constexpr std::array<std::uint16_t, 1> kActionOneMembership{1};
constexpr std::array<PresentationMembershipGroup, 2> kActionOneMembershipGroups{
    PresentationMembershipGroup{kActionOneMembership, UINT32_C(0x00050001)},
    PresentationMembershipGroup{kActionOneMembership, UINT32_C(0x00050001)},
};
constexpr TriggerDerivationInput kConsecutiveAttackReset{
    .current = {
        1,
        Dq9ActorId({BattleActorSide::ally, 0}),
        Dq9ActorId({BattleActorSide::enemy, 0}),
    },
    .previous = {
        1,
        Dq9ActorId({BattleActorSide::ally, 0}),
        Dq9ActorId({BattleActorSide::enemy, 0}),
    },
    .turnActionIndex = 1,
    .fixedMetadata = &kValidationMetadata,
    .actorMembershipGroups = kActionOneMembershipGroups,
    .actorPresentationFlag80 = true,
};
static_assert(ComputeConsecutiveAttackReset(
    kConsecutiveAttackReset,
    Dq9ActorId({BattleActorSide::enemy, 0})
));

constexpr bool RouteEquals(
    const PresentationTurnRoutes& routes,
    const std::uint16_t actorId,
    const std::span<const std::uint8_t> expected
) {
    const PresentationActorRoute* route = FindPresentationRoute(routes, actorId);
    if (route == nullptr || route->count != expected.size()) return false;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (route->nodes[index] != expected[index]) return false;
    }
    return true;
}

constexpr std::array kHerbActorsAtIndex2{
    PresentationActorInput{0, 55, 59},
    PresentationActorInput{3, 57, 56},
    PresentationActorInput{2, 67, 58},
    PresentationActorInput{1, 61, 61},
    PresentationActorInput{Dq9ActorId({BattleActorSide::enemy, 0}), 22, 22},
};
constexpr std::array kHerbTurnOrder{
    BattleActorRef{BattleActorSide::enemy, 0},
    BattleActorRef{BattleActorSide::enemy, 0},
    BattleActorRef{BattleActorSide::ally, 0},
    BattleActorRef{BattleActorSide::ally, 3},
    BattleActorRef{BattleActorSide::ally, 1},
    BattleActorRef{BattleActorSide::ally, 2},
};
constexpr auto kHerbTurnActors = [] {
    std::array<std::uint16_t, kHerbTurnOrder.size()> result{};
    for (std::size_t index = 0; index < kHerbTurnOrder.size(); ++index) {
        result[index] = Dq9ActorId(kHerbTurnOrder[index]);
    }
    return result;
}();
constexpr auto kHerbRoutesAtIndex2 = PlanPresentationRoutes(kHerbActorsAtIndex2, kHerbTurnActors, 2);
constexpr std::array<std::uint8_t, 4> kExpectedActor0Route{55, 46, 49, 59};
constexpr std::array<std::uint8_t, 2> kExpectedActor3Route{57, 56};
constexpr std::array<std::uint8_t, 2> kExpectedActor2Route{67, 58};
static_assert(kHerbRoutesAtIndex2.valid);
static_assert(RouteEquals(kHerbRoutesAtIndex2, 0, kExpectedActor0Route));
static_assert(RouteEquals(kHerbRoutesAtIndex2, 3, kExpectedActor3Route));
static_assert(RouteEquals(kHerbRoutesAtIndex2, 2, kExpectedActor2Route));
static_assert(FindPresentationRoute(kHerbRoutesAtIndex2, 1)->count == 0);
static_assert(FindPresentationRoute(
    kHerbRoutesAtIndex2,
    Dq9ActorId({BattleActorSide::enemy, 0})
)->count == 0);

constexpr std::array<PresentationMembershipGroup, 0> kNoMembershipGroups{};

constexpr TriggerDerivationInput kHerbIndex2Trigger{
    .current = {
        255,
        Dq9ActorId({BattleActorSide::ally, 0}),
        Dq9ActorId({BattleActorSide::ally, 1}),
    },
    .previous = {
        942,
        Dq9ActorId({BattleActorSide::enemy, 0}),
        Dq9ActorId({BattleActorSide::enemy, 0}),
    },
    .turnActionIndex = 2,
    .fixedMetadata = &kValidationMetadata,
    .actorMembershipGroups = kNoMembershipGroups,
    .fallbackMembershipGroups = kNoMembershipGroups,
};
constexpr TriggerDecision kHerbIndex2Decision = DecideFreeCamera(
    DeriveTriggerInput(kHerbIndex2Trigger, kHerbRoutesAtIndex2)
);
static_assert(kHerbIndex2Decision.callFreeCamera);
static_assert(!kHerbIndex2Decision.param5);

constexpr std::array kHerbActorsAtIndex3{
    PresentationActorInput{0, 68, 68},
    PresentationActorInput{3, 56, 66},
    PresentationActorInput{2, 58, 58},
    PresentationActorInput{1, 61, 65},
    PresentationActorInput{Dq9ActorId({BattleActorSide::enemy, 0}), 22, 22},
};
constexpr auto kHerbRoutesAtIndex3 = PlanPresentationRoutes(kHerbActorsAtIndex3, kHerbTurnActors, 3);
constexpr std::array<std::uint8_t, 3> kExpectedActor3Index3Route{56, 57, 66};
constexpr std::array<std::uint8_t, 7> kExpectedActor1Index3Route{61, 60, 59, 49, 48, 57, 65};
#ifndef FREECAM_DEBUG_ROUTES
static_assert(RouteEquals(kHerbRoutesAtIndex3, 3, kExpectedActor3Index3Route));
static_assert(RouteEquals(kHerbRoutesAtIndex3, 1, kExpectedActor1Index3Route));
#endif

constexpr TriggerDerivationInput kHerbIndex3Trigger{
    .current = {
        255,
        Dq9ActorId({BattleActorSide::ally, 3}),
        Dq9ActorId({BattleActorSide::ally, 0}),
    },
    .previous = {
        255,
        Dq9ActorId({BattleActorSide::ally, 0}),
        Dq9ActorId({BattleActorSide::ally, 1}),
    },
    .turnActionIndex = 3,
    .fixedMetadata = &kValidationMetadata,
    .actorMembershipGroups = kNoMembershipGroups,
    .fallbackMembershipGroups = kNoMembershipGroups,
};
constexpr TriggerDecision kHerbIndex3Decision = DecideFreeCamera(
    DeriveTriggerInput(kHerbIndex3Trigger, kHerbRoutesAtIndex3)
);
static_assert(kHerbIndex3Decision.callFreeCamera);
static_assert(kHerbIndex3Decision.param5);

constexpr std::array kEmptyActorRoute{
    PresentationActorInput{1, 65, 65},
};
constexpr std::array<std::uint16_t, 1> kOneActorAction{1};
constexpr auto kEmptyRoutes = PlanPresentationRoutes(kEmptyActorRoute, kOneActorAction, 0);
constexpr TriggerDerivationInput kSuppressedHerbTrigger{
    .current = {
        255,
        Dq9ActorId({BattleActorSide::ally, 1}),
        Dq9ActorId({BattleActorSide::ally, 3}),
    },
    .previous = {
        255,
        Dq9ActorId({BattleActorSide::ally, 3}),
        Dq9ActorId({BattleActorSide::ally, 0}),
    },
    .turnActionIndex = 4,
    .fixedMetadata = &kValidationMetadata,
    .actorMembershipGroups = kNoMembershipGroups,
    .fallbackMembershipGroups = kNoMembershipGroups,
};
static_assert(!DecideFreeCamera(DeriveTriggerInput(kSuppressedHerbTrigger, kEmptyRoutes)).callFreeCamera);

constexpr bool FallbackRestoresActorFootprint() {
    PresentationActorState actor{
        .actorId = Dq9ActorId({BattleActorSide::ally, 0}),
        .startNode = 40,
        .goalNode = 40,
        .auxiliaryNode = 31,
        .conflictInvalidated = true,
        .occupancyExpansionDepth = 1,
    };
    PresentationOccupancyMap occupancy{};
    occupancy[actor.startNode] = PresentationClassForActor(actor.actorId);
    occupancy[actor.auxiliaryNode] = PresentationClassForActor(actor.actorId);
    const PresentationGoalDecision decision = ChooseFallbackPresentationGoal(actor, occupancy);
    const std::uint8_t actorClass = PresentationClassForActor(actor.actorId);
    return decision.valid
        && occupancy[actor.startNode] == actorClass
        && occupancy[actor.auxiliaryNode] == actorClass;
}
static_assert(FallbackRestoresActorFootprint());

constexpr bool AssignmentInvalidatesTargetNeighborConflict() {
    std::array actors{
        PresentationActorState{
            .actorId = Dq9ActorId({BattleActorSide::ally, 0}),
            .startNode = 0,
            .goalNode = 0,
        },
        PresentationActorState{
            .actorId = Dq9ActorId({BattleActorSide::ally, 1}),
            .startNode = 40,
            .goalNode = 40,
            .worldX = kPresentationNodePositions[40].x,
            .worldZ = kPresentationNodePositions[40].z,
        },
        PresentationActorState{
            .actorId = Dq9ActorId({BattleActorSide::ally, 2}),
            .startNode = 48,
            .goalNode = 48,
            .auxiliaryNode = 48,
        },
    };
    PresentationOccupancyMap occupancy = BuildPresentationOccupancy(actors);
    constexpr std::array<std::uint16_t, 1> actionActors{
        Dq9ActorId({BattleActorSide::ally, 0})
    };
    const PresentationGoalDecision decision = AssignPresentationGoal(
        actors,
        0,
        1,
        occupancy,
        actionActors,
        2,
        PresentationNodeSearchMode::reference
    );
    return decision.valid
        && actors[2].conflictInvalidated
        && actors[2].auxiliaryNode == kInvalidPresentationNode
        && occupancy[48] == 0xff;
}
static_assert(AssignmentInvalidatesTargetNeighborConflict());

constexpr PresentationGoalDecision AssignmentWithActorExpansion(
    const std::uint8_t actorExpansion
) {
    std::array actors{
        PresentationActorState{
            .actorId = Dq9ActorId({BattleActorSide::ally, 0}),
            .startNode = 0,
            .goalNode = 0,
            .occupancyExpansionDepth = actorExpansion,
        },
        PresentationActorState{
            .actorId = Dq9ActorId({BattleActorSide::ally, 1}),
            .startNode = 40,
            .goalNode = 40,
            .worldX = kPresentationNodePositions[40].x,
            .worldZ = kPresentationNodePositions[40].z,
        },
    };
    PresentationOccupancyMap occupancy{};
    occupancy[0] = PresentationClassForActor(0);
    occupancy[40] = PresentationClassForActor(1);
    constexpr std::array<std::uint16_t, 1> actionActors{0};
    return AssignPresentationGoal(
        actors,
        0,
        1,
        occupancy,
        actionActors,
        2,
        PresentationNodeSearchMode::reference
    );
}
constexpr PresentationGoalDecision kNoActorExpansionDecision = AssignmentWithActorExpansion(0);
constexpr PresentationGoalDecision kLargeActorExpansionDecision = AssignmentWithActorExpansion(3);
static_assert(kNoActorExpansionDecision.goalNode == kLargeActorExpansionDecision.goalNode);
static_assert(kNoActorExpansionDecision.auxiliaryNode == kLargeActorExpansionDecision.auxiliaryNode);
} // namespace

int main() {
#ifdef FREECAM_DEBUG_ROUTES
    for (const std::uint16_t actorId : {std::uint16_t{3}, std::uint16_t{1}}) {
        std::cout << actorId << ':';
        const PresentationActorRoute* route = FindPresentationRoute(kHerbRoutesAtIndex3, actorId);
        for (std::size_t index = 0; route != nullptr && index < route->count; ++index) {
            std::cout << (index == 0 ? " " : ",") << static_cast<unsigned>(route->nodes[index]);
        }
        std::cout << '\n';
    }
#endif
    ResetFreeCameraBattle();
    auto& runtime = detail::ThreadContext();
    runtime.retryCounter = 4;
    runtime.previousCommonActionId = 123;
    runtime.hasPreviousAction = true;
    ResetFreeCameraBattle();
    if (!runtime.battleActive || runtime.retryCounter != 0 || runtime.hasPreviousAction) return 1;
    if (!BeginFreeCameraTurn(kHerbTurnOrder)) return 2;
    if (!runtime.currentTurnValid || runtime.currentTurnActionCount != 6 || runtime.previousActionIndex != -1) return 3;
    for (std::size_t index = 0; index < kHerbActorsAtIndex2.size(); ++index) {
        const auto& routeActor = kHerbActorsAtIndex2[index];
        if (!detail::SetPresentationActor(index, {
            .actorId = routeActor.actorId,
            .startNode = routeActor.startNode,
            .goalNode = routeActor.goalNode,
        })) return 4;
    }
    if (!detail::PlanCurrentActionRoutes(2)) return 6;
    if (!RouteEquals(runtime.currentRoutes, 0, kExpectedActor0Route)) return 7;
    if (!detail::CommitActionProgress(1001, 0, 6, {
            255,
            Dq9ActorId({BattleActorSide::ally, 0}),
            Dq9ActorId({BattleActorSide::ally, 1}),
        })) return 8;
    if (detail::CommitActionProgress(1001, 2, 6, {
            255,
            Dq9ActorId({BattleActorSide::ally, 3}),
            Dq9ActorId({BattleActorSide::ally, 0}),
        })) return 9;
    if (!detail::CommitActionProgress(1001, 1, 6, {
            255,
            Dq9ActorId({BattleActorSide::ally, 3}),
            Dq9ActorId({BattleActorSide::ally, 0}),
        })) return 10;
    if (runtime.previousDq9Action.actorId != Dq9ActorId({BattleActorSide::ally, 3})
        || runtime.previousActionIndex != 1) return 11;
    constexpr std::array<BattleActorRef, 61> kTooManyTurnActors{};
    if (BeginFreeCameraTurn(kTooManyTurnActors)) return 12;
    if (runtime.currentTurnValid || runtime.currentTurnActionCount != 0) return 13;
    if (detail::SetTurnActionActor(0, Dq9ActorId({BattleActorSide::ally, 0}))) return 14;
    ResetFreeCameraBattle();
    if (detail::ThreadContext().presentationActorCount != 0) return 15;
    if (!detail::SetPresentationActor(0, {
        .actorId = Dq9ActorId({BattleActorSide::ally, 0}),
        .startNode = 0,
        .goalNode = 0,
    })) return 16;
    if (!detail::SetPresentationActor(1, {
        .actorId = Dq9ActorId({BattleActorSide::ally, 1}),
        .startNode = 40,
        .goalNode = 40,
        .worldX = kPresentationNodePositions[40].x,
        .worldZ = kPresentationNodePositions[40].z,
    })) return 17;
    if (!detail::SetPresentationActor(2, {
        .actorId = Dq9ActorId({BattleActorSide::ally, 2}),
        .startNode = 48,
        .goalNode = 48,
        .auxiliaryNode = 48,
    })) return 18;
    if (!detail::BeginPresentationGoalSetup(PresentationNodeSearchMode::reference)) return 19;
    constexpr std::array<std::uint16_t, 1> setupActionActors{
        Dq9ActorId({BattleActorSide::ally, 0})
    };
    if (!detail::AssignActorPresentationGoal(
            Dq9ActorId({BattleActorSide::ally, 0}),
            Dq9ActorId({BattleActorSide::ally, 1}),
            setupActionActors,
            2,
                                              PresentationNodeSearchMode::reference)) return 20;
    if (!runtime.presentationActors[2].conflictInvalidated
        || runtime.presentationActors[2].auxiliaryNode != kInvalidPresentationNode) return 21;
    if (!detail::AssignActorFallbackPresentationGoal(
            Dq9ActorId({BattleActorSide::ally, 2})
        )) return 22;
    if (!detail::LoadFreeCameraMetadataOnce("freecam-camera-metadata.bin")) return 23;
    const FreeCameraFixedMetadata* fixedMetadata = detail::FreeCameraMetadata();
    if (fixedMetadata == nullptr
        || !fixedMetadata->HasBact(72)
        || fixedMetadata->HasBact(236)
        || !ComputeSelectorSuppression(fixedMetadata->SelectorProjection(255))) return 24;
    // call_once must not reopen or rebuild the cache on later calls.
    if (!detail::LoadFreeCameraMetadataOnce("file-that-does-not-exist.bin")) return 25;
    if (!detail::LoadFreeCameraMembershipMetadataOnce("freecam-membership-metadata.bin")) return 26;
    const FreeCameraMembershipMetadata* membershipMetadata = detail::LoadedFreeCameraMembershipMetadata();
    if (membershipMetadata == nullptr || membershipMetadata->actorProfileCount != 617) return 27;
    const std::uint32_t monster900Profile = membershipMetadata->ResolveMonsterProfile(900);
    if (monster900Profile == kInvalidMembershipProfile) return 28;
    const FreeCameraMembershipCell monster900Attack = membershipMetadata->ActorMembership(monster900Profile, 1);
    if (!monster900Attack.Present() || monster900Attack.selectorProjection != UINT32_C(0x000a0003)) return 29;
    if (membershipMetadata->ResolvePlayerProfile(2, 0) == kInvalidMembershipProfile) return 30;
    // The membership file follows the same startup-only call_once contract.
    if (!detail::LoadFreeCameraMembershipMetadataOnce("file-that-does-not-exist.bin")) return 31;
    if (!detail::LoadFreeCameraActionMetadataOnce("freecam-action-metadata.bin")) return 32;
    const FreeCameraActionMetadata* actionMetadata = detail::LoadedFreeCameraActionMetadata();
    if (actionMetadata == nullptr
        || actionMetadata->FallbackLookupActionId(72) != 72
        || actionMetadata->FallbackLookupActionId(345) != 0x0158
        || actionMetadata->FallbackLookupActionId(1023) != kInvalidFreeCameraActionId) return 33;
    // The action lookup file is also startup-only and is never reopened by the hot path.
    if (!detail::LoadFreeCameraActionMetadataOnce("file-that-does-not-exist.bin")) return 34;
    return 0;
}
