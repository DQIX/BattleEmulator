//
// Created by Owner on 2024/02/06.
//

#include <cassert>
#include <array>
#include <span>

#include "camera.h"
#include "BattleEmulator.h"
#include "lcg.h"
#include "debug.h"
#include "camera/dq9_action_mapper.hpp"
#include "camera/freecam_action_mapper.hpp"

namespace {

using dq9::freecam::fast::BattleActorRef;

#if defined(gerunikku)
thread_local bool gCameraDebugCapture = false;
thread_local int gCameraDebugTurnSerial = 0;
thread_local std::array<CameraDebugEvent, 1024> gCameraDebugEvents{};
thread_local std::size_t gCameraDebugEventCount = 0;
#endif

enum class CameraRule {
    none,
    free_camera,
};

[[nodiscard]] constexpr CameraRule RuleForAction(const int action) noexcept {
    switch (action) {
        case BattleEmulator::ATTACK_ENEMY:
        case BattleEmulator::ATTACK_ALLY:
        case BattleEmulator::BEAST_THRUST:
        case BattleEmulator::VITAL_POINT_THRUST:
        case BattleEmulator::THUNDER_THRUST:
        case BattleEmulator::SKY_ATTACK:
        case BattleEmulator::MERA_ZOMA:
        case BattleEmulator::MERCURIAL_THRUST:
            return CameraRule::free_camera;
        default:
            return CameraRule::none;
    }
}

inline void AssertCameraMapping(const int action) noexcept {
    const auto* binding = dq9::freecam::bindings::Find(action);
    assert(binding != nullptr && binding->mapped());
    (void)binding;
}

[[nodiscard]] bool FreeCameraBuilt(const uint64_t* NowState) noexcept {
    return (((*NowState) >> 8) & UINT64_C(0xf)) == 0;
}

[[nodiscard]] bool SetupCurrentAndFuturePresentationGoals(
    const int actionIndex,
    const int actionCount,
    const int32_t* actions,
    const BattleActorRef* actors,
    const BattleActorRef* targets,
    const dq9::freecam::actions::ActionMetadata& currentAction,
    const std::uint16_t currentActorId,
    const std::uint16_t currentTargetId
) noexcept {
    using namespace dq9::freecam::fast;
    if (!BeginPresentationGoalSetup()) return false;

    const auto presentationRecordTargetId = [](const int commonActionId,
                                               const std::uint16_t actorId,
                                               const std::uint16_t rawTargetId) noexcept {
        const auto* metadata = dq9::freecam::actions::Find(commonActionId);
        return metadata != nullptr && metadata->mapped()
            && metadata->targetScope == TargetScope::self
            ? actorId
            : rawTargetId;
    };
    const std::uint16_t currentPresentationTargetId =
        currentAction.targetScope == TargetScope::self ? currentActorId : currentTargetId;

    const std::array<std::uint16_t, 1> currentActionActorIds{currentActorId};
    std::array<bool, dq9::freecam::detail::kMaxPresentationActors> visited{};
    for (int futureIndex = actionIndex; futureIndex < actionCount; ++futureIndex) {
        if (actions[futureIndex] < 0 || !actors[futureIndex].valid()) continue;
        const std::uint16_t actorId = Dq9ActorId(actors[futureIndex]);
        const std::size_t actorSlot = FindPresentationActorIndex(actorId);
        if (actorSlot >= visited.size() || visited[actorSlot]) continue;
        // overlay_d_25:021E0AA0 marks the suffix actor visited before the
        // movement-eligibility test, so later actions by the same actor do not
        // get a second chance in this setup pass.
        visited[actorSlot] = true;
        if (!IsActorPresentationMovementEligible(actorSlot, currentPresentationTargetId)) continue;

        if (futureIndex == actionIndex) {
            if (!AssignActorPresentationGoal(
                    actorId,
                    currentPresentationTargetId,
                    std::span<const std::uint16_t>(currentActionActorIds.data(), currentActionActorIds.size()),
                    currentAction.attackFormationMode)) return false;
            continue;
        }

        // The entry value is compiler-stack residue. If its producer has not
        // been reproduced for this action path yet, do not invent a fallback
        // rule: retain the current-only behavior for that future participant.
        if (!RosterField4IsKnown(actorSlot)) continue;
        if (!RosterField4IsZero(actorSlot)) {
            if (!AssignActorFallbackPresentationGoal(actorId, true)) return false;
            continue;
        }
        if (!targets[futureIndex].valid()) continue;
        const std::uint16_t primaryTargetId = presentationRecordTargetId(
            actions[futureIndex],
            actorId,
            Dq9ActorId(targets[futureIndex])
        );
        const std::uint16_t resolvedTargetId = ResolveActorPresentationTarget(actorSlot, primaryTargetId);
        if (resolvedTargetId == kInvalidBattleActor) continue;
        // Live 021E0D34..021E0D5C passes the current action record to
        // 021E1FD8 even when the actor/target came from a future action.
        if (!AssignActorPresentationGoal(
                actorId,
                resolvedTargetId,
                std::span<const std::uint16_t>(currentActionActorIds.data(), currentActionActorIds.size()),
                currentAction.attackFormationMode)) return false;
    }

    // overlay_d_25:021E08BC has a second, distinct loop after the current/future
    // suffix pass. It revisits actor participants from action records strictly
    // before the current action, sharing the same visited set. Each previous
    // action rebuilds the occupancy map before its actor chain is examined.
    // BattleEmulator action records currently expose one acting BattleActorRef
    // per action, which is the head/only member of the ROM +0x10 actor chain.
    for (int previousIndex = 0; previousIndex < actionIndex; ++previousIndex) {
        if (!RebuildPresentationOccupancy()) return false;
        if (actions[previousIndex] < 0
            || !actors[previousIndex].valid()
            || !targets[previousIndex].valid()) continue;

        const std::uint16_t actorId = Dq9ActorId(actors[previousIndex]);
        const std::size_t actorSlot = FindPresentationActorIndex(actorId);
        if (actorSlot >= visited.size() || visited[actorSlot]) continue;
        visited[actorSlot] = true;
        if (!IsActorPresentationMovementEligible(actorSlot, currentPresentationTargetId)) continue;

        const std::uint16_t primaryTargetId = presentationRecordTargetId(
            actions[previousIndex],
            actorId,
            Dq9ActorId(targets[previousIndex])
        );
        const bool row4Nonzero = RosterField4IsKnown(actorSlot) && !RosterField4IsZero(actorSlot);
        if (!PreparePreviousActionPresentationParticipant(actorId, primaryTargetId, row4Nonzero)) return false;
    }
    return PlanCurrentActionRoutes(actionIndex);
}

[[nodiscard]] bool ApplyMinedCameraPlacementBounds(
    const std::uint16_t dq9ActionId,
    const std::uint16_t actorId,
    const std::uint16_t targetId
) noexcept {
    using namespace dq9::freecam::fast;
    std::uint8_t sequence = metadata::CameraPlacementSequence(dq9ActionId);
    for (unsigned index = 0; index < 4; ++index) {
        const std::uint8_t placement = sequence & UINT8_C(0x3);
        if (placement == 0) break;
        if (placement > 2) return false;
        const std::uint16_t placedActorId = placement == 1 ? actorId : targetId;
        if (placedActorId == kInvalidBattleActor || !ApplyCameraActorWorldBounds(placedActorId)) {
            return false;
        }
        sequence >>= 2;
    }
    return true;
}

} // namespace

