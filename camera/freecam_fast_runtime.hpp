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

static_assert(generated::kCameraMetadataBytes.size() == 6288);
static_assert(MagicIs(generated::kCameraMetadataBytes, 'F', 'C', 'M', '1'));
static_assert(ReadU32(generated::kCameraMetadataBytes, 4) == 3);
static_assert(ReadU32(generated::kCameraMetadataBytes, 8) == kActionCount);

static_assert(generated::kActionMetadataBytes.size() == 6164);
static_assert(MagicIs(generated::kActionMetadataBytes, 'F', 'C', 'M', 'A'));
static_assert(ReadU32(generated::kActionMetadataBytes, 4) == 4);
static_assert(ReadU32(generated::kActionMetadataBytes, 8) == kActionCount);
inline constexpr std::size_t kFormationModeOffset = ReadU32(generated::kActionMetadataBytes, 12);
inline constexpr std::size_t kPresentationTypeOffset = ReadU32(generated::kActionMetadataBytes, 16);
static_assert(kFormationModeOffset == 20 + kActionCount * 2);
static_assert(kPresentationTypeOffset == kFormationModeOffset + kActionCount);

static_assert(generated::kTargetSideCode.size() == kActionCount);
static_assert(generated::kTargetScopeCode.size() == kActionCount);
static_assert(generated::kActionClassificationPresent.size() == kActionCount);
static_assert(generated::kRepeatModeCode.size() == kActionCount);
static_assert(generated::kOperationTypeCode.size() == kActionCount);
static_assert(generated::kResourceCost.size() == kActionCount);
static_assert(generated::kTargetHandlerJudgment1.size() == kActionCount);
static_assert(generated::kTargetHandlerJudgment2.size() == kActionCount);
static_assert(generated::kHasAnyMinedFreeCameraTriggerSource.size() == kActionCount);
static_assert(generated::kCameraBehaviorCode.size() == kActionCount);
static_assert(generated::kFreeCameraMapperAllowed.size() == kActionCount);

static_assert(MagicIs(generated::kMembershipMetadataBytes, 'F', 'C', 'M', 'M'));
static_assert(ReadU32(generated::kMembershipMetadataBytes, 4) == 3);
static_assert(ReadU32(generated::kMembershipMetadataBytes, 8) == kActionCount);

static_assert(generated::kMonsterPresentationMetadataBytes.size() == 20 + 1024);
static_assert(MagicIs(generated::kMonsterPresentationMetadataBytes, 'F', 'C', 'M', 'P'));
static_assert(ReadU32(generated::kMonsterPresentationMetadataBytes, 4) == 1);
inline constexpr std::size_t kMonsterPresentationCapacity =
    ReadU32(generated::kMonsterPresentationMetadataBytes, 8);
static_assert(kMonsterPresentationCapacity == 1024);

inline constexpr std::size_t kActorProfileCount =
    ReadU32(generated::kMembershipMetadataBytes, 12);
inline constexpr std::size_t kPlayerProfileCount =
    ReadU32(generated::kMembershipMetadataBytes, 16);
inline constexpr std::size_t kMonsterProfileCount =
    ReadU32(generated::kMembershipMetadataBytes, 20);
inline constexpr std::size_t kSpecialProfileCount =
    ReadU32(generated::kMembershipMetadataBytes, 24);
inline constexpr std::size_t kBodyItemModelCount =
    ReadU16(generated::kMembershipMetadataBytes, 28);
inline constexpr std::size_t kWeaponItemModelCount =
    ReadU16(generated::kMembershipMetadataBytes, 30);

static_assert(kActorProfileCount == 617);
static_assert(kPlayerProfileCount == 13);
static_assert(kMonsterProfileCount == 438);
static_assert(kSpecialProfileCount == 1);
static_assert(kBodyItemModelCount == 183);
static_assert(kWeaponItemModelCount == 268);

struct PlayerProfileMapEntry {
    std::uint16_t firstModelCode{};
    std::uint16_t secondModelCode{};
    std::uint32_t profileIndex{kInvalidProfileIndex};
};

struct ActorProfileMapEntry {
    std::uint16_t actorKey{};
    std::uint32_t profileIndex{kInvalidProfileIndex};
};

struct ItemModelMapEntry {
    std::uint16_t itemId{};
    std::uint8_t modelCode{};
};

[[nodiscard]] constexpr bool HasBact(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return false;
    const std::size_t offset = 16 + (actionId >> 3);
    return ((generated::kCameraMetadataBytes[offset] >> (actionId & 7)) & 1U) != 0;
}

[[nodiscard]] constexpr std::uint32_t SelectorProjection(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return 0;
    return ReadU32(
        generated::kCameraMetadataBytes,
        16 + 128 + static_cast<std::size_t>(actionId) * 4
    );
}

[[nodiscard]] constexpr std::uint8_t TrackingCameraOneRngCount(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return 0;
    constexpr std::size_t offset = 16 + 128 + kActionCount * 4;
    return generated::kCameraMetadataBytes[offset + actionId];
}

