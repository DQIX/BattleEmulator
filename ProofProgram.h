#ifndef YO2_PROOF_PROGRAM_H
#define YO2_PROOF_PROGRAM_H
#include "ProofDomain.h"
#include <chrono>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace proof {
inline constexpr int slotCount=64;
enum class Native { Percent, Initiative, AttackDamage, HerbDamage, SpellDamage,
                    CriticalAttack, CriticalSpell, HalfDamage, VictimDamage,
                    ParalysisRelease, ChargeChance };
enum class Op { Set, Copy, Add, CompareLE, Jump, Call, Return, RandomSkip, NativeCall,
                Finish, Reject, FleeGuard };
struct Instruction {
    Op op=Op::Reject;
    int dst=0, x=0, y=0;
    Int immediate=0;
    int yes=0, no=0;
    Native native=Native::Percent;
    std::vector<int> args;
    std::string source;
    bool operator==(const Instruction&) const = default;
};
struct Routine { std::string name;std::vector<Instruction> code;bool operator==(const Routine&) const = default; };
struct RuleProgram { std::vector<Routine> routines;int entry=0;bool operator==(const RuleProgram&) const = default; };
struct RuleIdentity {
    std::string registry="yo2_be/asserted-turn";
    std::uint64_t version=2;
    RuleProgram program;
    // Explicit semantic ABI versions, not source-file hashes.
    std::array<unsigned,5> contracts{1,1,2,1,1}; // native, profile, assertion, alpha, replay
    bool operator==(const RuleIdentity&) const = default;
};
struct RegisteredRules {
    const RuleProgram program;
    const int maxDraws;
    const std::uint64_t maxSteps;
    const unsigned maxDepth;
    const std::uint64_t version;
    const RuleIdentity identity;
};
struct Limits {
    bool regional=false;
    int maxTurns=30;
    unsigned maxCellsPerPosition=8;
    unsigned maxLeavesPerRoot=8192;
    std::uint64_t maxWork=8'000'000;
    std::size_t maxBytes=128u*1024u*1024u;
    std::chrono::milliseconds time=std::chrono::seconds(15);
    unsigned maxCandidates=2048, maxRefinements=16, maxPrices=8;
    std::uint64_t maxCandidateScans=2'000'000;
};
struct Exhausted {};
struct Budget {
    const Limits& limits;
    std::chrono::steady_clock::time_point deadline;
    std::uint64_t work=0;
    std::size_t peakBytes=0;
    std::uint64_t candidateScans=0;
    unsigned candidates=0,refinements=0,prices=0;
    explicit Budget(const Limits& l):limits(l),deadline(std::chrono::steady_clock::now()+l.time){}
    void tick(std::uint64_t n=1) {
        if (n>limits.maxWork-std::min(work,limits.maxWork)) throw Exhausted{};
        work+=n;
        if (std::chrono::steady_clock::now()>=deadline) throw Exhausted{};
    }
    void memory(std::size_t bytes) {
        peakBytes=std::max(peakBytes,bytes);
        if (bytes>limits.maxBytes) throw Exhausted{};
    }
};
std::shared_ptr<const RegisteredRules> registerRules(RuleProgram program);
std::shared_ptr<const RegisteredRules> battleRules();
Int evaluateNative(Native native,std::span<const Int> arguments,int& position);
unsigned nativeDraws(Native native);
unsigned nativeArity(Native native);

enum class Terminal { Continue, Goal, Dead, Invalid, ForbiddenFlee };
struct Leaf {
    Box guard;
    std::array<Value,10> output;
    int position=0;
    Terminal terminal=Terminal::Invalid;
    bool operator==(const Leaf&) const = default;
};
std::vector<Leaf> step(const RegisteredRules& rules,const Box& input,int position,
                       int command,Budget& budget);
// Follow one concrete point while retaining the full input guard and affine
// output of its execution path. The returned guard is subsequently verified.
Leaf trace(const RegisteredRules& rules,const Box& domain,const Box& point,
           int position,int command,Budget& budget);

// Small assembler: labels are routine-local; all call targets are fixed.
class Assembler {
    RuleProgram program;
    int current=-1;
    std::vector<std::pair<int,std::string>> unresolved;
    std::vector<std::pair<std::string,int>> labels;
public:
    int declare(const std::string& name);
    void begin(int routine);
    void label(const std::string& name);
    void emit(Instruction instruction);
    void set(int dst,Int value,const char* source="");
    void copy(int dst,int src);
    void add(int dst,int src,int deltaSlot,int sign=1);
    void addConstant(int dst,int src,Int delta);
    void branch(int left,int right,const std::string& yes,const std::string& no);
    void branchConstant(int left,Int right,const std::string& yes,const std::string& no);
    void jump(const std::string& label);
    void call(int routine);
    void native(int dst,Native id,std::initializer_list<int> args);
    void skip(int n);
    void ret();
    void finish();
    void reject();
    void end();
    RuleProgram take();
};
}
#endif
