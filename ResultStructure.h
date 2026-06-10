//
// Created by ESv87g9gvea4 on 2025/03/24.
//

#ifndef RESULTSTRUCTURE_H
#define RESULTSTRUCTURE_H

#include <iostream>
#include <cstring>

#include "BattleEmulator.h"

/**
 * @brief 入力エントリを表す構造体
 *
 * この構造体は、ある一つのダメージ値と、対応する候補（candidates）のリストを持ちます。
 */
struct InputEntry {
    int damage = 0;
    std::vector<int> candidates;
};

/**
 * @brief バトルシミュレーションの入力を管理する構造体
 *
 * この構造体は、バトルにおける敵および味方のダメージと行動IDを管理します。
 */
struct ResultStructure {
    int Edamage[350] = {}; // 敵側ダメージ
    int EdamageCounter = 0;
    int Aactions[350] = {}; // 味方行動ID
    int AactionsCounter = 0;
    int Adamage[350] = {}; // 味方ダメージ（候補によって振り分け）
    int AdamageCounter = 0;
    int AII_damage[350] = {}; // 入力順序通りの全ダメージコピー
    int AII_damageCounter = 0;

    ResultStructure()
        : EdamageCounter(0), AactionsCounter(0),
          AdamageCounter(0), AII_damageCounter(0) {
        std::memset(Edamage, 0, sizeof(Edamage));
        std::memset(Aactions, 0, sizeof(Aactions));
        std::memset(Adamage, 0, sizeof(Adamage));
        std::memset(AII_damage, 0, sizeof(AII_damage));
    }

    void print() const {
        std::cout << "[Enemy Damage](" << EdamageCounter << "): ";
        for (int i = 0; i < EdamageCounter; ++i) {
            std::cout << Edamage[i] << " ";
        }
        std::cout << "\n[Ally Action IDs](" << AactionsCounter << "): ";
        for (int i = 0; i < AactionsCounter; ++i) {
            if (Aactions[i] == BattleEmulator::ATTACK_ALLY) {
                std::cout << "a" << " ";
            } else if (Aactions[i] == BattleEmulator::MIRACLE_SLASH) {
                std::cout << "m" << " ";
            } else if ((Aactions[i] == BattleEmulator::SPECIAL_MEDICINE) || (
                           Aactions[i] == BattleEmulator::SPECIAL_ANTIDOTE)) {
                std::cout << "h" << " ";
            } else {
                std::cout << Aactions[i] << " ";
            }
        }
        std::cout << "\n[Ally Damage](" << AdamageCounter << "): ";
        for (int i = 0; i < AdamageCounter; ++i) {
            std::cout << Adamage[i] << " ";
        }
        std::cout << "\n[AII Damage](" << AII_damageCounter << "): ";
        for (int i = 0; i < AII_damageCounter; ++i) {
            std::cout << AII_damage[i] << " ";
        }
        std::cout << std::endl;
    }
};


#endif //RESULTSTRUCTURE_H