// ROM-mined BACT opcode 0x0C camera-placement command order, packed as
// four 2-bit entries: 1=action actor (command type 5/0x0D),
// 2=action target (command type 6/0x0E), 0=end/unused.
[[nodiscard]] constexpr std::uint8_t CameraPlacementSequence(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return 0;
    constexpr std::size_t offset = 16 + 128 + kActionCount * 4 + kActionCount;
    return generated::kCameraMetadataBytes[offset + actionId];
}
static_assert(CameraPlacementSequence(UINT16_C(24)) == UINT8_C(0x09));

[[nodiscard]] constexpr std::uint16_t FallbackLookupActionId(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return kInvalidActionId;
    return ReadU16(
        generated::kActionMetadataBytes,
        20 + static_cast<std::size_t>(actionId) * 2
    );
}

[[nodiscard]] constexpr std::uint8_t AttackFormationMode(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return 0;
    return generated::kActionMetadataBytes[kFormationModeOffset + actionId];
}

[[nodiscard]] constexpr std::uint8_t PresentationType(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return 0;
    return generated::kActionMetadataBytes[kPresentationTypeOffset + actionId];
}

// Engine-internal presentation actions recovered from overlay code. Their
// fixed action metadata is still read from the ROM-mined constexpr tables;
// only the IDs themselves come from the documented code immediates.
// - 451 / 0x01C3: literal loaded at overlay_d_25:0215A4F8 before
//   FUN_02159C68(..., slot=1) on Magic-Mirror recovery.
// - 944 / 0x03B0: stored at overlay_d_25:0215DB54 by FUN_0215DA50 when
//   actorSnapshot[0x27] (slot-1 child count) is nonzero.
inline constexpr std::uint16_t kMagicMirrorRecoveryPresentationChildActionId = UINT16_C(451);
inline constexpr std::uint16_t kSlot1CleanupPresentationActionId = UINT16_C(944);

static_assert(PresentationType(kMagicMirrorRecoveryPresentationChildActionId) == 2);
static_assert(AttackFormationMode(kMagicMirrorRecoveryPresentationChildActionId) == 3);
static_assert(PresentationType(kSlot1CleanupPresentationActionId) == 0);
static_assert(AttackFormationMode(kSlot1CleanupPresentationActionId) == 0);

[[nodiscard]] constexpr bool HasActionClassification(const std::uint16_t actionId) {
    return actionId < kActionCount && generated::kActionClassificationPresent[actionId] != 0;
}

[[nodiscard]] constexpr std::uint8_t OperationType(const std::uint16_t actionId) {
    return actionId < kActionCount ? generated::kOperationTypeCode[actionId] : 0;
}

[[nodiscard]] constexpr std::uint8_t RepeatMode(const std::uint16_t actionId) {
    return actionId < kActionCount ? generated::kRepeatModeCode[actionId] : 0;
}

[[nodiscard]] constexpr std::uint8_t ResourceCost(const std::uint16_t actionId) {
    return actionId < kActionCount ? generated::kResourceCost[actionId] : 0;
}

[[nodiscard]] constexpr std::uint16_t TargetHandlerJudgment1(const std::uint16_t actionId) {
    return actionId < kActionCount ? generated::kTargetHandlerJudgment1[actionId] : 0;
}

[[nodiscard]] constexpr std::uint16_t TargetHandlerJudgment2(const std::uint16_t actionId) {
    return actionId < kActionCount ? generated::kTargetHandlerJudgment2[actionId] : 0;
}

[[nodiscard]] constexpr std::uint8_t MonsterOccupancyExpansionDepth(
    const std::uint16_t monsterId
) {
    if (monsterId >= kMonsterPresentationCapacity) return UINT8_C(0xff);
    return generated::kMonsterPresentationMetadataBytes[20 + monsterId];
}

[[nodiscard]] constexpr std::uint64_t ActorMembershipPacked(
    const std::uint32_t profileIndex,
    const std::uint16_t actionId
) {
    if (profileIndex >= kActorProfileCount || actionId >= kActionCount) return 0;
    constexpr std::size_t headerSize = 32;
    const std::size_t cell = static_cast<std::size_t>(profileIndex) * kActionCount + actionId;
    return ReadU64(generated::kMembershipMetadataBytes, headerSize + cell * 8);
}

[[nodiscard]] constexpr std::uint64_t FallbackMembershipPacked(const std::uint16_t actionId) {
    if (actionId >= kActionCount) return 0;
    constexpr std::size_t headerSize = 32;
    constexpr std::size_t actorCellsBytes = kActorProfileCount * kActionCount * 8;
    return ReadU64(
        generated::kMembershipMetadataBytes,
        headerSize + actorCellsBytes + static_cast<std::size_t>(actionId) * 8
    );
}

template <std::uint16_t ActionId>
[[nodiscard]] consteval bool HasAnyMinedFreeCameraTriggerSource() {
    static_assert(ActionId < kActionCount);
    return generated::kHasAnyMinedFreeCameraTriggerSource[ActionId] != 0;
}

