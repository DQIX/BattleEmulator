#ifndef YO2_SUFFIX_PROOF_H
#define YO2_SUFFIX_PROOF_H
#include "ProofKernel.h"
#include "RegionProof.h"

namespace proof {
enum class Status { Optimal, Win, ProvedFalse, Unknown, Error };
struct Statistics {
    std::uint64_t work=0,candidateScans=0;
    std::size_t peakBytes=0,cells=0,edges=0;
    unsigned candidates=0,refinements=0,prices=0;
    std::chrono::milliseconds elapsed{};
};
struct Result {
    Status status=Status::Unknown;
    int minimum=-1,provedFalseThrough=-1;
    std::vector<int> prefix,commands;
    Problem problem;
    std::unique_ptr<Certificate> certificate;
    std::unique_ptr<RegionCertificate> regionCertificate;
    Statistics statistics;
    std::string detail;
};
// A caller supplies the seed and exact raw turn boundary. H is a suffix bound.
// These functions link into the same native/WASM module as BattleEmulator.
Result solveSuffix(const Problem& problem,int horizon,const Limits& limits={});
Result extendPrefix(const Problem& initial,std::span<const int> prefix,int horizon,const Limits& limits={});
bool replayWin(const Problem& problem,std::span<const int> commands,const Limits& limits={});
}
#endif
