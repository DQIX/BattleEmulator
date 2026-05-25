//
// Created by Owner on 2024/02/05.
//

#ifndef NEWDIRECTORY_LCG_H
#define NEWDIRECTORY_LCG_H


#include <cassert>
#include <cstdint>

class lcg {
private:
    static constexpr int ARRAY_SIZE = 7000;
    static constexpr uint64_t LCG_MULTIPLIER = 0x5d588b656c078965;
    static constexpr uint64_t LCG_INCREMENT = 0x269ec3;
    static constexpr int LINEAR_ADVANCE_LIMIT = 32;

#if defined(OPTIMIZE_MODE)
    inline static thread_local uint32_t precalcTop32[ARRAY_SIZE] = {};
    inline static thread_local int nowCounter = 1;
    inline static thread_local uint64_t now_seed = 0;
    inline static thread_local bool init_mode = false;
#else
    inline static uint32_t precalcTop32[ARRAY_SIZE] = {};
    inline static int nowCounter = 1;
    inline static uint64_t now_seed = 0;
    inline static bool init_mode = false;
#endif

    static inline uint64_t lcg_rand(uint64_t seed) {
        return seed * LCG_MULTIPLIER + LCG_INCREMENT;
    }

    static inline uint64_t lcg_advance(uint64_t seed, uint64_t delta) {
        uint64_t a = LCG_MULTIPLIER;
        uint64_t c = LCG_INCREMENT;
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

    static inline void GenerateifNeed(int need) {
        assert(now_seed != 0);
        assert(need != 0);

        if (!init_mode || nowCounter > need) {
            return;
        }
        for (int i = nowCounter; i < need; ++i) {
            now_seed = lcg_rand(now_seed);
            precalcTop32[++nowCounter] = static_cast<uint32_t>(now_seed >> 32);
        }
    }

    static inline uint64_t nextTop32NoCache(int position) {
        assert(now_seed != 0);
        assert(position >= nowCounter);

        if (nowCounter < position) {
            int delta = position - nowCounter;
            if (delta <= LINEAR_ADVANCE_LIMIT) {
                do {
                    now_seed = lcg_rand(now_seed);
                } while (--delta != 0);
            } else {
                now_seed = lcg_advance(now_seed, static_cast<uint64_t>(delta));
            }
            nowCounter = position;
        }
        return now_seed >> 32;
    }

public:
    static inline void init(uint64_t seed, bool init = false) {
        now_seed = seed;
        nowCounter = 0;
        init_mode = init;

        if (init_mode) {
            GenerateifNeed(ARRAY_SIZE - 3);
        }
    }

    static inline int getPercent(int *position, int max) {
        assert(position != nullptr);
        assert((*position) < ARRAY_SIZE);

        uint64_t top;
        if (!init_mode) {
            top = nextTop32NoCache(*position);
        } else {
            GenerateifNeed((*position));
            top = precalcTop32[*position];
        }
        (*position)++;
        return static_cast<int>((top * static_cast<uint64_t>(max)) >> 32);
    }

    static inline double floatRand(int *position, double min, double max) {
        assert(position != nullptr);
        assert((*position) < ARRAY_SIZE);

        uint64_t top;
        if (!init_mode) {
            top = nextTop32NoCache(*position);
        } else {
            GenerateifNeed(*position);
            top = precalcTop32[*position];
        }
        (*position)++;

        double u = static_cast<double>(top) * (1.0 / 4294967296.0);
        return min + u * (max - min);
    }

    static inline double floatRand051_1(int *position) {
        assert(position != nullptr);
        assert((*position) < ARRAY_SIZE);

        uint64_t top;
        if (!init_mode) {
            top = nextTop32NoCache(*position);
        } else {
            GenerateifNeed(*position);
            top = precalcTop32[*position];
        }
        (*position)++;

        return 0.51 + static_cast<double>(top) * (0.49 / 4294967296.0);
    }

    static inline double floatRandAttack(int *position) {
        assert(position != nullptr);
        assert((*position) < ARRAY_SIZE);

        uint64_t top;
        if (!init_mode) {
            top = nextTop32NoCache(*position);
        } else {
            GenerateifNeed(*position);
            top = precalcTop32[*position];
        }
        (*position)++;

        return -1.0 + static_cast<double>(top) * (1.0 / 2147483648.0);
    }

    static inline int intRangeRand(int *position, int min, int max) {
        return min + getPercent(position, max - min + 1);
    }

    static inline uint8_t getSeed(int * position) {
        assert(position != nullptr);
        assert((*position) < ARRAY_SIZE);

        uint64_t top;
        if (!init_mode) {
            top = nextTop32NoCache(*position);
        } else {
            GenerateifNeed((*position));
            top = precalcTop32[(*position)];
        }
        (*position)++;
        return static_cast<uint8_t>(top & 1);
    }

    static inline int32_t getTop32(int *position) {
        assert(position != nullptr);
        assert((*position) < ARRAY_SIZE);

        uint64_t top;
        if (!init_mode) {
            top = nextTop32NoCache(*position);
        } else {
            GenerateifNeed((*position));
            top = precalcTop32[(*position)];
        }
        (*position)++;
        return static_cast<int32_t>(top);
    }
};


#endif //NEWDIRECTORY_LCG_H
