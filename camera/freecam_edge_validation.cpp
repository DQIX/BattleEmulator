#include <array>
#include <cstdint>
#include <iostream>
#include <span>

#include "../BattleEmulator.h"
#include "freecam_action_mapper.hpp"

namespace {
using namespace dq9::freecam;
using namespace dq9::freecam::detail;
using namespace dq9::freecam::fast;
using namespace dq9::freecam::bindings;

using ZakiAction = FreeCamera<24, BattleEmulator::ZAKI>;
using AttackAction = FreeCamera<1, BattleEmulator::ATTACK_ALLY>;

struct SweepStats {
    std::uint64_t goalCases{};
    std::uint64_t movingCases{};
    std::uint64_t zakiSuppressed{};
    std::uint64_t zakiTriggered{};
    std::uint64_t longRoutes{};
    std::uint8_t maxRoute{};
    std::uint8_t maxRouteStart{};
    std::uint8_t maxRouteTarget{};
    std::uint8_t maxRouteMode{};
};

PresentationActorState ActorState(const std::uint16_t actorId, const std::uint8_t node) {
    PresentationActorState actor{};
    actor.actorId = actorId;
    actor.startNode = node;
    actor.goalNode = node;
    actor.movementEnabled = true;
    if (node < kPresentationNodePositions.size()) {
        actor.worldX = kPresentationNodePositions[node].x;
        actor.worldZ = kPresentationNodePositions[node].z;
    }
    return actor;
}

bool IsUsableNode(const std::uint8_t node) {
    return node < kPresentationNodePositions.size() && kPresentationNodePositions[node].valid;
}

bool ValidateActorIdGeneralization() {
    std::array<std::uint16_t, 12> ids{};
    std::size_t count = 0;
    for (std::uint8_t ally = 0; ally < 4; ++ally) ids[count++] = Dq9ActorId({BattleActorSide::ally, ally});
    for (std::uint8_t enemy = 0; enemy < 8; ++enemy) ids[count++] = Dq9ActorId({BattleActorSide::enemy, enemy});
    for (std::size_t i = 0; i < count; ++i) {
        if (ids[i] == kInvalidBattleActor) return false;
        for (std::size_t j = 0; j < i; ++j) {
            if (ids[i] == ids[j]) return false;
        }
    }
    return true;
}

bool ValidateRosterSizes() {
    constexpr std::array<std::uint8_t, 12> nodes{0, 8, 72, 80, 4, 36, 44, 76, 20, 24, 56, 60};
    for (std::uint8_t allyCount = 1; allyCount <= 4; ++allyCount) {
        for (std::uint8_t enemyCount = 1; enemyCount <= 8 && allyCount + enemyCount <= 12; ++enemyCount) {
            ResetBattle();
            std::array<BattleActorRef, 12> order{};
            std::size_t actorCount = 0;
            for (std::uint8_t ally = 0; ally < allyCount; ++ally) {
                const BattleActorRef ref{BattleActorSide::ally, ally};
                order[actorCount] = ref;
                if (!SetPresentationActor(actorCount, ActorState(Dq9ActorId(ref), nodes[actorCount]))) return false;
                ++actorCount;
            }
            for (std::uint8_t enemy = 0; enemy < enemyCount; ++enemy) {
                const BattleActorRef ref{BattleActorSide::enemy, enemy};
                order[actorCount] = ref;
                if (!SetPresentationActor(actorCount, ActorState(Dq9ActorId(ref), nodes[actorCount]))) return false;
                ++actorCount;
            }
            if (!BeginTurn(std::span<const BattleActorRef>(order.data(), actorCount))) return false;
            for (std::size_t index = 0; index < actorCount; ++index) {
                if (FindPresentationActorIndex(Dq9ActorId(order[index])) != index) return false;
            }
        }
    }
    return true;
}

SweepStats SweepTwoActorRoutes() {
    SweepStats stats{};
    constexpr std::uint16_t actorId = 0;
    constexpr std::uint16_t targetId = 0x00c0;
    constexpr std::array<std::uint16_t, 1> currentActors{actorId};
    constexpr std::array<std::uint16_t, 1> order{actorId};
    const bool zakiNeedsRoute = ComputeSelectorSuppression(ZakiAction::actionSelectorProjection);

    for (std::uint8_t start = 0; start < 81; ++start) {
        if (!IsUsableNode(start)) continue;
        for (std::uint8_t target = 0; target < 81; ++target) {
            if (start == target || !IsUsableNode(target)) continue;
            for (std::uint8_t mode = 0; mode < 4; ++mode) {
                std::array<PresentationActorState, 2> actors{
                    ActorState(actorId, start), ActorState(targetId, target)
                };
                auto occupancy = BuildPresentationOccupancy(std::span<const PresentationActorState>(actors.data(), actors.size()));
                const auto decision = AssignPresentationGoal(
                    std::span<PresentationActorState>(actors.data(), actors.size()),
                    0, 1, occupancy,
                    std::span<const std::uint16_t>(currentActors.data(), currentActors.size()),
                    mode
                );
                if (!decision.valid || decision.goalNode >= 81) continue;
                ++stats.goalCases;
                const std::array<PresentationActorInput, 2> routeActors{
                    PresentationRouteInput(actors[0]), PresentationRouteInput(actors[1])
                };
                const auto routes = PlanPresentationRoutes(
                    std::span<const PresentationActorInput>(routeActors.data(), routeActors.size()),
                    std::span<const std::uint16_t>(order.data(), order.size()), 0
                );
                if (!routes.valid) continue;
                const auto* route = FindPresentationRoute(routes, actorId);
                const std::uint8_t routeCount = route == nullptr ? 0 : route->count;
                if (routeCount != 0) ++stats.movingCases;
                if (routeCount > 4) ++stats.longRoutes;
                if (routeCount > stats.maxRoute) {
                    stats.maxRoute = routeCount;
                    stats.maxRouteStart = start;
                    stats.maxRouteTarget = target;
                    stats.maxRouteMode = mode;
                }
                if (routeCount == 0 && zakiNeedsRoute) ++stats.zakiSuppressed;
                else ++stats.zakiTriggered;
            }
        }
    }
    return stats;
}

bool ValidateDecisionEdges() {
    ResetBattle();
    constexpr BattleActorRef actorRef{BattleActorSide::ally, 0};
    constexpr BattleActorRef targetRef{BattleActorSide::enemy, 0};
    constexpr std::array order{actorRef, actorRef};
    if (!BeginTurn(order)) return false;
    if (!SetPresentationActor(0, ActorState(Dq9ActorId(actorRef), 59))) return false;
    if (!SetPresentationActor(1, ActorState(Dq9ActorId(targetRef), 33))) return false;
    auto& state = ThreadContext();
    state.currentRoutes = {};
    state.currentRoutes.valid = true;
    state.currentRoutes.actorCount = 2;
    state.currentRoutes.actors[0].actorId = Dq9ActorId(actorRef);
    state.currentRoutes.actors[1].actorId = Dq9ActorId(targetRef);

    // Zaki 0x00050001 is suppressed only when the actor has no route.
    state.currentRoutes.actors[0].count = 0;
    const auto suppressed = Decide<ZakiAction>({
        .actorId = Dq9ActorId(actorRef),
        .targetId = Dq9ActorId(targetRef),
        .turnActionIndex = 0,
        .targetAuxiliaryNode = 1,
    });
    if (suppressed.callFreeCamera) return false;

    state.currentRoutes.actors[0].count = 2;
    state.currentRoutes.actors[0].nodes[0] = 59;
    state.currentRoutes.actors[0].nodes[1] = 50;
    const auto firstAction = Decide<ZakiAction>({
        .actorId = Dq9ActorId(actorRef),
        .targetId = Dq9ActorId(targetRef),
        .turnActionIndex = 0,
        .targetAuxiliaryNode = 1,
    });
    if (!firstAction.callFreeCamera || !firstAction.param5) return false;

    const auto laterAction = Decide<ZakiAction>({
        .actorId = Dq9ActorId(actorRef),
        .targetId = Dq9ActorId(targetRef),
        .turnActionIndex = 1,
        .targetAuxiliaryNode = 1,
    });
    if (!laterAction.callFreeCamera || laterAction.param5) return false;

    // A route above four nodes forces param5 even on a later action.
    state.currentRoutes.actors[1].count = 5;
    const auto longRoute = Decide<ZakiAction>({
        .actorId = Dq9ActorId(actorRef),
        .targetId = Dq9ActorId(targetRef),
        .turnActionIndex = 1,
        .targetAuxiliaryNode = 1,
    });
    if (!longRoute.callFreeCamera || !longRoute.param5) return false;
    state.currentRoutes.actors[1].count = 0;

    // 021DC394..021DC3C0 compares the previous action's actor[0] with
    // the current target ID. It does not compare against the current actor.
    state.hasPreviousAction = true;
    state.previousAction = {1, Dq9ActorId(targetRef), Dq9ActorId(actorRef)};
    const auto previousActorTarget = Decide<ZakiAction>({
        .actorId = Dq9ActorId(actorRef),
        .targetId = Dq9ActorId(targetRef),
        .turnActionIndex = 1,
        .targetAuxiliaryNode = 1,
    });
    if (!previousActorTarget.param5) return false;

    // Geometric overlap is the remaining force-mode1 exception.
    state.previousAction.actorId = Dq9ActorId(actorRef);
    const auto overlap = Decide<ZakiAction>({
        .actorId = Dq9ActorId(actorRef),
        .targetId = Dq9ActorId(targetRef),
        .turnActionIndex = 1,
        .targetAuxiliaryNode = 1,
        .actorAndTargetHaveGeometry = true,
        .actorTargetDistance = 10,
        .actorRadius = 20,
        .targetRadius = 20,
    });
    if (!overlap.param5) return false;
    return true;
}

bool ValidateConsecutiveAttackReset() {
    std::uint16_t resetMonsterId = 0xffff;
    for (std::uint32_t monsterId = 0; monsterId <= 0xffff; ++monsterId) {
        const std::uint32_t profile = ResolveMonsterProfile(static_cast<std::uint16_t>(monsterId));
        if (profile == kInvalidMembershipProfile) continue;
        if (AttackAction::ActorMembership(profile).count > 1) {
            resetMonsterId = static_cast<std::uint16_t>(monsterId);
            break;
        }
    }
    if (resetMonsterId == 0xffff) return false;

    ResetBattle();
    constexpr BattleActorRef actorRef{BattleActorSide::enemy, 0};
    constexpr BattleActorRef targetRef{BattleActorSide::ally, 0};
    constexpr std::array order{actorRef, actorRef};
    if (!BeginTurn(order)) return false;
    auto actor = ActorState(Dq9ActorId(actorRef), 22);
    actor.presentationFlags = kPresentationFlag80;
    if (!SetPresentationActor(0, actor)) return false;
    if (!SetPresentationActor(1, ActorState(Dq9ActorId(targetRef), 40))) return false;
    if (!SetMonsterMembershipProfile(0, resetMonsterId)) return false;

    auto& state = ThreadContext();
    state.currentRoutes = {};
    state.currentRoutes.valid = true;
    state.currentRoutes.actorCount = 2;
    state.currentRoutes.actors[0].actorId = Dq9ActorId(actorRef);
    state.currentRoutes.actors[1].actorId = Dq9ActorId(targetRef);
    state.hasPreviousAction = true;
    state.previousAction = {1, Dq9ActorId(actorRef), Dq9ActorId(targetRef)};
    state.previousActionIndex = 0;
    state.targetRecord02161720ActorId = Dq9ActorId(targetRef);

    const auto decision = Decide<AttackAction>({
        .actorId = Dq9ActorId(actorRef),
        .targetId = Dq9ActorId(targetRef),
        .turnActionIndex = 1,
        .targetAuxiliaryNode = 1,
    });
    return decision.resetOnly && !decision.callFreeCamera && decision.source == TriggerSource::reset_only;
}

bool ValidateMappedMetadata() {
    std::array<std::uint64_t, 16> modeCounts{};
    std::size_t mapped = 0;
    for (int common = 0; common <= BattleEmulator::ZARAKI; ++common) {
        const auto* binding = Find(common);
        if (binding == nullptr || !binding->mapped()) continue;
        ++mapped;
        if (binding->attackFormationMode >= modeCounts.size()) return false;
        ++modeCounts[binding->attackFormationMode];
    }
    std::cout << "MAPPED actions=" << mapped;
    for (std::size_t mode = 0; mode < modeCounts.size(); ++mode) {
        if (modeCounts[mode] != 0) std::cout << " mode" << mode << '=' << modeCounts[mode];
    }
    std::cout << '\n';
    return mapped >= 10;
}

bool ValidateRosterField4Compatibility() {
    ResetBattle();
    constexpr std::array<BattleActorRef, 12> roster{
        BattleActorRef{BattleActorSide::ally, 0},
        BattleActorRef{BattleActorSide::ally, 1},
        BattleActorRef{BattleActorSide::ally, 2},
        BattleActorRef{BattleActorSide::ally, 3},
        BattleActorRef{BattleActorSide::enemy, 0},
        BattleActorRef{BattleActorSide::enemy, 1},
        BattleActorRef{BattleActorSide::enemy, 2},
        BattleActorRef{BattleActorSide::enemy, 3},
        BattleActorRef{BattleActorSide::enemy, 4},
        BattleActorRef{BattleActorSide::enemy, 5},
        BattleActorRef{BattleActorSide::enemy, 6},
        BattleActorRef{BattleActorSide::enemy, 7},
    };
    constexpr std::array<std::uint8_t, 12> nodes{0, 8, 72, 80, 4, 36, 44, 76, 20, 24, 56, 60};
    for (std::size_t index = 0; index < roster.size(); ++index) {
        if (!SetPresentationActor(index, ActorState(Dq9ActorId(roster[index]), nodes[index]))) return false;
    }
    if (!BeginTurn(roster)) return false;

    if (!ApplyKnownRosterField4PostActionCompatibility(1)) return false;
    constexpr std::array<std::size_t, 8> type1Nonzero{0, 1, 2, 5, 6, 7, 8, 9};
    for (std::size_t index = 0; index < roster.size(); ++index) {
        bool expectedNonzero = false;
        for (const std::size_t candidate : type1Nonzero) expectedNonzero |= candidate == index;
        if (RosterField4IsZero(index) == expectedNonzero) return false;
    }

    if (!ApplyKnownRosterField4PostActionCompatibility(17)) return false;
    constexpr std::array<std::size_t, 8> type17Nonzero{0, 1, 4, 5, 6, 7, 8, 9};
    for (std::size_t index = 0; index < roster.size(); ++index) {
        bool expectedNonzero = false;
        for (const std::size_t candidate : type17Nonzero) expectedNonzero |= candidate == index;
        if (RosterField4IsZero(index) == expectedNonzero) return false;
    }

    if (!ApplyKnownRosterField4PostActionCompatibility(15)) return false;
    for (std::size_t index = 0; index < roster.size(); ++index) {
        if (index < 4) {
            if (!RosterField4IsKnown(index) || RosterField4IsZero(index)) return false;
        } else if (RosterField4IsKnown(index)) {
            return false;
        }
    }

    if (ApplyKnownRosterField4PostActionCompatibility(0) || HasRosterField4Compatibility()) return false;

    std::array<PresentationActorState, 3> actors{
        ActorState(0, 40),
        ActorState(0x00c0, 21),
        ActorState(0x00c1, 30),
    };
    actors[2].auxiliaryNode = 31;
    auto occupancy = BuildPresentationOccupancy(std::span<const PresentationActorState>(actors.data(), actors.size()));
    std::array<bool, 3> compatibility{};
    std::array<bool, 3> compatibilityKnown{};
    constexpr std::array<std::uint8_t, 1> conflictNodes{31};
    InvalidatePresentationConflicts(
        conflictNodes,
        occupancy,
        actors[0].actorId,
        actors[1].actorId,
        actors,
        compatibility,
        compatibilityKnown
    );
    return compatibility[2]
        && compatibilityKnown[2]
        && actors[2].conflictInvalidated
        && actors[2].auxiliaryNode == kInvalidPresentationNode;
}

} // namespace

