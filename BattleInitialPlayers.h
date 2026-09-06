#pragma once

#include "Player.h"
#include "setting.h"

// Moved from main.cpp; both executables use this single definition.
constexpr Player BasePlayers[2] = {
    // プレイヤー1
    {
        setting::Ally_MAX_HP, setting::Ally_MAX_HP, 61, 61, 66, 66, setting::ALLY_SPEED, setting::ALLY_SPEED, 29, setting::ALLY_CURRENT_MP, // 最初のメンバー
        setting::ALLY_CURRENT_MP, false, false, 0, false, 0, -1,
        // specialCharge, dirtySpecialCharge, specialChargeTurn, inactive, paralysis, paralysisLevel, paralysisTurns
        8, 1.0, false, -1, 0, -1, // SpecialMedicineCount, defence, sleeping, sleepingTurn, BuffLevel, BuffTurns
        false, -1, 0, -1, 0, false, 1, 1, 1, -1, 0, -1, false, 2, false, -1, -1, 7, false
    }, // hasMagicMirror, MagicMirrorTurn, AtkBuffLevel, AtkBuffTurn, TensionLevel

    // プレイヤー2
    {
        setting::ENEMY_MAX_HP, setting::ENEMY_MAX_HP, 56, 56, 58, 58, setting::ENEMY_SPEED, setting::ENEMY_SPEED, 0, 255, // 最初のメンバー
        255, false, false, 0, false, 0, -1,
        // specialCharge, dirtySpecialCharge, specialChargeTurn, inactive, paralysis, paralysisLevel, paralysisTurns
        0, 1.0, false, -1, 0, -1, // SpecialMedicineCount, defence, sleeping, sleepingTurn, BuffLevel, BuffTurns
        false, -1, 0, -1, 0, false, 0, 0, 0, -1, 0, -1, false, 2, false, -1, -1, 7, false
    } // hasMagicMirror, MagicMirrorTurn, AtkBuffLevel, AtkBuffTurn, TensionLevel
};
