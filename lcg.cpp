//
// Created by Owner on 2024/02/05.
//

#include "lcg.h"

#include <cassert>
#include <cstdint>

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

inline uint64_t lcg::lcg_rand(uint64_t seed) {
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

static inline uint64_t lcg_advance(uint64_t seed, uint64_t delta) {
    const uint64_t multiplier = 0x5d588b656c078965;
    const uint64_t increment = 0x269ec3;

    // Exponentiation by squaring for LCG: f(x)=a*x+c (mod 2^64).
    uint64_t a = multiplier;
    uint64_t c = increment;
    uint64_t acc_mult = 1;
    uint64_t acc_plus = 0;

    while (delta) {
        if (delta & 1ULL) {
            acc_mult = acc_mult * a;
            acc_plus = acc_plus * a + c;
        }
        c = c * (a + 1);
        a = a * a;
        delta >>= 1ULL;
    }

    return acc_mult * seed + acc_plus;
}

inline void lcg::GenerateifNeed(int need) {
    assert(now_seed != 0);
    assert(need != 0);

    // init_mode=false ではキャッシュ運用自体をしない
    if (!init_mode) {
        return;
    }

    // 配列に値を再計算して格納する
    if (nowCounter > need) {
        return;
    }
    for (int i = nowCounter; i < need; ++i) {
        now_seed = lcg_rand(now_seed);
        precalcTop32[++nowCounter] = static_cast<uint32_t>(now_seed >> 32);
    }
}

// init_mode=false のとき：キャッシュせず、position まで前方スキップして top32 を返す
inline uint64_t lcg::nextTop32NoCache(int position) {
    assert(now_seed != 0);
    // 既に通過した位置には戻れない（キャッシュ無しの制約）
    assert(position >= nowCounter);

    // nowCounter を position まで進める（ジャンプ対応）
    if (nowCounter < position) {
        const uint64_t delta = static_cast<uint64_t>(position - nowCounter);
        now_seed = lcg_advance(now_seed, delta);
        nowCounter = position;
    }
    // now_seed は「position に対応する seed」になっている
    return now_seed >> 32;
}

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

    if (init_mode) {
        GenerateifNeed(ARRAY_SIZE - 3);
    }
}

// ... existing code ...

uint8_t lcg::getSeed(int *position) {
    assert(position != nullptr);
    assert((*position) < ARRAY_SIZE);

    if (!init_mode) {
        const auto top = nextTop32NoCache(*position);
        const auto result = static_cast<uint8_t>(top & 1);
        (*position)++;
        return result;
    }

    GenerateifNeed((*position));
    const uint8_t result = precalcTop32[(*position)] & 1;
    (*position)++;
    return result;
}

int32_t lcg::getTop32(int *position) {
    assert(position != nullptr);
    assert((*position) < ARRAY_SIZE);

    if (!init_mode) {
        const auto top = nextTop32NoCache(*position);
        (*position)++;
        return top;
    }

    GenerateifNeed((*position));
    const auto result = precalcTop32[(*position)];
    (*position)++;
    return result;
}

int lcg::getPercent(int *position, int max) {
    assert(position != nullptr);
    assert((*position) < ARRAY_SIZE);

    if (!init_mode) {
        const auto top = nextTop32NoCache(*position);
        const uint64_t mul = top * max;
        auto roundedResult = static_cast<int>(mul >> 32);
        (*position)++;
        return roundedResult;
    }

    GenerateifNeed((*position));
    uint64_t mul = static_cast<uint64_t>(precalcTop32[*position]) * max;
    auto roundedResult = static_cast<int>(mul >> 32);

    (*position)++;
    return roundedResult;
}

double lcg::floatRand(int *position, double min, double max) {
    assert(position != nullptr);
    assert((*position) < ARRAY_SIZE);

    uint64_t top;
    if (!init_mode) {
        top = nextTop32NoCache(*position);
        (*position)++;
    } else {
        GenerateifNeed(*position);
        top = precalcTop32[*position];
        (*position)++;
    }

    double u = (double)top * (1.0 / 4294967296.0);
    return min + u * (max - min);
}

double lcg::floatRand051_1(int *position) {
    assert(position != nullptr);
    assert((*position) < ARRAY_SIZE);

    uint64_t top;
    if (!init_mode) {
        top = nextTop32NoCache(*position);
        (*position)++;
    } else {
        GenerateifNeed(*position);
        top = precalcTop32[*position];
        (*position)++;
    }

    return 0.51 + static_cast<double>(top) * (0.49 / 4294967296.0);
}

double lcg::floatRandAttack(int *position) {
    assert(position != nullptr);
    assert((*position) < ARRAY_SIZE);

    uint64_t top;
    if (!init_mode) {
        top = nextTop32NoCache(*position);
        (*position)++;
    } else {
        GenerateifNeed(*position);
        top = precalcTop32[*position];
        (*position)++;
    }

    return -1.0 + static_cast<double>(top) * (1.0 / 2147483648.0); // [-1,1)
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
