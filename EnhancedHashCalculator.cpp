//
// Enhanced State Hash Calculator Implementation
//

#include "EnhancedHashCalculator.h"

uint64_t EnhancedHashCalculator::computeStateHash(const Genome &genome) {
    uint64_t hash = 0;
    
    // Hash action sequence (existing logic but improved)
    for (int i = 0; i < 350; ++i) {
        if (genome.actions[i] == -1 || genome.actions[i] == 0) {
            break;
        }
        auto a = static_cast<uint64_t>(static_cast<uint32_t>(genome.actions[i]));
        hash = mixHash(hash, a);
        // Include position information to maintain sequence order
        hash = mixHash(hash, static_cast<uint64_t>(i));
    }
    
    // Add critical game state information to prevent hash collisions
    // Player HP and MP states
    hash = mixHash(hash, static_cast<uint64_t>(genome.AllyPlayer.hp) << 32);
    hash = mixHash(hash, static_cast<uint64_t>(genome.EnemyPlayer.hp) << 16);
    hash = mixHash(hash, static_cast<uint64_t>(genome.AllyPlayer.mp) << 8);
    
    // Turn and position information
    hash = mixHash(hash, static_cast<uint64_t>(genome.turn));
    hash = mixHash(hash, static_cast<uint64_t>(genome.position) << 4);
    hash = mixHash(hash, genome.state);
    
    // Important status effects that affect battle outcome
    hash = mixHash(hash, (genome.AllyPlayer.paralysis ? 1ULL : 0ULL) << 48);
    hash = mixHash(hash, (genome.AllyPlayer.sleeping ? 1ULL : 0ULL) << 47);
    
    // // Buff levels that significantly impact combat
    // hash = mixHash(hash, static_cast<uint64_t>(genome.AllyPlayer.BuffLevel) << 44);
    // hash = mixHash(hash, static_cast<uint64_t>(genome.AllyPlayer.AtkBuffLevel) << 40);
    // hash = mixHash(hash, static_cast<uint64_t>(genome.AllyPlayer.TensionLevel) << 36);
    //
    // Special abilities and charges
    hash = mixHash(hash, (genome.AllyPlayer.specialCharge ? 1ULL : 0ULL) << 35);
    hash = mixHash(hash, static_cast<uint64_t>(genome.AllyPlayer.specialChargeTurn) << 24);
    
    return hash;
}

uint64_t EnhancedHashCalculator::mixHash(uint64_t hash, uint64_t value, uint64_t multiplier) {
    return hash ^ (value + multiplier + (hash << 6) + (hash >> 2));
}