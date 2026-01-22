//
// Created by Owner on 2024/02/05.
//

#ifndef NEWDIRECTORY_PLAYER_H
#define NEWDIRECTORY_PLAYER_H

#include <algorithm>
#include <iostream>

#include "setting.h"


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

    int SpecialMedicineCount{0};
    double defence{0.0};
    bool sleeping{false};
    int sleepingTurn{0};
    int BuffLevel{0};
    int BuffTurns{0};
    int TensionLevel{0};
    bool rage{false};
    int MagicWaterCount{0};
    int speedLevel{0};
    int PoisonTurn{0};
    bool PoisonEnable{false};
    int SpecialAntidoteCount{0};
    bool acrobaticStar{false};
    int acrobaticStarTurn{0};
    int medicinal_herbs_count{setting::herbcount};

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
