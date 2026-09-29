#pragma once
#include <array>
#include <cstdint>
#include "freecam_fast_runtime.hpp"

namespace dq9::freecam::actions {
// Presentation mapping is deliberately distinct from free-camera eligibility.
// Action IDs are confirmed by freecam-action-trigger-table.csv and live calls.
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
    [[nodiscard]] constexpr bool mapped() const noexcept { return dq9ActionId != fast::metadata::kInvalidActionId; }
};
template <std::uint16_t Id>
[[nodiscard]] constexpr ActionMetadata Describe() noexcept {
    static_assert(Id < fast::metadata::kActionCount && fast::metadata::HasActionClassification(Id));
    return {Id, static_cast<fast::TargetSide>(generated::kTargetSideCode[Id]),
        static_cast<fast::TargetScope>(generated::kTargetScopeCode[Id]),
        fast::metadata::AttackFormationMode(Id), fast::metadata::PresentationType(Id),
        fast::metadata::OperationType(Id), fast::metadata::RepeatMode(Id), fast::metadata::ResourceCost(Id),
        fast::metadata::TargetHandlerJudgment1(Id), fast::metadata::TargetHandlerJudgment2(Id)};
}
inline constexpr auto kActions = [] {
    std::array<ActionMetadata, BattleEmulator::MAX_COMMON_ACTION_ID + 1> actions{};
    actions[BattleEmulator::ATTACK_ENEMY] = Describe<1>();
    actions[BattleEmulator::ATTACK_ALLY] = Describe<1>();
    actions[BattleEmulator::HEAL] = Describe<30>();
    actions[BattleEmulator::DEFENCE] = Describe<3>();
    actions[BattleEmulator::FLEE_ALLY] = Describe<225>();
    actions[BattleEmulator::FLEE_ENEMY] = Describe<225>();
    return actions;
}();
[[nodiscard]] constexpr const ActionMetadata* Find(int id) noexcept {
    return id >= 0 && id < static_cast<int>(kActions.size()) ? &kActions[id] : nullptr;
}
template <int Id>
[[nodiscard]] consteval std::uint16_t Dq9ActionId() {
    static_assert(Id >= 0 && Id < static_cast<int>(kActions.size()));
    constexpr auto action = kActions[Id].dq9ActionId;
    static_assert(action != fast::metadata::kInvalidActionId);
    return action;
}
}
