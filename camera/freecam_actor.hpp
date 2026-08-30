#pragma once

#include <cstdint>

namespace dq9::freecam::fast {

enum class BattleActorSide : std::uint8_t {
    ally,
    enemy,
};

struct BattleActorRef {
    BattleActorSide side{BattleActorSide::ally};
    std::uint8_t index{0xff};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return index != 0xff;
    }
};

inline constexpr std::uint16_t kInvalidBattleActor = UINT16_C(0xffff);

[[nodiscard]] constexpr std::uint16_t Dq9ActorId(const BattleActorRef actor) noexcept {
    if (!actor.valid()) return kInvalidBattleActor;
    return static_cast<std::uint16_t>(
        actor.index
        + (actor.side == BattleActorSide::enemy ? UINT16_C(0x00c0) : UINT16_C(0))
    );
}

static_assert(Dq9ActorId({BattleActorSide::ally, 0}) == 0x0000);
static_assert(Dq9ActorId({BattleActorSide::ally, 3}) == 0x0003);
static_assert(Dq9ActorId({BattleActorSide::enemy, 0}) == 0x00c0);
static_assert(Dq9ActorId({BattleActorSide::enemy, 3}) == 0x00c3);

} // namespace dq9::freecam::fast
