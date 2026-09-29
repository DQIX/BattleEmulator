#pragma once
#include <array>
#include <cstdint>
#include "dq9_action_mapper.hpp"
#include "freecam_fast_runtime.hpp"

namespace dq9::freecam::bindings {
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
    [[nodiscard]] constexpr bool mapped() const noexcept { return decide != nullptr; }
};
template <int CommonId>
[[nodiscard]] constexpr ActionBinding Bind() noexcept {
    constexpr auto id = actions::Dq9ActionId<CommonId>();
    static_assert(fast::metadata::IsFreeCameraMapperAllowed<id>(), "presentation-only actions must not enter the free-camera mapper");
    using Action = fast::FreeCamera<id, CommonId>;
    return {CommonId, id, Action::targetSide, Action::targetScope, Action::attackFormationMode, Action::presentationType,
        +[](fast::ActionRuntimeInput input) noexcept { return fast::Decide<Action>(input); },
        +[](int actionIndex, int turnActionCount, std::uint16_t actor, std::uint16_t target) noexcept {
            return fast::CommitActionProgress<Action>(actionIndex, turnActionCount, actor, target);
        }};
}
inline constexpr auto kFreeCameraActions = [] {
    std::array<ActionBinding, BattleEmulator::MAX_COMMON_ACTION_ID + 1> actions{};
    actions[BattleEmulator::ATTACK_ENEMY] = Bind<BattleEmulator::ATTACK_ENEMY>();
    actions[BattleEmulator::ATTACK_ALLY] = Bind<BattleEmulator::ATTACK_ALLY>();
    actions[BattleEmulator::HEAL] = Bind<BattleEmulator::HEAL>();
    return actions;
}();
[[nodiscard]] consteval bool ValidateFreeCameraActionTable() {
    for (std::size_t i = 0; i < kFreeCameraActions.size(); ++i) {
        const auto& binding = kFreeCameraActions[i];
        if (!binding.mapped()) {
            if (binding.commonActionId != -1 || binding.dq9ActionId != fast::metadata::kInvalidActionId) return false;
            continue;
        }
        const auto* action = actions::Find(static_cast<int>(i));
        if (binding.commonActionId != static_cast<int>(i) || action == nullptr || !action->mapped()
            || action->dq9ActionId != binding.dq9ActionId || action->targetSide != binding.targetSide
            || action->targetScope != binding.targetScope || action->attackFormationMode != binding.attackFormationMode
            || action->presentationType != binding.presentationType
            || generated::kHasAnyMinedFreeCameraTriggerSource[binding.dq9ActionId] == 0
            || generated::kFreeCameraMapperAllowed[binding.dq9ActionId] == 0) return false;
    }
    return true;
}
static_assert(ValidateFreeCameraActionTable());
[[nodiscard]] constexpr const ActionBinding* Find(int id) noexcept {
    return id >= 0 && id < static_cast<int>(kFreeCameraActions.size()) ? &kFreeCameraActions[id] : nullptr;
}
}
