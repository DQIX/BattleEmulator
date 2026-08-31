#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace dq9::freecam::detail {

inline constexpr std::uint8_t kInvalidPresentationNode = 0xff;
inline constexpr std::uint16_t kInvalidPresentationActor = 0xffff;
inline constexpr std::size_t kMaxPresentationActors = 12;
inline constexpr std::size_t kMaxPresentationActions = 60;
inline constexpr std::size_t kMaxPresentationRoute = 16;

#if defined(_MSC_VER)
#define DQ9_FREECAM_ROUTE_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define DQ9_FREECAM_ROUTE_FORCE_INLINE inline __attribute__((always_inline))
#else
#define DQ9_FREECAM_ROUTE_FORCE_INLINE inline
#endif

[[nodiscard]] DQ9_FREECAM_ROUTE_FORCE_INLINE constexpr bool IsPresentationNode(
    const int row,
    const int column
) noexcept {
    return row >= 0 && row < 9 && column >= 0 && column < 9 && !(row % 2 != 0 && column == 8);
}

[[nodiscard]] DQ9_FREECAM_ROUTE_FORCE_INLINE constexpr std::uint8_t PresentationNode(
    const int row,
    const int column
) noexcept {
    return IsPresentationNode(row, column)
        ? static_cast<std::uint8_t>(row * 9 + column)
        : kInvalidPresentationNode;
}

// Exact neighbor order from overlay_d_00:02170F58.
[[nodiscard]] DQ9_FREECAM_ROUTE_FORCE_INLINE constexpr std::array<std::uint8_t, 6> PresentationNeighbors(
    const std::uint8_t node
) noexcept {
    if (node >= 81) {
        return {kInvalidPresentationNode, kInvalidPresentationNode, kInvalidPresentationNode,
                kInvalidPresentationNode, kInvalidPresentationNode, kInvalidPresentationNode};
    }
    const int row = node / 9;
    const int column = node % 9;
    if (!IsPresentationNode(row, column)) {
        return {kInvalidPresentationNode, kInvalidPresentationNode, kInvalidPresentationNode,
                kInvalidPresentationNode, kInvalidPresentationNode, kInvalidPresentationNode};
    }
    if ((row & 1) == 0) {
        return {
            PresentationNode(row + 1, column - 1),
            PresentationNode(row + 1, column),
            PresentationNode(row, column - 1),
            PresentationNode(row, column + 1),
            PresentationNode(row - 1, column - 1),
            PresentationNode(row - 1, column),
        };
    }
    return {
        PresentationNode(row + 1, column),
        PresentationNode(row + 1, column + 1),
        PresentationNode(row, column - 1),
        PresentationNode(row, column + 1),
        PresentationNode(row - 1, column),
        PresentationNode(row - 1, column + 1),
    };
}

#undef DQ9_FREECAM_ROUTE_FORCE_INLINE

struct PresentationPath {
    // 02171498 writes goal -> ... -> start. The caller consumes nodes[count - 2]
    // as the next step from the start side.
    std::array<std::uint8_t, kMaxPresentationRoute> nodes{};
    std::uint8_t count{};
};

// Exact bounded shortest-path policy from overlay_d_00:02171498. Open nodes
// with equal distance are selected newest-first because 02171720 pushes at the
// list head and 02171738 keeps the first minimum it encounters.
[[nodiscard]] constexpr PresentationPath PresentationShortestPath(
    const std::uint8_t start,
    const std::uint8_t goal,
    const std::span<const std::uint8_t> blocked = {},
    const std::uint8_t maxRoute = 16
) noexcept {
    PresentationPath result{};
    if (!IsPresentationNode(start / 9, start % 9)
        || !IsPresentationNode(goal / 9, goal % 9)
        || maxRoute == 0) {
        return result;
    }

    std::array<bool, 81> unavailable{};
    for (const std::uint8_t node : blocked) {
        if (node < unavailable.size()) unavailable[node] = true;
    }
    unavailable[start] = false;
    std::array<std::uint8_t, 81> distance{};
    std::array<std::uint8_t, 81> parent{};
    // Exact representation of the ROM open-list policy without rescanning all
    // 81 nodes for every pop. All edges have cost 1, so open nodes can be
    // partitioned by distance. Within one distance the ROM is newest-first;
    // a singly-linked LIFO bucket is exactly the same order as list-head push.
    std::array<std::uint8_t, 81> bucketHead{};
    std::array<std::uint8_t, 81> nextInBucket{};
    distance.fill(0xff);
    parent.fill(kInvalidPresentationNode);
    bucketHead.fill(kInvalidPresentationNode);
    nextInBucket.fill(kInvalidPresentationNode);
    distance[start] = 0;
    bucketHead[0] = start;

    for (std::uint8_t currentDistance = 0; currentDistance < 81; ++currentDistance) {
        while (bucketHead[currentDistance] != kInvalidPresentationNode) {
            const std::uint8_t current = bucketHead[currentDistance];
            bucketHead[currentDistance] = nextInBucket[current];

            if (current == goal) {
                std::uint8_t node = goal;
                const std::uint8_t limit = maxRoute < kMaxPresentationRoute
                    ? maxRoute
                    : static_cast<std::uint8_t>(kMaxPresentationRoute);
                while (result.count < limit && node != kInvalidPresentationNode) {
                    result.nodes[result.count++] = node;
                    node = parent[node];
                }
                return result;
            }

            const std::uint8_t nextDistance = static_cast<std::uint8_t>(currentDistance + 1);
            if (nextDistance >= bucketHead.size()) continue;
            for (const std::uint8_t next : PresentationNeighbors(current)) {
                if (next == kInvalidPresentationNode || unavailable[next]) continue;
                // With unit edge costs and nondecreasing distance buckets, first
                // discovery is the shortest distance. The old implementation
                // likewise kept the first equal-distance parent.
                if (distance[next] != 0xff) continue;
                distance[next] = nextDistance;
                parent[next] = current;
                nextInBucket[next] = bucketHead[nextDistance];
                bucketHead[nextDistance] = next;
            }
        }
    }
    return result;
}

