//
// Created by Owner on 2024/02/05.
//

#ifndef NEWDIRECTORY_PLAYER_H
#define NEWDIRECTORY_PLAYER_H

#include <algorithm>
#include <iostream>


struct Player {
    int hp{};
    double maxHp{};
    int atk{};
    int defaultATK{};
    int def{};
    int defaultDEF{};
    int speed{};
    int defaultSpeed{};
    int HealPower{};
    int mp{};
    int maxMp{};
    bool specialCharge{};
    bool dirtySpecialCharge{};
    int specialChargeTurn{};
    bool paralysis{};
    int paralysisLevel{};
    int paralysisTurns{};

    int SpecialMedicineCount{};
    double defence{};
    bool sleeping{};
    int sleepingTurn{};
    int BuffLevel{};
    int BuffTurns{};
    bool hasMagicMirror{};
    int MagicMirrorTurn{};
    int AtkBuffLevel{};
    int AtkBuffTurn{};
    int TensionLevel{};
    bool rage{};
    int SageElixirCount{};
    int ElfinElixirCount{};
    int MagicWaterCount{};
    int speedTurn{};
    int speedLevel{};
    int PoisonTurn{};
    bool PoisonEnable{};
    int SpecialAntidoteCount{};
    bool acrobaticStar{};
    int acrobaticStarTurn{};
    int medicinal_herbs_count{};

    constexpr static bool isPlayerAlive(const Player &obj) {
        return obj.hp != 0;
    }

    static void reduceHp(Player &obj, int amount) {
        obj.hp -= amount;
        obj.hp = std::max(0, obj.hp);
    }

    static void heal(Player &obj, int amount) {
        obj.hp += amount;
        obj.hp = std::min(static_cast<int>(obj.maxHp), obj.hp);
    }
};

#endif //NEWDIRECTORY_PLAYER_H
