#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>

#include "freecam_route.hpp"

namespace dq9::freecam::detail {

inline constexpr std::uint8_t kInvalidPresentationTarget = 0xff;
inline constexpr std::uint32_t kPresentationFlag80 = 0x80;
inline constexpr std::uint32_t kPresentationFlag100 = 0x100;

enum class PresentationNodeSearchMode : std::uint8_t {
    reference,
    optimized,
};

struct PresentationNodePosition {
    std::int32_t x{};
    std::int32_t z{};
    bool valid{};
};

// Literal float bit patterns read at overlay_d_00:02170F40..02170F54.
// Keep each arithmetic step in float to match overlay_d_00:02170E78.
inline constexpr float kPresentationColumnStep = std::bit_cast<float>(UINT32_C(0x462646e1));
inline constexpr float kPresentationXOrigin = std::bit_cast<float>(UINT32_C(0xc72646e1));
inline constexpr float kPresentationOddRowOffset = std::bit_cast<float>(UINT32_C(0x45a646e1));
inline constexpr float kPresentationRowStepBase = std::bit_cast<float>(UINT32_C(0x45400000));
inline constexpr float kPresentationRowStepScale = std::bit_cast<float>(UINT32_C(0x40400000));
inline constexpr float kPresentationZOrigin = std::bit_cast<float>(UINT32_C(0xc7100000));

[[nodiscard]] constexpr PresentationNodePosition PresentationNodeWorldPosition(
    const std::uint8_t node
) noexcept {
    const int row = node / 9;
    const int column = node % 9;
    if (!IsPresentationNode(row, column)) return {};

    const float columnOffset = static_cast<float>(column) * kPresentationColumnStep;
    const float baseX = kPresentationXOrigin + columnOffset;
    const float parityOffset = static_cast<float>(row & 1) * kPresentationOddRowOffset;
    const float rowOffset = static_cast<float>(row) * kPresentationRowStepBase;
    const float scaledRowOffset = rowOffset * kPresentationRowStepScale;
    return {
        static_cast<std::int32_t>(baseX + parityOffset),
        static_cast<std::int32_t>(kPresentationZOrigin + scaledRowOffset),
        true,
    };
}

[[nodiscard]] constexpr std::uint64_t IntegerSquareRoot(std::uint64_t value) noexcept {
    std::uint64_t result = 0;
    std::uint64_t bit = UINT64_C(1) << 62;
    while (bit > value) bit >>= 2;
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

// GetEuclideanDistanceFromVec3Pointers writes squaredDistance*4 to the NDS
// square-root unit, then returns (floor(sqrt(value)) + 1) >> 1.
[[nodiscard]] constexpr std::uint32_t RoundedPresentationDistance(
    const std::int32_t fromX,
    const std::int32_t fromZ,
    const std::int32_t toX,
    const std::int32_t toZ
) noexcept {
    const std::int64_t dx = static_cast<std::int64_t>(fromX) - toX;
    const std::int64_t dz = static_cast<std::int64_t>(fromZ) - toZ;
    const std::uint64_t squared = static_cast<std::uint64_t>(dx * dx + dz * dz);
    const std::uint64_t doubledRoot = IntegerSquareRoot(squared * UINT64_C(4));
    return static_cast<std::uint32_t>((doubledRoot + 1) >> 1);
}

[[nodiscard]] constexpr std::uint64_t PresentationSquaredDistance(
    const std::int32_t fromX,
    const std::int32_t fromZ,
    const std::int32_t toX,
    const std::int32_t toZ
) noexcept {
    const std::int64_t dx = static_cast<std::int64_t>(fromX) - toX;
    const std::int64_t dz = static_cast<std::int64_t>(fromZ) - toZ;
    return static_cast<std::uint64_t>(dx * dx + dz * dz);
}

[[nodiscard]] constexpr std::array<PresentationNodePosition, 81> BuildPresentationNodePositions() noexcept {
    std::array<PresentationNodePosition, 81> result{};
    for (std::uint8_t node = 0; node < result.size(); ++node) {
        result[node] = PresentationNodeWorldPosition(node);
    }
    return result;
}

inline constexpr auto kPresentationNodePositions = BuildPresentationNodePositions();

// Exact scan/tie policy from overlay_d_25:021E1E50. Equal rounded distances
// keep the lower node because only a strict improvement replaces the result.
[[nodiscard]] constexpr std::uint8_t NearestPresentationNodeSimple(
    const std::int32_t worldX,
    const std::int32_t worldZ
) noexcept {
    std::uint32_t bestDistance = UINT32_C(0x00400000);
    std::uint8_t bestNode = 0;
    for (std::uint8_t node = 0; node < 81; ++node) {
        const PresentationNodePosition candidate = kPresentationNodePositions[node];
        if (!candidate.valid) continue;
        const std::uint32_t distance = RoundedPresentationDistance(
            worldX,
            worldZ,
            candidate.x,
            candidate.z
        );
        if (distance < bestDistance) {
            bestDistance = distance;
            bestNode = node;
        }
    }
    return bestNode;
}

// Exact optimized version. It computes the true squared-distance minimum,
// evaluates the game's rounded distance once, then returns the first node in
// the same rounding bucket. This preserves 021E1E50's strict-< tie policy while
// reducing integer square roots from one per valid node to one total.
[[nodiscard]] constexpr std::uint8_t NearestPresentationNodeFast(
    const std::int32_t worldX,
    const std::int32_t worldZ
) noexcept {
    std::uint64_t bestSquared = UINT64_MAX;
    std::uint8_t bestNode = 0;
    for (std::uint8_t node = 0; node < 81; ++node) {
        const PresentationNodePosition candidate = kPresentationNodePositions[node];
        if (!candidate.valid) continue;
        const std::uint64_t squared = PresentationSquaredDistance(
            worldX,
            worldZ,
            candidate.x,
            candidate.z
        );
        if (squared < bestSquared) {
            bestSquared = squared;
            bestNode = node;
        }
    }
    const PresentationNodePosition best = kPresentationNodePositions[bestNode];
    const std::uint32_t rounded = RoundedPresentationDistance(worldX, worldZ, best.x, best.z);
    const std::uint64_t doubledLower = rounded == 0 ? 0 : static_cast<std::uint64_t>(rounded) * 2 - 1;
    const std::uint64_t doubledUpper = static_cast<std::uint64_t>(rounded) * 2 + 1;
    const std::uint64_t lowerInclusive = doubledLower * doubledLower;
    const std::uint64_t upperExclusive = doubledUpper * doubledUpper;
    for (std::uint8_t node = 0; node < 81; ++node) {
        const PresentationNodePosition candidate = kPresentationNodePositions[node];
        if (!candidate.valid) continue;
        const std::uint64_t fourSquared = PresentationSquaredDistance(
            worldX,
            worldZ,
            candidate.x,
            candidate.z
        ) * UINT64_C(4);
        if (fourSquared >= lowerInclusive && fourSquared < upperExclusive) return node;
    }
    return bestNode;
}

[[nodiscard]] constexpr std::uint8_t NearestPresentationNode(
    const std::int32_t worldX,
    const std::int32_t worldZ
) noexcept {
    return NearestPresentationNodeFast(worldX, worldZ);
}

struct PresentationActorState {
    std::uint16_t actorId{kInvalidPresentationActor};
    std::uint8_t startNode{kInvalidPresentationNode};
    std::uint8_t goalNode{kInvalidPresentationNode};
    std::uint8_t auxiliaryNode{kInvalidPresentationNode};
    std::uint8_t targetNode{kInvalidPresentationNode};
    std::uint8_t cachedTargetId{kInvalidPresentationTarget};
    std::uint8_t auxiliaryTargetId{kInvalidPresentationTarget};
    bool movementEnabled{};
    bool partyStateBlocked{};
    bool conflictInvalidated{};
    std::uint8_t occupancyExpansionDepth{};
    std::uint32_t presentationFlags{};
    std::int32_t worldX{};
    std::int32_t worldY{};
    std::int32_t worldZ{};
};

[[nodiscard]] constexpr PresentationActorInput PresentationRouteInput(
    const PresentationActorState& actor
) noexcept {
    return {actor.actorId, actor.startNode, actor.goalNode};
}

// Exact priority from overlay_d_25:021E2818:
// presentation+0x4C, presentation+0x4D, action primary target, invalid.
[[nodiscard]] constexpr std::uint16_t ResolvePresentationTarget(
    const PresentationActorState& actor,
    const std::uint16_t primaryTargetId
) noexcept {
    if (actor.cachedTargetId != kInvalidPresentationTarget) return actor.cachedTargetId;
    if (actor.auxiliaryTargetId != kInvalidPresentationTarget) return actor.auxiliaryTargetId;
    return primaryTargetId;
}

// Exact boolean from overlay_d_25:021E2850. All fields are deterministic actor
// state owned by the subsystem; no caller-supplied force/suppress boolean exists.
[[nodiscard]] constexpr bool IsPresentationMovementEligible(
    const PresentationActorState& actor,
    const std::uint16_t targetActorId
) noexcept {
    if (!actor.movementEnabled) return false;                                  // 0204AD08 / +0x56
    if (targetActorId == actor.actorId) return false;                          // self target
    if ((actor.presentationFlags & kPresentationFlag80) != 0) return false;    // 020499F0
    if (!IsPresentationNode(actor.startNode / 9, actor.startNode % 9)) return false;
    if ((actor.presentationFlags & kPresentationFlag100) != 0) return false;   // 02049A88
    if (actor.actorId < 4 && actor.partyStateBlocked) return false;            // actor state +0x180 & 1
    return true;
}

using PresentationOccupancyMap = std::array<std::uint8_t, 81>;

[[nodiscard]] constexpr std::uint8_t PresentationClassForActor(
    const std::uint16_t actorId
) noexcept {
    return actorId < 4
        ? static_cast<std::uint8_t>(actorId + 0xf2)
        : static_cast<std::uint8_t>(actorId + 0x36);
}

constexpr void ClearPresentationClass(
    PresentationOccupancyMap& occupancy,
    const std::uint8_t presentationClass
) noexcept {
    for (std::uint8_t& value : occupancy) {
        if (value == presentationClass) value = 0;
    }
}

constexpr void PaintPresentationClass(
    PresentationOccupancyMap& occupancy,
    const std::uint8_t origin,
    const std::uint8_t presentationClass,
    const std::uint8_t maximumDepth,
    const std::uint8_t holdDepth = 0,
    const std::uint8_t currentDepth = 0
) noexcept {
    if (maximumDepth == currentDepth) return;
    const std::uint8_t remainingHold = holdDepth == 0
        ? 0
        : static_cast<std::uint8_t>(holdDepth - 1);
    const std::uint8_t nextDepth = remainingHold == 0
        ? static_cast<std::uint8_t>(currentDepth + 1)
        : currentDepth;
    for (const std::uint8_t node : PresentationNeighbors(origin)) {
        if (node == kInvalidPresentationNode) continue;
        if (remainingHold == 0 && occupancy[node] >= 0xf2) continue;
        if (nextDepth < maximumDepth) {
            PaintPresentationClass(
                occupancy,
                node,
                presentationClass,
                maximumDepth,
                remainingHold,
                nextDepth
            );
        }
        if (remainingHold == 0) occupancy[node] = presentationClass;
    }
}

// overlay_d_25:021E1CF0 restores an actor after its presentation class was
// removed from the occupancy map. Unlike the turn-wide 021E1A1C rebuild, this
// path restores start, auxiliary, and the auxiliary-centered footprint before
// the caller writes a replacement goal.
constexpr void RestorePresentationActorFootprint(
    PresentationOccupancyMap& occupancy,
    const PresentationActorState& actor
) noexcept {
    const std::uint8_t presentationClass = PresentationClassForActor(actor.actorId);
    if (actor.startNode < occupancy.size()) occupancy[actor.startNode] = presentationClass;
    if (actor.auxiliaryNode < occupancy.size()) {
        occupancy[actor.auxiliaryNode] = presentationClass;
        if (actor.occupancyExpansionDepth != 0) {
            PaintPresentationClass(
                occupancy,
                actor.auxiliaryNode,
                presentationClass,
                actor.occupancyExpansionDepth
            );
        }
    }
}

constexpr void PaintPresentationDistance(
    PresentationOccupancyMap& occupancy,
    const std::uint8_t origin,
    const std::uint8_t maximumDistance,
    const std::uint8_t holdDepth,
    const std::uint8_t currentDistance = 0
) noexcept {
    const std::uint8_t remainingHold = holdDepth == 0
        ? 0
        : static_cast<std::uint8_t>(holdDepth - 1);
    const std::uint8_t nextDistance = remainingHold == 0
        ? static_cast<std::uint8_t>(currentDistance + 1)
        : currentDistance;
    for (const std::uint8_t node : PresentationNeighbors(origin)) {
        if (node == kInvalidPresentationNode) continue;
        if (remainingHold == 0 && occupancy[node] >= 0xf2) continue;
        if (nextDistance < maximumDistance) {
            PaintPresentationDistance(
                occupancy,
                node,
                maximumDistance,
                remainingHold,
                nextDistance
            );
        }
        if (remainingHold == 0
            && (occupancy[node] == 0 || nextDistance < occupancy[node])) {
            occupancy[node] = nextDistance;
        }
    }
}

[[nodiscard]] constexpr PresentationOccupancyMap BuildPresentationOccupancy(
    const std::span<const PresentationActorState> actors,
    PresentationOccupancyMap occupancy = {}
) noexcept {
    for (std::uint8_t& value : occupancy) {
        if (value != 0xff) value = 0;
    }
    for (const PresentationActorState& actor : actors) {
        std::uint8_t node = actor.goalNode;
        if (node == kInvalidPresentationNode) node = actor.startNode;
        const std::uint8_t presentationClass = PresentationClassForActor(actor.actorId);
        if (node < occupancy.size()) occupancy[node] = presentationClass;
        if (actor.occupancyExpansionDepth != 0 && node < occupancy.size()) {
            PaintPresentationClass(
                occupancy,
                node,
                presentationClass,
                actor.occupancyExpansionDepth
            );
        }
        if (actor.auxiliaryNode < occupancy.size()) {
            occupancy[actor.auxiliaryNode] = presentationClass;
        }
    }
    return occupancy;
}

[[nodiscard]] constexpr std::size_t FindPresentationActorState(
    const std::span<const PresentationActorState> actors,
    const std::uint16_t actorId
) noexcept {
    for (std::size_t index = 0; index < actors.size(); ++index) {
        if (actors[index].actorId == actorId) return index;
    }
    return actors.size();
}

constexpr void InvalidatePresentationConflicts(
    const std::span<const std::uint8_t> nodes,
    PresentationOccupancyMap& occupancy,
    const std::uint16_t excludedActorId,
    const std::uint16_t excludedTargetId,
    const std::span<PresentationActorState> actors
) noexcept {
    for (const std::uint8_t node : nodes) {
        if (node >= occupancy.size()) continue;
        occupancy[node] = 0xff;
        for (PresentationActorState& actor : actors) {
            if (actor.actorId == excludedActorId || actor.actorId == excludedTargetId) continue;
            const std::uint8_t occupied = actor.auxiliaryNode != kInvalidPresentationNode
                ? actor.auxiliaryNode
                : actor.startNode;
            if (occupied == node) {
                actor.conflictInvalidated = true;
                actor.auxiliaryNode = kInvalidPresentationNode;
            }
        }
    }
}

struct PresentationGoalDecision {
    std::uint8_t goalNode{kInvalidPresentationNode};
    std::uint8_t auxiliaryNode{kInvalidPresentationNode};
    bool goalChanged{};
    bool actorAtInnerLayer{};
    bool valid{};
};

[[nodiscard]] constexpr PresentationGoalDecision ChooseFallbackPresentationGoal(
    const PresentationActorState& actor,
    PresentationOccupancyMap& occupancy
) noexcept {
    PresentationGoalDecision result{
        actor.goalNode,
        actor.auxiliaryNode,
        false,
        false,
        true,
    };
    const auto neighbors = PresentationNeighbors(actor.startNode);
    std::uint8_t freeNeighborCount = 0;
    for (const std::uint8_t node : neighbors) {
        if (node != kInvalidPresentationNode && occupancy[node] < 0xf2) ++freeNeighborCount;
    }
    if (!actor.conflictInvalidated && freeNeighborCount >= 4) return result;

    std::uint8_t bestSecondRingCount = 0;
    std::uint8_t bestNode = kInvalidPresentationNode;
    for (const std::uint8_t neighbor : neighbors) {
        if (neighbor == kInvalidPresentationNode) continue;
        std::uint8_t count = 0;
        for (const std::uint8_t node : PresentationNeighbors(neighbor)) {
            if (node == kInvalidPresentationNode || occupancy[node] >= 0xf2) continue;
            ++count;
            if (count > bestSecondRingCount) {
                bestSecondRingCount = count;
                bestNode = node;
            }
        }
    }
    if (bestNode < occupancy.size()
        && (freeNeighborCount < bestSecondRingCount || actor.conflictInvalidated)) {
        ClearPresentationClass(occupancy, PresentationClassForActor(actor.actorId));
        RestorePresentationActorFootprint(occupancy, actor);
        result.goalNode = bestNode;
        result.goalChanged = bestNode != actor.goalNode;
    }
    return result;
}

[[nodiscard]] constexpr std::uint8_t ChooseNearestPresentationCandidate(
    const std::uint8_t origin,
    const std::span<const std::uint8_t> candidates
) noexcept {
    const PresentationNodePosition from = origin < kPresentationNodePositions.size()
        ? kPresentationNodePositions[origin]
        : PresentationNodePosition{};
    std::uint64_t bestScore = UINT64_C(0x00400000);
    std::uint8_t bestNode = 0;
    for (const std::uint8_t node : candidates) {
        if (node >= kPresentationNodePositions.size()) continue;
        const PresentationNodePosition to = kPresentationNodePositions[node];
        if (!from.valid || !to.valid) continue;
        const std::int64_t dx = static_cast<std::int64_t>(from.x) - to.x;
        const std::int64_t dz = static_cast<std::int64_t>(from.z) - to.z;
        const std::uint64_t score = ((static_cast<std::uint64_t>(dx * dx) + 0x800) >> 12)
            + ((static_cast<std::uint64_t>(dz * dz) + 0x800) >> 12);
        if (score < bestScore) {
            bestScore = score;
            bestNode = node;
        }
    }
    return bestNode;
}

[[nodiscard]] constexpr std::uint8_t FindNeighborAtPresentationLevel(
    const std::uint8_t origin,
    const PresentationOccupancyMap& levels,
    const std::uint8_t level
) noexcept {
    for (const std::uint8_t node : PresentationNeighbors(origin)) {
        if (node != kInvalidPresentationNode && levels[node] == level) return node;
    }
    return kInvalidPresentationNode;
}

constexpr void InvalidateGoalNeighborConflicts(
    const std::uint8_t goal,
    PresentationOccupancyMap& occupancy,
    const PresentationOccupancyMap& targetArea,
    const std::uint8_t maximumLayer,
    const std::size_t currentActorIndex,
    const std::span<PresentationActorState> actors
) noexcept {
    std::array<std::uint8_t, 6> conflictNodes{};
    std::size_t count = 0;
    for (const std::uint8_t node : PresentationNeighbors(goal)) {
        if (node != kInvalidPresentationNode
            && maximumLayer > 0
            && targetArea[node] == maximumLayer - 1) {
            conflictNodes[count++] = node;
        }
    }
    const std::uint16_t currentActorId = currentActorIndex < actors.size()
        ? actors[currentActorIndex].actorId
        : kInvalidPresentationActor;
    InvalidatePresentationConflicts(
        std::span<const std::uint8_t>(conflictNodes.data(), count),
        occupancy,
        currentActorId,
        kInvalidPresentationActor,
        actors
    );
}

// Allocation-free translation of overlay_d_25:021E1FD8. actionActorIds is the
// ordinary actor list from the current action record; attackFormationMode is
// fixed action metadata ((attackStructure+0x18 >> 12) & 0xF), not a runtime flag.
[[nodiscard]] constexpr PresentationGoalDecision AssignPresentationGoal(
    const std::span<PresentationActorState> actors,
    const std::size_t actorIndex,
    const std::size_t targetIndex,
    PresentationOccupancyMap& occupancy,
    const std::span<const std::uint16_t> actionActorIds,
    const std::uint8_t attackFormationMode,
    const PresentationNodeSearchMode searchMode = PresentationNodeSearchMode::optimized
) noexcept {
    if (actorIndex >= actors.size() || targetIndex >= actors.size()) return {};
    PresentationActorState& actor = actors[actorIndex];
    PresentationActorState& target = actors[targetIndex];
    const std::uint8_t oldGoal = actor.goalNode;
    const std::uint8_t actorClass = PresentationClassForActor(actor.actorId);
    ClearPresentationClass(occupancy, actorClass);
    // 021E1FD8 snapshots the cleared map before 021E2904 writes durable
    // conflict markers to the shared map. Distance layers use this snapshot.
    PresentationOccupancyMap levels = occupancy;

    bool actorIsInAction = false;
    for (const std::uint16_t actorId : actionActorIds) {
        if (actorId == actor.actorId) {
            actorIsInAction = true;
            break;
        }
    }
    std::uint8_t pivot = target.auxiliaryNode;
    if (actorIsInAction) {
        pivot = searchMode == PresentationNodeSearchMode::optimized
            ? NearestPresentationNodeFast(target.worldX, target.worldZ)
            : NearestPresentationNodeSimple(target.worldX, target.worldZ);
    } else {
        if (pivot == kInvalidPresentationNode) pivot = target.goalNode;
        if (pivot == kInvalidPresentationNode) pivot = target.startNode;
    }
    if (actorIsInAction) {
        const auto conflictNodes = PresentationNeighbors(pivot);
        InvalidatePresentationConflicts(
            std::span<const std::uint8_t>(conflictNodes.data(), conflictNodes.size()),
            occupancy,
            actor.actorId,
            target.actorId,
            actors
        );
    }

    // 021E1FD8 reads the +0xA4 footprint descriptor through param_2, the
    // target presentation object. Using the acting actor changes layer counts.
    const std::uint8_t expansion = target.occupancyExpansionDepth;
    const std::uint8_t maximumLayer = expansion == 0
        ? 2
        : static_cast<std::uint8_t>(expansion + 2);
    const std::uint8_t holdDepth = expansion == 0
        ? 1
        : static_cast<std::uint8_t>(expansion + 1);
    PaintPresentationDistance(levels, pivot, maximumLayer, holdDepth);

    PresentationOccupancyMap targetArea{};
    if (actorIsInAction) {
        PaintPresentationDistance(targetArea, pivot, maximumLayer, holdDepth);
        targetArea[pivot] = PresentationClassForActor(target.actorId);
        PaintPresentationClass(
            targetArea,
            pivot,
            PresentationClassForActor(target.actorId),
            expansion,
            holdDepth
        );
    }

    const std::uint8_t actorLevel = actor.startNode < levels.size() ? levels[actor.startNode] : 0;
    bool keepGoal = false;
    bool actorAtInnerLayer = false;
    if (actorIsInAction) {
        if (attackFormationMode == 2) {
            keepGoal = actorLevel == maximumLayer;
        } else if (actorLevel != 0 && actorLevel <= maximumLayer) {
            keepGoal = true;
            actorAtInnerLayer = actorLevel == maximumLayer - 1;
        }
    } else {
        keepGoal = actorLevel == maximumLayer;
    }

    PresentationGoalDecision result{
        actor.goalNode,
        actor.auxiliaryNode,
        false,
        actorAtInnerLayer,
        true,
    };
    if (keepGoal) {
        RestorePresentationActorFootprint(occupancy, actor);
        if (actorIsInAction) {
            const std::uint8_t auxiliary = FindNeighborAtPresentationLevel(
                actor.startNode,
                levels,
                maximumLayer
            );
            if (auxiliary != kInvalidPresentationNode) {
                actor.auxiliaryNode = auxiliary;
                occupancy[auxiliary] = actorClass;
            }
            InvalidateGoalNeighborConflicts(
                actor.startNode,
                occupancy,
                targetArea,
                maximumLayer,
                actorIndex,
                actors
            );
        }
        actor.targetNode = target.goalNode;
        result.auxiliaryNode = actor.auxiliaryNode;
        return result;
    }

    std::array<std::uint8_t, 12> outerCandidates{};
    std::array<std::uint8_t, 6> innerCandidates{};
    std::size_t outerCount = 0;
    std::size_t innerCount = 0;
    for (std::uint8_t node = 0; node < levels.size(); ++node) {
        if (levels[node] == maximumLayer - 1 && innerCount < innerCandidates.size()) {
            innerCandidates[innerCount++] = node;
        } else if (levels[node] == maximumLayer && outerCount < outerCandidates.size()) {
            outerCandidates[outerCount++] = node;
        }
    }

    std::uint8_t goal = kInvalidPresentationNode;
    if (outerCount == 1) {
        goal = outerCandidates[0];
    } else if (outerCount > 1) {
        goal = ChooseNearestPresentationCandidate(
            actor.startNode,
            std::span<const std::uint8_t>(outerCandidates.data(), outerCount)
        );
    } else if (innerCount == 1) {
        goal = innerCandidates[0];
    } else if (innerCount > 1) {
        goal = ChooseNearestPresentationCandidate(
            actor.startNode,
            std::span<const std::uint8_t>(innerCandidates.data(), innerCount)
        );
    }

    if (actorIsInAction && attackFormationMode != 2) {
        PresentationOccupancyMap adjacent{};
        PaintPresentationDistance(adjacent, pivot, 1, holdDepth);
        if ((actor.startNode < adjacent.size() && adjacent[actor.startNode] == 1)
            || actor.startNode == pivot) {
            goal = actor.startNode;
        }
    }
    if (goal >= occupancy.size()) goal = actor.startNode;
    actor.goalNode = goal;
    if (goal < occupancy.size()) occupancy[goal] = actorClass;
    result.goalNode = goal;
    result.goalChanged = goal != oldGoal;

    if (actorIsInAction) {
        const std::uint8_t auxiliary = FindNeighborAtPresentationLevel(goal, levels, maximumLayer);
        if (auxiliary != kInvalidPresentationNode) {
            actor.auxiliaryNode = auxiliary;
            occupancy[auxiliary] = actorClass;
        }
        InvalidateGoalNeighborConflicts(
            goal,
            occupancy,
            targetArea,
            maximumLayer,
            actorIndex,
            actors
        );
    }
    actor.cachedTargetId = static_cast<std::uint8_t>(target.actorId);
    actor.targetNode = target.goalNode;
    result.auxiliaryNode = actor.auxiliaryNode;
    return result;
}

static_assert(PresentationNodeWorldPosition(55).x == -31925);
static_assert(PresentationNodeWorldPosition(55).z == 18432);
static_assert(PresentationNodeWorldPosition(57).x == -10641);
static_assert(PresentationNodeWorldPosition(57).z == 18432);
static_assert(PresentationNodeWorldPosition(61).x == 31925);
static_assert(PresentationNodeWorldPosition(61).z == 18432);
static_assert(NearestPresentationNode(-31925, 18432) == 55);
static_assert(NearestPresentationNode(-10641, 18432) == 57);
static_assert(NearestPresentationNode(31925, 18432) == 61);
static_assert(NearestPresentationNodeFast(-31925, 18432) == 55);
static_assert(NearestPresentationNodeFast(-10641, 18432) == 57);
static_assert(NearestPresentationNodeFast(31925, 18432) == 61);
static_assert(NearestPresentationNodeSimple(-31925, 18432) == NearestPresentationNodeFast(-31925, 18432));
static_assert(NearestPresentationNodeSimple(-10641, 18432) == NearestPresentationNodeFast(-10641, 18432));
static_assert(NearestPresentationNodeSimple(31925, 18432) == NearestPresentationNodeFast(31925, 18432));

} // namespace dq9::freecam::detail
