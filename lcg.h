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

    static int getPercent(int *position, int max);

    static double floatRand(int *position, double min, double max);

    static int intRangeRand(int *position, int min, int max);

    static uint32_t getSeed(int * position);
};


#endif //NEWDIRECTORY_LCG_H