template <std::uint16_t ActionId>
[[nodiscard]] consteval bool IsFreeCameraMapperAllowed() {
    static_assert(ActionId < kActionCount);
    return generated::kFreeCameraMapperAllowed[ActionId] != 0;
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

[[nodiscard]] consteval auto BuildBodyItemModels() {
    std::array<ItemModelMapEntry, kBodyItemModelCount> result{};
    constexpr std::size_t offset0 = 32
        + kActorProfileCount * kActionCount * 8
        + kActionCount * 8
        + kPlayerProfileCount * 8
        + kMonsterProfileCount * 8
        + kSpecialProfileCount * 8;
    for (std::size_t index = 0; index < result.size(); ++index) {
        const std::size_t offset = offset0 + index * 4;
        result[index] = {
            ReadU16(generated::kMembershipMetadataBytes, offset),
            generated::kMembershipMetadataBytes[offset + 2],
        };
    }
    return result;
}

[[nodiscard]] consteval auto BuildWeaponItemModels() {
    std::array<ItemModelMapEntry, kWeaponItemModelCount> result{};
    constexpr std::size_t offset0 = 32
        + kActorProfileCount * kActionCount * 8
        + kActionCount * 8
        + kPlayerProfileCount * 8
        + kMonsterProfileCount * 8
        + kSpecialProfileCount * 8
        + kBodyItemModelCount * 4;
    for (std::size_t index = 0; index < result.size(); ++index) {
        const std::size_t offset = offset0 + index * 4;
        result[index] = {
            ReadU16(generated::kMembershipMetadataBytes, offset),
            generated::kMembershipMetadataBytes[offset + 2],
        };
    }
    return result;
}

inline constexpr auto kPlayerProfiles = BuildPlayerProfiles();
inline constexpr auto kMonsterProfiles = BuildMonsterProfiles();
inline constexpr auto kSpecialProfiles = BuildSpecialProfiles();
inline constexpr auto kBodyItemModels = BuildBodyItemModels();
inline constexpr auto kWeaponItemModels = BuildWeaponItemModels();

} // namespace metadata

inline constexpr std::uint32_t kInvalidMembershipProfile = metadata::kInvalidProfileIndex;
inline constexpr std::uint16_t kNoEquipmentItemId = UINT16_C(0xffff);
inline constexpr std::uint16_t kInvalidPlayerModelCode = UINT16_C(0xffff);

struct MembershipCell {
    std::uint32_t selectorProjection{};
    std::uint16_t count{};
    std::uint16_t trackingCameraOneRngCount{};

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
        static_cast<std::uint16_t>((packed >> 48) & UINT64_C(0xffff)),
    };
}

