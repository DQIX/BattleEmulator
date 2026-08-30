#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "freecam_actor.hpp"
#include "freecam_fast_generated.hpp"
#include "freecam_route.hpp"
#include "freecam_setup.hpp"

namespace dq9::freecam::fast {

namespace metadata {

template <std::size_t N>
[[nodiscard]] constexpr std::uint16_t ReadU16(
    const std::array<std::uint8_t, N>& bytes,
    const std::size_t offset
) noexcept {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[offset])
        | (static_cast<std::uint16_t>(bytes[offset + 1]) << 8)
    );
}

template <std::size_t N>
[[nodiscard]] constexpr std::uint32_t ReadU32(
    const std::array<std::uint8_t, N>& bytes,
    const std::size_t offset
) noexcept {
    return static_cast<std::uint32_t>(bytes[offset])
        | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
        | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16)
        | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

template <std::size_t N>
[[nodiscard]] constexpr std::uint64_t ReadU64(
    const std::array<std::uint8_t, N>& bytes,
    const std::size_t offset
) noexcept {
    return static_cast<std::uint64_t>(ReadU32(bytes, offset))
        | (static_cast<std::uint64_t>(ReadU32(bytes, offset + 4)) << 32);
}

template <std::size_t N>
[[nodiscard]] constexpr bool MagicIs(
    const std::array<std::uint8_t, N>& bytes,
    const char a,
    const char b,
    const char c,
    const char d
) noexcept {
    return N >= 4
        && bytes[0] == static_cast<std::uint8_t>(a)
        && bytes[1] == static_cast<std::uint8_t>(b)
        && bytes[2] == static_cast<std::uint8_t>(c)
        && bytes[3] == static_cast<std::uint8_t>(d);
}

inline constexpr std::size_t kActionCount = 1024;
inline constexpr std::uint16_t kInvalidActionId = UINT16_C(0xffff);
inline constexpr std::uint32_t kInvalidProfileIndex = UINT32_C(0xffffffff);

static_assert(generated::kCameraMetadataBytes.size() == 4240);
static_assert(MagicIs(generated::kCameraMetadataBytes, 'F', 'C', 'M', '1'));
static_assert(ReadU32(generated::kCameraMetadataBytes, 4) == 1);
static_assert(ReadU32(generated::kCameraMetadataBytes, 8) == kActionCount);

static_assert(generated::kActionMetadataBytes.size() == 3088);
static_assert(MagicIs(generated::kActionMetadataBytes, 'F', 'C', 'M', 'A'));
static_assert(ReadU32(generated::kActionMetadataBytes, 4) == 2);
static_assert(ReadU32(generated::kActionMetadataBytes, 8) == kActionCount);
inline constexpr std::size_t kFormationModeOffset = ReadU32(generated::kActionMetadataBytes, 12);
static_assert(kFormationModeOffset == 16 + kActionCount * 2);

static_assert(generated::kTargetSideCode.size() == kActionCount);
static_assert(generated::kTargetScopeCode.size() == kActionCount);

static_assert(MagicIs(generated::kMembershipMetadataBytes, 'F', 'C', 'M', 'M'));
static_assert(ReadU32(generated::kMembershipMetadataBytes, 4) == 1);
static_assert(ReadU32(generated::kMembershipMetadataBytes, 8) == kActionCount);

inline constexpr std::size_t kActorProfileCount =
    ReadU32(generated::kMembershipMetadataBytes, 12);
inline constexpr std::size_t kPlayerProfileCount =
    ReadU32(generated::kMembershipMetadataBytes, 16);
inline constexpr std::size_t kMonsterProfileCount =
    ReadU32(generated::kMembershipMetadataBytes, 20);
inline constexpr std::size_t kSpecialProfileCount =
    ReadU32(generated::kMembershipMetadataBytes, 24);

