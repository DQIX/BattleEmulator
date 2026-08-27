//
// Created by Owner on 2024/02/06.
//

#include <cassert>

#include "camera.h"
#include "BattleEmulator.h"
#include "lcg.h"
#include "camera/freecam_action_mapper.hpp"

namespace {

enum class CameraRule {
    none,
    free_camera,
    free_camera_with_tracking_fallback,
};

[[nodiscard]] constexpr CameraRule RuleForAction(const int action) noexcept {
    switch (action) {
        case BattleEmulator::ATTACK_ALLY:
        case BattleEmulator::SKY_ATTACK:
        case BattleEmulator::MERA_ZOMA:
            return CameraRule::free_camera;
        case BattleEmulator::MERCURIAL_THRUST:
            // Runtime action 69 has a free-camera BACT. Its selector suppresses
            // the free-camera call only when there is no presentation route.
            // This existing tracking-camera rule is the moving-actor path, so
            // try free camera first and use tracking only when construction fails.
            return CameraRule::free_camera_with_tracking_fallback;
        default:
            return CameraRule::none;
    }
}

inline void AssertCameraMapping(const int action) noexcept {
    const auto* binding = dq9::freecam::bindings::Find(action);
    assert(binding != nullptr && binding->mapped());
    (void)binding;
}

[[nodiscard]] bool FreeCameraBuilt(const uint64_t* NowState) noexcept {
    return (((*NowState) >> 8) & UINT64_C(0xf)) == 0;
}

} // namespace

void camera::Main(int *position, const int32_t actions[5], uint64_t * NowState, bool preemptive1, bool bakuti) {
    (void)preemptive1;

    bool preemptive = true;
    auto moture = false;
    for (int i = 0; i < 3; ++i) {
        const int32_t after = actions[i];
        if (after < 0) break;

        //守備力が高すぎる場合(ダメージ0)true、盾ガードは偽
        if (bakuti && after == BattleEmulator::SKY_ATTACK) {
            moture = true;
        }
        if (moture && after == BattleEmulator::MERA_ZOMA) {
            AssertCameraMapping(after);
            onFreeCameraMove(position, after, 1, NowState);
            continue;
        }

        const CameraRule rule = RuleForAction(after);
        if (rule != CameraRule::none) {
            AssertCameraMapping(after);
            onFreeCameraMove(position, after, preemptive ? 1 : 0, NowState);

            if (rule == CameraRule::free_camera_with_tracking_fallback
                && !FreeCameraBuilt(NowState)) {
                (*position)++; // free camera不成立時の追尾カメラ
            }
        }
        if (after != BattleEmulator::ATTACK_ALLY) {//味方の攻撃→上空だとフリーカメラが特異点の挙動する
            preemptive = false;
        }
    }
}

void camera::onFreeCameraMove(int *position, const int action, const int param5, uint64_t * NowState) {
    auto counter = ((*NowState) >> 8) & 0xf;
    do {
        if (param5 == 0) {
            (*position)++;
            if (counter == 0) {
                counter++;
                break;
            }
            auto ret = lcg::getPercent(position, 5 - counter);
            if (ret == 0 || counter == 5) {
                counter = 0;
                (*position) += 1;
                if (action == BattleEmulator::ATTACK_ALLY){
                    (*position)+=2;
                }
            } else {
                counter++;
            }
        } else {
            (*position)++;
            if (counter == 0) {
                (*position)++;//引数5が1なら強制的に実行
                counter = 0;
                if (action == BattleEmulator::ATTACK_ALLY){
                    (*position)+=2;
                }
                break;
            }
            (*position)++;
            counter = 0;
            (*position)++;
            if (action == BattleEmulator::ATTACK_ALLY){
                (*position)+=2;
            }

        }
    } while (false);
    (*NowState) &= ~0xf00;
    (*NowState) |= (counter << 8);
}