// Returns the goal-to-start route length used by 02171498, capped at maxRoute.
// Zero means no route. The start node is allowed even when listed as blocked.
[[nodiscard]] constexpr std::uint8_t PresentationRouteLength(
    const std::uint8_t start,
    const std::uint8_t goal,
    const std::span<const std::uint8_t> blocked = {},
    const std::uint8_t maxRoute = 16
) noexcept {
    return PresentationShortestPath(start, goal, blocked, maxRoute).count;
}

struct PresentationActorInput {
    std::uint16_t actorId{kInvalidPresentationActor};
    std::uint8_t startNode{kInvalidPresentationNode};
    std::uint8_t goalNode{kInvalidPresentationNode};
};

struct PresentationActorRoute {
    std::uint16_t actorId{kInvalidPresentationActor};
    std::array<std::uint8_t, kMaxPresentationRoute> nodes{};
    std::uint8_t count{};

    [[nodiscard]] constexpr bool operator==(const PresentationActorRoute&) const = default;
};

struct PresentationTurnRoutes {
    std::array<PresentationActorRoute, kMaxPresentationActors> actors{};
    std::uint8_t actorCount{};
    bool valid{};

    [[nodiscard]] constexpr bool operator==(const PresentationTurnRoutes&) const = default;
};

[[nodiscard]] constexpr const PresentationActorRoute* FindPresentationRoute(
    const PresentationTurnRoutes& routes,
    const std::uint16_t actorId
) noexcept {
    for (std::size_t index = 0; index < routes.actorCount; ++index) {
        if (routes.actors[index].actorId == actorId) return &routes.actors[index];
    }
    return nullptr;
}