static_assert(kActorProfileCount == 617);
static_assert(kPlayerProfileCount == 13);
static_assert(kMonsterProfileCount == 438);
static_assert(kSpecialProfileCount == 1);

struct PlayerProfileMapEntry {
    std::uint16_t firstModelCode{};
    std::uint16_t secondModelCode{};
    std::uint32_t profileIndex{kInvalidProfileIndex};
};

struct ActorProfileMapEntry {
    std::uint16_t actorKey{};
    std::uint32_t profileIndex{kInvalidProfileIndex};
};

[[nodiscard]] consteval bool HasBact(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return false;
    const std::size_t offset = 16 + (actionId >> 3);
    return ((generated::kCameraMetadataBytes[offset] >> (actionId & 7)) & 1U) != 0;
}

[[nodiscard]] consteval std::uint32_t SelectorProjection(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return 0;
    return ReadU32(
        generated::kCameraMetadataBytes,
        16 + 128 + static_cast<std::size_t>(actionId) * 4
    );
}

[[nodiscard]] consteval std::uint16_t FallbackLookupActionId(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return kInvalidActionId;
    return ReadU16(
        generated::kActionMetadataBytes,
        16 + static_cast<std::size_t>(actionId) * 2
    );
}

[[nodiscard]] consteval std::uint8_t AttackFormationMode(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return 0;
    return generated::kActionMetadataBytes[kFormationModeOffset + actionId];
}

[[nodiscard]] consteval std::uint64_t ActorMembershipPacked(
    const std::uint32_t profileIndex,
    const std::uint16_t actionId
) {
    if (profileIndex >= kActorProfileCount || actionId >= kActionCount) return 0;
    constexpr std::size_t headerSize = 32;
    const std::size_t cell = static_cast<std::size_t>(profileIndex) * kActionCount + actionId;
    return ReadU64(generated::kMembershipMetadataBytes, headerSize + cell * 8);
}

[[nodiscard]] consteval std::uint64_t FallbackMembershipPacked(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return 0;
    constexpr std::size_t headerSize = 32;
    constexpr std::size_t actorCellsBytes = kActorProfileCount * kActionCount * 8;
    return ReadU64(
        generated::kMembershipMetadataBytes,
        headerSize + actorCellsBytes + static_cast<std::size_t>(actionId) * 8
    );
}

template <std::uint16_t ActionId>
[[nodiscard]] consteval auto BuildActorMembershipColumn() {
    static_assert(ActionId < kActionCount);
    std::array<std::uint64_t, kActorProfileCount> result{};
    for (std::size_t profile = 0; profile < result.size(); ++profile) {
        result[profile] = ActorMembershipPacked(static_cast<std::uint32_t>(profile), ActionId);
    }
    return result;
}

[[nodiscard]] consteval auto BuildPlayerProfiles() {
    std::array<PlayerProfileMapEntry, kPlayerProfileCount> result{};
    constexpr std::size_t offset0 = 32
        + kActorProfileCount * kActionCount * 8
        + kActionCount * 8;
    for (std::size_t index = 0; index < result.size(); ++index) {
        const std::size_t offset = offset0 + index * 8;
        result[index] = {
            ReadU16(generated::kMembershipMetadataBytes, offset),
            ReadU16(generated::kMembershipMetadataBytes, offset + 2),
            ReadU32(generated::kMembershipMetadataBytes, offset + 4),
        };
    }
    return result;
}

[[nodiscard]] consteval auto BuildMonsterProfiles() {
    std::array<ActorProfileMapEntry, kMonsterProfileCount> result{};
    constexpr std::size_t offset0 = 32
        + kActorProfileCount * kActionCount * 8
        + kActionCount * 8
        + kPlayerProfileCount * 8;
    for (std::size_t index = 0; index < result.size(); ++index) {
        const std::size_t offset = offset0 + index * 8;
        result[index] = {
            ReadU16(generated::kMembershipMetadataBytes, offset),
            ReadU32(generated::kMembershipMetadataBytes, offset + 4),
        };
    }
    return result;
}

