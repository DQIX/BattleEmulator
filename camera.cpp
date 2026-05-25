//
// Created by Owner on 2024/02/06.
//

#include "camera.h"
#include "BattleEmulator.h"
#include "lcg.h"

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
            } else {
                counter++;
            }
        } else {
            (*position)++;
            if (counter == 0) {
                (*position)++;//引数5が1なら強制的に実行
                counter = 0;
                break;
            }
            (*position)++;
            counter = 0;
            (*position)++;
        }
    } while (false);
    (*NowState) &= ~0xf00;
    (*NowState) |= (counter << 8);
}
