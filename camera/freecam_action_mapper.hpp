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
    bool mayTriggerFreeCamera{};
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
        Action::mayTriggerFreeCamera,
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

    /** C:\Users\owner\Documents\tunnelworkspace\battle_harness\dq9-action-target-classification.csv */
    actions[BattleEmulator::ATTACK_ENEMY] = Bind<1, BattleEmulator::ATTACK_ENEMY>();
    actions[BattleEmulator::ATTACK_ALLY] = Bind<1, BattleEmulator::ATTACK_ALLY>();
    actions[BattleEmulator::DEFENCE] = Bind<3, BattleEmulator::DEFENCE>();
    actions[BattleEmulator::SKY_ATTACK] = Bind<540, BattleEmulator::SKY_ATTACK>();
    actions[BattleEmulator::MERA_ZOMA] = Bind<11, BattleEmulator::MERA_ZOMA>();
    actions[BattleEmulator::MERCURIAL_THRUST] = Bind<69, BattleEmulator::MERCURIAL_THRUST>();
    actions[BattleEmulator::THUNDER_THRUST] = Bind<72,BattleEmulator::THUNDER_THRUST>();
    actions[BattleEmulator::BEAST_THRUST] = Bind<70, BattleEmulator::BEAST_THRUST>();
    actions[BattleEmulator::VITAL_POINT_THRUST] = Bind<71, BattleEmulator::VITAL_POINT_THRUST>();
    actions[BattleEmulator::ZAKI] = Bind<24, BattleEmulator::ZAKI>();
    actions[BattleEmulator::ZARAKI] = Bind<25, BattleEmulator::ZARAKI>();
    actions[BattleEmulator::MEDICINAL_HERBS] = Bind<255, BattleEmulator::MEDICINAL_HERBS>();
#if defined(gerunikku)
    actions[BattleEmulator::KABUFF] = Bind<42, BattleEmulator::KABUFF>();
    actions[BattleEmulator::WHIPPING_BOY] = Bind<929, BattleEmulator::WHIPPING_BOY>();
    actions[BattleEmulator::HELM_SPLITTER] = Bind<109, BattleEmulator::HELM_SPLITTER>();
    actions[BattleEmulator::DOUBLE_EDGED_SLASH] = Bind<175, BattleEmulator::DOUBLE_EDGED_SLASH>();
    actions[BattleEmulator::GERUNIKKU_MERAMI] = Bind<10, BattleEmulator::GERUNIKKU_MERAMI>();
    actions[BattleEmulator::GERUNIKKU_BAGIMA] = Bind<19, BattleEmulator::GERUNIKKU_BAGIMA>();
    actions[BattleEmulator::EERIE_LIGHT] = Bind<155, BattleEmulator::EERIE_LIGHT>();
    actions[BattleEmulator::GERUNIKKU_MEDAPANI] = Bind<912, BattleEmulator::GERUNIKKU_MEDAPANI>();
    actions[BattleEmulator::GERUNIKKU_BAGIMA_STRONG] = Bind<463, BattleEmulator::GERUNIKKU_BAGIMA_STRONG>();
    actions[BattleEmulator::MAGIC_MIRROR] = Bind<55, BattleEmulator::MAGIC_MIRROR>();
#endif

    return actions;
}();

[[nodiscard]] constexpr const ActionBinding* Find(const int commonActionId) noexcept {
    return commonActionId >= 0 && commonActionId <= BattleEmulator::CURE_CONFUSION
        ? &kFreeCameraActions[static_cast<std::size_t>(commonActionId)]
        : nullptr;
}

[[nodiscard]] constexpr bool MayTrigger(const int commonActionId) noexcept {
    const ActionBinding* binding = Find(commonActionId);
    return binding != nullptr && binding->mapped() && binding->mayTriggerFreeCamera;
}

static_assert(kFreeCameraActions[BattleEmulator::ATTACK_ENEMY].dq9ActionId == 1);
static_assert(kFreeCameraActions[BattleEmulator::ATTACK_ALLY].dq9ActionId == 1);
static_assert(kFreeCameraActions[BattleEmulator::THUNDER_THRUST].dq9ActionId == 72);
static_assert(kFreeCameraActions[BattleEmulator::BEAST_THRUST].dq9ActionId == 70);
static_assert(kFreeCameraActions[BattleEmulator::VITAL_POINT_THRUST].dq9ActionId == 71);
static_assert(kFreeCameraActions[BattleEmulator::ZAKI].dq9ActionId == 24);
static_assert(kFreeCameraActions[BattleEmulator::ZARAKI].dq9ActionId == 25);
static_assert(kFreeCameraActions[BattleEmulator::MEDICINAL_HERBS].dq9ActionId == 255);
static_assert(kFreeCameraActions[BattleEmulator::MERA_ZOMA].dq9ActionId == 11);
static_assert(kFreeCameraActions[BattleEmulator::SKY_ATTACK].dq9ActionId == 540);
static_assert(kFreeCameraActions[BattleEmulator::MERCURIAL_THRUST].dq9ActionId == 69);
#if defined(gerunikku)
static_assert(kFreeCameraActions[BattleEmulator::WHIPPING_BOY].dq9ActionId == 929);
static_assert(kFreeCameraActions[BattleEmulator::HELM_SPLITTER].dq9ActionId == 109);
static_assert(kFreeCameraActions[BattleEmulator::DOUBLE_EDGED_SLASH].dq9ActionId == 175);
static_assert(kFreeCameraActions[BattleEmulator::GERUNIKKU_MERAMI].dq9ActionId == 10);
static_assert(kFreeCameraActions[BattleEmulator::GERUNIKKU_BAGIMA].dq9ActionId == 19);
static_assert(kFreeCameraActions[BattleEmulator::EERIE_LIGHT].dq9ActionId == 155);
static_assert(kFreeCameraActions[BattleEmulator::GERUNIKKU_MEDAPANI].dq9ActionId == 912);
static_assert(kFreeCameraActions[BattleEmulator::GERUNIKKU_BAGIMA_STRONG].dq9ActionId == 463);

inline constexpr std::uint32_t kGerunikuHeroProfile = fast::ResolvePlayerProfile(2, 0);
inline constexpr std::uint32_t kBadKarmourProfile = fast::ResolveMonsterProfile(0x0118);
inline constexpr std::uint32_t kHootinghamGoreProfile = fast::ResolveMonsterProfile(0x013a);
static_assert(kGerunikuHeroProfile != fast::kInvalidMembershipProfile);
static_assert(kBadKarmourProfile != fast::kInvalidMembershipProfile);
static_assert(kHootinghamGoreProfile != fast::kInvalidMembershipProfile);
static_assert(fast::FreeCamera<24, BattleEmulator::ZAKI>::mayTriggerFreeCamera);
static_assert(fast::FreeCamera<155, BattleEmulator::EERIE_LIGHT>::mayTriggerFreeCamera);
static_assert(fast::FreeCamera<1, BattleEmulator::ATTACK_ENEMY>::mayTriggerFreeCamera);
#endif

} // namespace dq9::freecam::bindings
