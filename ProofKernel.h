#ifndef YO2_PROOF_KERNEL_H
#define YO2_PROOF_KERNEL_H
#include "ProofProgram.h"
#include "Player.h"
#include <map>
#include <set>

namespace proof {
inline constexpr std::array<int,8> selectionOrder{25,59,27,53,23,26,61,56};
struct BattleState {
    Player players[2]{};
    int position=1;
    std::uint64_t nowState=0;
};
struct Problem {
    std::uint64_t seed=0;
    BattleState root;
};
Box alpha(const BattleState& state);
Box baseBox(const Problem& problem);
Box selectable(Box box,int command);
void validateProblem(const Problem& problem,int horizon,const RegisteredRules& rules);
bool sameProblem(const Problem& a,const Problem& b);
BattleState initialState();

struct CellKey {
    int position=0,local=0;
    auto operator<=>(const CellKey&) const = default;
};
struct PartitionNode {
    Box box;
    Predicate predicate;
    int yes=-1,no=-1;
    bool leaf() const {return yes==-1 && no==-1;}
    bool operator==(const PartitionNode&) const = default;
};
struct Partition {
    std::vector<PartitionNode> nodes;
    unsigned leafCount=1;
    explicit Partition(Box base={}) {nodes.push_back({base,{ },-1,-1});}
    int classify(const Box& point) const;
    bool split(int local,Predicate predicate,unsigned limit);
    void verify(const Box& base,unsigned limit,Budget& budget) const;
};
struct Partitions {
    int firstPosition=0,lastPosition=0;
    std::uint64_t version=1;
    std::vector<Partition> trees;
    Partitions()=default;
    Partitions(const Problem& p,int horizon,const RegisteredRules& rules);
    Partition& at(int p);
    const Partition& at(int p) const;
    bool refine(CellKey cell,Predicate predicate,unsigned limit);
};
struct LocalProof {
    int position=0,command=0;
    Box domain;
    bool detailed=false;
    std::vector<Leaf> leaves;
};
// Cache entries are guarded rules, independent of local cell ids and prices.
// Full data are rechecked by a fresh verifier; no submitted checked flag exists.
struct Coverage {
    std::uint64_t version=1;
    std::vector<LocalProof> records;
    mutable std::map<std::pair<int,int>,std::vector<std::size_t>> lookup;
    mutable std::size_t indexedRecords=0;
    const LocalProof* containing(int p,int command,const Box& domain,bool detailedOnly=false) const;
    std::size_t require(int p,int command,const Box& domain,Budget& budget);
    bool expand(int p,int command,const Box& domain,const RegisteredRules& rules,Budget& budget);
    std::size_t bytes() const;
};
struct Edge {
    int command=0;
    Box guard;
    std::array<Int,4> gain{}; // E-E', A'-A, M'-M, I'-I upper vector
    bool completion=false,goal=false,dead=false;
    int target=-1,firstPosition=0,lastPosition=0;
    std::size_t proof=0,leaf=0;
};
struct Model {
    int horizon=0,firstPosition=0,lastPosition=0;
    std::uint64_t partitionVersion=0,coverageVersion=0;
    std::vector<CellKey> cells;
    std::vector<std::vector<int>> cellIndex;
    std::vector<std::vector<int>> support;
    std::vector<std::vector<Edge>> edges;
    int root=-1;
    int index(CellKey cell) const;
    std::size_t bytes() const;
};
Model rebuildSupport(const Problem& problem,int horizon,const RegisteredRules& rules,
                     const Partitions& partitions,Coverage& coverage,Budget& budget,
                     bool readOnlyProof=false);
struct Prices {
    Int Q=256;
    std::array<Int,3> resource{};
    bool operator==(const Prices&) const = default;
};
using Bound=Int;
inline constexpr Bound unreachable=std::numeric_limits<Int>::min();
// The sentinel is tested before every addition; it is never an operand.
inline Bound extendBound(Int weight,Bound tail) {
    if(tail==unreachable) return unreachable;
    auto value=add(weight,tail);
    if(value==unreachable) throw std::overflow_error("finite bound collides with unreachable tag");
    return value;
}
struct DynamicProgram {
    std::vector<std::vector<Bound>> bound;
    std::vector<std::vector<int>> distance;
    Bound root=unreachable;
};
DynamicProgram maxPlus(const Model& model,Prices prices,Budget& budget,bool withDistances=true);
bool excludesWin(const Problem& problem,const DynamicProgram& dp,Prices prices);
struct Certificate {
    std::uint64_t ruleVersion=0;
    Problem problem;
    int horizon=0;
    Partitions partitions;
    Coverage coverage;
    Prices prices;
    std::vector<std::vector<Bound>> bound;
    RuleIdentity ruleIdentity;
};
bool verifyFalse(const Problem& problem,const Certificate& certificate,Budget& budget);
}
#endif