// Compile-time action binding. DQ9 action ID and BattleEmulator common ID are
// both template arguments, so no runtime common-ID -> DQ9-ID mapping table is
// needed. The full ROM table is read only during constant evaluation; each
// instantiated action retains only its own actor-membership column plus fixed
// action/fallback values.
template <std::uint16_t Dq9ActionId, int BattleEmulatorCommonId = -1>
struct FreeCamera {
    static_assert(Dq9ActionId < metadata::kActionCount);
    static_assert(BattleEmulatorCommonId >= -1);

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
    static inline constexpr std::uint8_t presentationType =
        metadata::PresentationType(Dq9ActionId);
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
[[nodiscard]] constexpr std::uint16_t ResolveItemModelCode(
    const std::array<metadata::ItemModelMapEntry, N>& entries,
    const std::uint16_t itemId
) noexcept {
    std::size_t first = 0;
    std::size_t last = N;
    while (first < last) {
        const std::size_t middle = first + (last - first) / 2;
        if (entries[middle].itemId < itemId) first = middle + 1;
        else last = middle;
    }
    return first < N && entries[first].itemId == itemId
        ? entries[first].modelCode
        : kInvalidPlayerModelCode;
}

[[nodiscard]] constexpr std::uint16_t ResolveBodyModelCode(const std::uint16_t itemId) noexcept {
    return ResolveItemModelCode(metadata::kBodyItemModels, itemId);
}

[[nodiscard]] constexpr std::uint16_t ResolveWeaponModelCode(const std::uint16_t itemId) noexcept {
    return ResolveItemModelCode(metadata::kWeaponItemModels, itemId);
}

[[nodiscard]] constexpr std::uint32_t ResolvePlayerProfileFromEquipment(
    const std::uint16_t bodyItemId,
    const std::uint16_t primaryWeaponItemId
) noexcept {
    const std::uint16_t firstModelCode = ResolveBodyModelCode(bodyItemId);
    if (firstModelCode == kInvalidPlayerModelCode) return kInvalidMembershipProfile;
    const std::uint16_t secondModelCode = primaryWeaponItemId == kNoEquipmentItemId
        ? UINT16_C(0)
        : ResolveWeaponModelCode(primaryWeaponItemId);
    if (secondModelCode == kInvalidPlayerModelCode) return kInvalidMembershipProfile;
    return ResolvePlayerProfile(firstModelCode, secondModelCode);
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

struct TrackingCameraDecision {
    TriggerSource source{TriggerSource::none};
    std::uint16_t rngCount{};
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

    [[nodiscard]] constexpr bool operator==(const ActionState&) const = default;
};

struct ActionRuntimeInput {
    std::uint16_t actorId{kInvalidBattleActor};
    std::uint16_t targetId{kInvalidBattleActor};
    std::uint16_t turnActionIndex{};
    // ROM path: current action record (0216268C) -> target ID -> 0200FCC4(actor)
    // -> actor+0x13C presentation pointer -> FUN_0204A20C/0204A214 -> presentation+0x1E.
    // PresentationActorState::auxiliaryNode mirrors that +0x1E byte. This is not
    // a party/enemy index or the presentationActors[] array index.
    std::uint8_t targetAuxiliaryNode{0xff};
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
    // ROM controller+0x57C8 indexes the 0x28-byte presentation action-record
    // array, which includes synthetic records such as 944. Keep that index
    // separate from previousActionIndex, which validates the real
    // BattleEmulator action array used by the route planner.
    int presentationActionRecordIndex{-1};
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

        [[nodiscard]] constexpr bool operator==(const NearestNodeCache&) const = default;
    };
    std::array<NearestNodeCache, detail::kMaxPresentationActors> nearestNodeCache{};
    std::uint8_t presentationActorCount{};
    std::array<std::uint16_t, detail::kMaxPresentationActions> turnActionActors{};
    // overlay_d_25:021E1958 leaves roster row+4 uninitialized. 021E08BC later
    // consumes only its zero/nonzero state. Keep that compiler-stack artifact
    // explicit instead of inventing an actor/action semantic for it.
    std::array<bool, detail::kMaxPresentationActors> rosterField4Nonzero{};
    // Knowledge is per physical row. A live-confirmed four-row encounter must
    // not invent residue for rows 4..11 in a larger encounter.
    std::array<bool, detail::kMaxPresentationActors> rosterField4Known{};
    bool rosterField4CompatibilityValid{};
    detail::PresentationOccupancyMap presentationOccupancy{};
    bool presentationGoalSetupActive{};
    detail::PresentationTurnRoutes currentRoutes{};
    int plannedActionIndex{-1};

    [[nodiscard]] constexpr bool operator==(const RuntimeState&) const = default;
};

inline thread_local RuntimeState gRuntimeState{};
inline thread_local RuntimeState* gRuntimeStateOverride = nullptr;

[[nodiscard]] inline RuntimeState& ThreadContext() noexcept {
    return gRuntimeStateOverride != nullptr ? *gRuntimeStateOverride : gRuntimeState;
}

inline void BindThreadContext(RuntimeState* state) noexcept {
    gRuntimeStateOverride = state;
}

inline void UnbindThreadContext() noexcept {
    gRuntimeStateOverride = nullptr;
}

inline void InvalidateCurrentRoutes(RuntimeState& state) noexcept {
    state.currentRoutes = {};
    state.plannedActionIndex = -1;
}

inline void ResetBattle() noexcept {
    auto& state = ThreadContext();
    state = {};
    state.battleActive = true;
    state.turnActionActors.fill(detail::kInvalidPresentationActor);
    state.presentationMembershipProfiles.fill(kInvalidMembershipProfile);
    state.rosterField4Nonzero.fill(false);
    state.rosterField4Known.fill(false);
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
    state.presentationActionRecordIndex = -1;
    state.currentTurnActionCount = static_cast<int>(actionOrder.size());
    state.currentTurnValid = true;
    state.turnActionActors.fill(detail::kInvalidPresentationActor);
    state.targetRecord02161720ActorId = kInvalidBattleActor;
    state.rosterField4CompatibilityValid = false;
    state.rosterField4Known.fill(false);
    state.presentationGoalSetupActive = false;
    InvalidateCurrentRoutes(state);
    for (std::size_t index = 0; index < actionOrder.size(); ++index) {
        state.turnActionActors[index] = Dq9ActorId(actionOrder[index]);
    }
    return true;
}

inline void InvalidateRosterField4Compatibility() noexcept {
    auto& state = ThreadContext();
    state.rosterField4CompatibilityValid = false;
    state.rosterField4Known.fill(false);
}

[[nodiscard]] inline bool SetRosterField4Compatibility(
    const std::span<const bool> nonzero
) noexcept {
    auto& state = ThreadContext();
    if (nonzero.size() != state.presentationActorCount) return false;
    state.rosterField4Nonzero.fill(false);
    state.rosterField4Known.fill(false);
    for (std::size_t index = 0; index < nonzero.size(); ++index) {
        state.rosterField4Nonzero[index] = nonzero[index];
        state.rosterField4Known[index] = true;
    }
    state.rosterField4CompatibilityValid = true;
    return true;
}

[[nodiscard]] inline bool SetRosterField4CompatibilityPrefix(
    const std::span<const bool> nonzero
) noexcept {
    auto& state = ThreadContext();
    if (nonzero.empty() || state.presentationActorCount == 0) return false;
    state.rosterField4Nonzero.fill(false);
    state.rosterField4Known.fill(false);
    const std::size_t count = nonzero.size() < state.presentationActorCount
        ? nonzero.size()
        : state.presentationActorCount;
    for (std::size_t index = 0; index < count; ++index) {
        state.rosterField4Nonzero[index] = nonzero[index];
        state.rosterField4Known[index] = true;
    }
    state.rosterField4CompatibilityValid = true;
    return true;
}

[[nodiscard]] inline bool HasRosterField4Compatibility() noexcept {
    return ThreadContext().rosterField4CompatibilityValid;
}

[[nodiscard]] inline bool RosterField4IsKnown(const std::size_t index) noexcept {
    const auto& state = ThreadContext();
    return index < state.presentationActorCount && state.rosterField4Known[index];
}

[[nodiscard]] inline bool RosterField4IsZero(const std::size_t index) noexcept {
    const auto& state = ThreadContext();
    return index < state.presentationActorCount
        && state.rosterField4Known[index]
        && !state.rosterField4Nonzero[index];
}

[[nodiscard]] inline bool SetRosterField4SlotNonzero(
    const std::size_t index,
    const bool nonzero
) noexcept {
    auto& state = ThreadContext();
    if (index >= state.presentationActorCount) return false;
    state.rosterField4Nonzero[index] = nonzero;
    state.rosterField4Known[index] = true;
    state.rosterField4CompatibilityValid = true;
    return true;
}

// Stack compatibility for the battle HUD page-counter renderer:
//   020515EC -> 02051880 -> 02050678 -> 02050BD0 -> 0204F284/0204F2F8.
// The stale 021E1958 row+4 words are an ABI side effect of that renderer, not
// a presentation-type property.  The mask itself is ROM-mined from the ARM9
// call immediates plus font_lv5.gp2 metrics by build_freecam_renderer_metadata.mjs.
[[nodiscard]] inline bool ApplyBattleHudRendererResidueCompatibility() noexcept {
    std::array<bool, 4> prefix{};
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        prefix[index] =
            ((generated::kBattleHudRendererResiduePrefixMask >> index) & UINT8_C(1)) != 0;
    }
    return SetRosterField4CompatibilityPrefix(prefix);
}

