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
    std::uint8_t attackFormationMode{};
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
        Action::attackFormationMode,
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
    std::array<ActionBinding, BattleEmulator::CURE_CONFUSION + 1> actions{};

    actions[BattleEmulator::ATTACK_ENEMY] = Bind<1, BattleEmulator::ATTACK_ENEMY>();
    actions[BattleEmulator::ATTACK_ALLY] = Bind<1, BattleEmulator::ATTACK_ALLY>();
    actions[BattleEmulator::SKY_ATTACK] = Bind<540, BattleEmulator::SKY_ATTACK>();
    actions[BattleEmulator::MERA_ZOMA] = Bind<11, BattleEmulator::MERA_ZOMA>();
    actions[BattleEmulator::MERCURIAL_THRUST] = Bind<69, BattleEmulator::MERCURIAL_THRUST>();
    actions[BattleEmulator::THUNDER_THRUST] = Bind<72,BattleEmulator::THUNDER_THRUST>();
    actions[BattleEmulator::BEAST_THRUST] = Bind<70, BattleEmulator::BEAST_THRUST>();
    actions[BattleEmulator::VITAL_POINT_THRUST] = Bind<71, BattleEmulator::VITAL_POINT_THRUST>();
    actions[BattleEmulator::ZAKI] = Bind<24, BattleEmulator::ZAKI>();
    actions[BattleEmulator::ZARAKI] = Bind<25, BattleEmulator::ZARAKI>();
    // Live battle_damage_trace evidence in the Geruniku encounter:
    // common GERUNIKKU_MERAMI executes DQ9 action 0x000A and
    // common GERUNIKKU_BAGIMA executes DQ9 action 0x0013.  These bindings are
    // required even when the fixed metadata ultimately suppresses free camera,
    // because their presentation routes mutate the state consumed by later
    // actions (notably Zaki).  Do not replace this with encounter-specific
    // route patches in camera.cpp.
    actions[BattleEmulator::GERUNIKKU_MERAMI] = Bind<10, BattleEmulator::GERUNIKKU_MERAMI>();
    actions[BattleEmulator::GERUNIKKU_BAGIMA] = Bind<19, BattleEmulator::GERUNIKKU_BAGIMA>();
    actions[BattleEmulator::WHIPPING_BOY] = Bind<109, BattleEmulator::WHIPPING_BOY>();
    actions[BattleEmulator::MEDICINAL_HERBS] = Bind<255, BattleEmulator::MEDICINAL_HERBS>();
    return actions;
}();

[[nodiscard]] constexpr const ActionBinding* Find(const int commonActionId) noexcept {
    return commonActionId >= 0 && commonActionId < static_cast<int>(kFreeCameraActions.size())
        ? &kFreeCameraActions[static_cast<std::size_t>(commonActionId)]
        : nullptr;
}

static_assert(kFreeCameraActions[BattleEmulator::ATTACK_ENEMY].dq9ActionId == 1);
static_assert(kFreeCameraActions[BattleEmulator::ATTACK_ALLY].dq9ActionId == 1);
static_assert(kFreeCameraActions[BattleEmulator::THUNDER_THRUST].dq9ActionId == 72);
static_assert(kFreeCameraActions[BattleEmulator::BEAST_THRUST].dq9ActionId == 70);
static_assert(kFreeCameraActions[BattleEmulator::VITAL_POINT_THRUST].dq9ActionId == 71);
static_assert(kFreeCameraActions[BattleEmulator::ZAKI].dq9ActionId == 24);
static_assert(kFreeCameraActions[BattleEmulator::ZARAKI].dq9ActionId == 25);
static_assert(kFreeCameraActions[BattleEmulator::GERUNIKKU_MERAMI].dq9ActionId == 10);
static_assert(kFreeCameraActions[BattleEmulator::GERUNIKKU_BAGIMA].dq9ActionId == 19);
static_assert(kFreeCameraActions[BattleEmulator::WHIPPING_BOY].dq9ActionId == 109);
static_assert(kFreeCameraActions[BattleEmulator::MEDICINAL_HERBS].dq9ActionId == 255);
static_assert(kFreeCameraActions[BattleEmulator::MERA_ZOMA].dq9ActionId == 11);
static_assert(kFreeCameraActions[BattleEmulator::SKY_ATTACK].dq9ActionId == 540);
static_assert(kFreeCameraActions[BattleEmulator::MERCURIAL_THRUST].dq9ActionId == 69);

} // namespace dq9::freecam::bindings
