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

    actions[BattleEmulator::ATTACK_ALLY] = Bind<
        1,
        BattleEmulator::ATTACK_ALLY
    >();
    actions[BattleEmulator::THUNDER_THRUST] = Bind<
        72,
        BattleEmulator::THUNDER_THRUST
    >();
    actions[BattleEmulator::MEDICINAL_HERBS] = Bind<
        255,
        BattleEmulator::MEDICINAL_HERBS
    >();

    actions[BattleEmulator::INSULATE] = Bind<
        1742,
        BattleEmulator::INSULATE
    >();
    actions[BattleEmulator::PSYCHE_UP_ALLY] = {};
    actions[BattleEmulator::GOSPEL_SONG] = {};
    actions[BattleEmulator::SPECIAL_MEDICINE] = {};
    actions[BattleEmulator::MAGIC_WATER] = {};
    actions[BattleEmulator::ELFIN_ELIXIR] = {};
    actions[BattleEmulator::SAGE_ELIXIR] = {};
    actions[BattleEmulator::RESTORE_MP] = {};
    actions[BattleEmulator::MAGIC_BURST] = {};
    actions[BattleEmulator::MEDITATION] = {};
    actions[BattleEmulator::DEFENDING_CHAMPION] = {};
    actions[BattleEmulator::DARK_BREATH] = {};
    actions[BattleEmulator::PSYCHE_UP] = {};
    actions[BattleEmulator::FULLHEAL] = {};
    actions[BattleEmulator::DISRUPTIVE_WAVE] = {};
    actions[BattleEmulator::MIDHEAL] = {};
    actions[BattleEmulator::LULLAB_EYE] = {};
    actions[BattleEmulator::LIGHTNING_STORM] = {};
    actions[BattleEmulator::MULTITHRUST] = {};
    actions[BattleEmulator::MERA_ZOMA] = {};
    actions[BattleEmulator::FREEZING_BLIZZARD] = {};
    actions[BattleEmulator::DOUBLE_UP] = {};
    actions[BattleEmulator::MORE_HEAL] = {};
    actions[BattleEmulator::MAGIC_MIRROR] = {};
    actions[BattleEmulator::CRITICAL_ATTACK] = {};
    actions[BattleEmulator::BUFF] = {};
    actions[BattleEmulator::ULTRA_HIGH_SPEED_COMBO] = {};
    actions[BattleEmulator::SLEEPING] = {};
    actions[BattleEmulator::CURE_SLEEPING] = {};
    actions[BattleEmulator::DEFENCE] = {};
    actions[BattleEmulator::CURE_PARALYSIS] = {};
    actions[BattleEmulator::LAUGH] = {};
    actions[BattleEmulator::BURNING_BREATH] = {};
    actions[BattleEmulator::ATTACK_ENEMY] = {};
    actions[BattleEmulator::SKY_ATTACK] = {};
    actions[BattleEmulator::INACTIVE_ALLY] = {};
    actions[BattleEmulator::PARALYSIS] = {};
    actions[BattleEmulator::INACTIVE_ENEMY] = {};
    actions[BattleEmulator::HEAL] = {};
    actions[BattleEmulator::MERCURIAL_THRUST] = {};

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
