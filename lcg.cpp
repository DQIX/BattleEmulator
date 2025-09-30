//
// Created by Owner on 2024/02/05.
//

#include "lcg.h"
#include <cstdint>
#include <cmath>  // cmathヘッダーをインクルードする
#include <iostream>

// 許可する最大乱数の数
const int ARRAY_SIZE = 100000;

double precalculatedValues[ARRAY_SIZE]; // 固定メモリ
uint64_t seeds[ARRAY_SIZE];             // 固定メモリ
int nowCounter = 1;
uint64_t now_seed;

void lcg::init(uint64_t seed) {
    nowCounter = 1;
    now_seed = seed;
}

void lcg::GenerateifNeed(int need) {
    // 配列に値を再計算して格納する
    if(nowCounter > need){
        return;
    }
    for (int i = nowCounter; i < need+2; ++i) {
        now_seed = lcg_rand(now_seed);
        precalculatedValues[nowCounter] = calculatePercent(now_seed) * 0.01;
        seeds[nowCounter++] = now_seed >> 32;//これがないとlcgのパフォーマンスが大幅に落ちる
    }
}

// Linear congruential generator function
uint64_t lcg::lcg_rand(uint64_t seed) {
    // Constants for the LCG formula
    const uint64_t multiplier = 0x5d588b656c078965;
    const uint64_t increment = 0x269ec3;
    const uint64_t modulo = 0xFFFFFFFFFFFFFFFF;

    // Update the seed using the LCG formula
    seed = seed * multiplier + increment;

    // Apply modulo to keep the result within the specified range
    seed = seed & modulo;

    return seed;
}

// Function to calculate percent in C++
double lcg::calculatePercent(uint64_t input) {
    // Right shift the input by 32 bits
    uint64_t output = input >> 32;

    // Multiply the result by 100
    output *= 100;

    // Divide the result by (2 << 31) and apply % 2
    return static_cast<double>(output) / (2ULL << 31);
}

int lcg::getPercent(int *position, int max) {
    // nullptrでないことを確認
    if (position == nullptr) {
        throw std::invalid_argument("Null pointer passed to incrementPosition.");
    }
    if ((*position) >= ARRAY_SIZE) {
        std::cerr << "out of range!!!" << std::endl;
        return 0;
    }
    GenerateifNeed((*position));
    double result = precalculatedValues[*position];
    double scaledResult = result * max;

    // floorしてintにキャスト
    int roundedResult = static_cast<int>(floor(scaledResult));

    // ポインタの指す位置をインクリメント
    (*position)++;
    return roundedResult;
}

double lcg::floatRand(int *position, double min, double max) {
    if (position == nullptr) {
        throw std::invalid_argument("Null pointer passed to incrementPosition.");
    }
    if ((*position) >= ARRAY_SIZE) {
        std::cerr << "out of range!!!" << std::endl;
        return 0;
    }
    GenerateifNeed((*position));
    double result = precalculatedValues[*position];
    (*position)++;
    return min + result * (max - min);
}

int lcg::intRangeRand(int *position, int min, int max) {
    return min + getPercent(position, max - min + 1);
}

uint64_t lcg::getSeed(int *position) {
    if (position == nullptr) {
        throw std::invalid_argument("Null pointer passed to incrementPosition.");
    }
    if ((*position) >= ARRAY_SIZE) {
        std::cerr << "out of range!!!" << std::endl;
        return 0;
    }
    GenerateifNeed((*position));
    uint64_t result = seeds[*position];
    (*position)++;
    return result;
}
