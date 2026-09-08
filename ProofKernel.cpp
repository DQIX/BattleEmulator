#include "ProofKernel.h"
#include "BattleEmulator.h"
#include "lcg.h"
#include <bit>
#include <tuple>

namespace proof {
namespace {
std::vector<Int> rawFields(const Player& p) {
    return {p.hp,p.maxHp,p.atk,p.defaultATK,p.def,p.defaultDEF,p.speed,p.defaultSpeed,
        p.HealPower,p.mp,p.maxMp,p.specialCharge,p.dirtySpecialCharge,p.specialChargeTurn,
        p.paralysis,p.paralysisLevel,p.paralysisTurns,p.SpecialMedicineCount,std::bit_cast<Int>(p.defence),
        p.sleeping,p.sleepingTurn,p.BuffLevel,p.BuffTurns,p.hasMagicMirror,p.MagicMirrorTurn,
        p.AtkBuffLevel,p.AtkBuffTurn,p.TensionLevel,p.rage,p.SageElixirCount,p.ElfinElixirCount,
        p.MagicWaterCount,p.speedTurn,p.speedLevel,p.PoisonTurn,p.PoisonEnable,p.SpecialAntidoteCount,
        p.acrobaticStar,p.acrobaticStarTurn,p.rageTurns,p.medicinal_herbs_count,p.inactive};
}
void checkMasks(const Box& b) {
    for(int j=0;j<6;++j) if(b.mode[j]&~((1u<<modeSizes[j])-1))
        throw std::invalid_argument("undefined mode bit");
}
std::array<Int,4> gains(const Box& guard,const std::array<Value,10>& out) {
    std::array<Int,4> result;
    for(int j=0;j<4;++j) {
        auto x=j==0?guard.resource[j].hi:guard.resource[j].lo;
        const auto& v=out[j];
        if(v.kind!=Value::Constant && (v.kind!=Value::Resource || v.axis!=j))
            throw std::logic_error("weight resource type");
        auto next=v.kind==Value::Constant?v.offset:add(x,v.offset);
        result[j]=j==0?add(x,neg(next)):add(next,neg(x));
    }
    return result;
}
Int weight(const Edge& e,Prices p) {
    Int n=mul(p.Q,e.gain[0]);
    for(int j=0;j<3;++j) n=add(n,mul(p.resource[j],e.gain[j+1]));
    return n;
}
void validPrices(Prices p) {
    if(p.Q<=0) throw std::invalid_argument("nonpositive price denominator");
    for(auto x:p.resource) if(x<0) throw std::invalid_argument("negative resource price");
}
}
Box alpha(const BattleState& s) {
    const auto& a=s.players[0];const auto& e=s.players[1];Box b;
    b.resource={Interval{e.hp,e.hp},{a.hp,a.hp},{a.mp,a.mp},{a.medicinal_herbs_count,a.medicinal_herbs_count}};
    std::array<Int,6> mode{a.specialCharge?a.specialChargeTurn:-1,a.paralysis?a.paralysisTurns:5,
        a.acrobaticStar?a.acrobaticStarTurn:0,e.rage?e.rageTurns:0,a.inactive,Int((s.nowState>>8)&15)};
    for(int j=0;j<6;++j) {
        auto first=modeValues[j].begin(),last=first+modeSizes[j];auto it=std::find(first,last,mode[j]);
        if(it==last) throw std::invalid_argument("raw state outside mode enum");
        b.mode[j]=1u<<std::distance(first,it);
    }
    return b;
}
Box baseBox(const Problem& p) {
    Box b;b.resource={Interval{1,456},{1,65},{0,p.root.players[0].mp},{0,p.root.players[0].medicinal_herbs_count}};
    for(int j=0;j<6;++j) b.mode[j]=(1u<<modeSizes[j])-1;
    return b;
}
Box selectable(Box b,int command) {
    using B=BattleEmulator;
    switch(command) {
        case B::HEAL:b.resource[2].lo=std::max<Int>(b.resource[2].lo,2);break;
        case B::CRACK_ALLY:b.resource[2].lo=std::max<Int>(b.resource[2].lo,3);break;
        case B::MEDICINAL_HERBS:b.resource[3].lo=std::max<Int>(b.resource[3].lo,1);break;
        case B::ACROBATIC_STAR:b.mode[0]&=0xfc;b.mode[2]&=1;break;
        case B::FLEE_ALLY:b.mode[1]&=1;break;
        case B::ATTACK_ALLY:case B::DRAGON_SLASH:case B::DEFENCE:break;
        default:b.resource[0]={1,0};break;
    }
    return b;
}
void validateProblem(const Problem& p,int h,const RegisteredRules& rules) {
    const auto& a=p.root.players[0];const auto& e=p.root.players[1];
    if(h<0 || h>350 || p.seed==0 || p.root.position<1 ||
        add(p.root.position,mul(h,rules.maxDraws))>4999 ||
        add(Int((p.root.nowState>>12)&0xfffff),h)>=0xfffff)
        throw std::invalid_argument("problem exceeds the existing emulator's seed/turn/RNG domain");
    if(a.maxHp!=65 || e.maxHp!=456 || a.atk!=61 || a.defaultATK!=61 || a.def!=66 || a.defaultDEF!=66 ||
        a.speed!=40 || a.defaultSpeed!=40 || e.atk!=56 || e.defaultATK!=56 || e.def!=58 ||
        e.defaultDEF!=58 || e.speed!=54 || e.defaultSpeed!=54 ||
        a.hp<0 || a.hp>65 || e.hp<0 || e.hp>456 || a.mp<0 || a.mp>22 ||
        a.medicinal_herbs_count<0 || a.medicinal_herbs_count>7 ||
        a.sleeping || e.sleeping || e.paralysis || e.inactive || e.specialCharge || e.acrobaticStar ||
        a.specialChargeTurn<std::numeric_limits<int>::min()+h ||
        a.acrobaticStarTurn<std::numeric_limits<int>::min()+h ||
        a.paralysisLevel>std::numeric_limits<int>::max()-h)
        throw std::invalid_argument("unsupported battle profile or unsafe raw timer");
    auto state=alpha(p.root);
    if(a.hp && e.hp && !baseBox(p).contains(state)) throw std::invalid_argument("root outside BaseBox");
}
bool sameProblem(const Problem& a,const Problem& b) {
    return a.seed==b.seed && a.root.position==b.root.position && a.root.nowState==b.root.nowState &&
        rawFields(a.root.players[0])==rawFields(b.root.players[0]) && rawFields(a.root.players[1])==rawFields(b.root.players[1]);
}
BattleState initialState() {
    BattleState s;
    s.players[0]=Player{65,65,61,61,66,66,40,40,29,22,22,false,false,0,false,0,-1,
        8,1.0,false,-1,0,-1,false,-1,0,-1,0,false,1,1,1,-1,0,-1,false,2,false,-1,-1,7,false};
    s.players[1]=Player{456,456,56,56,58,58,54,54,0,255,255,false,false,0,false,0,-1,
        0,1.0,false,-1,0,-1,false,-1,0,-1,0,false,0,0,0,-1,0,-1,false,2,false,-1,-1,7,false};
    return s;
}
int Partition::classify(const Box& point) const {
    if(nodes.empty() || !nodes[0].box.contains(point)) throw std::logic_error("classification outside BaseBox");
    int id=0;
    while(!nodes.at(id).leaf()) {
        const auto& n=nodes[id];
        if(nodes.at(n.yes).box.contains(point)) id=n.yes;
        else if(nodes.at(n.no).box.contains(point)) id=n.no;
        else throw std::logic_error("classification needs an output preimage split");
    }
    return id;
}
bool Partition::split(int id,Predicate predicate,unsigned limit) {
    if(id<0 || id>=int(nodes.size()) || !nodes[id].leaf() || leafCount>=limit) return false;
    auto yes=restrictBox(nodes[id].box,predicate,true),no=restrictBox(nodes[id].box,predicate,false);
    if(yes.empty() || no.empty()) return false;
    nodes[id].predicate=predicate;nodes[id].yes=int(nodes.size());nodes[id].no=int(nodes.size()+1);
    nodes.push_back({yes,{},-1,-1});nodes.push_back({no,{},-1,-1});++leafCount;return true;
}
void Partition::verify(const Box& base,unsigned limit,Budget& budget) const {
    if(nodes.empty() || !(nodes[0].box==base) || leafCount>limit || nodes.size()!=2*leafCount-1)
        throw std::invalid_argument("partition root/count");
    std::vector<unsigned char> seen(nodes.size());std::vector<int> work{0};unsigned leaves=0;
    while(!work.empty()) {
        budget.tick();int id=work.back();work.pop_back();
        if(id<0 || id>=int(nodes.size()) || seen[id]++) throw std::invalid_argument("partition cycle/shared child");
        const auto& n=nodes[id];checkMasks(n.box);
        if(n.box.empty()) throw std::invalid_argument("empty partition node");
        if(n.leaf()) {++leaves;continue;}
        if(n.yes<0 || n.no<0 || n.yes>=int(nodes.size()) || n.no>=int(nodes.size()))
            throw std::invalid_argument("partition successor");
        if(!(restrictBox(n.box,n.predicate,true)==nodes[n.yes].box) ||
           !(restrictBox(n.box,n.predicate,false)==nodes[n.no].box))
            throw std::invalid_argument("partition does not cover both predicate sides");
        work.push_back(n.no);work.push_back(n.yes);
    }
    if(leaves!=leafCount || std::find(seen.begin(),seen.end(),0)!=seen.end())
        throw std::invalid_argument("orphan partition node");
}
Partitions::Partitions(const Problem& p,int h,const RegisteredRules& rules) {
    firstPosition=p.root.position;lastPosition=int(add(firstPosition,mul(h,rules.maxDraws)));
    trees.assign(lastPosition-firstPosition+1,Partition(baseBox(p)));
}
Partition& Partitions::at(int p) {
    if(p<firstPosition || p>lastPosition) throw std::logic_error("undefined RNG partition");
    return trees.at(p-firstPosition);
}
const Partition& Partitions::at(int p) const {
    if(p<firstPosition || p>lastPosition) throw std::logic_error("undefined RNG partition");
    return trees.at(p-firstPosition);
}
bool Partitions::refine(CellKey cell,Predicate predicate,unsigned limit) {
    if(!at(cell.position).split(cell.local,predicate,limit)) return false;
    ++version;return true;
}
const LocalProof* Coverage::containing(int p,int c,const Box& d,bool detailedOnly) const {
    if(indexedRecords>records.size()) {lookup.clear();indexedRecords=0;}
    while(indexedRecords<records.size()) {
        const auto& r=records[indexedRecords];lookup[{r.position,r.command}].push_back(indexedRecords++);
    }
    const auto found=lookup.find({p,c});if(found==lookup.end()) return nullptr;
    for(int pass=0;pass<(detailedOnly?1:2);++pass) for(std::size_t n=found->second.size();n!=0;) {
        auto id=found->second[--n];const auto& r=records.at(id);
        if(r.detailed==(pass==0) && r.position==p && r.command==c && r.domain.contains(d)) return &r;
    }
    return nullptr;
}
std::size_t Coverage::require(int p,int c,const Box& d,Budget& budget) {
    budget.tick();
    if(auto found=containing(p,c,d)) return std::size_t(found-records.data());
    budget.memory(bytes()+sizeof(LocalProof)*2*(records.size()+1));
    records.push_back({p,c,d,false,{}});++version;return records.size()-1;
}
bool Coverage::expand(int p,int c,const Box& d,const RegisteredRules& rules,Budget& budget) {
    budget.tick();
    if(containing(p,c,d,true)) return false;
    auto leaves=step(rules,d,p,c,budget);
    budget.memory(bytes()+leaves.capacity()*sizeof(Leaf)+sizeof(LocalProof)*2*(records.size()+1));
    records.push_back({p,c,d,true,std::move(leaves)});++version;return true;
}
std::size_t Coverage::bytes() const {
    std::size_t n=records.capacity()*sizeof(LocalProof);
    for(const auto& r:records) n+=r.leaves.capacity()*sizeof(Leaf);
    for(const auto& [key,ids]:lookup) n+=128+ids.capacity()*sizeof(std::size_t);
    return n;
}
int Model::index(CellKey c) const {
    if(c.position<firstPosition || c.position>lastPosition) throw std::logic_error("undefined output position");
    const auto& map=cellIndex.at(c.position-firstPosition);
    if(c.local<0 || c.local>=int(map.size()) || map[c.local]<0) throw std::logic_error("undefined output cell");
    return map[c.local];
}
std::size_t Model::bytes() const {
    std::size_t n=cells.capacity()*sizeof(CellKey);
    n+=(edges.capacity()+support.capacity()+cellIndex.capacity())*sizeof(std::vector<int>);
    for(const auto& e:edges) n+=e.capacity()*sizeof(Edge);
    for(const auto& s:support) n+=s.capacity()*sizeof(int);
    for(const auto& s:cellIndex) n+=s.capacity()*sizeof(int);
    return n;
}

namespace {
void completionEdges(std::vector<Edge>& edges,int p,int command,const Box& domain,
                     std::size_t proof,const RegisteredRules& rules) {
    auto emit=[&](Int loss,Int recovery,Int mp,Int herb) {
        if(domain.resource[2].lo<mp || domain.resource[3].lo<herb)
            throw std::logic_error("completion refunds/overspends resource");
        Edge e;e.command=command;e.guard=domain;e.proof=proof;e.completion=true;
        e.goal=domain.resource[0].lo<=loss;
        e.firstPosition=p;e.lastPosition=p+rules.maxDraws;
        e.gain={std::min(loss,domain.resource[0].hi),std::min(recovery,65-domain.resource[1].lo),-mp,-herb};
        edges.push_back(e);
    };
    // Whitelisted TURN_ENTRY cases, kernel spec sections 8 and 15. This
    // completion has no executed prefix and cannot refund a previous payment.
    using B=BattleEmulator;
    switch(command) {
        case B::ATTACK_ALLY:emit(128,0,0,0);break;
        case B::DRAGON_SLASH:emit(98,0,0,0);break;
        case B::DEFENCE:case B::FLEE_ALLY:case B::ACROBATIC_STAR:emit(64,0,0,0);break;
        case B::CRACK_ALLY:emit(64,0,0,0);emit(98,0,3,0);break;
        case B::HEAL:emit(64,0,0,0);emit(64,65,2,0);break;
        case B::MEDICINAL_HERBS:emit(64,0,0,0);emit(64,39,0,1);break;
        default:throw std::logic_error("unregistered completion command");
    }
}
}
namespace {
Model buildSupport(const Problem& p,int h,const RegisteredRules& rules,const Partitions& partitions,
                   const Coverage& coverage,Coverage* generator,Budget& budget) {
    validateProblem(p,h,rules);
    if(partitions.firstPosition!=p.root.position || partitions.lastPosition!=p.root.position+h*rules.maxDraws ||
        partitions.trees.size()!=std::size_t(partitions.lastPosition-partitions.firstPosition+1))
        throw std::invalid_argument("partition family position set");
    Model m;m.horizon=h;m.firstPosition=partitions.firstPosition;m.lastPosition=partitions.lastPosition;
    m.partitionVersion=partitions.version;m.coverageVersion=coverage.version;
    m.cellIndex.resize(partitions.trees.size());
    for(int pos=m.firstPosition;pos<=m.lastPosition;++pos) {
        const auto& tree=partitions.at(pos);tree.verify(baseBox(p),budget.limits.maxCellsPerPosition,budget);
        auto& map=m.cellIndex[pos-m.firstPosition];map.assign(tree.nodes.size(),-1);
        for(int id=0;id<int(tree.nodes.size());++id) if(tree.nodes[id].leaf()) {
            map[id]=int(m.cells.size());m.cells.push_back({pos,id});
        }
    }
    m.root=m.index({p.root.position,partitions.at(p.root.position).classify(alpha(p.root))});
    m.edges.resize(m.cells.size());m.support.resize(h+1);m.support[0]={m.root};
    std::vector<unsigned char> built(m.cells.size());
    for(int t=0;t<h;++t) {
        std::vector<unsigned char> next(m.cells.size());
        std::vector<int> ranges(m.lastPosition-m.firstPosition+2);
        for(int q:m.support[t]) {
            budget.tick();const auto cell=m.cells[q];
            if(cell.position>p.root.position+t*rules.maxDraws) throw std::logic_error("support outside envelope");
            if(!built[q]) {
                const auto domain=partitions.at(cell.position).nodes[cell.local].box;
                for(int command:selectionOrder) {
                    const auto d=selectable(domain,command);budget.tick();if(d.empty()) continue;
                    std::size_t record;
                    if(!generator) {
                        budget.tick();auto found=coverage.containing(cell.position,command,d);
                        if(!found) throw std::invalid_argument("missing nonempty command proof root");record=std::size_t(found-coverage.records.data());
                    } else record=generator->require(cell.position,command,d,budget);
                    const auto& local=coverage.records[record];
                    if(!local.detailed) {completionEdges(m.edges[q],cell.position,command,d,record,rules);continue;}
                    for(std::size_t leafId=0;leafId<local.leaves.size();++leafId) {
                        budget.tick();const auto& leaf=local.leaves[leafId];
                        auto guard=d.intersect(leaf.guard);if(guard.empty()) continue;
                        if(leaf.terminal==Terminal::Invalid) throw std::logic_error("Invalid cannot discharge a legal proof root");
                        // A checked violation of the user's skipTurn assertion
                        // is recorded as a forbidden execution, not omitted.
                        if(leaf.terminal==Terminal::ForbiddenFlee) {
                            if(command!=BattleEmulator::FLEE_ALLY) throw std::logic_error("assertion on another command");
                            Edge edge;edge.command=command;edge.guard=guard;edge.dead=true;
                            edge.proof=record;edge.leaf=leafId;m.edges[q].push_back(std::move(edge));continue;
                        }
                        auto out=image(guard,leaf.output);
                        for(int j=0;j<4;++j) if(out.resource[j].lo<0 || out.resource[j].hi>(j==0?456:j==1?65:baseBox(p).resource[j].hi))
                            throw std::logic_error("resource preservation outside envelope");
                        if(leaf.position<cell.position || leaf.position>cell.position+rules.maxDraws)
                            throw std::logic_error("local output RNG contract");
                        auto emit=[&](Box part,int target,bool goal,bool dead) {
                            Edge edge;edge.command=command;edge.guard=part;edge.gain=gains(part,leaf.output);
                            edge.target=target;edge.goal=goal;edge.dead=dead;edge.proof=record;edge.leaf=leafId;
                            m.edges[q].push_back(std::move(edge));
                        };
                        if(leaf.terminal==Terminal::Goal) {emit(guard,-1,true,false);continue;}
                        if(leaf.terminal==Terminal::Dead) {emit(guard,-1,false,true);continue;}
                        if(!baseBox(p).contains(out)) throw std::logic_error("continuation outside envelope before clipping");
                        const auto& targetTree=partitions.at(leaf.position);
                        for(int id=0;id<int(targetTree.nodes.size());++id) if(targetTree.nodes[id].leaf()) {
                            budget.tick();auto part=preimage(guard,leaf.output,targetTree.nodes[id].box);
                            if(!part.empty()) emit(part,m.index({leaf.position,id}),false,false);
                        }
                    }
                }
                built[q]=1;
            }
            for(const auto& edge:m.edges[q]) {
                budget.tick();if(edge.dead) continue;
                if(edge.completion) {
                    if(edge.firstPosition<m.firstPosition || edge.lastPosition>p.root.position+(t+1)*rules.maxDraws)
                        throw std::logic_error("completion outside next envelope");
                    ++ranges.at(edge.firstPosition-m.firstPosition);
                    --ranges.at(edge.lastPosition-m.firstPosition+1);
                } else if(!edge.goal) {
                    if(edge.target<0) throw std::logic_error("missing detailed destination");next[edge.target]=1;
                }
            }
            budget.memory(m.bytes()+coverage.bytes()+next.capacity()+built.capacity());
        }
        int active=0;
        for(int pos=m.firstPosition;pos<=m.lastPosition;++pos) {
            budget.tick();active+=ranges[pos-m.firstPosition];
            if(active) for(int q:m.cellIndex[pos-m.firstPosition]) if(q>=0) next[q]=1;
        }
        for(int q=0;q<int(next.size());++q) if(next[q]) m.support[t+1].push_back(q);
    }
    m.coverageVersion=coverage.version;
    return m;
}
}
Model rebuildSupport(const Problem& p,int h,const RegisteredRules& rules,const Partitions& partitions,
                     Coverage& coverage,Budget& budget,bool readOnlyProof) {
    return buildSupport(p,h,rules,partitions,coverage,readOnlyProof?nullptr:&coverage,budget);
}

namespace {
void maximum(Bound& current,Bound candidate) {
    current=std::max(current,candidate);
}
struct RangeMaximum {
    int start=0,count=0,width=1;
    std::vector<Bound> tree;
    std::vector<int> missing;
    RangeMaximum(const Model& m,const std::vector<int>& support,const std::vector<Bound>& values,Budget& budget) {
        start=m.firstPosition;count=m.lastPosition-start+1;while(width<count) width*=2;
        tree.assign(width*2,unreachable);missing.resize(count+1);std::vector<int> present(count);
        for(auto q:support) {budget.tick();int i=m.cells[q].position-start;++present[i];maximum(tree[width+i],values[q]);}
        for(int i=0;i<count;++i) {
            unsigned leaves=0;for(int q:m.cellIndex[i]) if(q>=0) ++leaves;
            missing[i+1]=missing[i]+(present[i]!=int(leaves));
        }
        for(int i=width-1;i>0;--i) {tree[i]=tree[i*2];maximum(tree[i],tree[i*2+1]);}
        budget.tick(tree.size()+missing.size());
    }
    Bound query(int lo,int hi,Budget& budget) const {
        if(lo<start || hi>=start+count || lo>hi || missing[hi-start+1]!=missing[lo-start])
            throw std::logic_error("completion target missing from Support/DP");
        Bound result=unreachable;int l=lo-start+width,r=hi-start+width+1;
        while(l<r) {budget.tick();if(l&1) maximum(result,tree[l++]);if(r&1) maximum(result,tree[--r]);l/=2;r/=2;}
        return result;
    }
};
}
DynamicProgram maxPlus(const Model& m,Prices prices,Budget& budget,bool distances) {
    validPrices(prices);DynamicProgram dp;
    const auto n=m.cells.size();
    budget.memory(m.bytes()+(m.horizon+1)*n*(sizeof(Bound)+(distances?sizeof(int):0)));
    dp.bound.assign(m.horizon+1,std::vector<Bound>(n,unreachable));
    if(distances) dp.distance.assign(m.horizon+1,std::vector<int>(n,-1));
    // Live B_0 is -infinity. GOAL is implicit zero at every layer.
    for(int t=m.horizon-1;t>=0;--t) {
        RangeMaximum range(m,m.support[t+1],dp.bound[t+1],budget);
        std::vector<Bound> negativeDistance;
        std::unique_ptr<RangeMaximum> distanceRange;
        if(distances) {
            negativeDistance.assign(n,unreachable);
            for(int q:m.support[t+1]) if(dp.distance[t+1][q]>=0) negativeDistance[q]=-dp.distance[t+1][q];
            distanceRange=std::make_unique<RangeMaximum>(m,m.support[t+1],negativeDistance,budget);
        }
        std::vector<unsigned char> present(n);for(int q:m.support[t+1]) present[q]=1;
        for(int q:m.support[t]) {
            Bound best=unreachable;int length=-1;
            for(const auto& e:m.edges[q]) {
                budget.tick();if(e.dead) continue;
                Bound continuation=e.goal?0:unreachable;
                Bound dist=e.goal?0:unreachable;
                if(e.completion) {
                    maximum(continuation,range.query(e.firstPosition,e.lastPosition,budget));
                    if(distances) maximum(dist,distanceRange->query(e.firstPosition,e.lastPosition,budget));
                } else if(!e.goal) {
                    if(e.target<0 || !present.at(e.target)) throw std::logic_error("detailed target missing from Support");
                    continuation=dp.bound[t+1][e.target];
                    if(distances && dp.distance[t+1][e.target]>=0) dist=-dp.distance[t+1][e.target];
                }
                maximum(best,extendBound(weight(e,prices),continuation));
                if(distances && dist!=unreachable) {
                    int proposed=1-int(dist);if(length<0 || proposed<length) length=proposed;
                }
            }
            dp.bound[t][q]=best;if(distances) dp.distance[t][q]=length;
        }
    }
    dp.root=dp.bound[0].at(m.root);return dp;
}
bool excludesWin(const Problem& p,const DynamicProgram& dp,Prices prices) {
    validPrices(prices);if(dp.root==unreachable) return true;
    auto s=alpha(p.root);Int bound=dp.root;
    for(int j=0;j<3;++j) bound=add(bound,mul(prices.resource[j],s.resource[j+1].lo));
    return mul(prices.Q,s.resource[0].lo)>bound;
}
bool verifyFalse(const Problem& p,const Certificate& c,Budget& budget) {
    auto rules=battleRules();budget.tick();
    if(c.ruleVersion!=rules->version || c.ruleIdentity!=rules->identity || !sameProblem(p,c.problem)) return false;
    validateProblem(p,c.horizon,*rules);validPrices(c.prices);
    if(p.root.players[1].hp==0) return false;
    if(c.horizon==0 || p.root.players[0].hp==0) return c.bound.empty() && c.coverage.records.empty();
    lcg::init(p.seed,true);
    for(const auto& record:c.coverage.records) {
        budget.tick();checkMasks(record.domain);
        if(record.domain.empty() || !baseBox(p).contains(record.domain) ||
            !(selectable(record.domain,record.command)==record.domain)) return false;
        if(record.detailed) {
            auto computed=step(*rules,record.domain,record.position,record.command,budget);
            if(computed!=record.leaves) return false;
        } else if(!record.leaves.empty()) return false;
    }
    // Rebuild every required root and all layers from the submitted partition
    // family. A missing command proof is an error, never an absent edge.
    c.coverage.lookup.clear();c.coverage.indexedRecords=0;
    auto m=buildSupport(p,c.horizon,*rules,c.partitions,c.coverage,nullptr,budget);
    auto dp=maxPlus(m,c.prices,budget,false);
    return dp.bound==c.bound && excludesWin(p,dp,c.prices);
}
}
