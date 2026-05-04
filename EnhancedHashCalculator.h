//
// Enhanced State Hash Calculator for A* Algorithm
// Fixes the issue where identical action sequences with different game states have same hash
//

#ifndef ENHANCED_HASH_CALCULATOR_H
#define ENHANCED_HASH_CALCULATOR_H

#include <cstdint>
#include "Genome.h"

class EnhancedHashCalculator {
public:
    // Enhanced state hash function that includes game state information
    static uint64_t computeStateHash(const Genome &genome);
    
private:
    // Helper function to mix hash values
    static uint64_t mixHash(uint64_t hash, uint64_t value, uint64_t multiplier = 0x9e3779b97f4a7c15ULL);
};

#endif // ENHANCED_HASH_CALCULATOR_H