#if defined(gerunikku)
void camera::SetDebugCapture(const bool enabled) noexcept {
    gCameraDebugCapture = enabled;
}

void camera::ClearDebugEvents() noexcept {
    gCameraDebugEventCount = 0;
    gCameraDebugTurnSerial = 0;
}

std::size_t camera::DebugEventCount() noexcept {
    return gCameraDebugEventCount;
}

CameraDebugEvent camera::DebugEventAt(const std::size_t index) noexcept {
    return index < gCameraDebugEventCount ? gCameraDebugEvents[index] : CameraDebugEvent{};
}
#endif

bool camera::ResetBattle(const CameraPresentationActor *actors, const std::size_t actorCount) {
    using namespace dq9::freecam::fast;
    if (actors == nullptr || actorCount > dq9::freecam::detail::kMaxPresentationActors) return false;
    dq9::freecam::fast::ResetBattle();
    for (std::size_t index = 0; index < actorCount; ++index) {
        const CameraPresentationActor &source = actors[index];
        const std::uint16_t actorId = Dq9ActorId(source.actor);
        if (actorId == kInvalidBattleActor) return false;
        const std::uint8_t node = dq9::freecam::detail::NearestPresentationNodeFast(source.worldX, source.worldZ);
        if (!SetPresentationActor(index, {
                .actorId = actorId,
                .startNode = node,
                .goalNode = node,
                .movementEnabled = source.movementEnabled,
                .occupancyExpansionDepth = source.occupancyExpansionDepth,
                .presentationFlags = source.presentationFlags,
                .worldX = source.worldX,
                .worldY = source.worldY,
                .worldZ = source.worldZ,
                .battleWorldKnown = source.battleWorldKnown,
                .battleWorldX = source.battleWorldX,
                .battleWorldY = source.battleWorldY,
                .battleWorldZ = source.battleWorldZ,
            })) return false;
        switch (source.membershipKind) {
            case CameraMembershipKind::player:
                if (!SetPlayerMembershipProfileFromEquipment(
                        index,
                        source.membershipKeyA,
                        source.membershipKeyB
                    )) return false;
                break;
            case CameraMembershipKind::monster:
                if (!SetMonsterMembershipProfile(index, source.membershipKeyA)) return false;
                if (source.battleMonsterId != 0xffff
                    && !SetMonsterPresentationMetadata(index, source.battleMonsterId)) return false;
                break;
            case CameraMembershipKind::special:
                if (!SetSpecialActorMembershipProfile(index, source.membershipKeyA)) return false;
                break;
            case CameraMembershipKind::none:
                break;
        }
    }
    return true;
}

