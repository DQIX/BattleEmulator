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

    static uint64_t nextTop32NoCache(int position);

    static int calculatePercent(uint64_t input);

public:
    struct RuntimeState {
        uint64_t seed;
        int counter;
        bool cached;
    };

    static void init(uint64_t seed, bool init = false);
    // Checkpoints belong to the same initialized seed; the cached table is shared.
    static RuntimeState CaptureRuntimeState() noexcept;
    static void RestoreRuntimeState(RuntimeState state) noexcept;

    /**
     * Do not write `(void)lcg::getPercent(position, max)` when only the RNG
     * consumption matters. Write `(*position)++` and preserve the ROM LR in
     * the call-site comment so RNG traces remain debuggable.
     * @param position
     * @param max
     * @return
     */
    static int getPercent(int *position, int max);

    static double floatRand(int *position, double min, double max);

    static double floatRand051_1(int *position);

    static double floatRandAttack(int *position);

    static int intRangeRand(int *position, int min, int max);

    static uint8_t getSeed(int * position);

    static int32_t getTop32(int *position);
};


#endif //NEWDIRECTORY_LCG_H
