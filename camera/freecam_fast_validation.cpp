#include <array>
#include <cstdint>

#include <BattleEmulator.h>
#include "freecam_action_mapper.hpp"

namespace {
using namespace dq9::freecam;
using namespace dq9::freecam::fast;
using namespace dq9::freecam::bindings;

static_assert(kFreeCameraActions[BattleEmulator::ATTACK_ALLY].dq9ActionId == 1);
static_assert(kFreeCameraActions[BattleEmulator::THUNDER_THRUST].dq9ActionId == 72);
static_assert(kFreeCameraActions[BattleEmulator::MEDICINAL_HERBS].dq9ActionId == 255);
static_assert(!kFreeCameraActions[BattleEmulator::MERA_ZOMA].mapped());

using AttackAllyAction = FreeCamera<1, BattleEmulator::ATTACK_ALLY>;
using ThunderThrustAction = FreeCamera<72, BattleEmulator::THUNDER_THRUST>;
using MedicinalHerbsAction = FreeCamera<255, BattleEmulator::MEDICINAL_HERBS>;

static_assert(AttackAllyAction::targetSide == TargetSide::opponent);
static_assert(AttackAllyAction::targetScope == TargetScope::single_formation);
static_assert(ThunderThrustAction::targetSide == TargetSide::opponent);
static_assert(ThunderThrustAction::targetScope == TargetScope::single);
static_assert(MedicinalHerbsAction::targetSide == TargetSide::ally);
static_assert(MedicinalHerbsAction::targetScope == TargetScope::single);

static_assert(ThunderThrustAction::actionHasBact);
static_assert(ThunderThrustAction::fallbackLookupActionId == 72);
static_assert(MedicinalHerbsAction::actionHasBact);
static_assert(ComputeSelectorSuppression(MedicinalHerbsAction::actionSelectorProjection));
static_assert(sizeof(AttackAllyAction::actorMembershipPacked)
    == metadata::kActorProfileCount * sizeof(std::uint64_t));

constexpr std::uint32_t kMonster900Profile = ResolveMonsterProfile(900);
static_assert(kMonster900Profile != kInvalidMembershipProfile);
constexpr MembershipCell kMonster900Attack = AttackAllyAction::ActorMembership(kMonster900Profile);
static_assert(kMonster900Attack.Present());
static_assert(kMonster900Attack.selectorProjection == UINT32_C(0x000a0003));

constexpr std::uint32_t kPlayer0200Profile = ResolvePlayerProfile(2, 0);
static_assert(kPlayer0200Profile != kInvalidMembershipProfile);

static_assert(Dq9ActorId({BattleActorSide::ally, 0}) == 0x0000);
static_assert(Dq9ActorId({BattleActorSide::enemy, 0}) == 0x00c0);

} // namespace

int main() {
    ResetBattle();
    auto& state = ThreadContext();
    if (!state.battleActive || state.retryCounter != 0 || state.hasPreviousAction) return 1;

    constexpr std::array order{
        BattleActorRef{BattleActorSide::enemy, 0},
        BattleActorRef{BattleActorSide::ally, 0},
    };
    if (!BeginTurn(order)) return 2;
    if (state.currentTurnActionCount != 2 || !state.currentTurnValid) return 3;

    if (!SetPresentationActor(0, {
            .actorId = Dq9ActorId({BattleActorSide::enemy, 0}),
            .startNode = 22,
            .goalNode = 22,
        })) return 4;
    if (!SetMonsterMembershipProfile(0, 900)) return 5;
    if (PresentationMembershipProfileForActor(
            Dq9ActorId({BattleActorSide::enemy, 0})
        ) != kMonster900Profile) return 6;

    return 0;
}
