//
// Created by Owner on 2024/02/06.
//

#include <cassert>
#include <array>
#include <span>

#include "camera.h"
#include "BattleEmulator.h"
#include "lcg.h"
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
    free_camera_with_tracking_fallback,
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
            return CameraRule::free_camera;
        case BattleEmulator::MERCURIAL_THRUST:
            return CameraRule::free_camera_with_tracking_fallback;
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
        if (!IsActorPresentationMovementEligible(actorSlot, currentTargetId)) continue;

        if (futureIndex == actionIndex) {
            if (!AssignActorPresentationGoal(
                    actorId,
                    currentTargetId,
                    std::span<const std::uint16_t>(currentActionActorIds.data(), currentActionActorIds.size()),
                    currentAction.attackFormationMode)) return false;
            continue;
        }

        // The entry value is compiler-stack residue. If its producer has not
        // been reproduced for this action path yet, do not invent a fallback
        // rule: retain the current-only behavior for that future participant.
        if (!HasRosterField4Compatibility()) continue;
        if (!RosterField4IsZero(actorSlot)) {
            if (!AssignActorFallbackPresentationGoal(actorId)) return false;
            continue;
        }
        if (!targets[futureIndex].valid()) continue;
        const std::uint16_t primaryTargetId = Dq9ActorId(targets[futureIndex]);
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
    return PlanCurrentActionRoutes(actionIndex);
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
            })) return false;
        switch (source.membershipKind) {
            case CameraMembershipKind::player:
                if (!SetPlayerMembershipProfile(index, source.membershipKeyA, source.membershipKeyB)) return false;
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

void camera::Main(int *position, const int32_t *actions, const BattleActorRef *actors, const BattleActorRef *targets,
                  const int actionCount, uint64_t *NowState, bool preemptive1, bool bakuti) {
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
                    runtimeDecision = binding->decide({
                        .actorId = runtimeActorId,
                        .targetId = runtimeTargetId,
                        .turnActionIndex = static_cast<std::uint16_t>(i),
                        .currentActorId = runtimeActorId,
                        .targetPresentationSlot = targetSlot < 0xff ? static_cast<std::uint8_t>(targetSlot) : std::uint8_t{0xff},
                    });
                    hasRuntimeDecision = true;
                }
            }
        }

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
            const bool productionWouldCall = after == BattleEmulator::ZAKI
                ? hasRuntimeDecision && runtimeDecision.callFreeCamera
                : manualWouldCall;
            debugEventIndex = gCameraDebugEventCount;
            auto& debugEvent = gCameraDebugEvents[gCameraDebugEventCount++];
            debugEvent = {
                .turnSerial = debugTurnSerial,
                .actionIndex = i,
                .commonActionId = after,
                .actorId = runtimeActorId,
                .targetId = runtimeTargetId,
                .actorRouteCount = actorRouteCount,
                .maxRouteCount = maxRouteCount,
                .triggerSource = static_cast<std::uint8_t>(runtimeDecision.source),
                .mapped = binding != nullptr && binding->mapped(),
                .runtimeDecisionAvailable = hasRuntimeDecision,
                .runtimeCallFreeCamera = runtimeDecision.callFreeCamera,
                .runtimeParam5 = runtimeDecision.param5,
                .runtimeResetOnly = runtimeDecision.resetOnly,
                .manualRuleWouldCall = manualWouldCall,
                .productionCalledFreeCamera = productionWouldCall,
            };
            const auto& state = ThreadContext();
            debugEvent.presentationActorCount = state.presentationActorCount;
            for (std::size_t actorIndex = 0;
                 actorIndex < state.presentationActorCount && actorIndex < debugEvent.startNodesBefore.size();
                 ++actorIndex) {
                debugEvent.startNodesBefore[actorIndex] = state.presentationActors[actorIndex].startNode;
            }
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
            onFreeCameraMove(position, after, 1, NowState);
#if defined(gerunikku)
            finalizeDebugEvent();
#endif
            continue;
        }

        if (after == BattleEmulator::ZAKI && hasRuntimeDecision) {
            if (runtimeDecision.callFreeCamera) {
                AssertCameraMapping(after);
                onFreeCameraMove(position, after, runtimeDecision.param5 ? 1 : 0, NowState);
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
            if (actionMetadata != nullptr) {
                (void)ApplyKnownRosterField4PostActionCompatibility(actionMetadata->presentationType);
            }
            if (after != BattleEmulator::ATTACK_ALLY) preemptive = false;
#if defined(gerunikku)
            finalizeDebugEvent();
#endif
            continue;
        }
        if (rule != CameraRule::none) {
            AssertCameraMapping(after);
            onFreeCameraMove(position, after, preemptive ? 1 : 0, NowState);

            if (rule == CameraRule::free_camera_with_tracking_fallback
                && !FreeCameraBuilt(NowState)) {
                (*position)++; // free camera不成立時の追尾カメラ TDOD1: これほんまか？追尾カメラとフリーカメラは同時に発生しないか？要検討
            }
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
            (void)ApplyKnownRosterField4PostActionCompatibility(actionMetadata->presentationType);
        }
#if defined(gerunikku)
        finalizeDebugEvent();
#endif
    }
}

void camera::onFreeCameraMove(int *position, const int action, const int param5, uint64_t * NowState) {
    auto counter = ((*NowState) >> 8) & 0xf;
    do {
        if (param5 == 0) {
            (*position)++;
            if (counter == 0) {
                counter++;
                break;
            }
            auto ret = lcg::getPercent(position, 5 - counter);
            if (ret == 0 || counter == 5) {
                counter = 0;
                (*position) += 1;
                if (action == BattleEmulator::ATTACK_ALLY){
                    (*position)+=2;
                }
            } else {
                counter++;
            }
        } else {
            (*position)++;
            if (counter == 0) {
                (*position)++;//引数5が1なら強制的に実行
                counter = 0;
                if (action == BattleEmulator::ATTACK_ALLY){
                    (*position)+=2;
                }
                break;
            }
            (*position)++;
            counter = 0;
            (*position)++;
            if (action == BattleEmulator::ATTACK_ALLY){
                (*position)+=2;
            }

        }
    } while (false);
    (*NowState) &= ~0xf00;
    (*NowState) |= (counter << 8);
}
