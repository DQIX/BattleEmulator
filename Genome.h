//
// Created by Owner on 2024/11/21.
//

#ifndef NEWDIRECTORY_GENOME_H
#define NEWDIRECTORY_GENOME_H

#include <cstdint>
#include "Player.h"

// 遺伝子型
struct Genome {
    Player AllyPlayer;
    Player EnemyPlayer;
    Player IronKnightA;
    Player IronKnightB;
    uint64_t state;
    int turn;
    int position;
    int fitness;
    int Visited;
    int EActions[2];
    int Aactions;
    int compromiseScore;      // 状態異常などの妥協可能度（小さい方が良い）
    bool isEliminated;        // 閾値超えで切り捨てるフラグ
    int actions[350];
    bool Initialized;
    int processed;
    bool canMove;

    // 比較演算子（優先度付きキュー用、fitnessが高い方が優先）
    bool operator<(const Genome& other) const {
        return (fitness - compromiseScore) < (other.fitness + other.compromiseScore);
        //return fitness< other.fitness;
    }


    bool operator>(const Genome& other) const {
        return (fitness - compromiseScore) > (other.fitness + other.compromiseScore);
        //return fitness > other.fitness;

    }
};



#endif //NEWDIRECTORY_GENOME_H