[[nodiscard]] consteval auto BuildSpecialProfiles() {
    std::array<ActorProfileMapEntry, kSpecialProfileCount> result{};
    constexpr std::size_t offset0 = 32
        + kActorProfileCount * kActionCount * 8
        + kActionCount * 8
        + kPlayerProfileCount * 8
        + kMonsterProfileCount * 8;
    for (std::size_t index = 0; index < result.size(); ++index) {
        const std::size_t offset = offset0 + index * 8;
        result[index] = {
            ReadU16(generated::kMembershipMetadataBytes, offset),
            ReadU32(generated::kMembershipMetadataBytes, offset + 4),
        };
    }
    return result;
}

inline constexpr auto kPlayerProfiles = BuildPlayerProfiles();
inline constexpr auto kMonsterProfiles = BuildMonsterProfiles();
inline constexpr auto kSpecialProfiles = BuildSpecialProfiles();

} // namespace metadata

inline constexpr std::uint32_t kInvalidMembershipProfile = metadata::kInvalidProfileIndex;

struct MembershipCell {
    std::uint32_t selectorProjection{};
    std::uint16_t count{};

    [[nodiscard]] constexpr bool Present() const noexcept {
        return count != 0;
    }
};

enum class TargetSide : std::uint8_t {
    none_or_context = 0,
    opponent = 1,
    ally = 2,
};

enum class TargetScope : std::uint8_t {
    none_or_context = 0,
    self = 1,
    single = 2,
    all_on_side = 3,
    group = 4,
    single_formation = 5,
    force_special = 6,
    field_context = 7,
    actor_specific = 8,
};

[[nodiscard]] constexpr MembershipCell DecodeMembershipCell(const std::uint64_t packed) noexcept {
    return {
        static_cast<std::uint32_t>(packed),
        static_cast<std::uint16_t>((packed >> 32) & UINT64_C(0xffff)),
    };
}

// Compile-time action binding. DQ9 action ID and BattleEmulator common ID are
// both template arguments, so no runtime common-ID -> DQ9-ID mapping table is
// needed. The full ROM table is read only during constant evaluation; each
// instantiated action retains only its own actor-membership column plus fixed
// action/fallback values.
template <std::uint16_t Dq9ActionId, int BattleEmulatorCommonId>
struct FreeCamera {
    static_assert(Dq9ActionId < metadata::kActionCount);
    static_assert(BattleEmulatorCommonId >= 0);

    static inline constexpr std::uint16_t dq9ActionId = Dq9ActionId;
    static inline constexpr int commonActionId = BattleEmulatorCommonId;
    static inline constexpr TargetSide targetSide =
        static_cast<TargetSide>(generated::kTargetSideCode[Dq9ActionId]);
    static inline constexpr TargetScope targetScope =
        static_cast<TargetScope>(generated::kTargetScopeCode[Dq9ActionId]);
    static inline constexpr bool actionHasBact = metadata::HasBact(Dq9ActionId);
    static inline constexpr std::uint32_t actionSelectorProjection =
        metadata::SelectorProjection(Dq9ActionId);
    static inline constexpr std::uint16_t fallbackLookupActionId =
        metadata::FallbackLookupActionId(Dq9ActionId);
    static inline constexpr std::uint8_t attackFormationMode =
        metadata::AttackFormationMode(Dq9ActionId);
    static inline constexpr std::uint64_t fallbackMembershipPacked =
        fallbackLookupActionId == metadata::kInvalidActionId
            ? UINT64_C(0)
            : metadata::FallbackMembershipPacked(fallbackLookupActionId);
    static inline constexpr auto actorMembershipPacked =
        metadata::BuildActorMembershipColumn<Dq9ActionId>();

