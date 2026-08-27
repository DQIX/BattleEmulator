#include <array>
#include <cstdint>

#include "freecam_action_mapper.hpp"

namespace {
using namespace dq9::freecam;
using namespace dq9::freecam::fast;
using namespace dq9::freecam::bindings;

static_assert(AttackAlly::dq9ActionId == 1);
static_assert(AttackAlly::commonActionId == BattleEmulator::ATTACK_ALLY);
static_assert(DragonSlash::dq9ActionId == 63);
static_assert(MiracleSlash::dq9ActionId == 65);
static_assert(ThunderThrust::dq9ActionId == 72);
static_assert(MedicinalHerbs::dq9ActionId == 255);
static_assert(MedicinalHerbs::commonActionId == BattleEmulator::MEDICINAL_HERBS);

static_assert(ThunderThrust::actionHasBact);
static_assert(ThunderThrust::fallbackLookupActionId == 72);
static_assert(MedicinalHerbs::actionHasBact);
static_assert(ComputeSelectorSuppression(MedicinalHerbs::actionSelectorProjection));
static_assert(sizeof(AttackAlly::actorMembershipPacked)
    == metadata::kActorProfileCount * sizeof(std::uint64_t));

constexpr std::uint32_t kMonster900Profile = ResolveMonsterProfile(900);
static_assert(kMonster900Profile != kInvalidMembershipProfile);
constexpr MembershipCell kMonster900Attack = AttackAlly::ActorMembership(kMonster900Profile);
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
