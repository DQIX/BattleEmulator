//
// Created by Owner on 2024/02/06.
//

#ifndef NEWDIRECTORY_CAMERA_H
#define NEWDIRECTORY_CAMERA_H


#include <cstdint>
#include <cstddef>
#include <array>
#include "camera/freecam_actor.hpp"
#include "camera/freecam_fast_runtime.hpp"

enum class CameraMembershipKind : std::uint8_t {
    none,
    player,
    monster,
    special,
};

struct CameraPresentationActor {
    dq9::freecam::fast::BattleActorRef actor{};
    std::int32_t worldX{};
    std::int32_t worldY{};
    std::int32_t worldZ{};
    std::uint32_t presentationFlags{};
    std::uint8_t occupancyExpansionDepth{};
    bool movementEnabled{true};
    CameraMembershipKind membershipKind{CameraMembershipKind::none};
    std::uint16_t membershipKeyA{};
    std::uint16_t membershipKeyB{};
    std::uint16_t battleMonsterId{0xffff};
};

#if defined(gerunikku)
struct CameraDebugEvent {
    static constexpr std::size_t kMaxPresentationActors = 12;
    int turnSerial{};
    int actionIndex{};
    int commonActionId{};
    std::uint16_t actorId{0xffff};
    std::uint16_t targetId{0xffff};
    std::uint8_t actorRouteCount{};
    std::uint8_t maxRouteCount{};
    std::uint8_t triggerSource{};
    bool mapped{};
    bool runtimeDecisionAvailable{};
    bool runtimeCallFreeCamera{};
    bool runtimeParam5{};
    bool runtimeResetOnly{};
    bool manualRuleWouldCall{};
    bool productionCalledFreeCamera{};
    std::uint8_t presentationActorCount{};
    std::array<std::uint8_t, kMaxPresentationActors> startNodesBefore{};
    std::array<std::uint8_t, kMaxPresentationActors> startNodesAfter{};
};
#endif

class camera {
public:
    using RuntimeSnapshot = dq9::freecam::fast::RuntimeState;

    static bool ResetBattle(const CameraPresentationActor *actors, std::size_t actorCount);
    static RuntimeSnapshot CaptureRuntimeState() noexcept;
    static void RestoreRuntimeState(const RuntimeSnapshot& state) noexcept;
    static void BindRuntimeState(RuntimeSnapshot* state) noexcept;
    static void UnbindRuntimeState() noexcept;
    static void Main(int *position, const int32_t *actions,
                     const dq9::freecam::fast::BattleActorRef *actors,
                     const dq9::freecam::fast::BattleActorRef *targets,
                     int actionCount, uint64_t *NowState, bool preemptive, bool bakuti,
                     bool traceBoundaries = false);

#if defined(gerunikku)
    static void SetDebugCapture(bool enabled) noexcept;
    static void ClearDebugEvents() noexcept;
    static std::size_t DebugEventCount() noexcept;
    static CameraDebugEvent DebugEventAt(std::size_t index) noexcept;
#endif

private:
    static void onFreeCameraMove(int *position, int action, int param5, uint64_t *NowState,
                                 bool traceBoundaries);
};


#endif //NEWDIRECTORY_CAMERA_H