    [[nodiscard]] static constexpr MembershipCell ActorMembership(
        const std::uint32_t profileIndex
    ) noexcept {
        return profileIndex < actorMembershipPacked.size()
            ? DecodeMembershipCell(actorMembershipPacked[profileIndex])
            : MembershipCell{};
    }

    [[nodiscard]] static constexpr MembershipCell FallbackMembership() noexcept {
        return DecodeMembershipCell(fallbackMembershipPacked);
    }
};

[[nodiscard]] constexpr std::uint32_t ResolvePlayerProfile(
    const std::uint16_t firstModelCode,
    const std::uint16_t secondModelCode
) noexcept {
    const std::uint32_t wanted =
        (static_cast<std::uint32_t>(firstModelCode) << 16) | secondModelCode;
    std::size_t first = 0;
    std::size_t last = metadata::kPlayerProfiles.size();
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        const auto& entry = metadata::kPlayerProfiles[middle];
        const std::uint32_t candidate =
            (static_cast<std::uint32_t>(entry.firstModelCode) << 16) | entry.secondModelCode;
        if (candidate < wanted) first = middle + 1;
        else last = middle;
    }
    if (first >= metadata::kPlayerProfiles.size()) return kInvalidMembershipProfile;
    const auto& entry = metadata::kPlayerProfiles[first];
    return entry.firstModelCode == firstModelCode && entry.secondModelCode == secondModelCode
        ? entry.profileIndex
        : kInvalidMembershipProfile;
}

template <std::size_t N>
[[nodiscard]] constexpr std::uint32_t ResolveActorProfile(
    const std::array<metadata::ActorProfileMapEntry, N>& entries,
    const std::uint16_t actorKey
) noexcept {
    std::size_t first = 0;
    std::size_t last = N;
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if (entries[middle].actorKey < actorKey) first = middle + 1;
        else last = middle;
    }
    return first < N && entries[first].actorKey == actorKey
        ? entries[first].profileIndex
        : kInvalidMembershipProfile;
}

[[nodiscard]] constexpr std::uint32_t ResolveMonsterProfile(const std::uint16_t monsterId) noexcept {
    return ResolveActorProfile(metadata::kMonsterProfiles, monsterId);
}

[[nodiscard]] constexpr std::uint32_t ResolveSpecialActorProfile(const std::uint16_t resourceId) noexcept {
    return ResolveActorProfile(metadata::kSpecialProfiles, resourceId);
}

enum class TriggerSource : std::uint8_t {
    none,
    actor_membership,
    action_bact,
    fallback_membership,
    reset_only,
};

struct TriggerDecision {
    TriggerSource source{TriggerSource::none};
    bool callFreeCamera{};
    bool param5{};
    bool resetOnly{};
};

[[nodiscard]] constexpr bool ComputeSelectorSuppression(
    const std::uint32_t projection
) noexcept {
    const bool has0c = (projection & 1) != 0;
    if (has0c && (projection & 2) != 0) return false;
    if (has0c && static_cast<std::uint16_t>(projection >> 16) != 10) return true;
    return (projection & (4 | 8)) != 0;
}

struct ActionState {
    std::uint16_t dq9ActionId{};
    std::uint16_t actorId{kInvalidBattleActor};
    std::uint16_t targetId{kInvalidBattleActor};
};

struct ActionRuntimeInput {
    std::uint16_t actorId{kInvalidBattleActor};
    std::uint16_t targetId{kInvalidBattleActor};
    std::uint16_t turnActionIndex{};
    std::uint16_t currentActorId{kInvalidBattleActor};
    std::uint8_t targetPresentationSlot{0xff};
    bool actorAndTargetHaveGeometry{};
    std::int32_t actorTargetDistance{};
    std::int32_t actorRadius{};
    std::int32_t targetRadius{};
};

