//
// Created by Owner on 2024/02/06.
//

#include "camera.h"
#include "BattleEmulator.h"
#include "lcg.h"

#include <array>

namespace {
struct CameraStep {
    uint8_t offsetBeforeRandom;
    uint8_t usesRandom;
    uint8_t randomMax;
    uint8_t offsetIfZero;
    uint8_t offsetIfNonZero;
    uint8_t counterIfZero;
    uint8_t counterIfNonZero;
};

constexpr std::array<CameraStep, 106> makeCameraStepTable() {
    std::array<CameraStep, 106> table{};
    for (int param5 = 0; param5 <= 1; ++param5) {
        for (int counter = 0; counter <= 5; ++counter) {
            const int index = param5 * 10 + counter;
            if (param5 == 0) {
                if (counter == 0) {
                    table[index] = {1, 0, 0, 0, 0, 1, 1};
                } else {
                    table[index] = {
                        1,
                        1,
                        static_cast<uint8_t>(5 - counter),
                        1,
                        0,
                        0,
                        static_cast<uint8_t>(counter + 1)
                    };
                }
            } else {
                table[index] = {
                    static_cast<uint8_t>(counter == 0 ? 2 : 3),
                    0,
                    0,
                    0,
                    0,
                    0,
                    0
                };
            }
        }
    }
    return table;
}

constexpr auto cameraStepTable = makeCameraStepTable();
}

void camera::Main(int *position, const int32_t actions[5], uint64_t * NowState, bool bakuti) {
    bool preemptive = true;
    for (int i = 0; i < 3; ++i) {
        int32_t after = actions[i];
        if (after == BattleEmulator::ATTACK_ALLY) {
            onFreeCameraMove(position, preemptive ? 1 : 0, NowState);
        }else if(after == BattleEmulator::ATTACK_ENEMY){
            (*position)++;//追尾カメラ
        }
        if (after != BattleEmulator::ATTACK_ALLY) {//味方の攻撃→上空だとフリーカメラが特異点の挙動する
            preemptive = false;
        }
    }
}

//constexprルッキングテーブルにすれば速い
//ここのパスは、乱数消費は同じでも、別のコルーチンなので、状態に圧縮するのはNG
void camera::onFreeCameraMove(int *position, const int param5, uint64_t * NowState) {
    auto counter = static_cast<int>((*NowState >> 8) & 0xf);
    const auto &step = cameraStepTable[param5 * 10 + counter];
    (*position) += step.offsetBeforeRandom;

    if (step.usesRandom) {
        if (lcg::getPercent(position, step.randomMax) == 0) {
            (*position) += step.offsetIfZero;
            counter = step.counterIfZero;
        } else {
            (*position) += step.offsetIfNonZero;
            counter = step.counterIfNonZero;
        }
    } else {
        counter = step.counterIfZero;
    }

    (*NowState) = (*NowState & ~0xf00ULL) | (static_cast<uint64_t>(counter) << 8);
}