camera::RuntimeSnapshot camera::CaptureRuntimeState() noexcept {
    return dq9::freecam::fast::ThreadContext();
}

void camera::RestoreRuntimeState(const RuntimeSnapshot& state) noexcept {
    dq9::freecam::fast::ThreadContext() = state;
}

void camera::BindRuntimeState(RuntimeSnapshot* state) noexcept {
    dq9::freecam::fast::BindThreadContext(state);
}

void camera::UnbindRuntimeState() noexcept {
    dq9::freecam::fast::UnbindThreadContext();
}

void camera::Main(int *position, const int32_t *actions, const BattleActorRef *actors, const BattleActorRef *targets,
                  const std::uint8_t *slot1ChildCounts,
                  const std::uint16_t *slot1LastChildActionIds,
                  const int actionCount, uint64_t *NowState, bool preemptive1, bool bakuti,
                  const bool traceBoundaries) {
    (void)preemptive1;

    using namespace dq9::freecam::fast;
    std::array<BattleActorRef, dq9::freecam::detail::kMaxPresentationActions> actionOrder{};
    if (actionCount < 0 || static_cast<std::size_t>(actionCount) > actionOrder.size()) return;
    for (int i = 0; i < actionCount; ++i) actionOrder[static_cast<std::size_t>(i)] = actors[i];
    const bool runtimeReady = BeginTurn(std::span<const BattleActorRef>(actionOrder.data(), actionCount));
#if defined(gerunikku)
    const int debugTurnSerial = gCameraDebugTurnSerial++;
#endif

    bool preemptive = true;
    auto moture = false;
    auto processSlot1CleanupPresentationRecord = [&](const int sourceActionIndex,
                                                     const std::uint16_t actorId) noexcept {
        if (!runtimeReady || actorId == kInvalidBattleActor
            || slot1ChildCounts == nullptr
            || sourceActionIndex < 0 || sourceActionIndex >= actionCount
            || slot1ChildCounts[sourceActionIndex] == 0) {
            return;
        }

        using CleanupAction = FreeCamera<metadata::kSlot1CleanupPresentationActionId>;
        const auto& beforeCleanup = ThreadContext();
        const std::size_t actorSlot = FindPresentationActorIndex(actorId);
        const std::uint8_t actorAuxiliaryNode = actorSlot < beforeCleanup.presentationActorCount
            ? beforeCleanup.presentationActors[actorSlot].auxiliaryNode
            : std::uint8_t{0xff};
        const TriggerDecision cleanupDecision = Decide<CleanupAction>({
            .actorId = actorId,
            .targetId = actorId,
            .turnActionIndex = static_cast<std::uint16_t>(beforeCleanup.presentationActionRecordIndex + 1),
            .targetAuxiliaryNode = actorAuxiliaryNode,
        });

        DEBUG_TRACE_IF(traceBoundaries,
                       std::cout << "TRACE presentation slot1-child sourceActionIndex=" << sourceActionIndex
                                 << " count=" << static_cast<unsigned>(slot1ChildCounts[sourceActionIndex])
                                 << " childAction="
                                 << (slot1LastChildActionIds != nullptr
                                         ? slot1LastChildActionIds[sourceActionIndex]
                                         : UINT16_C(0xffff))
                                 << " syntheticAction="
                                 << metadata::kSlot1CleanupPresentationActionId
                                 << " actor=0x" << std::hex << actorId << std::dec
                                 << " call=" << cleanupDecision.callFreeCamera
                                 << " param5=" << cleanupDecision.param5 << '\n');

        if (cleanupDecision.callFreeCamera) {
            onFreeCameraMove(position,
                             -1,
                             cleanupDecision.param5 ? 1 : 0,
                             NowState,
                             traceBoundaries);
        }
        constexpr std::uint8_t cleanupTrackingRngCount =
            metadata::TrackingCameraOneRngCount(metadata::kSlot1CleanupPresentationActionId);
        for (std::uint8_t trackingIndex = 0; trackingIndex < cleanupTrackingRngCount; ++trackingIndex) {
            DEBUG_TRACE_IF(traceBoundaries,
                           std::cout << "TRACE rng lr=0x0216f0e4 max=8 consume=" << *position << '\n');
            (*position)++;
        }

        (void)AppendSlot1CleanupPresentationRecord(actorId);
        (void)ApplyKnownRosterField4PostActionCompatibility(
            metadata::PresentationType(metadata::kSlot1CleanupPresentationActionId)
        );

#if defined(gerunikku)
        if (gCameraDebugCapture && gCameraDebugEventCount < gCameraDebugEvents.size()) {
            auto& event = gCameraDebugEvents[gCameraDebugEventCount++];
            event = {
                .turnSerial = debugTurnSerial,
                .actionIndex = sourceActionIndex,
                .commonActionId = -1,
                .dq9ActionId = metadata::kSlot1CleanupPresentationActionId,
                .actorId = actorId,
                .targetId = actorId,
                .triggerSource = static_cast<std::uint8_t>(cleanupDecision.source),
                .mapped = true,
                .runtimeDecisionAvailable = true,
                .runtimeCallFreeCamera = cleanupDecision.callFreeCamera,
                .runtimeParam5 = cleanupDecision.param5,
                .runtimeResetOnly = cleanupDecision.resetOnly,
                .productionCalledFreeCamera = cleanupDecision.callFreeCamera,
                .syntheticPresentationRecord = true,
                .slot1ChildCount = slot1ChildCounts[sourceActionIndex],
                .slot1LastChildActionId = static_cast<std::uint16_t>(
                    slot1LastChildActionIds != nullptr
                        ? slot1LastChildActionIds[sourceActionIndex]
                        : UINT16_C(0xffff)
                ),
            };
            const auto& state = ThreadContext();
            event.presentationActorCount = state.presentationActorCount;
            for (std::size_t index = 0;
                 index < state.presentationActorCount && index < event.startNodesAfter.size(); ++index) {
                event.startNodesBefore[index] = state.presentationActors[index].startNode;
                event.startNodesAfter[index] = state.presentationActors[index].startNode;
            }
        }
#endif
    };
    for (int i = 0; i < actionCount; ++i) {
        const int32_t after = actions[i];
        if (after < 0) break;

        const auto* actionMetadata = dq9::freecam::actions::Find(after);
        const auto* binding = dq9::freecam::bindings::Find(after);
        const bool hasActionMetadata = actionMetadata != nullptr && actionMetadata->mapped();
        TriggerDecision runtimeDecision{};
        bool hasRuntimeDecision = false;
        bool hasPresentationSetup = false;
        std::uint16_t runtimeActorId = kInvalidBattleActor;
        std::uint16_t runtimeTargetId = kInvalidBattleActor;
        if (runtimeReady && hasActionMetadata
            && actors[i].valid() && targets[i].valid()) {
            runtimeActorId = Dq9ActorId(actors[i]);
            runtimeTargetId = Dq9ActorId(targets[i]);
            if (SetupCurrentAndFuturePresentationGoals(
                    i,
                    actionCount,
                    actions,
                    actors,
                    targets,
                    *actionMetadata,
                    runtimeActorId,
                    runtimeTargetId)) {
                hasPresentationSetup = true;
                if (binding != nullptr && binding->mapped()) {
                    const std::size_t targetSlot = FindPresentationActorIndex(runtimeTargetId);
                    const auto& presentationState = ThreadContext();
                    const std::uint8_t targetAuxiliaryNode = targetSlot < presentationState.presentationActorCount
                        ? presentationState.presentationActors[targetSlot].auxiliaryNode
                        : std::uint8_t{0xff};
                    runtimeDecision = binding->decide({
                        .actorId = runtimeActorId,
                        .targetId = runtimeTargetId,
                        .turnActionIndex = static_cast<std::uint16_t>(presentationState.presentationActionRecordIndex + 1),
                        .targetAuxiliaryNode = targetAuxiliaryNode,
                    });
                    hasRuntimeDecision = true;
                }
            }
        }
        const TrackingCameraDecision trackingCameraDecision = hasActionMetadata
            ? TrackingCameraFor(actionMetadata->dq9ActionId, runtimeActorId)
            : TrackingCameraDecision{};

        const CameraRule rule = RuleForAction(after);
#if defined(gerunikku)
        std::size_t debugEventIndex = gCameraDebugEvents.size();
        if (gCameraDebugCapture && gCameraDebugEventCount < gCameraDebugEvents.size()) {
            std::uint8_t actorRouteCount = 0;
            std::uint8_t maxRouteCount = 0;
            if (hasRuntimeDecision) {
                if (const auto* route = FindCurrentRoute(runtimeActorId); route != nullptr) {
                    actorRouteCount = route->count;
                }
                const auto& state = ThreadContext();
                for (std::size_t routeIndex = 0; routeIndex < state.currentRoutes.actorCount; ++routeIndex) {
                    if (state.currentRoutes.actors[routeIndex].count > maxRouteCount) {
                        maxRouteCount = state.currentRoutes.actors[routeIndex].count;
                    }
                }
            }
            const bool manualWouldCall = rule != CameraRule::none;
            const bool productionWouldCall = hasRuntimeDecision
                ? runtimeDecision.callFreeCamera
                : manualWouldCall;
            const std::uint32_t membershipProfile =
                PresentationMembershipProfileForActor(runtimeActorId);
            const std::uint16_t actorMembershipCount = hasActionMetadata
                ? DecodeMembershipCell(metadata::ActorMembershipPacked(
                    membershipProfile,
                    actionMetadata->dq9ActionId
                )).count
                : 0;
            debugEventIndex = gCameraDebugEventCount;
            auto& debugEvent = gCameraDebugEvents[gCameraDebugEventCount++];
            debugEvent = {
                .turnSerial = debugTurnSerial,
                .actionIndex = i,
                .commonActionId = after,
                .dq9ActionId = actionMetadata != nullptr
                    ? actionMetadata->dq9ActionId
                    : metadata::kInvalidActionId,
                .presentationType = actionMetadata != nullptr
                    ? actionMetadata->presentationType
                    : UINT8_C(0xff),
                .actorId = runtimeActorId,
                .targetId = runtimeTargetId,
                .actorRouteCount = actorRouteCount,
                .maxRouteCount = maxRouteCount,
                .membershipProfile = membershipProfile,
                .actorMembershipCount = actorMembershipCount,
                .triggerSource = static_cast<std::uint8_t>(runtimeDecision.source),
                .mapped = binding != nullptr && binding->mapped(),
                .runtimeDecisionAvailable = hasRuntimeDecision,
                .runtimeCallFreeCamera = runtimeDecision.callFreeCamera,
                .runtimeParam5 = runtimeDecision.param5,
                .runtimeResetOnly = runtimeDecision.resetOnly,
                .manualRuleWouldCall = manualWouldCall,
                .productionCalledFreeCamera = productionWouldCall,
                .syntheticPresentationRecord = false,
                .slot1ChildCount = static_cast<std::uint8_t>(
                    slot1ChildCounts != nullptr ? slot1ChildCounts[i] : UINT8_C(0)
                ),
                .slot1LastChildActionId = static_cast<std::uint16_t>(
                    slot1LastChildActionIds != nullptr
                        ? slot1LastChildActionIds[i]
                        : UINT16_C(0xffff)
                ),
            };
            const auto& state = ThreadContext();
            debugEvent.presentationActorCount = state.presentationActorCount;
            debugEvent.routeActorCount = state.currentRoutes.actorCount;
            for (std::size_t routeIndex = 0;
                 routeIndex < state.currentRoutes.actorCount && routeIndex < debugEvent.routeActorIds.size();
                 ++routeIndex) {
                const auto& route = state.currentRoutes.actors[routeIndex];
                debugEvent.routeActorIds[routeIndex] = route.actorId;
                debugEvent.routeCounts[routeIndex] = route.count;
                for (std::size_t nodeIndex = 0;
                     nodeIndex < route.count && nodeIndex < debugEvent.routeNodes[routeIndex].size();
                     ++nodeIndex) {
                    debugEvent.routeNodes[routeIndex][nodeIndex] = route.nodes[nodeIndex];
                }
            }
            for (std::size_t actorIndex = 0;
                 actorIndex < state.presentationActorCount && actorIndex < debugEvent.startNodesBefore.size();
                 ++actorIndex) {
                debugEvent.startNodesBefore[actorIndex] = state.presentationActors[actorIndex].startNode;
                debugEvent.goalNodes[actorIndex] = state.presentationActors[actorIndex].goalNode;
                debugEvent.targetNodes[actorIndex] = state.presentationActors[actorIndex].targetNode;
                debugEvent.auxiliaryNodes[actorIndex] = state.presentationActors[actorIndex].auxiliaryNode;
                debugEvent.rosterField4Known[actorIndex] = state.rosterField4Known[actorIndex];
                debugEvent.rosterField4Nonzero[actorIndex] = state.rosterField4Nonzero[actorIndex];
            }
            debugEvent.presentationOccupancy = state.presentationOccupancy;
        }
        auto finalizeDebugEvent = [&]() noexcept {
            if (debugEventIndex >= gCameraDebugEventCount) return;
            auto& debugEvent = gCameraDebugEvents[debugEventIndex];
            const auto& state = ThreadContext();
            for (std::size_t actorIndex = 0;
                 actorIndex < state.presentationActorCount && actorIndex < debugEvent.startNodesAfter.size();
                 ++actorIndex) {
                debugEvent.startNodesAfter[actorIndex] = state.presentationActors[actorIndex].startNode;
            }
        };
#endif

        //守備力が高すぎる場合(ダメージ0)true、盾ガードは偽
        if (bakuti && after == BattleEmulator::SKY_ATTACK) {
            moture = true;
        }
        if (moture && after == BattleEmulator::MERA_ZOMA) {
            AssertCameraMapping(after);
            onFreeCameraMove(position, after, 1, NowState, traceBoundaries);
#if defined(gerunikku)
            finalizeDebugEvent();
#endif
            continue;
        }

        if (after == BattleEmulator::ZAKI && hasRuntimeDecision) {
            if (runtimeDecision.callFreeCamera) {
                AssertCameraMapping(after);
                onFreeCameraMove(position, after, runtimeDecision.param5 ? 1 : 0, NowState,
                                 traceBoundaries);
            }
            if (hasPresentationSetup && actionMetadata != nullptr) {
                (void)ApplyMinedCameraPlacementBounds(
                    actionMetadata->dq9ActionId,
                    runtimeActorId,
                    runtimeTargetId
                );
            }
            if (hasPresentationSetup && actionMetadata != nullptr) {
                (void)CommitActionProgressRaw(
                    i,
                    actionCount,
                    after,
                    actionMetadata->dq9ActionId,
                    runtimeActorId,
                    runtimeTargetId
                );
            }
            (void)CompleteActionPresentation(runtimeActorId, i);
            processSlot1CleanupPresentationRecord(i, runtimeActorId);
            if (actionMetadata != nullptr) {
                (void)ApplyKnownRosterField4PostActionCompatibility(actionMetadata->presentationType);
            }
            if (after != BattleEmulator::ATTACK_ALLY) preemptive = false;
#if defined(gerunikku)
            finalizeDebugEvent();
#endif
            continue;
        }
        if (hasRuntimeDecision) {
            if (runtimeDecision.callFreeCamera) {
                AssertCameraMapping(after);
                onFreeCameraMove(position, after, runtimeDecision.param5 ? 1 : 0,
                                 NowState, traceBoundaries);
            }
        } else if (rule != CameraRule::none) {
            AssertCameraMapping(after);
            onFreeCameraMove(position, after, preemptive ? 1 : 0, NowState, traceBoundaries);
        }
        if (hasPresentationSetup && actionMetadata != nullptr) {
            (void)ApplyMinedCameraPlacementBounds(
                actionMetadata->dq9ActionId,
                runtimeActorId,
                runtimeTargetId
            );
        }
        // ROM presentation order: optional free-camera selector/retry runs before
        // the action's tracking-camera setup. FUN_0216F038 consumes RandInt(8)
        // at 0x0216F0E0 (return/LR observation 0x0216F0E4).
        for (std::uint16_t trackingIndex = 0;
             trackingIndex < trackingCameraDecision.rngCount;
             ++trackingIndex) {
            DEBUG_TRACE_IF(traceBoundaries,
                           std::cout << "TRACE rng lr=0x0216f0e4 max=8 consume=" << *position << '\n');
            (*position)++; // lr: 0x0216f0e4, max: 8
        }
        if (after != BattleEmulator::ATTACK_ALLY) {//味方の攻撃→上空だとフリーカメラが特異点の挙動する
            preemptive = false;
        }
        if (hasPresentationSetup && actionMetadata != nullptr) {
            (void)CommitActionProgressRaw(
                i,
                actionCount,
                after,
                actionMetadata->dq9ActionId,
                runtimeActorId,
                runtimeTargetId
            );
            (void)CompleteActionPresentation(runtimeActorId, i);
            // DQ9 929 is presentation type 0, but seed-0x13 live-ROM tracing
            // proves that the independent battle-HUD renderer executes while
            // it is active:
            //   020515EC -> 02051880 -> 02050678 -> 02050BD0 -> 0204F284.
            // Apply that renderer's ROM-mined ABI footprint directly instead
            // of pretending the residue is a presentation-type-15 property.
            if (after == BattleEmulator::WHIPPING_BOY) {
                const bool applied = ApplyBattleHudRendererResidueCompatibility();
                assert(applied && "battle HUD renderer residue compatibility failed");
            } else {
                (void)ApplyKnownRosterField4PostActionCompatibility(actionMetadata->presentationType);
            }
            processSlot1CleanupPresentationRecord(i, runtimeActorId);
        }
#if defined(gerunikku)
        finalizeDebugEvent();
#endif
    }
}

