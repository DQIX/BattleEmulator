//
// Created by Owner on 2024/02/05.
//

#include "lcg.h"
#include <cstdint>
#include <cmath>  // cmathヘッダーをインクルードする
#include <iostream>

// Define the size of the array
const int ARRAY_SIZE = 5000;

uint32_t precalcTop32[ARRAY_SIZE]; // 固定メモリ
int nowCounter = 1;
uint64_t base_seed;     // init時の初期シード
uint64_t now_seed;      // 現在のシード（逐次 or ジャンプ後）
uint64_t now_seed_F;    // 論理的に何 step 進んだか
bool init_mode;         // true = 初期一括生成モード

//ロングジャンプの定数
static const unsigned long long LCG_A[65] = {
    0x5d588b656c078965ULL,
    0x722c73d8054341d9ULL,
    0x355bc66e0285e9f1ULL,
    0x6c5234d0ae3294e1ULL,
    0x4d5a22005a78edc1ULL,
    0x84d4844b75beeb81ULL,
    0x2c8b173c56221701ULL,
    0xf02de276ca552e01ULL,
    0x9804b5dd28ee5c01ULL,
    0xd0379e6982ecb801ULL,
    0x8cb37296ca197001ULL,
    0xd6824c74a532e001ULL,
    0x82ba37c58e65c001ULL,
    0x35c2f8fc2ccb8001ULL,
    0xec0087bc99970001ULL,
    0x0beee68a332e0001ULL,
    0xf7b12958665c0001ULL,
    0xef8fc3c0ccb80001ULL,
    0xe6d54bc199700001ULL,
    0x2481a88332e00001ULL,
    0x645f950665c00001ULL,
    0x36303a0ccb800001ULL,
    0x2224b41997000001ULL,
    0x1b5a68332e000001ULL,
    0x92f8d0665c000001ULL,
    0x9701a0ccb8000001ULL,
    0xf243419970000001ULL,
    0xf5868332e0000001ULL,
    0x2f0d0665c0000001ULL,
    0x6e1a0ccb80000001ULL,
    0x1c34199700000001ULL,
    0x3868332e00000001ULL,
    0x70d0665c00000001ULL,
    0xe1a0ccb800000001ULL,
    0xc341997000000001ULL,
    0x868332e000000001ULL,
    0x0d0665c000000001ULL,
    0x1a0ccb8000000001ULL,
    0x3419970000000001ULL,
    0x68332e0000000001ULL,
    0xd0665c0000000001ULL,
    0xa0ccb80000000001ULL,
    0x4199700000000001ULL,
    0x8332e00000000001ULL,
    0x0665c00000000001ULL,
    0x0ccb800000000001ULL,
    0x1997000000000001ULL,
    0x332e000000000001ULL,
    0x665c000000000001ULL,
    0xccb8000000000001ULL,
    0x9970000000000001ULL,
    0x32e0000000000001ULL,
    0x65c0000000000001ULL,
    0xcb80000000000001ULL,
    0x9700000000000001ULL,
    0x2e00000000000001ULL,
    0x5c00000000000001ULL,
    0xb800000000000001ULL,
    0x7000000000000001ULL,
    0xe000000000000001ULL,
    0xc000000000000001ULL,
    0x8000000000000001ULL,
    0x0000000000000001ULL,
    0x0000000000000001ULL,
    0x0000000000000001ULL
};

//ロングジャンプの定数2
static const unsigned long long LCG_C[65] = {
    0x0000000000269ec3ULL,
    0x7188d00c55ae9cb2ULL,
    0x0a8b4e34c910a194ULL,
    0x229675654eac71e8ULL,
    0x9d8474851566aed0ULL,
    0x0e7f4341592709a0ULL,
    0xaf8278456068c340ULL,
    0x6545aeb598dc4680ULL,
    0xed1f0ea72ee38d00ULL,
    0x536510bd3a731a00ULL,
    0x6a3422cd27963400ULL,
    0x5c11e19f19ec6800ULL,
    0xb85689215ed8d000ULL,
    0xa5d4d84f69b1a000ULL,
    0x42ce3cd183634000ULL,
    0x705a4a6dc6c68000ULL,
    0x9d08d8068d8d0000ULL,
    0xb64abcb91b1a0000ULL,
    0xb4b9ac2236340000ULL,
    0x440423046c680000ULL,
    0xc24b7108d8d00000ULL,
    0xeda38e11b1a00000ULL,
    0x7f79cc2363400000ULL,
    0x8fbe5846c6800000ULL,
    0x62a7b08d8d000000ULL,
    0xd1fb611b1a000000ULL,
    0xd6a6c23634000000ULL,
    0x780d846c68000000ULL,
    0x1b1b08d8d0000000ULL,
    0xe23611b1a0000000ULL,
    0x746c236340000000ULL,
    0xa8d846c680000000ULL,
    0x51b08d8d00000000ULL,
    0xa3611b1a00000000ULL,
    0x46c2363400000000ULL,
    0x8d846c6800000000ULL,
    0x1b08d8d000000000ULL,
    0x3611b1a000000000ULL,
    0x6c23634000000000ULL,
    0xd846c68000000000ULL,
    0xb08d8d0000000000ULL,
    0x611b1a0000000000ULL,
    0xc236340000000000ULL,
    0x846c680000000000ULL,
    0x08d8d00000000000ULL,
    0x11b1a00000000000ULL,
    0x2363400000000000ULL,
    0x46c6800000000000ULL,
    0x8d8d000000000000ULL,
    0x1b1a000000000000ULL,
    0x3634000000000000ULL,
    0x6c68000000000000ULL,
    0xd8d0000000000000ULL,
    0xb1a0000000000000ULL,
    0x6340000000000000ULL,
    0xc680000000000000ULL,
    0x8d00000000000000ULL,
    0x1a00000000000000ULL,
    0x3400000000000000ULL,
    0x6800000000000000ULL,
    0xd000000000000000ULL,
    0xa000000000000000ULL,
    0x4000000000000000ULL,
    0x8000000000000000ULL,
    0x0000000000000000ULL
};

static inline uint64_t long_jump(uint64_t state, uint64_t k) noexcept {
    unsigned int i = 0;
    while (k != 0) {
        if (k & 1ULL) {
            state = state * LCG_A[i] + LCG_C[i];
        }
        k >>= 1;
        ++i;
    }
    return state;
}



/**
 * 指定された乱数シードで線形合同法生成器を初期化します。
 *
 * @param seed 初期化に使用する乱数シード
 * @param init 必要に応じて初期乱数列をすべて生成するかどうかを制御するフラグ、trueに設定すると動的生成が無効になる。
 */
void lcg::init(uint64_t seed, bool init) {
    base_seed   = seed;
    now_seed    = seed;
    now_seed_F  = 0;
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
    // 配列に値を再計算して格納する
    if(nowCounter > need){
        return;
    }
    if (init_mode) {
        for (int i = nowCounter; i < need+2; ++i) {
            now_seed = lcg_rand(now_seed);
            precalcTop32[++nowCounter] = static_cast<uint32_t>(now_seed >> 32);
        }
    } else {
        now_seed = long_jump(now_seed, need - nowCounter);
        precalcTop32[need] = static_cast<uint32_t>(now_seed >> 32);
        nowCounter = need;
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
    if (position == nullptr) {
        throw std::invalid_argument("Null pointer passed");
    }
    if ((*position) >= ARRAY_SIZE) {
        std::cerr << "out of range!!!" << std::endl;
        return min;
    }

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
