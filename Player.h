//
// Created by Owner on 2024/02/05.
//

#ifndef NEWDIRECTORY_PLAYER_H
#define NEWDIRECTORY_PLAYER_H

#include <algorithm>
#include <iostream>


/**
 * @brief プレイヤーのステータスや状態を管理する構造体
 *
 * この構造体は、ゲーム内のプレイヤーのパラメータ、状態、行動ステータス、
 * 特殊な効果やターン制状態異常などを管理します。
 */
struct Player {
    int hp = 0;
    double maxHp = 0;
    int atk = 0;
    int defaultATK = 0;
    int def = 0;
    int defaultDEF = 0;
    int speed = 0;
    int defaultSpeed = 0;
    int HealPower = 0;
    int mp = 0;
    int maxMp = 0;
    bool specialCharge = false;
    bool dirtySpecialCharge = false;
    int specialChargeTurn = 0;
    bool paralysis = false;
    int paralysisLevel = 0;
    int paralysisTurns = -1;

    int SpecialMedicineCount = 6;
    double defence = 1.0;
    bool sleeping = false;
    int sleepingTurn = -1;
    int BuffLevel = 0;
    int BuffTurns = -1;
    bool hasMagicMirror = false;
    int MagicMirrorTurn = -1;
    int AtkBuffLevel = 0;
    int AtkBuffTurn = -1;
    int TensionLevel = 0;
    bool rage = false;
    int SageElixirCount = 2;
    int ElfinElixirCount = 1;
    int MagicWaterCount = 1;
    int speedTurn = -1;
    int speedLevel = 0;
    int PoisonTurn = -1;
    bool PoisonEnable = false;
    int SpecialAntidoteCount = 2;
    bool acrobaticStar = false;
    int acrobaticStarTurn = 0;

    /**
     * @brief プレイヤーが生存しているかを判定する関数
     *
     * 指定されたプレイヤーオブジェクトの現在のHPが0でないかを確認し、
     * 生存しているかどうかを判定します。
     *
     * @param obj 判定対象のプレイヤーオブジェクト
     * @return プレイヤーが生存している場合はtrue、死亡している場合はfalse
     */
    constexpr static bool isPlayerAlive(const Player &obj) {
        return obj.hp != 0;
    }

    /**
     * @brief プレイヤーのHPを減少させる関数
     *
     * 指定されたプレイヤーオブジェクトのHPを指定された量だけ減少させます。
     * HPが0未満になる場合、最小値0として補正されます。
     *
     * @param obj HPを減少させる対象のプレイヤーオブジェクト
     * @param amount 減少させるHPの量
     */
    static void reduceHp(Player &obj, int amount) {
        obj.hp -= amount;
        obj.hp = std::max(0, obj.hp);
    }

    /**
     * @brief プレイヤーのHPを回復する関数
     *
     * 指定されたプレイヤーオブジェクトのHPを指定された量だけ回復します。
     * 回復後のHPは最大HPを超えないように制限されます。
     *
     * @param obj HPを回復させる対象のプレイヤーオブジェクト
     * @param amount 回復させるHPの量
     */
    static void heal(Player &obj, int amount) {
        obj.hp += amount;
        obj.hp = std::min(static_cast<int>(obj.maxHp), obj.hp);
    }
};

#endif //NEWDIRECTORY_PLAYER_H