int main() {
    if (!ValidateActorIdGeneralization()) {
        std::cerr << "FAIL actor-id-generalization\n";
        return 1;
    }
    if (!ValidateRosterSizes()) {
        std::cerr << "FAIL roster-size-sweep\n";
        return 2;
    }
    if (!ValidateMappedMetadata()) {
        std::cerr << "FAIL mapped-metadata\n";
        return 3;
    }
    if (!ValidateDecisionEdges()) {
        std::cerr << "FAIL decision-edges\n";
        return 4;
    }
    if (!ValidateConsecutiveAttackReset()) {
        std::cerr << "FAIL consecutive-attack-reset\n";
        return 5;
    }
    if (!ValidateRosterField4Compatibility()) {
        std::cerr << "FAIL roster-field4-compatibility\n";
        return 6;
    }

    const SweepStats stats = SweepTwoActorRoutes();
    if (stats.goalCases == 0 || stats.zakiSuppressed == 0 || stats.zakiTriggered == 0 || stats.maxRoute == 0) {
        std::cerr << "FAIL route-sweep-empty-branch\n";
        return 7;
    }
    std::cout << "ROUTE_SWEEP goalCases=" << stats.goalCases
              << " moving=" << stats.movingCases
              << " zakiSuppressed=" << stats.zakiSuppressed
              << " zakiTriggered=" << stats.zakiTriggered
              << " longRoutes=" << stats.longRoutes
              << " maxRoute=" << static_cast<unsigned>(stats.maxRoute)
              << " maxRouteCase=start:" << static_cast<unsigned>(stats.maxRouteStart)
              << ",target:" << static_cast<unsigned>(stats.maxRouteTarget)
              << ",mode:" << static_cast<unsigned>(stats.maxRouteMode) << '\n';
    std::cout << "PASS freecam-edge-validation\n";
    return 0;
}
