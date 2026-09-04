#ifndef NEWDIRECTORY_EXACTDECISIONPROOF_H
#define NEWDIRECTORY_EXACTDECISIONPROOF_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Player.h"

struct ExactDecisionProofResult {
    bool reachable = false;
    uint64_t expandedStates = 0;
    uint64_t generatedTransitions = 0;
    double elapsedMilliseconds = 0.0;
    std::vector<std::size_t> frontierSizes;
};

class ExactDecisionProof {
public:
    // Decides whether enemy.hp == 0 is reachable in at most `horizon`
    // legal hero turns from `initialPlayers` for the fixed LCG seed.
    static ExactDecisionProofResult Run(const Player initialPlayers[2], uint64_t seed, int horizon);
};

#endif // NEWDIRECTORY_EXACTDECISIONPROOF_H