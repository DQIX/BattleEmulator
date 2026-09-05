//
// Created by Owner on 2024/02/05.
//

#ifndef NEWDIRECTORY_PLAYER_H
#define NEWDIRECTORY_PLAYER_H

#include <algorithm>
#include <cstdint>
#include <iostream>

struct Player {
    int hp;
    int maxHp;
    int atk;
    int defaultATK;
    int def;
    int defaultDEF;
    int speed;
    int HealPower;
    int mp = 0;
    int maxMp = 0;
    bool specialCharge = false;
    bool dirtySpecialCharge = false;
    int specialChargeTurn = 0;
    bool paralysis = false;
    int paralysisLevel = 0;
    int paralysisTurns = -1;

    int SpecialMedicineCount = 3;
    double defence = 1.0;
    bool sleeping = false;
    int sleepingTurn = -1;
    int BuffLevel = 0;
    int BuffTurns = -1;
    bool hasMagicMirror = false; // DQ9 live combat +0x14 bit 0x200 (Mirror Shield / spell reflection)
    int MagicMirrorTurn = -1;
    int AtkBuffLevel = 0;
    int AtkBuffTurn = -1;
    int TensionLevel = 0;
    bool rage = false;
    int SageElixirCount = 1;
    int ElfinElixirCount = 1;
    int MagicWaterCount = 1;
    int InsulateLevel = 0;
    int InsulateTurns = -1;
    bool inactive = false;
    int rageTurns = -1;
    int magicResistanceLevel = 0;
    bool confused = false;
    int confusionTurns = -1;
    int guardedBy = -1;
    // Keep new state fields at the end: legacy fixtures use positional aggregate
    // initialization for Player and must retain their existing field ordering.
    int MagicMirrorRecoveryTurn = 0;
    uint8_t aiResourceGateMask = 0;

    [[nodiscard]] constexpr bool operator==(const Player&) const = default;

    // 他のメンバー変数やメンバー関数を追加する可能性があります

    constexpr static bool isPlayerAlive(const Player &obj) {
        return obj.hp != 0;
    }

    static void reduceHp(Player &obj, int amount) {
        obj.hp -= amount;
        obj.hp = std::max(0, obj.hp);
    }

    static void heal(Player &obj, int amount) {
        obj.hp += amount;
        obj.hp = std::min((obj.maxHp), obj.hp);
    }
};

#endif //NEWDIRECTORY_PLAYER_H
