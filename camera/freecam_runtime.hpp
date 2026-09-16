#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "freecam_setup.hpp"
#include "freecam_state.hpp"

namespace dq9::freecam {

namespace detail {

struct FreeCameraThreadContext {
    bool battleActive{};
    bool currentTurnValid{};
    bool hasPreviousAction{};
    std::uint8_t retryCounter{};
    int previousCommonActionId{};
    int previousActionIndex{-1};
    int currentTurnActionCount{};
    TriggerActionState previousDq9Action{};
    // Raw result of overlay_d_00:02161720 for the current action target record.
    // This is deliberately kept as an internal RE observation; BattleEmulator
    // callers must not supply a guessed semantic "selected target" field.
    std::uint16_t targetRecord02161720ActorId{kInvalidBattleActor};
    std::array<PresentationActorState, kMaxPresentationActors> presentationActors{};
    std::array<std::uint32_t, kMaxPresentationActors> presentationMembershipProfiles{};
    struct NearestNodeCache {
        std::int32_t worldX{};
        std::int32_t worldZ{};
        std::uint8_t node{};
        PresentationNodeSearchMode mode{PresentationNodeSearchMode::optimized};
        bool valid{};
    };
    std::array<NearestNodeCache, kMaxPresentationActors> nearestNodeCache{};
    std::uint8_t presentationActorCount{};
    std::array<std::uint16_t, kMaxPresentationActions> turnActionActors{};
    PresentationOccupancyMap presentationOccupancy{};
    bool presentationGoalSetupActive{};
    PresentationTurnRoutes currentRoutes{};
    int plannedActionIndex{-1};
};

// One independent state owner per simulation thread. A thread may run more
// than one battle, so ResetFreeCameraBattle() still defines battle lifetime.
inline thread_local FreeCameraThreadContext gFreeCameraThreadContext{};

[[nodiscard]] inline FreeCameraThreadContext& ThreadContext() noexcept {
    return gFreeCameraThreadContext;
}

inline void InvalidateCurrentRoutes(FreeCameraThreadContext& state) noexcept {
    state.currentRoutes = {};
    state.plannedActionIndex = -1;
}

[[nodiscard]] inline bool CommitActionProgress(
    const int commonActionId,
    const int actionIndex,
    const int turnActionCount,
    const TriggerActionState mappedDq9Action
) noexcept {
    auto& state = ThreadContext();
    if (!state.battleActive
        || !state.currentTurnValid
        || turnActionCount != state.currentTurnActionCount
        || actionIndex < 0
        || actionIndex >= turnActionCount
        || (actionIndex == 0 && state.previousActionIndex != -1)
        || (actionIndex > 0 && state.previousActionIndex != actionIndex - 1)) {
        return false;
    }
    state.hasPreviousAction = true;
    state.previousCommonActionId = commonActionId;
    state.previousActionIndex = actionIndex;
    state.currentTurnActionCount = turnActionCount;
    state.previousDq9Action = mappedDq9Action;
    state.targetRecord02161720ActorId = kInvalidBattleActor;
    return true;
}

// Private RE adapter hook. overlay_d_00:02161720 is not exposed as a
// BattleEmulator semantic input; only code that actually reproduces that DQ9
// target-record accessor may publish its observed actor ID here.
inline void SetTargetRecord02161720ActorId(
    const std::uint16_t actorId
) noexcept {
    ThreadContext().targetRecord02161720ActorId = actorId;
}

// Private BattleEmulator adapter hooks. They keep DQ9 actor IDs/nodes inside
// the subsystem and are intentionally not part of the public lifecycle API.
inline bool SetPresentationActor(
    const std::size_t index,
    const PresentationActorState actor
) noexcept {
    auto& state = ThreadContext();
    if (index >= state.presentationActors.size() || actor.actorId == kInvalidPresentationActor) return false;
    const auto& previous = state.presentationActors[index];
    if (previous.actorId != actor.actorId) {
        state.presentationMembershipProfiles[index] = kInvalidMembershipProfile;
    }
    if (previous.actorId != actor.actorId
        || previous.worldX != actor.worldX
        || previous.worldZ != actor.worldZ) {
        state.nearestNodeCache[index] = {};
    }
    const bool routeInputChanged = previous.actorId != actor.actorId
        || previous.startNode != actor.startNode
        || previous.goalNode != actor.goalNode;
    state.presentationActors[index] = actor;
    state.presentationGoalSetupActive = false;
    if (state.presentationActorCount <= index) {
        state.presentationActorCount = static_cast<std::uint8_t>(index + 1);
    }
    if (routeInputChanged) InvalidateCurrentRoutes(state);
    return true;
}

[[nodiscard]] inline bool SetPlayerPresentationMembershipProfile(
    const std::size_t index,
    const std::uint16_t firstModelCode,
    const std::uint16_t secondModelCode
) noexcept {
    auto& state = ThreadContext();
    const FreeCameraMembershipMetadata* metadata = LoadedFreeCameraMembershipMetadata();
    if (index >= state.presentationActorCount || metadata == nullptr) return false;
    const std::uint32_t profile = metadata->ResolvePlayerProfile(firstModelCode, secondModelCode);
    if (profile == kInvalidMembershipProfile) return false;
    state.presentationMembershipProfiles[index] = profile;
    return true;
}

[[nodiscard]] inline bool SetMonsterPresentationMembershipProfile(
    const std::size_t index,
    const std::uint16_t monsterId
) noexcept {
    auto& state = ThreadContext();
    const FreeCameraMembershipMetadata* metadata = LoadedFreeCameraMembershipMetadata();
    if (index >= state.presentationActorCount || metadata == nullptr) return false;
    const std::uint32_t profile = metadata->ResolveMonsterProfile(monsterId);
    if (profile == kInvalidMembershipProfile) return false;
    state.presentationMembershipProfiles[index] = profile;
    return true;
}

[[nodiscard]] inline bool SetSpecialActorPresentationMembershipProfile(
    const std::size_t index,
    const std::uint16_t resourceId
) noexcept {
    auto& state = ThreadContext();
    const FreeCameraMembershipMetadata* metadata = LoadedFreeCameraMembershipMetadata();
    if (index >= state.presentationActorCount || metadata == nullptr) return false;
    const std::uint32_t profile = metadata->ResolveSpecialActorProfile(resourceId);
    if (profile == kInvalidMembershipProfile) return false;
    state.presentationMembershipProfiles[index] = profile;
    return true;
}

[[nodiscard]] inline std::size_t FindPresentationActorIndex(const std::uint16_t actorId) noexcept;

[[nodiscard]] inline std::uint32_t PresentationMembershipProfileForActor(
    const std::uint16_t actorId
) noexcept {
    const auto& state = ThreadContext();
    const std::size_t index = FindPresentationActorIndex(actorId);
    return index < state.presentationActorCount
        ? state.presentationMembershipProfiles[index]
        : kInvalidMembershipProfile;
}

[[nodiscard]] inline std::size_t FindPresentationActorIndex(const std::uint16_t actorId) noexcept {
    const auto& state = ThreadContext();
    for (std::size_t index = 0; index < state.presentationActorCount; ++index) {
        if (state.presentationActors[index].actorId == actorId) return index;
    }
    return state.presentationActors.size();
}

[[nodiscard]] inline bool ResetPresentationGoalsToStart() noexcept {
    auto& state = ThreadContext();
    if (state.presentationActorCount > state.presentationActors.size()) return false;
    bool changed = false;
    for (std::size_t index = 0; index < state.presentationActorCount; ++index) {
        auto& actor = state.presentationActors[index];
        changed = changed || actor.goalNode != actor.startNode;
        actor.goalNode = actor.startNode;
        actor.targetNode = kInvalidPresentationNode;
        actor.conflictInvalidated = false;
    }
    state.presentationGoalSetupActive = false;
    if (changed) InvalidateCurrentRoutes(state);
    return true;
}

[[nodiscard]] inline bool RefreshPresentationStartNode(
    const std::size_t index,
    const PresentationNodeSearchMode mode = PresentationNodeSearchMode::optimized
) noexcept {
    auto& state = ThreadContext();
    if (index >= state.presentationActorCount) return false;
    auto& actor = state.presentationActors[index];
    if (actor.startNode != actor.auxiliaryNode) return true;

    auto& cache = state.nearestNodeCache[index];
    if (!cache.valid
        || cache.worldX != actor.worldX
        || cache.worldZ != actor.worldZ
        || cache.mode != mode) {
        cache.worldX = actor.worldX;
        cache.worldZ = actor.worldZ;
        cache.node = mode == PresentationNodeSearchMode::optimized
            ? NearestPresentationNodeFast(actor.worldX, actor.worldZ)
            : NearestPresentationNodeSimple(actor.worldX, actor.worldZ);
        cache.mode = mode;
        cache.valid = true;
    }
    if (actor.startNode != cache.node) {
        actor.startNode = cache.node;
        state.presentationGoalSetupActive = false;
        InvalidateCurrentRoutes(state);
    }
    return true;
}

// Starts the 021E08BC goal-assignment stage after the roster has been supplied.
// Occupancy is durable across assignments because 021E2904/021E2530 write
// conflict markers that a rebuild from actor fields would lose.
[[nodiscard]] inline bool BeginPresentationGoalSetup(
    const PresentationNodeSearchMode mode = PresentationNodeSearchMode::optimized
) noexcept {
    auto& state = ThreadContext();
    if (state.presentationActorCount > state.presentationActors.size()) return false;
    if (!ResetPresentationGoalsToStart()) return false;
    for (std::size_t index = 0; index < state.presentationActorCount; ++index) {
        if (!RefreshPresentationStartNode(index, mode)) return false;
    }
    state.presentationOccupancy = BuildPresentationOccupancy(
        std::span<const PresentationActorState>(
            state.presentationActors.data(),
            state.presentationActorCount
        )
    );
    state.presentationGoalSetupActive = true;
    return true;
}

[[nodiscard]] inline bool SetPresentationGoal(
    const std::size_t index,
    const std::uint8_t node
) noexcept {
    auto& state = ThreadContext();
    if (index >= state.presentationActorCount) return false;
    if (state.presentationActors[index].goalNode == node) return true;
    state.presentationActors[index].goalNode = node;
    state.presentationGoalSetupActive = false;
    InvalidateCurrentRoutes(state);
    return true;
}

[[nodiscard]] inline bool SetPresentationAuxiliaryNode(
    const std::size_t index,
    const std::uint8_t node
) noexcept {
    auto& state = ThreadContext();
    if (index >= state.presentationActorCount) return false;
    state.presentationActors[index].auxiliaryNode = node;
    state.presentationGoalSetupActive = false;
    return true;
}

[[nodiscard]] inline bool SetPresentationTargetNode(
    const std::size_t index,
    const std::uint8_t node
) noexcept {
    auto& state = ThreadContext();
    if (index >= state.presentationActorCount) return false;
    state.presentationActors[index].targetNode = node;
    state.presentationGoalSetupActive = false;
    return true;
}

[[nodiscard]] inline std::uint16_t ResolveActorPresentationTarget(
    const std::size_t index,
    const std::uint16_t primaryTargetId
) noexcept {
    const auto& state = ThreadContext();
    if (index >= state.presentationActorCount) return kInvalidBattleActor;
    return ResolvePresentationTarget(state.presentationActors[index], primaryTargetId);
}

[[nodiscard]] inline bool IsActorPresentationMovementEligible(
    const std::size_t index,
    const std::uint16_t targetActorId
) noexcept {
    const auto& state = ThreadContext();
    return index < state.presentationActorCount
        && IsPresentationMovementEligible(state.presentationActors[index], targetActorId);
}

[[nodiscard]] inline bool AssignActorPresentationGoal(
    const std::uint16_t actorId,
    const std::uint16_t targetId,
    const std::span<const std::uint16_t> actionActorIds,
    const std::uint8_t attackFormationMode,
    const PresentationNodeSearchMode mode = PresentationNodeSearchMode::optimized
) noexcept {
    auto& state = ThreadContext();
    if (!state.presentationGoalSetupActive) return false;
    const std::size_t actorIndex = FindPresentationActorIndex(actorId);
    const std::size_t targetIndex = FindPresentationActorIndex(targetId);
    if (actorIndex >= state.presentationActorCount || targetIndex >= state.presentationActorCount) return false;
    const PresentationGoalDecision decision = AssignPresentationGoal(
        std::span<PresentationActorState>(
            state.presentationActors.data(),
            state.presentationActorCount
        ),
        actorIndex,
        targetIndex,
        state.presentationOccupancy,
        actionActorIds,
        attackFormationMode,
        mode
    );
    if (!decision.valid) return false;
    if (decision.goalChanged) InvalidateCurrentRoutes(state);
    return true;
}

[[nodiscard]] inline bool AssignActorFallbackPresentationGoal(
    const std::uint16_t actorId
) noexcept {
    auto& state = ThreadContext();
    if (!state.presentationGoalSetupActive) return false;
    const std::size_t actorIndex = FindPresentationActorIndex(actorId);
    if (actorIndex >= state.presentationActorCount) return false;
    PresentationActorState& actor = state.presentationActors[actorIndex];
    const PresentationGoalDecision decision = ChooseFallbackPresentationGoal(
        actor,
        state.presentationOccupancy
    );
    if (!decision.valid) return false;
    actor.goalNode = decision.goalNode;
    if (decision.goalChanged) InvalidateCurrentRoutes(state);
    return true;
}

inline bool SetTurnActionActor(
    const int actionIndex,
    const std::uint16_t actorId
) noexcept {
    auto& state = ThreadContext();
    if (!state.currentTurnValid
        || actionIndex < 0
        || actionIndex >= state.currentTurnActionCount
        || static_cast<std::size_t>(actionIndex) >= state.turnActionActors.size()) {
        return false;
    }
    auto& slot = state.turnActionActors[static_cast<std::size_t>(actionIndex)];
    if (slot == actorId) return true;
    slot = actorId;
    InvalidateCurrentRoutes(state);
    return true;
}

[[nodiscard]] inline bool PlanCurrentActionRoutes(const int actionIndex) noexcept {
    auto& state = ThreadContext();
    if (!state.currentTurnValid
        || actionIndex < 0
        || actionIndex >= state.currentTurnActionCount
        || state.presentationActorCount > state.presentationActors.size()) {
        return false;
    }
    if (state.currentRoutes.valid && state.plannedActionIndex == actionIndex) return true;
    std::array<PresentationActorInput, kMaxPresentationActors> routeActors{};
    for (std::size_t index = 0; index < state.presentationActorCount; ++index) {
        routeActors[index] = PresentationRouteInput(state.presentationActors[index]);
    }
    state.currentRoutes = PlanPresentationRoutes(
        std::span<const PresentationActorInput>(
            routeActors.data(),
            state.presentationActorCount
        ),
        std::span<const std::uint16_t>(
            state.turnActionActors.data(),
            static_cast<std::size_t>(state.currentTurnActionCount)
        ),
        static_cast<std::size_t>(actionIndex)
    );
    state.plannedActionIndex = state.currentRoutes.valid ? actionIndex : -1;
    return state.currentRoutes.valid;
}

[[nodiscard]] inline TriggerDecision DecideCurrentFreeCamera(
    TriggerDerivationInput input
) noexcept {
    input.fixedMetadata = FreeCameraMetadata();
    input.actionMetadata = LoadedFreeCameraActionMetadata();
    input.membershipMetadata = LoadedFreeCameraMembershipMetadata();
    input.actorMembershipProfile = PresentationMembershipProfileForActor(input.current.actorId);
    const auto& state = ThreadContext();
    return DecideFreeCamera(DeriveTriggerInput(
        input,
        state.currentRoutes,
        state.targetRecord02161720ActorId
    ));
}

[[nodiscard]] inline bool BeginFreeCameraTurnStorage(const int turnActionCount) noexcept {
    auto& state = ThreadContext();
    state.battleActive = true;
    state.previousActionIndex = -1;
    state.turnActionActors.fill(kInvalidPresentationActor);
    state.targetRecord02161720ActorId = kInvalidBattleActor;
    state.presentationGoalSetupActive = false;
    InvalidateCurrentRoutes(state);
    if (turnActionCount < 0
        || static_cast<std::size_t>(turnActionCount) > kMaxPresentationActions) {
        state.currentTurnActionCount = 0;
        state.currentTurnValid = false;
        return false;
    }
    state.currentTurnActionCount = turnActionCount;
    state.currentTurnValid = true;
    return true;
}

} // namespace detail

// Call once at the beginning of every battle, including consecutive battles
// executed on the same worker thread.
inline void ResetFreeCameraBattle() noexcept {
    detail::ThreadContext() = {};
    auto& state = detail::ThreadContext();
    state.battleActive = true;
    state.turnActionActors.fill(detail::kInvalidPresentationActor);
    state.presentationMembershipProfiles.fill(kInvalidMembershipProfile);
}

// Call once when a new turn order is fixed. BattleEmulator supplies only the
// ally/enemy actor order; DQ9 actor IDs remain internal to this subsystem.
// This does not reset the battle-wide free-camera retry counter/history.
[[nodiscard]] inline bool BeginFreeCameraTurn(
    const std::span<const BattleActorRef> actionOrder
) noexcept {
    if (!detail::BeginFreeCameraTurnStorage(static_cast<int>(actionOrder.size()))) return false;
    for (std::size_t index = 0; index < actionOrder.size(); ++index) {
        if (!detail::SetTurnActionActor(
                static_cast<int>(index),
                detail::Dq9ActorId(actionOrder[index])
            )) {
            return false;
        }
    }
    return true;
}

} // namespace dq9::freecam