struct RuntimeState {
    bool battleActive{};
    bool currentTurnValid{};
    bool hasPreviousAction{};
    std::uint8_t retryCounter{};
    int previousCommonActionId{};
    int previousActionIndex{-1};
    int currentTurnActionCount{};
    ActionState previousAction{};
    // Exact semantic name is intentionally not guessed. This remains the raw
    // actor ID returned by overlay_d_00:02161720 for the current target record.
    std::uint16_t targetRecord02161720ActorId{kInvalidBattleActor};

    std::array<detail::PresentationActorState, detail::kMaxPresentationActors> presentationActors{};
    std::array<std::uint32_t, detail::kMaxPresentationActors> presentationMembershipProfiles{};
    struct NearestNodeCache {
        std::int32_t worldX{};
        std::int32_t worldZ{};
        std::uint8_t node{};
        detail::PresentationNodeSearchMode mode{detail::PresentationNodeSearchMode::optimized};
        bool valid{};
    };
    std::array<NearestNodeCache, detail::kMaxPresentationActors> nearestNodeCache{};
    std::uint8_t presentationActorCount{};
    std::array<std::uint16_t, detail::kMaxPresentationActions> turnActionActors{};
    detail::PresentationOccupancyMap presentationOccupancy{};
    bool presentationGoalSetupActive{};
    detail::PresentationTurnRoutes currentRoutes{};
    int plannedActionIndex{-1};
};

inline thread_local RuntimeState gRuntimeState{};

[[nodiscard]] inline RuntimeState& ThreadContext() noexcept {
    return gRuntimeState;
}

inline void InvalidateCurrentRoutes(RuntimeState& state) noexcept {
    state.currentRoutes = {};
    state.plannedActionIndex = -1;
}

inline void ResetBattle() noexcept {
    gRuntimeState = {};
    gRuntimeState.battleActive = true;
    gRuntimeState.turnActionActors.fill(detail::kInvalidPresentationActor);
    gRuntimeState.presentationMembershipProfiles.fill(kInvalidMembershipProfile);
}

[[nodiscard]] inline bool BeginTurn(const std::span<const BattleActorRef> actionOrder) noexcept {
    auto& state = ThreadContext();
    if (actionOrder.size() > detail::kMaxPresentationActions) {
        state.currentTurnActionCount = 0;
        state.currentTurnValid = false;
        return false;
    }
    state.battleActive = true;
    state.previousActionIndex = -1;
    state.currentTurnActionCount = static_cast<int>(actionOrder.size());
    state.currentTurnValid = true;
    state.turnActionActors.fill(detail::kInvalidPresentationActor);
    state.targetRecord02161720ActorId = kInvalidBattleActor;
    state.presentationGoalSetupActive = false;
    InvalidateCurrentRoutes(state);
    for (std::size_t index = 0; index < actionOrder.size(); ++index) {
        state.turnActionActors[index] = Dq9ActorId(actionOrder[index]);
    }
    return true;
}

