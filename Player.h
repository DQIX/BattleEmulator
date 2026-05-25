//
// Created by Owner on 2024/02/05.
//

#ifndef NEWDIRECTORY_PLAYER_H
#define NEWDIRECTORY_PLAYER_H

#include <algorithm>
#include <iostream>


struct Player {
    int hp;
    double maxHp;
    int atk;
    int def;
    int speed;
    int HealPower;
    int mp = 0;
    int maxMp = 0;

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
        obj.hp = std::min(static_cast<int>(obj.maxHp), obj.hp);
    }
};

#endif //NEWDIRECTORY_PLAYER_H
