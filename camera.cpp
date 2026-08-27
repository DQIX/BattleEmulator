//
// Created by Owner on 2024/02/06.
//

#include <array>
#include <span>

#include "camera.h"
#include "BattleEmulator.h"
#include "lcg.h"
#include "camera/freecam_action_mapper.hpp"

namespace {

using dq9::freecam::fast::BattleActorRef;
using dq9::freecam::fast::BattleActorSide;
using dq9::freecam::fast::TargetSide;
using dq9::freecam::fast::TargetScope;
using dq9::freecam::bindings::ActionBinding;

[[nodiscard]] int ActionCount(const int32_t* actions) noexcept {
    int count = 0;
    while (count < 3 && actions[count] >= 0) ++count;
    return count;
}

[[nodiscard]] bool BuildTurnOrder(
    const int actionCount,
    const bool playerHasInitiative,
    std::array<BattleActorRef, 3>& order
) noexcept {
    if (actionCount <= 0 || actionCount > 3) return false;

    constexpr BattleActorRef ally{BattleActorSide::ally, 0};
    constexpr BattleActorRef enemy{BattleActorSide::enemy, 0};

    if (playerHasInitiative) {
        // With fewer than three recorded actions, the old camera API cannot
        // distinguish a skipped ally action from a battle that ended early.
        if (actionCount != 3) return false;
        order[0] = ally;
        order[1] = enemy;
        order[2] = enemy;
        return true;
    }

    order[0] = enemy;
    if (actionCount >= 2) order[1] = enemy;
    if (actionCount == 3) order[2] = ally;
    return true;
}

[[nodiscard]] bool ActorMatchesTargetSide(
    const std::uint16_t actorId,
    const TargetSide side
) noexcept {
    switch (side) {
        case TargetSide::ally:
            return actorId < UINT16_C(0x00c0);
        case TargetSide::opponent:
            return actorId >= UINT16_C(0x00c0);
        default:
            return false;
    }
}

[[nodiscard]] std::uint16_t UniqueActorOnTargetSide(
    const dq9::freecam::fast::RuntimeState& state,
    const TargetSide side
) noexcept {
    std::uint16_t result = dq9::freecam::fast::kInvalidBattleActor;
    for (std::size_t index = 0; index < state.presentationActorCount; ++index) {
        const std::uint16_t candidate = state.presentationActors[index].actorId;
        if (!ActorMatchesTargetSide(candidate, side)) continue;
        if (result != dq9::freecam::fast::kInvalidBattleActor) {
            return dq9::freecam::fast::kInvalidBattleActor;
        }
        result = candidate;
    }
    return result;
}

[[nodiscard]] std::uint16_t TargetActorId(
    const ActionBinding& binding,
    const std::uint16_t actorId,
    const dq9::freecam::fast::RuntimeState& state
) noexcept {
    if (binding.targetScope == TargetScope::self) return actorId;

    // The classification describes the target side/scope, not the selected
    // primary actor. If there is exactly one actor on that side, it is safe to
    // resolve here. Otherwise the old camera API does not contain enough target
    // information and the fast path must not invent one.
    switch (binding.targetScope) {
        case TargetScope::single:
        case TargetScope::single_formation:
        case TargetScope::group:
        case TargetScope::all_on_side:
            return UniqueActorOnTargetSide(state, binding.targetSide);
        default:
            return dq9::freecam::fast::kInvalidBattleActor;
    }
}

struct FastCameraResult {
    bool handled{};
    bool callFreeCamera{};
    bool param5{};
};

[[nodiscard]] FastCameraResult TryFastFreeCamera(
    const int action,
    const int actionIndex,
    const int actionCount
) {
    const ActionBinding* binding = dq9::freecam::bindings::Find(action);
    if (binding == nullptr || !binding->mapped()) return {};

    auto& state = dq9::freecam::fast::ThreadContext();
    if (!state.currentTurnValid
        || state.currentTurnActionCount != actionCount
        || actionIndex < 0
        || actionIndex >= actionCount) return {};

    const std::uint16_t actorId = state.turnActionActors[static_cast<std::size_t>(actionIndex)];
    const std::uint16_t targetId = TargetActorId(*binding, actorId, state);
    if (actorId == dq9::freecam::fast::kInvalidBattleActor
        || targetId == dq9::freecam::fast::kInvalidBattleActor) return {};

    const std::size_t actorSlot = dq9::freecam::fast::FindPresentationActorIndex(actorId);
    if (actorSlot >= state.presentationActorCount) return {};
    if (dq9::freecam::fast::PresentationMembershipProfileForActor(actorId)
        == dq9::freecam::fast::kInvalidMembershipProfile) return {};
    if (!dq9::freecam::fast::PlanCurrentActionRoutes(actionIndex)) return {};

    const std::size_t targetSlot = dq9::freecam::fast::FindPresentationActorIndex(targetId);
    const auto decision = binding->decide({
        .actorId = actorId,
        .targetId = targetId,
        .turnActionIndex = static_cast<std::uint16_t>(actionIndex),
        .currentActorId = actorId,
        .targetPresentationSlot = targetSlot < state.presentationActorCount
            ? static_cast<std::uint8_t>(targetSlot)
            : UINT8_C(0xff),
        .actorAndTargetHaveGeometry = false,
    });

    if (!binding->commit(actionIndex, actionCount, actorId, targetId)) return {};
    return {true, decision.callFreeCamera, decision.param5};
}

} // namespace

void camera::Main(int *position, const int32_t actions[5], uint64_t * NowState, bool preemptive1, bool bakuti) {
    const int actionCount = ActionCount(actions);
    std::array<BattleActorRef, 3> turnOrder{};
    bool fastTurnReady = BuildTurnOrder(actionCount, preemptive1, turnOrder);

    if (fastTurnReady) {
        if (!dq9::freecam::fast::ThreadContext().battleActive) {
            dq9::freecam::fast::ResetBattle();
        }
        fastTurnReady = dq9::freecam::fast::BeginTurn(
            std::span<const BattleActorRef>(turnOrder.data(), static_cast<std::size_t>(actionCount))
        );
    }

    bool preemptive = true;
    auto moture = false;
    for (int i = 0; i < 3; ++i) {
        const int32_t after = actions[i];
        if (after < 0) break;

        //守備力が高すぎる場合(ダメージ0)true、盾ガードは偽
        if (bakuti && after == BattleEmulator::SKY_ATTACK) {
            moture = true;
        }
        if (moture && after == BattleEmulator::MERA_ZOMA) {
            onFreeCameraMove(position, after, 1, NowState);
            continue;
        }

        if (fastTurnReady) {
            const FastCameraResult fast = TryFastFreeCamera(after, i, actionCount);
            if (fast.handled) {
                if (fast.callFreeCamera) {
                    onFreeCameraMove(position, after, fast.param5 ? 1 : 0, NowState);
                }
                continue;
            }
        }

        // Fast runtime cannot be used until this action has a confirmed DQ9 ID
        // and the presentation actor/profile state is available. Preserve the
        // existing camera behavior for those paths.
        if (after == BattleEmulator::ATTACK_ALLY
            || after == BattleEmulator::SKY_ATTACK
            || after == BattleEmulator::MERA_ZOMA) {
            onFreeCameraMove(position, after, preemptive ? 1 : 0, NowState);
        } else if (after == BattleEmulator::MERCURIAL_THRUST) {
            (*position)++;//追尾カメラ
        }
        if (after != BattleEmulator::ATTACK_ALLY) {//味方の攻撃→上空だとフリーカメラが特異点の挙動する
            preemptive = false;
        }
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
