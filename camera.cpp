//
// Created by Owner on 2024/02/06.
//

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <span>

#include "camera.h"
#include "BattleEmulator.h"
#include "lcg.h"
#include "camera/freecam_action_mapper.hpp"

namespace {

using dq9::freecam::fast::BattleActorRef;
using dq9::freecam::fast::BattleActorSide;

[[nodiscard]] constexpr BattleActorRef ActorRefForBattleIndex(const int actor) noexcept {
    return actor == 0
        ? BattleActorRef{BattleActorSide::ally, 0}
        : BattleActorRef{BattleActorSide::enemy, static_cast<std::uint8_t>(actor - 1)};
}

[[nodiscard]] constexpr std::uint16_t ActorIdForBattleIndex(const int actor) noexcept {
    return actor >= 0 && actor < 4
        ? dq9::freecam::fast::Dq9ActorId(ActorRefForBattleIndex(actor))
        : dq9::freecam::fast::kInvalidBattleActor;
}

inline void ResetFreeCameraCounter(uint64_t* NowState) noexcept {
    (*NowState) &= ~UINT64_C(0xf00);
}

#if defined(gerunikku)
inline void EnsureGerunikuMembershipProfiles() noexcept {
    using namespace dq9::freecam::fast;
    auto& state = ThreadContext();
    if (state.presentationActorCount != 0) return;

    (void)SetPresentationActor(0, {.actorId = ActorIdForBattleIndex(0)});
    (void)SetPresentationActor(1, {.actorId = ActorIdForBattleIndex(1)});
    (void)SetPresentationActor(2, {.actorId = ActorIdForBattleIndex(2)});
    (void)SetPresentationActor(3, {.actorId = ActorIdForBattleIndex(3)});
    (void)SetPlayerMembershipProfile(0, 2, 0);
    (void)SetMonsterMembershipProfile(1, 0x0118);
    (void)SetMonsterMembershipProfile(2, 0x013a);
    (void)SetMonsterMembershipProfile(3, 0x0118);
}
#endif

} // namespace

void camera::Main(int *position, const int32_t *actions, const int *actors, const int *targets,
                  const int actionCount, uint64_t *NowState, bool preemptive, bool bakuti) {
    (void)preemptive;
    (void)bakuti;

    using namespace dq9::freecam;
    using namespace dq9::freecam::fast;

    const int boundedActionCount = std::clamp(actionCount, 0, 8);
    bool hasFreeCameraCandidate = false;
    for (int i = 0; i < boundedActionCount; ++i) {
        if (bindings::MayTrigger(actions[i])) {
            hasFreeCameraCandidate = true;
            break;
        }
    }
    if (!hasFreeCameraCandidate) return;

    auto& runtime = ThreadContext();
    if (!runtime.battleActive) ResetBattle();
#if defined(gerunikku)
    EnsureGerunikuMembershipProfiles();
#endif

    std::array<BattleActorRef, 8> turnOrder{};
    for (int i = 0; i < boundedActionCount; ++i) {
        turnOrder[static_cast<std::size_t>(i)] = ActorRefForBattleIndex(actors[i]);
    }
    if (!BeginTurn(std::span<const BattleActorRef>(turnOrder.data(),
                                                   static_cast<std::size_t>(boundedActionCount)))) {
        return;
    }

    for (int i = 0; i < boundedActionCount; ++i) {
        const int action = actions[i];
        const auto* binding = bindings::Find(action);
        const std::uint16_t actorId = ActorIdForBattleIndex(actors[i]);
        const std::uint16_t targetId = ActorIdForBattleIndex(targets[i]);

        if (binding == nullptr || !binding->mapped()) {
            runtime.previousActionIndex = i;
            runtime.hasPreviousAction = false;
            runtime.targetRecord02161720ActorId = kInvalidBattleActor;
            continue;
        }

        if (!binding->mayTriggerFreeCamera) {
            const bool committed = binding->commit(i, boundedActionCount, actorId, targetId);
            assert(committed);
            (void)committed;
            continue;
        }

        SetTargetRecord02161720ActorId(targetId);

        // Presentation coordinates/routes are not invented here. Until the fixed
        // Geruniku encounter positions are wired from ROM evidence, route planning
        // may remain unavailable; that must not synthesize tracking-camera RNG.
        (void)PlanCurrentActionRoutes(i);

        const TriggerDecision decision = binding->decide({
            .actorId = actorId,
            .targetId = targetId,
            .turnActionIndex = static_cast<std::uint16_t>(i),
            .currentActorId = actorId,
            .targetPresentationSlot = 0xff,
            .actorAndTargetHaveGeometry = false,
        });
        if (decision.resetOnly) {
            ResetFreeCameraCounter(NowState);
        } else if (decision.callFreeCamera) {
            onFreeCameraMove(position, action, decision.param5 ? 1 : 0, NowState);
        }

        const bool committed = binding->commit(i, boundedActionCount, actorId, targetId);
        assert(committed);
        (void)committed;
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
