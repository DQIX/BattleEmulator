//
// Created by Owner on 2024/11/21.
//

#ifndef NEWDIRECTORY_GENOME_H
#define NEWDIRECTORY_GENOME_H

#include <cstdint>
#include "Player.h"

/**
 * @struct Genome
 * @brief バトルの最適化における個体の情報を管理する構造体
 *
 * この構造体は最適化アルゴリズムで使用される個体（Genome）のステータスや状態を表現します。
 * 各個体はプレイヤー情報、ゲーム状態、ターン数、アクション履歴、フィットネススコアなどの情報を含みます。
 * また、優先度付きキューでの操作のための比較演算子も実装されています。
 */
struct Genome {
    Player AllyPlayer{};
    Player EnemyPlayer{};
    uint64_t state = 0;
    int turn = 0;
    int position = 0;
    int fitness = 0;
    int Visited = 0;
    int EActions[2] = {};
    int Aactions = 0;
    int actions[350] = {};
    bool Initialized = false;
    int processed = 0;

    // 比較演算子（優先度付きキュー用、fitnessが高い方が優先）
    bool operator<(const Genome& other) const {
        return (fitness) < (other.fitness);
        //return fitness< other.fitness;
    }


    bool operator>(const Genome& other) const {
        return (fitness) > (other.fitness);
        //return fitness > other.fitness;

    }
};



#endif //NEWDIRECTORY_GENOME_H
