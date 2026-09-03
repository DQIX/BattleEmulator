//
// Created by Owner on 2024/02/05.
//

#ifndef NEWDIRECTORY_LCG_H
#define NEWDIRECTORY_LCG_H


#include <cstdint>

class lcg {
private:
    static void GenerateifNeed(int need);

    static uint64_t lcg_rand(uint64_t seed);

    static int calculatePercent(uint64_t input);

public:
    static void init(uint64_t seed, bool init = false);

    // Read-only access for exact proof helpers.  This never advances the
    // caller's RNG position; it only ensures the existing precalc table has
    // been generated far enough.
    static uint32_t peekTop32(int position);

    static int peekPercent(int position, int max);

    // Changes once per init(), allowing read-only caches to invalidate
    // themselves when the input seed changes.
    static uint64_t generation();

    static int getPercent(int *position, int max);

    static double floatRand(int *position, double min, double max);

    static double floatRand051_1(int *position);

    static double floatRandAttack(int *position);

    static int intRangeRand(int *position, int min, int max);

    static uint8_t getSeed(int * position);
};


#endif //NEWDIRECTORY_LCG_H