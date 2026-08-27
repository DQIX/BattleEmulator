#pragma once

#include <array>
#include <cstdint>

#include "freecam_fast_runtime.hpp"

namespace dq9::freecam::bindings {

// BattleEmulator.h must be included by the translation unit before this header.
// The array index is the BattleEmulator common action ID. An empty element means
// that the DQ9 runtime action ID has not been confirmed yet.

struct ActionBinding {
    using DecideFunction = fast::TriggerDecision (*)(fast::ActionRuntimeInput) noexcept;
    using CommitFunction = bool (*)(int, int, std::uint16_t, std::uint16_t) noexcept;

    std::uint16_t dq9ActionId{fast::metadata::kInvalidActionId};
    fast::TargetSide targetSide{fast::TargetSide::none_or_context};
    fast::TargetScope targetScope{fast::TargetScope::none_or_context};
    DecideFunction decide{};
    CommitFunction commit{};

    [[nodiscard]] constexpr bool mapped() const noexcept {
        return decide != nullptr;
    }
};

template <std::uint16_t Dq9ActionId, int CommonActionId>
[[nodiscard]] constexpr ActionBinding Bind() noexcept {
    using Action = fast::FreeCamera<Dq9ActionId, CommonActionId>;
    return {
        Dq9ActionId,
        Action::targetSide,
        Action::targetScope,
        +[](const fast::ActionRuntimeInput input) noexcept {
            return fast::Decide<Action>(input);
        },
        +[](const int actionIndex,
            const int turnActionCount,
            const std::uint16_t actorId,
            const std::uint16_t targetId) noexcept {
            return fast::CommitActionProgress<Action>(
                actionIndex,
                turnActionCount,
                actorId,
                targetId
            );
        },
    };
}

inline constexpr auto kFreeCameraActions = [] {
    std::array<ActionBinding, BattleEmulator::INSULATE + 1> actions{};
    actions[BattleEmulator::ATTACK_ALLY] = Bind<1, BattleEmulator::ATTACK_ALLY>();
    actions[BattleEmulator::THUNDER_THRUST] = Bind<72,BattleEmulator::THUNDER_THRUST>();
    actions[BattleEmulator::MEDICINAL_HERBS] = Bind<255, BattleEmulator::MEDICINAL_HERBS>();
    actions[BattleEmulator::INSULATE] = Bind<61, BattleEmulator::INSULATE>();
    actions[BattleEmulator::PSYCHE_UP_ALLY] = Bind<161, BattleEmulator::PSYCHE_UP_ALLY>();
    actions[BattleEmulator::GOSPEL_SONG] = Bind<506, BattleEmulator::GOSPEL_SONG>();
    actions[BattleEmulator::SPECIAL_MEDICINE] = Bind<398, BattleEmulator::GOSPEL_SONG>();;
    actions[BattleEmulator::MAGIC_WATER] = Bind<409, BattleEmulator::MAGIC_WATER>();
    actions[BattleEmulator::ELFIN_ELIXIR] = Bind<411, BattleEmulator::ELFIN_ELIXIR>();
    actions[BattleEmulator::SAGE_ELIXIR] = Bind<410, BattleEmulator::SAGE_ELIXIR>();
    actions[BattleEmulator::RESTORE_MP] = Bind<558, BattleEmulator::RESTORE_MP>();
    actions[BattleEmulator::MAGIC_BURST] = Bind<583, BattleEmulator::MAGIC_BURST>();
    actions[BattleEmulator::MEDITATION] = Bind<587, BattleEmulator::MAGIC_BURST>();
    actions[BattleEmulator::DEFENDING_CHAMPION] = Bind<135, BattleEmulator::DEFENDING_CHAMPION>();
    actions[BattleEmulator::DARK_BREATH] = Bind<534, BattleEmulator::DARK_BREATH>();
    actions[BattleEmulator::PSYCHE_UP] = Bind<161, BattleEmulator::PSYCHE_UP>();
    actions[BattleEmulator::FULLHEAL] = Bind<33, BattleEmulator::FULLHEAL>();
    actions[BattleEmulator::DISRUPTIVE_WAVE] = Bind<189, BattleEmulator::DISRUPTIVE_WAVE>();
    actions[BattleEmulator::MIDHEAL] = Bind<31, BattleEmulator::MIDHEAL>();
    actions[BattleEmulator::LULLAB_EYE] = Bind<563, BattleEmulator::MIDHEAL>();
    actions[BattleEmulator::LIGHTNING_STORM] = Bind<304, BattleEmulator::LIGHTNING_STORM>();
    actions[BattleEmulator::MULTITHRUST] = Bind<73, BattleEmulator::MULTITHRUST>();
    actions[BattleEmulator::MERA_ZOMA] = Bind<11, BattleEmulator::MERA_ZOMA>();
    actions[BattleEmulator::FREEZING_BLIZZARD] = Bind<269, BattleEmulator::FREEZING_BLIZZARD>();
    actions[BattleEmulator::DOUBLE_UP] = Bind<173, BattleEmulator::DOUBLE_UP>();
    actions[BattleEmulator::MORE_HEAL] = Bind<32, BattleEmulator::MORE_HEAL>();
    actions[BattleEmulator::MAGIC_MIRROR] = Bind<137, BattleEmulator::MAGIC_MIRROR>();
    actions[BattleEmulator::CRITICAL_ATTACK] = Bind<244, BattleEmulator::CRITICAL_ATTACK>();
    actions[BattleEmulator::BUFF] = Bind<41, BattleEmulator::BUFF>();
    actions[BattleEmulator::ULTRA_HIGH_SPEED_COMBO] = Bind<542, BattleEmulator::ULTRA_HIGH_SPEED_COMBO>();
    actions[BattleEmulator::SLEEPING] = Bind<>(?, BattleEmulator::SLEEPING());
    actions[BattleEmulator::CURE_SLEEPING] = Bind<?, BattleEmulator::CURE_SLEEPING>();
    actions[BattleEmulator::DEFENCE] = Bind<3, BattleEmulator::DEFENCE>();
    actions[BattleEmulator::CURE_PARALYSIS] = Bind<?, BattleEmulator::CURE_PARALYSIS>();
    actions[BattleEmulator::LAUGH] = Bind<577, BattleEmulator::LAUGH>();
    actions[BattleEmulator::BURNING_BREATH] = Bind<272, BattleEmulator::BURNING_BREATH>();
    actions[BattleEmulator::ATTACK_ENEMY] = Bind<1, BattleEmulator::ATTACK_ALLY>();
    actions[BattleEmulator::SKY_ATTACK] = Bind<540, BattleEmulator::SKY_ATTACK>();
    actions[BattleEmulator::INACTIVE_ALLY] = Bind<?, BattleEmulator::INACTIVE_ALLY>();
    actions[BattleEmulator::PARALYSIS] = Bind<?, BattleEmulator::PARALYSIS>();
    actions[BattleEmulator::INACTIVE_ENEMY] = Bind<?, BattleEmulator::INACTIVE_ALLY>();
    actions[BattleEmulator::HEAL] = Bind<30, BattleEmulator::HEAL>();
    actions[BattleEmulator::MERCURIAL_THRUST] = Bind<69, BattleEmulator::MERCURIAL_THRUST>();

    return actions;
}();

[[nodiscard]] constexpr const ActionBinding* Find(const int commonActionId) noexcept {
    return commonActionId >= 0 && commonActionId <= BattleEmulator::INSULATE
        ? &kFreeCameraActions[static_cast<std::size_t>(commonActionId)]
        : nullptr;
}

static_assert(kFreeCameraActions[BattleEmulator::ATTACK_ALLY].dq9ActionId == 1);
static_assert(kFreeCameraActions[BattleEmulator::THUNDER_THRUST].dq9ActionId == 72);
static_assert(kFreeCameraActions[BattleEmulator::MEDICINAL_HERBS].dq9ActionId == 255);
static_assert(!kFreeCameraActions[BattleEmulator::MERA_ZOMA].mapped());

} // namespace dq9::freecam::bindings