[[nodiscard]] inline bool ApplyKnownRosterField4PostActionCompatibility(
    const std::uint8_t presentationType
) noexcept {
    auto& state = ThreadContext();
    std::array<bool, detail::kMaxPresentationActors> pattern{};
    switch (presentationType) {
        case 1:
            // Live seed-8 evidence after both Bagima and Merami. These are
            // physical work-row slots, not actor identities.
            for (const std::size_t index : {0u, 1u, 2u, 5u, 6u, 7u, 8u, 9u}) {
                if (index < pattern.size()) pattern[index] = true;
            }
            break;
        case 17:
            // Live seed-8 evidence after Zaki.
            for (const std::size_t index : {0u, 1u, 4u, 5u, 6u, 7u, 8u, 9u}) {
                if (index < pattern.size()) pattern[index] = true;
            }
            break;
        case generated::kTensionGainPresentationType: {
            // This presentation path is one known trigger of the independent
            // FUN_020515EC battle-HUD renderer lifecycle.
            return ApplyBattleHudRendererResidueCompatibility();
        }
        default:
            InvalidateRosterField4Compatibility();
            return false;
    }
    return SetRosterField4Compatibility(
        std::span<const bool>(pattern.data(), state.presentationActorCount)
    );
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

[[nodiscard]] inline TrackingCameraDecision TrackingCameraFor(
    const std::uint16_t dq9ActionId,
    const std::uint16_t actorId
) noexcept {
    if (dq9ActionId >= metadata::kActionCount) return {};

    const std::uint32_t profile = PresentationMembershipProfileForActor(actorId);
    const MembershipCell actorMembership = DecodeMembershipCell(
        metadata::ActorMembershipPacked(profile, dq9ActionId)
    );
    if (actorMembership.Present()) {
        return {TriggerSource::actor_membership, actorMembership.trackingCameraOneRngCount};
    }

    if (metadata::HasBact(dq9ActionId)) {
        return {TriggerSource::action_bact, metadata::TrackingCameraOneRngCount(dq9ActionId)};
    }

    const std::uint16_t fallbackActionId = metadata::FallbackLookupActionId(dq9ActionId);
    if (fallbackActionId != metadata::kInvalidActionId) {
        const MembershipCell fallbackMembership = DecodeMembershipCell(
            metadata::FallbackMembershipPacked(fallbackActionId)
        );
        if (fallbackMembership.Present()) {
            return {TriggerSource::fallback_membership, fallbackMembership.trackingCameraOneRngCount};
        }
    }
    return {};
}

[[nodiscard]] inline bool SetPlayerMembershipProfileFromEquipment(
    const std::size_t actorIndex,
    const std::uint16_t bodyItemId,
    const std::uint16_t primaryWeaponItemId
) noexcept {
    auto& state = ThreadContext();
    if (actorIndex >= state.presentationActorCount) return false;
    const std::uint32_t profile = ResolvePlayerProfileFromEquipment(bodyItemId, primaryWeaponItemId);
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

[[nodiscard]] inline bool SetMonsterPresentationMetadata(
    const std::size_t actorIndex,
    const std::uint16_t monsterId
) noexcept {
    auto& state = ThreadContext();
    if (actorIndex >= state.presentationActorCount) return false;
    constexpr std::uint8_t invalidDepth = UINT8_C(0xff);
    const std::uint8_t depth = metadata::MonsterOccupancyExpansionDepth(monsterId);
    if (depth == invalidDepth) return false;
    auto& actor = state.presentationActors[actorIndex];
    if (actor.occupancyExpansionDepth != depth) {
        actor.occupancyExpansionDepth = depth;
        state.presentationGoalSetupActive = false;
        InvalidateCurrentRoutes(state);
    }
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

[[nodiscard]] inline std::uint16_t ResolveActorPresentationTarget(
    const std::size_t index,
    const std::uint16_t primaryTargetId
) noexcept {
    const auto& state = ThreadContext();
    if (index >= state.presentationActorCount) return kInvalidBattleActor;
    return detail::ResolvePresentationTarget(state.presentationActors[index], primaryTargetId);
}

[[nodiscard]] inline bool IsActorPresentationMovementEligible(
    const std::size_t index,
    const std::uint16_t targetActorId
) noexcept {
    const auto& state = ThreadContext();
    return index < state.presentationActorCount
        && detail::IsPresentationMovementEligible(state.presentationActors[index], targetActorId);
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
        mode,
        state.rosterField4CompatibilityValid
            ? std::span<bool>(state.rosterField4Nonzero.data(), state.presentationActorCount)
            : std::span<bool>{},
        state.rosterField4CompatibilityValid
            ? std::span<bool>(state.rosterField4Known.data(), state.presentationActorCount)
            : std::span<bool>{}
    );
    if (!decision.valid) return false;
    if (decision.goalChanged) InvalidateCurrentRoutes(state);
    return true;
}

[[nodiscard]] inline bool AssignActorFallbackPresentationGoal(
    const std::uint16_t actorId,
    const bool rosterField4Nonzero = false
) noexcept {
    auto& state = ThreadContext();
    if (!state.presentationGoalSetupActive) return false;
    const std::size_t actorIndex = FindPresentationActorIndex(actorId);
    if (actorIndex >= state.presentationActorCount) return false;
    auto& actor = state.presentationActors[actorIndex];
    const detail::PresentationGoalDecision decision = detail::ChooseFallbackPresentationGoal(
        actor,
        state.presentationOccupancy,
        rosterField4Nonzero
    );
    if (!decision.valid) return false;
    actor.goalNode = decision.goalNode;
    if (decision.goalChanged) InvalidateCurrentRoutes(state);
    return true;
}

[[nodiscard]] inline bool RebuildPresentationOccupancy() noexcept {
    auto& state = ThreadContext();
    if (!state.presentationGoalSetupActive
        || state.presentationActorCount > state.presentationActors.size()) return false;
    state.presentationOccupancy = detail::BuildPresentationOccupancy(
        std::span<const detail::PresentationActorState>(
            state.presentationActors.data(),
            state.presentationActorCount
        ),
        state.presentationOccupancy
    );
    return true;
}

// Exact previous-action participant cleanup from overlay_d_25:021E0D74..
// 021E0F20. For participants belonging to actions earlier than the current
// presentation record, 021E08BC first tries to reuse presentation+0x1E as the
// goal when that node is compatible with the actor class, otherwise it runs
// the ordinary fallback-goal chooser. It then unconditionally clears +0x1E
// through 0204A1FC(actor, 0xFF), and refreshes +0x1F from the participant's
// resolved target goal. This is battle-engine state maintenance, not an
// action- or monster-specific camera rule.
[[nodiscard]] inline bool PreparePreviousActionPresentationParticipant(
    const std::uint16_t actorId,
    const std::uint16_t primaryTargetId,
    const bool rosterField4Nonzero = false
) noexcept {
    auto& state = ThreadContext();
    if (!state.presentationGoalSetupActive) return false;
    const std::size_t actorIndex = FindPresentationActorIndex(actorId);
    if (actorIndex >= state.presentationActorCount) return false;

    auto& actor = state.presentationActors[actorIndex];
    const std::uint8_t auxiliary = actor.auxiliaryNode;
    const std::uint8_t actorClass = detail::PresentationClassForActor(actor.actorId);
    const bool canReuseAuxiliary = auxiliary < state.presentationOccupancy.size()
        && (state.presentationOccupancy[auxiliary] < UINT8_C(0xf2)
            || state.presentationOccupancy[auxiliary] == actorClass);
    if (canReuseAuxiliary) {
        actor.goalNode = auxiliary;
    } else {
        const detail::PresentationGoalDecision decision = detail::ChooseFallbackPresentationGoal(
            actor,
            state.presentationOccupancy,
            rosterField4Nonzero
        );
        if (!decision.valid) return false;
        actor.goalNode = decision.goalNode;
    }

    actor.auxiliaryNode = detail::kInvalidPresentationNode;

    const std::uint16_t resolvedTargetId = detail::ResolvePresentationTarget(actor, primaryTargetId);
    const std::size_t targetIndex = FindPresentationActorIndex(resolvedTargetId);
    if (targetIndex < state.presentationActorCount) {
        actor.targetNode = state.presentationActors[targetIndex].goalNode;
    }
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

// PlanPresentationRoutes returns coordinated routes for every presentation
// participant, not just the actor whose battle action is currently executing.
// The ROM likewise writes/moves non-acting participants during formation
// changes.  Commit the whole planned formation atomically; invalidating after
// the first actor would discard the remaining routes.
[[nodiscard]] inline bool CommitAllCurrentRouteEnds() noexcept {
    auto& state = ThreadContext();
    if (!state.currentRoutes.valid) return false;

    bool changed = false;
    for (std::size_t routeIndex = 0; routeIndex < state.currentRoutes.actorCount; ++routeIndex) {
        const auto& route = state.currentRoutes.actors[routeIndex];
        if (route.actorId == detail::kInvalidPresentationActor || route.count == 0) continue;
        const std::size_t actorIndex = FindPresentationActorIndex(route.actorId);
        if (actorIndex >= state.presentationActorCount) return false;
        const std::uint8_t node = route.nodes[route.count - 1];
        if (node >= detail::kPresentationNodePositions.size()) return false;
        const auto position = detail::kPresentationNodePositions[node];
        if (!position.valid) return false;

        auto& actor = state.presentationActors[actorIndex];
        actor.startNode = node;
        actor.goalNode = node;
        actor.worldX = position.x;
        actor.worldZ = position.z;
        state.nearestNodeCache[actorIndex] = {};
        changed = true;
    }
    if (changed) InvalidateCurrentRoutes(state);
    return true;
}

// Exact presentation-state side effect of main:0216964C -> 0204ACA8 /
// 0204A904. 0204ACA8 first clears flag bits 0..1 and sets 0x20, then
// 02049B10(actor, 0) chooses the new start node with the priority
// auxiliary -> goal -> existing start, clears goal and auxiliary, and moves
// the actor to the chosen presentation node. 0204A904 subsequently clears
// flag bit 2. This reset is shared by multiple camera/presentation paths; it
// is not an action-specific rule.
[[nodiscard]] inline bool ResetAllPresentationActorsForCameraPlacement() noexcept {
    auto& state = ThreadContext();
    if (state.presentationActorCount > state.presentationActors.size()) return false;

    for (std::size_t index = 0; index < state.presentationActorCount; ++index) {
        auto& actor = state.presentationActors[index];
        actor.presentationFlags = (actor.presentationFlags & ~UINT32_C(0x7)) | UINT32_C(0x20);

        std::uint8_t newStart = actor.startNode;
        if (actor.auxiliaryNode != detail::kInvalidPresentationNode) {
            newStart = actor.auxiliaryNode;
        } else if (actor.goalNode != detail::kInvalidPresentationNode) {
            newStart = actor.goalNode;
        }

        if (newStart != detail::kInvalidPresentationNode) {
            if (newStart >= detail::kPresentationNodePositions.size()) return false;
            const auto position = detail::kPresentationNodePositions[newStart];
            if (!position.valid) return false;
            actor.startNode = newStart;
            actor.worldX = position.x;
            actor.worldZ = position.z;
        }
        actor.goalNode = detail::kInvalidPresentationNode;
        actor.auxiliaryNode = detail::kInvalidPresentationNode;
        state.nearestNodeCache[index] = {};
    }

    state.presentationGoalSetupActive = false;
    InvalidateCurrentRoutes(state);
    return true;
}

inline constexpr std::int32_t kCameraActorWorldBound = INT32_C(0x6000);

// Exact final bounds branch of overlay_d_00:0216F62C. The camera-placement
// routine clamps X/Z to +/-0x6000. If either component changes, it runs the
// global 0216964C actor reset first, then restores only the actor currently
// being placed to the clamped world position.
[[nodiscard]] inline bool ApplyCameraActorWorldBounds(const std::uint16_t actorId) noexcept {
    auto& state = ThreadContext();
    const std::size_t actorIndex = FindPresentationActorIndex(actorId);
    if (actorIndex >= state.presentationActorCount) return false;

    const auto clampComponent = [](const std::int32_t value) noexcept {
        if (value > kCameraActorWorldBound) return kCameraActorWorldBound;
        if (value < -kCameraActorWorldBound) return -kCameraActorWorldBound;
        return value;
    };

    const std::int32_t clampedX = clampComponent(state.presentationActors[actorIndex].worldX);
    const std::int32_t clampedZ = clampComponent(state.presentationActors[actorIndex].worldZ);
    if (clampedX == state.presentationActors[actorIndex].worldX
        && clampedZ == state.presentationActors[actorIndex].worldZ) {
        return true;
    }

    if (!ResetAllPresentationActorsForCameraPlacement()) return false;
    auto& actor = state.presentationActors[actorIndex];
    actor.worldX = clampedX;
    actor.worldZ = clampedZ;
    state.nearestNodeCache[actorIndex] = {};
    return true;
}

[[nodiscard]] inline bool CompleteActionPresentation(
    const std::uint16_t actorId,
    const int actionIndex
) noexcept {
    (void)actorId;
    (void)actionIndex;
    return CommitAllCurrentRouteEnds();
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
    const bool hasPriorPresentationRecord = state.presentationActionRecordIndex >= 0;
    const bool consecutiveAttackReset = hasPriorPresentationRecord
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

    // overlay_d_25:021626CC returns the previous action record when
    // controller+0x57C8 > 0. 021DC394..021DC3C0 then resolves actor[0]
    // from that previous record and compares it with the current target ID.
    // It is not a comparison against the current acting actor.
    const bool targetIsPreviousActionActor = hasPriorPresentationRecord
        && state.hasPreviousAction
        && input.targetAuxiliaryNode != 0xff
        && input.targetId == state.previousAction.actorId;
    const bool actorsOverlap = input.actorAndTargetHaveGeometry
        && input.actorTargetDistance < ((input.actorRadius + input.targetRadius) >> 1);
    const bool forceMode1Exception = anyRouteAbove4 || targetIsPreviousActionActor || actorsOverlap;
    return {
        source,
        true,
        !hasPriorPresentationRecord || forceMode1Exception,
        false,
    };
}

[[nodiscard]] inline bool CommitActionProgressRaw(
    const int actionIndex,
    const int turnActionCount,
    const int commonActionId,
    const std::uint16_t dq9ActionId,
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
    state.previousCommonActionId = commonActionId;
    state.previousActionIndex = actionIndex;
    ++state.presentationActionRecordIndex;
    state.previousAction = {dq9ActionId, actorId, targetId};
    state.targetRecord02161720ActorId = kInvalidBattleActor;
    return true;
}

// FUN_0215DA50 creates one DQ9 action 944 self-record for an actor snapshot
// when slot-1 child count (snapshot+0x27) is nonzero, then clears that count.
// This updates only the presentation-record history; the real action index is
// deliberately unchanged so route planning continues to index the original
// BattleEmulator action list.
[[nodiscard]] inline bool AppendSlot1CleanupPresentationRecord(
    const std::uint16_t actorId
) noexcept {
    auto& state = ThreadContext();
    if (!state.battleActive || !state.currentTurnValid) return false;
    const std::size_t actorIndex = FindPresentationActorIndex(actorId);
    if (actorIndex >= state.presentationActorCount) return false;

    auto& actor = state.presentationActors[actorIndex];
    // Live 0x03B0 records leave the return/start node intact and invalidate
    // the transient goal/aux/target presentation bytes before the next real
    // action. These fields correspond to the observed 0xFF values used by
    // FUN_0204A20C and the next 021DC1D4 selector pass.
    actor.goalNode = detail::kInvalidPresentationNode;
    actor.auxiliaryNode = detail::kInvalidPresentationNode;
    actor.targetNode = detail::kInvalidPresentationNode;
    state.nearestNodeCache[actorIndex] = {};
    InvalidateCurrentRoutes(state);

    state.hasPreviousAction = true;
    state.previousCommonActionId = -1;
    ++state.presentationActionRecordIndex;
    state.previousAction = {
        metadata::kSlot1CleanupPresentationActionId,
        actorId,
        actorId,
    };
    state.targetRecord02161720ActorId = kInvalidBattleActor;
    return true;
}

template <typename Action>
[[nodiscard]] inline bool CommitActionProgress(
    const int actionIndex,
    const int turnActionCount,
    const std::uint16_t actorId,
    const std::uint16_t targetId
) noexcept {
    return CommitActionProgressRaw(
        actionIndex,
        turnActionCount,
        Action::commonActionId,
        Action::dq9ActionId,
        actorId,
        targetId
    );
}

static_assert(Dq9ActorId({BattleActorSide::ally, 0}) == 0x0000);
static_assert(Dq9ActorId({BattleActorSide::enemy, 0}) == 0x00c0);
static_assert(!ComputeSelectorSuppression(UINT32_C(0x00050003)));
static_assert(ComputeSelectorSuppression(UINT32_C(0x000a0005)));

} // namespace dq9::freecam::fast
