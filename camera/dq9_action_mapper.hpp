#pragma once

#include <array>
#include <cstdint>

#include "freecam_fast_runtime.hpp"

namespace dq9::freecam::actions {

// Common-action -> DQ9-action crosswalk for actions that BattleEmulator
// already models and whose DQ9 ID is confirmed by ROM mining / live records.
// This is NOT a free-camera whitelist. Free-camera eligibility belongs only in
// freecam_action_mapper.hpp.
//
// Every DQ9 ID below must exist in camera/dq9-action-target-classification.csv.
// All other fixed action fields are pulled automatically from
// freecam_fast_generated.hpp through fast::metadata constexpr accessors.
struct ActionMetadata {
    std::uint16_t dq9ActionId{fast::metadata::kInvalidActionId};
    fast::TargetSide targetSide{fast::TargetSide::none_or_context};
    fast::TargetScope targetScope{fast::TargetScope::none_or_context};
    std::uint8_t attackFormationMode{};
    std::uint8_t presentationType{};
    std::uint8_t operationType{};
    std::uint8_t repeatMode{};
    std::uint8_t resourceCost{};
    std::uint16_t targetHandlerJudgment1{};
    std::uint16_t targetHandlerJudgment2{};

    [[nodiscard]] constexpr bool mapped() const noexcept {
        return dq9ActionId != fast::metadata::kInvalidActionId;
    }
};

template <std::uint16_t Dq9ActionId>
[[nodiscard]] constexpr ActionMetadata Describe() noexcept {
    static_assert(Dq9ActionId < fast::metadata::kActionCount);
    static_assert(
        fast::metadata::HasActionClassification(Dq9ActionId),
        "DQ9 action ID is absent from camera/dq9-action-target-classification.csv"
    );
    return {
        Dq9ActionId,
        static_cast<fast::TargetSide>(generated::kTargetSideCode[Dq9ActionId]),
        static_cast<fast::TargetScope>(generated::kTargetScopeCode[Dq9ActionId]),
        fast::metadata::AttackFormationMode(Dq9ActionId),
        fast::metadata::PresentationType(Dq9ActionId),
        fast::metadata::OperationType(Dq9ActionId),
        fast::metadata::RepeatMode(Dq9ActionId),
        fast::metadata::ResourceCost(Dq9ActionId),
        fast::metadata::TargetHandlerJudgment1(Dq9ActionId),
        fast::metadata::TargetHandlerJudgment2(Dq9ActionId),
    };
}

inline constexpr auto kActions = [] {
    std::array<ActionMetadata, BattleEmulator::MAX_COMMON_ACTION_ID + 1> actions{};

    actions[BattleEmulator::ATTACK_ENEMY] = Describe<1>();
    actions[BattleEmulator::ATTACK_ALLY] = Describe<1>();
    actions[BattleEmulator::SKY_ATTACK] = Describe<540>();
    actions[BattleEmulator::MERA_ZOMA] = Describe<11>();
    actions[BattleEmulator::MERCURIAL_THRUST] = Describe<69>();
    actions[BattleEmulator::THUNDER_THRUST] = Describe<72>();
    actions[BattleEmulator::BEAST_THRUST] = Describe<70>();
    actions[BattleEmulator::VITAL_POINT_THRUST] = Describe<71>();
    actions[BattleEmulator::DRAGON_SLASH] = Describe<63>();
    actions[BattleEmulator::MIRACLE_SLASH] = Describe<65>();
    actions[BattleEmulator::ZAKI] = Describe<24>();
    actions[BattleEmulator::ZARAKI] = Describe<25>();
    actions[BattleEmulator::MEDICINAL_HERBS] = Describe<255>();
    // Ally Psyche Up / ためる. Live ROM action 161 is self-target and uses
    // presentation type 15. Its presentation path matters here because it
    // leaves compiler-stack residue consumed by overlay_d_25:021E08BC.
    actions[BattleEmulator::PSYCHE_UP_ALLY] = Describe<161>();

    // Gerunikku battle actions already implemented by BattleEmulator. These
    // mappings are presentation metadata, not declarations that freecam runs.
    actions[BattleEmulator::GERUNIKKU_MERAMI] = Describe<10>();
    actions[BattleEmulator::GERUNIKKU_BAGIMA] = Describe<19>();
    actions[BattleEmulator::GERUNIKKU_BAGIMA_STRONG] = Describe<463>();
    actions[BattleEmulator::EERIE_LIGHT] = Describe<155>();
    actions[BattleEmulator::MAGIC_MIRROR] = Describe<137>();
    actions[BattleEmulator::GERUNIKKU_MAGIC_MIRROR] = Describe<55>();
    actions[BattleEmulator::INACTIVE_ENEMY] = Describe<503>();
    actions[BattleEmulator::GERUNIKKU_MEDAPANI] = Describe<912>();
    actions[BattleEmulator::WHIPPING_BOY] = Describe<929>();
    actions[BattleEmulator::HELM_SPLITTER] = Describe<109>();
    actions[BattleEmulator::KABUFF] = Describe<42>();
    actions[BattleEmulator::DOUBLE_EDGED_SLASH] = Describe<175>();
    return actions;
}();

[[nodiscard]] constexpr const ActionMetadata* Find(const int commonActionId) noexcept {
    return commonActionId >= 0 && commonActionId < static_cast<int>(kActions.size())
        ? &kActions[static_cast<std::size_t>(commonActionId)]
        : nullptr;
}

template <int CommonActionId>
[[nodiscard]] consteval std::uint16_t Dq9ActionId() {
    static_assert(CommonActionId >= 0 && CommonActionId < static_cast<int>(kActions.size()));
    constexpr std::uint16_t actionId = kActions[static_cast<std::size_t>(CommonActionId)].dq9ActionId;
    static_assert(actionId != fast::metadata::kInvalidActionId, "common action has no confirmed DQ9 mapping");
    return actionId;
}

static_assert(kActions[BattleEmulator::WHIPPING_BOY].dq9ActionId == 929);
static_assert(kActions[BattleEmulator::HELM_SPLITTER].dq9ActionId == 109);
static_assert(kActions[BattleEmulator::INACTIVE_ENEMY].dq9ActionId == 503);
static_assert(kActions[BattleEmulator::PSYCHE_UP_ALLY].dq9ActionId == 161);
static_assert(kActions[BattleEmulator::PSYCHE_UP_ALLY].presentationType == 15);

} // namespace dq9::freecam::actions
