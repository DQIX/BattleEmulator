//
// Created by Owner on 2024/02/06.
//

#include "camera.h"
#include "BattleEmulator.h"
#include "lcg.h"

void camera::Main(int *position, const int32_t actions[5], uint64_t * NowState, bool preemptive1, bool bakuti) {
    bool preemptive = true;
    uint64_t before = -1;
    auto moture = false;
    for (int i = 0; i < 3; ++i) {
        int32_t after = actions[i];
        //一部の特異点の挙動について対策する

        //守備力が高すぎる場合(ダメージ0)true、盾ガードは偽
        if (bakuti && after == BattleEmulator::SKY_ATTACK) {
            moture = true;
        }
        if (moture && after == BattleEmulator::MERA_ZOMA) {
            onFreeCameraMove(position, after, 1, NowState);
            continue;
        }
        if (after == BattleEmulator::ATTACK_ALLY||after == BattleEmulator::SKY_ATTACK||after == BattleEmulator::MERA_ZOMA) {
            onFreeCameraMove(position, after, preemptive ? 1 : 0, NowState);
        }else if(after == BattleEmulator::MERCURIAL_THRUST){
            (*position)++;//追尾カメラ
        }
        if (after != BattleEmulator::ATTACK_ALLY) {//味方の攻撃→上空だとフリーカメラが特異点の挙動する
            preemptive = false;
        }
        before = after;
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
