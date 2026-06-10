//
// Created by Owner on 2024/02/06.
//

#include <vector>
#include "camera.h"
#include "BattleEmulator.h"
#include "lcg.h"

/**
 * 指定されたアクションに基づいてdq9のカメラをシミュレーションします。
 *
 * @param position 現在のlcgの乱数の位置を示すポインタ。
 * @param actions アクションの配列。実際のこのターンで行動したアクションが含まれる。
 * @param NowState 現在の状態を示すポインタ。カメラの内部状態を管理するために使用される。
 */
void camera::Main(int *position, const int32_t actions[5], uint64_t * NowState) {
    bool preemptive = true;
    for (int i = 0; i < 3; ++i) {
        int32_t after = actions[i];
        if (after == BattleEmulator::ATTACK_ALLY) {
            onFreeCameraMove(position, after, preemptive ? 1 : 0, NowState);
        } else if (after == BattleEmulator::POISON_ATTACK || after == BattleEmulator::MIRACLE_SLASH || after ==
                   BattleEmulator::DRAGON_SLASH) {
            (*position)++;//追尾カメラ
        }
        if (after != BattleEmulator::ATTACK_ALLY) {//味方の攻撃→上空だとフリーカメラが特異点の挙動する
            preemptive = false;
        }
    }
}

/**
 * dq9のフリーカメラの乱数消費をシミュレーションします。
 *
 * @param position 現在のlcg乱数の位置を示すポインタ。変更可能です。
 * @param action 実行されるアクションを示す値。BattleEmulatorクラスで定義されたアクションを使用します。
 * @param param5 移動の制御に使用される追加の引数。0または1の値を取ります。
 * @param NowState 現在の状態を格納するポインタ。カメラの状態を管理するために使用され、内部の「カウンタ」に関連する部分を更新します。
 */
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
