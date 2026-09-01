#pragma once

#include <array>
#include <cstdint>

#include "dq9_action_mapper.hpp"
#include "freecam_fast_runtime.hpp"

namespace dq9::freecam::bindings {

// BattleEmulator.h must be included by the translation unit before this header.
// The array index is the BattleEmulator common action ID. An empty element means
// that the DQ9 runtime action ID has not been confirmed yet.

struct ActionBinding {
    using DecideFunction = fast::TriggerDecision (*)(fast::ActionRuntimeInput) noexcept;
    using CommitFunction = bool (*)(int, int, std::uint16_t, std::uint16_t) noexcept;

    int commonActionId{-1};
    std::uint16_t dq9ActionId{fast::metadata::kInvalidActionId};
    fast::TargetSide targetSide{fast::TargetSide::none_or_context};
    fast::TargetScope targetScope{fast::TargetScope::none_or_context};
    std::uint8_t attackFormationMode{};
    std::uint8_t presentationType{};
    DecideFunction decide{};
    CommitFunction commit{};

    [[nodiscard]] constexpr bool mapped() const noexcept {
        return decide != nullptr;
    }
};

template <int CommonActionId>
[[nodiscard]] constexpr ActionBinding Bind() noexcept {
    constexpr std::uint16_t Dq9ActionId = actions::Dq9ActionId<CommonActionId>();
    // This mapper is ONLY for actions that can enter the free-camera trigger
    // pipeline. Presentation/route emulation is not a reason to register an
    // action here. ROM-mined BACT + actor-membership + fallback-membership
    // data is the build-time gate: a statically triggerless action must fail
    // compilation instead of silently expanding this mapper's responsibility.
    static_assert(
        fast::metadata::IsFreeCameraMapperAllowed<Dq9ActionId>(),
        "freecam_action_mapper: DQ9 action is not classified as a free-camera action; do not register presentation-only/state-dependent/tracking actions here"
    );
    using Action = fast::FreeCamera<Dq9ActionId, CommonActionId>;
    return {
        CommonActionId,
        Dq9ActionId,
        Action::targetSide,
        Action::targetScope,
        Action::attackFormationMode,
        Action::presentationType,
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

    //C:\Users\owner\Documents\tunnelworkspace\battle_harness\dq9-action-target-classification.csv
    //mapperの謎のintはすでにデータマイニング済みだぞ何してんの？本当に何を四天王
    actions[BattleEmulator::ATTACK_ENEMY] = Bind<BattleEmulator::ATTACK_ENEMY>();
    actions[BattleEmulator::ATTACK_ALLY] = Bind<BattleEmulator::ATTACK_ALLY>();
    actions[BattleEmulator::SKY_ATTACK] = Bind<BattleEmulator::SKY_ATTACK>();
    actions[BattleEmulator::MERA_ZOMA] = Bind<BattleEmulator::MERA_ZOMA>();
    actions[BattleEmulator::MERCURIAL_THRUST] = Bind<BattleEmulator::MERCURIAL_THRUST>();
    actions[BattleEmulator::THUNDER_THRUST] = Bind<BattleEmulator::THUNDER_THRUST>();
    actions[BattleEmulator::BEAST_THRUST] = Bind<BattleEmulator::BEAST_THRUST>();
    actions[BattleEmulator::VITAL_POINT_THRUST] = Bind<BattleEmulator::VITAL_POINT_THRUST>();
    actions[BattleEmulator::HELM_SPLITTER] = Bind<BattleEmulator::HELM_SPLITTER>();
    actions[BattleEmulator::ZAKI] = Bind<BattleEmulator::ZAKI>();
    actions[BattleEmulator::ZARAKI] = Bind<BattleEmulator::ZARAKI>();
    actions[BattleEmulator::MEDICINAL_HERBS] = Bind<BattleEmulator::MEDICINAL_HERBS>();
    actions[BattleEmulator::GERUNIKKU_MERAMI] = Bind<BattleEmulator::GERUNIKKU_MERAMI>();
    return actions;
}();

[[nodiscard]] consteval bool ValidateFreeCameraActionTable() {
    for (std::size_t commonActionId = 0; commonActionId < kFreeCameraActions.size(); ++commonActionId) {
        const auto& binding = kFreeCameraActions[commonActionId];
        if (!binding.mapped()) {
            if (binding.commonActionId != -1) return false;
            if (binding.dq9ActionId != fast::metadata::kInvalidActionId) return false;
            continue;
        }

        // The table index IS the ecosystem-wide BattleEmulator common/god ID.
        // A mapped entry is invalid if its embedded common ID disagrees with
        // the slot, even if a caller could otherwise still find it by index.
        if (binding.commonActionId != static_cast<int>(commonActionId)) return false;

        const auto* generalAction = actions::Find(static_cast<int>(commonActionId));
        if (generalAction == nullptr || !generalAction->mapped()) return false;
        if (generalAction->dq9ActionId != binding.dq9ActionId) return false;
        if (generalAction->targetSide != binding.targetSide) return false;
        if (generalAction->targetScope != binding.targetScope) return false;
        if (generalAction->attackFormationMode != binding.attackFormationMode) return false;
        if (generalAction->presentationType != binding.presentationType) return false;

        // Prevent "presentation-only action in the freecam mapper" from being
        // hidden behind another initialization path. Every mapped entry must
        // have at least one trigger source in ROM-mined BACT / actor membership
        // / fallback membership data.
        if (binding.dq9ActionId >= fast::metadata::kActionCount) return false;
        if (generated::kHasAnyMinedFreeCameraTriggerSource[binding.dq9ActionId] == 0) return false;
        if (generated::kFreeCameraMapperAllowed[binding.dq9ActionId] == 0) return false;
    }
    return true;
}

static_assert(
    ValidateFreeCameraActionTable(),
    "kFreeCameraActions contains an invalid, mismatched, or ROM-triggerless entry"
);

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
static_assert(kFreeCameraActions[BattleEmulator::HELM_SPLITTER].dq9ActionId == 109);
static_assert(kFreeCameraActions[BattleEmulator::ZAKI].dq9ActionId == 24);
static_assert(kFreeCameraActions[BattleEmulator::ZARAKI].dq9ActionId == 25);
static_assert(kFreeCameraActions[BattleEmulator::MEDICINAL_HERBS].dq9ActionId == 255);
static_assert(kFreeCameraActions[BattleEmulator::GERUNIKKU_MERAMI].dq9ActionId == 10);
static_assert(kFreeCameraActions[BattleEmulator::MERA_ZOMA].dq9ActionId == 11);
static_assert(kFreeCameraActions[BattleEmulator::SKY_ATTACK].dq9ActionId == 540);
static_assert(kFreeCameraActions[BattleEmulator::MERCURIAL_THRUST].dq9ActionId == 69);

} // namespace dq9::freecam::bindings
