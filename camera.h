//
// Created by Owner on 2024/02/06.
//

#ifndef NEWDIRECTORY_CAMERA_H
#define NEWDIRECTORY_CAMERA_H


#include <cstdint>
#include <cstddef>
#include "camera/freecam_actor.hpp"

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
};

class camera {
public:
    static bool ResetBattle(const CameraPresentationActor *actors, std::size_t actorCount);
    static void Main(int *position, const int32_t *actions,
                     const dq9::freecam::fast::BattleActorRef *actors,
                     const dq9::freecam::fast::BattleActorRef *targets,
                     int actionCount, uint64_t *NowState, bool preemptive, bool bakuti);

private:
    static void onFreeCameraMove(int *position, int action, int param5, uint64_t *NowState);
};


#endif //NEWDIRECTORY_CAMERA_H