void camera::onFreeCameraMove(int *position, const int action, const int param5, uint64_t * NowState,
                              const bool traceBoundaries) {
    auto counter = ((*NowState) >> 8) & 0xf;
    do {
        if (param5 == 0) {
            DEBUG_TRACE_IF(traceBoundaries,
                           std::cout << "TRACE rng lr=0x0216fe40 max=100 consume=" << *position << '\n');
            (*position)++;//(void)lcg::getPercent(position, 100);
            if (counter == 0) {
                counter++;
                break;
            }
            DEBUG_TRACE_IF(traceBoundaries,
                           std::cout << "TRACE rng lr=0x0216fe68 max=" << (5 - counter)
                                     << " consume=" << *position << '\n');
            auto ret = lcg::getPercent(position, 5 - counter); // lr=0x0216fe68
            if (ret == 0 || counter == 5) {
                counter = 0;
                DEBUG_TRACE_IF(traceBoundaries,
                               std::cout << "TRACE rng lr=0x0216fff8 max=4 consume=" << *position << '\n');
                (*position)++;//lr=0x0216fff8 max=4　(void)lcg::getPercent(position, 4); // 「(void)lcg::」はアホの姿焼きなので、晒しておく
            } else {
                counter++;
            }
        } else {
            DEBUG_TRACE_IF(traceBoundaries,
                           std::cout << "TRACE rng lr=0x0216fe40 max=100 consume=" << *position << '\n');
            (*position)++;//lr=0x0216fe40 max=100 (void)lcg::getPercent(position, 100);
            if (counter == 0) {
                DEBUG_TRACE_IF(traceBoundaries,
                               std::cout << "TRACE rng lr=0x0216fff8 max=4 consume=" << *position << '\n');
                (*position)++;//lr=0x0216fff8 max=4 (void)lcg::getPercent(position, 4);//引数5が1なら強制的に実行
                counter = 0;
                break;
            }
            DEBUG_TRACE_IF(traceBoundaries,
                           std::cout << "TRACE rng lr=0x0216fe68 max=" << (5 - counter)
                                     << " consume=" << *position << '\n');
            (*position)++;//lr=0x0216fe68 (void)lcg::getPercent(position, 5 - counter);
            counter = 0;
            DEBUG_TRACE_IF(traceBoundaries,
                           std::cout << "TRACE rng lr=0x0216fff8 max=4 consume=" << *position << '\n');
            (*position)++;//lr=0x0216fff8 max=4 (void)lcg::getPercent(position, 4);

        }
    } while (false);
    (*NowState) &= ~0xf00;
    (*NowState) |= (counter << 8);
}