// External form of overlay_d_25:021E0F48 through its 0204A230 writes.
// actionActorIds is the whole turn in execution order. The game visits it from
// currentActionIndex to the end and then wraps to the beginning.
[[nodiscard]] constexpr PresentationTurnRoutes PlanPresentationRoutes(
    const std::span<const PresentationActorInput> actors,
    const std::span<const std::uint16_t> actionActorIds,
    const std::size_t currentActionIndex
) noexcept {
    PresentationTurnRoutes result{};
    if (actors.size() > kMaxPresentationActors
        || actionActorIds.size() > kMaxPresentationActions
        || currentActionIndex > actionActorIds.size()) {
        return result;
    }

    std::array<std::uint8_t, kMaxPresentationActors> positions{};
    std::array<std::array<std::uint8_t, kMaxPresentationRoute>, kMaxPresentationActors> rawRoutes{};
    std::array<std::array<bool, kMaxPresentationRoute>, kMaxPresentationActors> skip{};
    std::array<std::uint8_t, kMaxPresentationActors> rawCounts{};
    std::array<std::uint16_t, kMaxPresentationActions> orderedActions{};
    std::size_t orderedCount = 0;

    for (std::size_t index = 0; index < actors.size(); ++index) {
        if (!IsPresentationNode(actors[index].startNode / 9, actors[index].startNode % 9)
            || !IsPresentationNode(actors[index].goalNode / 9, actors[index].goalNode % 9)) {
            return result;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (actors[prior].actorId == actors[index].actorId) return result;
        }
        result.actors[index].actorId = actors[index].actorId;
        positions[index] = actors[index].startNode;
    }
    result.actorCount = static_cast<std::uint8_t>(actors.size());

    for (std::size_t index = currentActionIndex; index < actionActorIds.size(); ++index) {
        if (actionActorIds[index] != kInvalidPresentationActor) orderedActions[orderedCount++] = actionActorIds[index];
    }
    for (std::size_t index = 0; index < currentActionIndex; ++index) {
        if (actionActorIds[index] != kInvalidPresentationActor) orderedActions[orderedCount++] = actionActorIds[index];
    }

    std::size_t pass = 0;
    bool changed = true;
    while (changed && pass <= kMaxPresentationRoute) {
        // 021E1458..021E14A8 copies the previous pass endpoint into a new
        // mutable list node. Skip checks keep reading the node that represents
        // this pass's start, even after the active node advances actors.
        const auto passStartPositions = positions;
        changed = false;
        for (std::size_t actionSequenceIndex = 0; actionSequenceIndex < orderedCount; ++actionSequenceIndex) {
            std::size_t actorIndex = actors.size();
            for (std::size_t candidate = 0; candidate < actors.size(); ++candidate) {
                if (actors[candidate].actorId == orderedActions[actionSequenceIndex]) {
                    actorIndex = candidate;
                    break;
                }
            }
            if (actorIndex == actors.size() || rawCounts[actorIndex] >= kMaxPresentationRoute) continue;

            const std::uint8_t current = rawCounts[actorIndex] == 0
                ? actors[actorIndex].startNode
                : rawRoutes[actorIndex][rawCounts[actorIndex] - 1];
            const std::uint8_t goal = actors[actorIndex].goalNode;
            if (current == goal) continue;

            std::array<std::uint8_t, kMaxPresentationActors> blocked{};
            std::size_t blockedCount = 0;
            for (std::size_t index = 0; index < actors.size(); ++index) {
                if (positions[index] != goal) blocked[blockedCount++] = positions[index];
            }
            const PresentationPath path = PresentationShortestPath(
                current,
                goal,
                std::span<const std::uint8_t>(blocked.data(), blockedCount),
                static_cast<std::uint8_t>(kMaxPresentationRoute)
            );
            if (path.count < 2) continue;

            if (rawCounts[actorIndex] == 0) {
                rawRoutes[actorIndex][0] = positions[actorIndex];
                rawCounts[actorIndex] = 1;
            }
            if (rawCounts[actorIndex] >= kMaxPresentationRoute) continue;
            const std::uint8_t next = path.nodes[path.count - 2];
            rawRoutes[actorIndex][rawCounts[actorIndex]++] = next;
            positions[actorIndex] = next;

            if (rawCounts[actorIndex] > 2 && pass > 0) {
                const std::uint8_t before = rawRoutes[actorIndex][rawCounts[actorIndex] - 3];
                const std::uint8_t after = rawRoutes[actorIndex][rawCounts[actorIndex] - 1];
                const auto beforeNeighbors = PresentationNeighbors(before);
                const auto afterNeighbors = PresentationNeighbors(after);
                const auto& comparison = passStartPositions;
                bool foundCommon = false;
                bool allCommonUnoccupied = true;
                for (const std::uint8_t left : beforeNeighbors) {
                    if (left == kInvalidPresentationNode) continue;
                    for (const std::uint8_t right : afterNeighbors) {
                        if (right == kInvalidPresentationNode || left != right) continue;
                        foundCommon = true;
                        for (std::size_t index = 0; index < actors.size(); ++index) {
                            // This is intentionally the action-sequence index, matching 021E13C8.
                            if (index != actionSequenceIndex && comparison[index] == left) {
                                allCommonUnoccupied = false;
                                break;
                            }
                        }
                        if (!allCommonUnoccupied) break;
                    }
                    if (!allCommonUnoccupied) break;
                }
                if (foundCommon && allCommonUnoccupied) {
                    skip[actorIndex][rawCounts[actorIndex] - 2] = true;
                }
            }
            changed = true;
        }
        ++pass;
    }

    for (std::size_t actorIndex = 0; actorIndex < actors.size(); ++actorIndex) {
        for (std::size_t routeIndex = 0; routeIndex < rawCounts[actorIndex]; ++routeIndex) {
            if (skip[actorIndex][routeIndex]) continue;
            auto& output = result.actors[actorIndex];
            output.nodes[output.count++] = rawRoutes[actorIndex][routeIndex];
        }
    }
    result.valid = true;
    return result;
}

static_assert(PresentationNeighbors(0)[1] == 9);
static_assert(PresentationNeighbors(0)[3] == 1);
static_assert(PresentationNeighbors(8)[0] == 16);
static_assert(PresentationNeighbors(8)[1] == kInvalidPresentationNode);
static_assert(PresentationRouteLength(0, 0) == 1);
static_assert(PresentationRouteLength(0, 1) == 2);
static_assert(PresentationShortestPath(0, 1).nodes[0] == 1);
static_assert(PresentationShortestPath(0, 1).nodes[1] == 0);

} // namespace dq9::freecam::detail
