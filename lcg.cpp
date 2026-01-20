//
// Created by Owner on 2024/02/05.
//

#include "lcg.h"

#include <cassert>
#include <cstdint>
#include <cmath>  // cmathヘッダーをインクルードする

#if defined(OPTIMIZE_MODE)
// Define the size of the array
const int ARRAY_SIZE = 5000;

thread_local uint32_t precalcTop32[ARRAY_SIZE]; // 固定メモリ
thread_local int nowCounter = 1;
thread_local uint64_t now_seed = 0;      // 現在のシード（逐次 or ジャンプ後）
thread_local bool init_mode;         // true = 初期一括生成モード
#else
const int ARRAY_SIZE = 5000;
uint32_t precalcTop32[ARRAY_SIZE]; // 固定メモリ
int nowCounter = 1;
uint64_t now_seed;      // 現在のシード（逐次 or ジャンプ後）
bool init_mode;         // true = 初期一括生成モード
#endif





/**
 * 指定された乱数シードで線形合同法生成器を初期化します。
 *
 * @param seed 初期化に使用する乱数シード
 * @param init 必要に応じて初期乱数列をすべて生成するかどうかを制御するフラグ、trueに設定すると動的生成が無効になる。
 */
void lcg::init(uint64_t seed, bool init) {
    now_seed    = seed;
    nowCounter  = 0;
    init_mode   = init;

    if (init) {
        GenerateifNeed(ARRAY_SIZE - 3);
    }
}

/**
 * 必要に応じて乱数を追加生成します。
 * 指定されたインデックス位置までの値が計算されていない場合に、値を再計算して格納します。
 *
 * @param need 更新を必要とする配列のインデックス
 */
void lcg::GenerateifNeed(int need) {
    assert(now_seed != 0);
    // 配列に値を再計算して格納する
    if(nowCounter > need){
        return;
    }
    for (int i = nowCounter; i < need+2; ++i) {
        now_seed = lcg_rand(now_seed);
        precalcTop32[++nowCounter] = static_cast<uint32_t>(now_seed >> 32);
    }
}

/**
 * 線形合同法（LCG）を使用して次の擬似乱数を生成します。
 *
 * @param seed 擬似乱数生成のための現在のシード値
 * @return 計算された次の擬似乱数シード値
 */
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

/**
 * 入力された値から線形合同法に基づいて百分率を計算します。
 * これはdq9と一定の互換性のある処理になっています。
 *
 * @param input 計算の基となる64ビットの入力値
 * @return 計算された百分率を表す値
 */
int lcg::calculatePercent(uint64_t input) {
    // Right shift the input by 32 bits
    uint64_t output = input >> 32;

    return static_cast<int>(output * 1000000 >> 32);
}

uint32_t lcg::getSeed(const int *position) {
    return precalcTop32[*position];
}


int lcg::getPercent(int *position, int max) {
    // nullptrでないことを確認
    assert(position != nullptr);
    assert((*position) < ARRAY_SIZE);
    GenerateifNeed((*position));
    uint64_t mul = static_cast<uint64_t>(precalcTop32[*position]) * max;
    auto roundedResult = static_cast<int>(mul >> 32);

    // ポインタの指す位置をインクリメント
    (*position)++;
    return roundedResult;
}

/**
 * 指定された位置と最大値を使用して、線形合同法による乱数を整数として取得します。
 * 位置は計算後に自動的にインクリメントされます。
 *
 * @param position 乱数の現在の位置を保持するポインタ
 *                 nullptrの場合は例外がスローされます。
 * @param max 結果の最大値。0~[最大-1]までを返す
 * @return 0以上max-1未満の整数値
 *         ただし、範囲外エラーが発生した場合は0を返します。
 * @throw std::invalid_argument positionがnullptrの場合
 */
double lcg::floatRand(int *position, double min, double max) {
    assert(position != nullptr);
    assert((*position) < ARRAY_SIZE);
    GenerateifNeed(*position);
    uint32_t top = precalcTop32[*position];
    (*position)++;

    // [0,1) に正規化（1.0 になることはない）
    double u = (double)top * (1.0 / 4294967296.0);

    return min + u * (max - min);
}


/**
 * 指定された範囲内で整数型の乱数を生成します。
 *
 * @param position 現在の生成位置を表すポインタ
 * @param min 生成する乱数の最小値
 * @param max 生成する乱数の最大値
 * @return minからmaxの範囲内の乱数
 *
 * @throw std::invalid_argument positionがnullptrの場合にスローされます。
 * @note 指定されたpositionはインクリメントされます。
 * @note minおよびmaxは端の値を含みます。
 */
int lcg::intRangeRand(int *position, int min, int max) {
    return min + getPercent(position, max - min + 1);
}
