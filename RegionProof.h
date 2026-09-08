#ifndef YO2_REGION_PROOF_H
#define YO2_REGION_PROOF_H
#include "ProofKernel.h"

namespace proof {
// A hotel room is a Box of inputs, not a stored combat individual. Its eight
// obligations cover every selectable command. Children have one less turn.
// -1: command not selectable; -2: checked dead/assertion leaf; -3: base bound.
struct RegionNode {
    Box domain;
    int position=0,remaining=0;
    std::array<int,8> children{};
};
struct RegionCertificate {
    RuleIdentity rules;
    Problem problem;
    int horizon=0,root=-1;
    std::vector<RegionNode> nodes;
};
struct RegionSearch {
    std::vector<int> commands;
    std::unique_ptr<RegionCertificate> negative;
    int provedFalseThrough=-1;
    std::size_t rooms=0,transitions=0;
};
// Backwards guard propagation refines only the rooms needed by this query.
// All rejection decisions are replayed by verifyRegions over entire Boxes.
RegionSearch searchRegions(const Problem&,int,const RegisteredRules&,Budget&,
                          const std::function<void(std::span<const int>)>&);
bool verifyRegions(const Problem&,const RegionCertificate&,Budget&);
}
#endif