[[nodiscard]] inline bool SetPresentationActor(
    const std::size_t index,
    const detail::PresentationActorState actor
) noexcept {
    auto& state = ThreadContext();
    if (index >= state.presentationActors.size()
        || actor.actorId == detail::kInvalidPresentationActor) return false;
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

[[nodiscard]] inline std::size_t FindPresentationActorIndex(const std::uint16_t actorId) noexcept {
    const auto& state = ThreadContext();
    for (std::size_t index = 0; index < state.presentationActorCount; ++index) {
        if (state.presentationActors[index].actorId == actorId) return index;
    }
    return state.presentationActors.size();
}

[[nodiscard]] inline std::uint32_t PresentationMembershipProfileForActor(
    const std::uint16_t actorId
) noexcept {
    const auto& state = ThreadContext();
    const std::size_t index = FindPresentationActorIndex(actorId);
    return index < state.presentationActorCount
        ? state.presentationMembershipProfiles[index]
        : kInvalidMembershipProfile;
}

[[nodiscard]] inline bool SetPlayerMembershipProfile(
    const std::size_t actorIndex,
    const std::uint16_t firstModelCode,
    const std::uint16_t secondModelCode
) noexcept {
    auto& state = ThreadContext();
    if (actorIndex >= state.presentationActorCount) return false;
    const std::uint32_t profile = ResolvePlayerProfile(firstModelCode, secondModelCode);
    if (profile == kInvalidMembershipProfile) return false;
    state.presentationMembershipProfiles[actorIndex] = profile;
    return true;
}

[[nodiscard]] inline bool SetMonsterMembershipProfile(
    const std::size_t actorIndex,
    const std::uint16_t monsterId
) noexcept {
    auto& state = ThreadContext();
    if (actorIndex >= state.presentationActorCount) return false;
    const std::uint32_t profile = ResolveMonsterProfile(monsterId);
    if (profile == kInvalidMembershipProfile) return false;
    state.presentationMembershipProfiles[actorIndex] = profile;
    return true;
}

[[nodiscard]] inline bool SetSpecialActorMembershipProfile(
    const std::size_t actorIndex,
    const std::uint16_t resourceId
) noexcept {
    auto& state = ThreadContext();
    if (actorIndex >= state.presentationActorCount) return false;
    const std::uint32_t profile = ResolveSpecialActorProfile(resourceId);
    if (profile == kInvalidMembershipProfile) return false;
    state.presentationMembershipProfiles[actorIndex] = profile;
    return true;
}

[[nodiscard]] inline bool ResetPresentationGoalsToStart() noexcept {
    auto& state = ThreadContext();
    if (state.presentationActorCount > state.presentationActors.size()) return false;
    bool changed = false;
    for (std::size_t index = 0; index < state.presentationActorCount; ++index) {
        auto& actor = state.presentationActors[index];
        changed = changed || actor.goalNode != actor.startNode;
        actor.goalNode = actor.startNode;
        actor.targetNode = detail::kInvalidPresentationNode;
        actor.conflictInvalidated = false;
    }
    state.presentationGoalSetupActive = false;
    if (changed) InvalidateCurrentRoutes(state);
    return true;
}

[[nodiscard]] inline bool RefreshPresentationStartNode(
    const std::size_t index,
    const detail::PresentationNodeSearchMode mode = detail::PresentationNodeSearchMode::optimized
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
        cache.node = mode == detail::PresentationNodeSearchMode::optimized
            ? detail::NearestPresentationNodeFast(actor.worldX, actor.worldZ)
            : detail::NearestPresentationNodeSimple(actor.worldX, actor.worldZ);
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

[[nodiscard]] inline bool BeginPresentationGoalSetup(
    const detail::PresentationNodeSearchMode mode = detail::PresentationNodeSearchMode::optimized
) noexcept {
    auto& state = ThreadContext();
    if (!ResetPresentationGoalsToStart()) return false;
    for (std::size_t index = 0; index < state.presentationActorCount; ++index) {
        if (!RefreshPresentationStartNode(index, mode)) return false;
    }
    state.presentationOccupancy = detail::BuildPresentationOccupancy(
        std::span<const detail::PresentationActorState>(
            state.presentationActors.data(),
            state.presentationActorCount
        )
    );
    state.presentationGoalSetupActive = true;
    return true;
}

[[nodiscard]] inline bool AssignActorPresentationGoal(
    const std::uint16_t actorId,
    const std::uint16_t targetId,
    const std::span<const std::uint16_t> actionActorIds,
    const std::uint8_t attackFormationMode,
    const detail::PresentationNodeSearchMode mode = detail::PresentationNodeSearchMode::optimized
) noexcept {
    auto& state = ThreadContext();
    if (!state.presentationGoalSetupActive) return false;
    const std::size_t actorIndex = FindPresentationActorIndex(actorId);
    const std::size_t targetIndex = FindPresentationActorIndex(targetId);
    if (actorIndex >= state.presentationActorCount || targetIndex >= state.presentationActorCount) return false;
    const detail::PresentationGoalDecision decision = detail::AssignPresentationGoal(
        std::span<detail::PresentationActorState>(
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

[[nodiscard]] inline bool AssignActorFallbackPresentationGoal(const std::uint16_t actorId) noexcept {
    auto& state = ThreadContext();
    if (!state.presentationGoalSetupActive) return false;
    const std::size_t actorIndex = FindPresentationActorIndex(actorId);
    if (actorIndex >= state.presentationActorCount) return false;
    auto& actor = state.presentationActors[actorIndex];
    const detail::PresentationGoalDecision decision = detail::ChooseFallbackPresentationGoal(
        actor,
        state.presentationOccupancy
    );
    if (!decision.valid) return false;
    actor.goalNode = decision.goalNode;
    if (decision.goalChanged) InvalidateCurrentRoutes(state);
    return true;
}

[[nodiscard]] inline bool PlanCurrentActionRoutes(const int actionIndex) noexcept {
    auto& state = ThreadContext();
    if (!state.currentTurnValid
        || actionIndex < 0
        || actionIndex >= state.currentTurnActionCount
        || state.presentationActorCount > state.presentationActors.size()) return false;
    if (state.currentRoutes.valid && state.plannedActionIndex == actionIndex) return true;
    std::array<detail::PresentationActorInput, detail::kMaxPresentationActors> routeActors{};
    for (std::size_t index = 0; index < state.presentationActorCount; ++index) {
        routeActors[index] = detail::PresentationRouteInput(state.presentationActors[index]);
    }
    state.currentRoutes = detail::PlanPresentationRoutes(
        std::span<const detail::PresentationActorInput>(routeActors.data(), state.presentationActorCount),
        std::span<const std::uint16_t>(
            state.turnActionActors.data(),
            static_cast<std::size_t>(state.currentTurnActionCount)
        ),
        static_cast<std::size_t>(actionIndex)
    );
    state.plannedActionIndex = state.currentRoutes.valid ? actionIndex : -1;
    return state.currentRoutes.valid;
}

inline void SetTargetRecord02161720ActorId(const std::uint16_t actorId) noexcept {
    ThreadContext().targetRecord02161720ActorId = actorId;
}

[[nodiscard]] inline const detail::PresentationActorRoute* FindCurrentRoute(
    const std::uint16_t actorId
) noexcept {
    return detail::FindPresentationRoute(ThreadContext().currentRoutes, actorId);
}

[[nodiscard]] inline bool CommitCurrentRouteEnd(const std::uint16_t actorId) noexcept {
    auto& state = ThreadContext();
    const std::size_t actorIndex = FindPresentationActorIndex(actorId);
    if (actorIndex >= state.presentationActorCount) return false;
    const detail::PresentationActorRoute* route = FindCurrentRoute(actorId);
    if (route == nullptr || route->count == 0) return true;
    const std::uint8_t node = route->nodes[route->count - 1];
    if (node >= detail::kPresentationNodePositions.size()) return false;
    const auto position = detail::kPresentationNodePositions[node];
    if (!position.valid) return false;
    auto& actor = state.presentationActors[actorIndex];
    actor.startNode = node;
    actor.goalNode = node;
    actor.worldX = position.x;
    actor.worldZ = position.z;
    state.nearestNodeCache[actorIndex] = {};
    InvalidateCurrentRoutes(state);
    return true;
}

[[nodiscard]] inline bool CompleteActionPresentation(
    const std::uint16_t actorId,
    const int actionIndex
) noexcept {
    if (!CommitCurrentRouteEnd(actorId)) return false;
    if (!BeginPresentationGoalSetup()) return false;
    if (!AssignActorFallbackPresentationGoal(actorId)) return false;
    if (!PlanCurrentActionRoutes(actionIndex)) return false;
    return CommitCurrentRouteEnd(actorId);
}

template <typename Action>
[[nodiscard]] inline TriggerDecision Decide(const ActionRuntimeInput input) noexcept {
    static_assert(Action::dq9ActionId < metadata::kActionCount);
    auto& state = ThreadContext();
    const std::uint32_t profile = PresentationMembershipProfileForActor(input.actorId);
    const MembershipCell actorMembership = Action::ActorMembership(profile);
    const MembershipCell fallbackMembership = Action::FallbackMembership();

    TriggerSource source = TriggerSource::none;
    std::uint32_t selectorProjection = 0;
    if (actorMembership.Present()) {
        source = TriggerSource::actor_membership;
        selectorProjection = actorMembership.selectorProjection;
    } else if (Action::actionHasBact) {
        source = TriggerSource::action_bact;
        selectorProjection = Action::actionSelectorProjection;
    } else if (fallbackMembership.Present()) {
        source = TriggerSource::fallback_membership;
        selectorProjection = fallbackMembership.selectorProjection;
    }
    if (source == TriggerSource::none) return {};

    const detail::PresentationActorRoute* actorRoute = FindCurrentRoute(input.actorId);
    const std::uint8_t actorRouteCount = actorRoute == nullptr ? 0 : actorRoute->count;

    bool anyRouteAbove4 = false;
    for (std::size_t index = 0; index < state.currentRoutes.actorCount; ++index) {
        if (state.currentRoutes.actors[index].count > 4) {
            anyRouteAbove4 = true;
            break;
        }
    }

    const std::size_t actorIndex = FindPresentationActorIndex(input.actorId);
    const bool actorPresentationFlag80 = actorIndex < state.presentationActorCount
        && (state.presentationActors[actorIndex].presentationFlags & detail::kPresentationFlag80) != 0;
    const bool consecutiveAttackReset = input.turnActionIndex > 0
        && Action::dq9ActionId == 1
        && state.hasPreviousAction
        && state.previousAction.dq9ActionId == 1
        && input.actorId != kInvalidBattleActor
        && input.actorId == state.previousAction.actorId
        && input.targetId != kInvalidBattleActor
        && input.targetId == state.previousAction.targetId
        && state.targetRecord02161720ActorId == state.previousAction.targetId
        && actorPresentationFlag80
        && actorMembership.count > 1;
    if (consecutiveAttackReset) {
        return {TriggerSource::reset_only, false, false, true};
    }

    if (actorRouteCount == 0 && ComputeSelectorSuppression(selectorProjection)) return {};

    const bool targetIsCurrentActor = input.targetPresentationSlot != 0xff
        && input.targetId == input.currentActorId;
    const bool actorsOverlap = input.actorAndTargetHaveGeometry
        && input.actorTargetDistance < ((input.actorRadius + input.targetRadius) >> 1);
    const bool forceMode1Exception = anyRouteAbove4 || targetIsCurrentActor || actorsOverlap;
    return {
        source,
        true,
        input.turnActionIndex == 0 || forceMode1Exception,
        false,
    };
}

template <typename Action>
[[nodiscard]] inline bool CommitActionProgress(
    const int actionIndex,
    const int turnActionCount,
    const std::uint16_t actorId,
    const std::uint16_t targetId
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
    state.previousCommonActionId = Action::commonActionId;
    state.previousActionIndex = actionIndex;
    state.previousAction = {Action::dq9ActionId, actorId, targetId};
    state.targetRecord02161720ActorId = kInvalidBattleActor;
    return true;
}

static_assert(Dq9ActorId({BattleActorSide::ally, 0}) == 0x0000);
static_assert(Dq9ActorId({BattleActorSide::enemy, 0}) == 0x00c0);
static_assert(!ComputeSelectorSuppression(UINT32_C(0x00050003)));
static_assert(ComputeSelectorSuppression(UINT32_C(0x000a0005)));

} // namespace dq9::freecam::fast
