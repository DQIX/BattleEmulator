#include "RegionProof.h"
#include "lcg.h"
#include <bit>
#include <numeric>

namespace proof {
namespace {
Box illegalRegion(Box base,const Box& point,int command) {
    auto legal=selectable(base,command);
    for(int j=0;j<4;++j) {
        if(point.resource[j].lo<legal.resource[j].lo) {
            base.resource[j].hi=legal.resource[j].lo-1;return base;
        }
    }
    for(int j=0;j<6;++j) if(point.mode[j]&~legal.mode[j]) {
        base.mode[j]&=std::uint8_t(~legal.mode[j]);return base;
    }
    throw std::logic_error("no selectable complement for illegal point");
}
Box pointIn(const Box& b) {
    Box p=b;
    for(auto& r:p.resource) r.hi=r.lo;
    for(auto& m:p.mode) m=std::uint8_t(1u<<std::countr_zero(unsigned(m)));
    return p;
}
class Rooms {
    const Problem& problem;
    const RegisteredRules& rules;
    Budget& budget;
    Box base;
    std::vector<std::vector<int>> hotel;
    std::vector<std::vector<Leaf>> transitions;
    std::size_t transitionBytes=0,transitionCount=0;
    int width;
    std::vector<int> path;
public:
    std::vector<RegionNode> nodes;
    std::vector<int> witness;
    Rooms(const Problem& p,int h,const RegisteredRules& r,Budget& b)
        :problem(p),rules(r),budget(b),base(baseBox(p)),width(h+1) {
        hotel.resize(5000*width);transitions.resize(5000*8);
    }
    void memory() {
        budget.memory(nodes.capacity()*sizeof(RegionNode)+transitionBytes+
            hotel.capacity()*sizeof(std::vector<int>)+transitions.capacity()*sizeof(std::vector<Leaf>));
    }
    Leaf transition(int p,int action,const Box& point) {
        auto& list=transitions.at(p*8+action);
        for(const auto& leaf:list) {budget.tick();if(leaf.guard.contains(point)) return leaf;}
        auto leaf=trace(rules,selectable(base,selectionOrder[action]),point,p,selectionOrder[action],budget);
        auto old=list.capacity();list.push_back(leaf);
        transitionBytes+=(list.capacity()-old)*sizeof(Leaf);++transitionCount;memory();return leaf;
    }
    int remember(RegionNode node) {
        auto& room=hotel.at(node.position*width+node.remaining);
        // Only proved inclusion can evict a cached inequality. All certificate
        // nodes remain immutable, so old child references retain their meaning.
        room.erase(std::remove_if(room.begin(),room.end(),[&](int id) {
            budget.tick();return node.domain.contains(nodes[id].domain);
        }),room.end());
        int id=int(nodes.size());nodes.push_back(std::move(node));room.push_back(id);memory();return id;
    }
    int decide(int p,int h,const Box& point) {
        budget.tick();
        const auto& room=hotel.at(p*width+h);
        for(auto it=room.rbegin();it!=room.rend();++it) {
            budget.tick();if(nodes[*it].domain.contains(point)) return *it;
        }
        RegionNode node;node.position=p;node.remaining=h;node.domain=base;node.children.fill(-1);
        // The registered TURN_ENTRY completion proves at most 128 damage per
        // turn, including one counter. Enemy recovery only lowers net damage.
        if(h==0 || point.resource[0].lo>mul(128,h)) {
            node.domain.resource[0].lo=std::max<Int>(1,add(mul(128,h),1));
            node.children.fill(-3);return remember(std::move(node));
        }
        struct Option {int action;Leaf leaf;Box output;};
        std::vector<Option> options;options.reserve(8);
        for(int a=0;a<8;++a) {
            int c=selectionOrder[a];
            if(selectable(point,c).empty()) {
                node.domain=node.domain.intersect(illegalRegion(base,point,c));continue;
            }
            auto leaf=transition(p,a,point);
            if(leaf.terminal==Terminal::Invalid) throw std::logic_error("Invalid cannot prove a region");
            auto out=leaf.terminal==Terminal::ForbiddenFlee?point:image(point,leaf.output);
            options.push_back({a,std::move(leaf),out});
        }
        // Ordering proposes a witness; it never drops a proof obligation.
        std::stable_sort(options.begin(),options.end(),[](const Option& a,const Option& b) {
            auto score=[](const Option& x) {
                if(x.leaf.terminal==Terminal::Goal) return Int(-1000000);
                if(x.leaf.terminal!=Terminal::Continue) return Int(1000000);
                return 16*x.output.resource[0].lo-3*x.output.resource[1].lo-
                    8*x.output.resource[2].lo-40*x.output.resource[3].lo-
                    (x.output.mode[2]>1?180:0)-(x.output.mode[0]>2?50:0);
            };
            return score(a)<score(b);
        });
        for(const auto& option:options) {
            const auto& leaf=option.leaf;
            path.push_back(selectionOrder[option.action]);
            if(leaf.terminal==Terminal::Goal) {witness=path;path.pop_back();return -1;}
            Box valid=leaf.guard;
            if(leaf.terminal==Terminal::Continue) {
                if(!base.contains(image(leaf.guard,leaf.output))) throw std::logic_error("region output preservation");
                int child=decide(leaf.position,h-1,option.output);
                if(child<0) {path.pop_back();return -1;}
                node.children[option.action]=child;
                valid=preimage(valid,leaf.output,nodes[child].domain);
            } else {
                node.children[option.action]=-2;
            }
            path.pop_back();node.domain=node.domain.intersect(valid);
            if(!node.domain.contains(point)) throw std::logic_error("backward guard lost query input");
        }
        return remember(std::move(node));
    }
    RegionCertificate certificate(int root,int horizon) {
        RegionCertificate result{rules.identity,problem,horizon,-1,{}};
        // Export only the dependency closure of this proof, in child-first
        // order. Unrelated work from larger horizons is not trusted or retained.
        std::vector<unsigned char> used(nodes.size());std::vector<int> pending{root};
        while(!pending.empty()) {
            budget.tick();int id=pending.back();pending.pop_back();if(used[id]) continue;
            used[id]=1;for(int child:nodes[id].children) if(child>=0) pending.push_back(child);
        }
        std::vector<int> remap(nodes.size(),-1);
        for(int id=0;id<int(nodes.size());++id) if(used[id]) {
            budget.tick();auto node=nodes[id];
            for(int& child:node.children) if(child>=0) {
                if(child>=id || remap[child]<0) throw std::logic_error("cyclic regional proof");
                child=remap[child];
            }
            remap[id]=int(result.nodes.size());result.nodes.push_back(std::move(node));
        }
        result.root=remap[root];return result;
    }
    std::size_t countTransitions() const {return transitionCount;}
};
}

bool verifyRegions(const Problem& problem,const RegionCertificate& c,Budget& budget) {
    auto rules=battleRules();
    if(c.rules!=rules->identity || !sameProblem(problem,c.problem)) return false;
    validateProblem(problem,c.horizon,*rules);lcg::init(problem.seed,true);
    if(c.root<0 || c.root>=int(c.nodes.size())) return false;
    const auto base=baseBox(problem);
    for(int id=0;id<int(c.nodes.size());++id) {
        budget.tick();const auto& node=c.nodes[id];
        if(node.domain.empty() || !base.contains(node.domain) || node.remaining<0 || node.remaining>c.horizon ||
           node.position<problem.root.position || node.position>problem.root.position+(c.horizon-node.remaining)*rules->maxDraws)
            return false;
        if(std::all_of(node.children.begin(),node.children.end(),[](int x){return x==-3;})) {
            if(node.domain.resource[0].lo<=mul(128,node.remaining)) return false;
            continue;
        }
        if(node.remaining==0) return false;
        for(int a=0;a<8;++a) {
            budget.tick();auto domain=selectable(node.domain,selectionOrder[a]);int child=node.children[a];
            if(domain.empty()) {if(child!=-1) return false;continue;}
            if(child==-1 || child==-3 || child>=id) return false;
            // Re-execute the whole claimed domain with the independent full
            // branching stepper. A traced point or generator flag cannot omit
            // any nonempty case, including the explicit skipTurn assertion.
            auto leaves=step(*rules,domain,node.position,selectionOrder[a],budget);
            if(leaves.empty()) return false;
            for(const auto& leaf:leaves) {
                if(leaf.terminal==Terminal::Invalid || leaf.terminal==Terminal::Goal) return false;
                if(leaf.terminal==Terminal::Dead || leaf.terminal==Terminal::ForbiddenFlee) continue;
                if(child<0) return false;
                const auto& target=c.nodes[child];auto output=image(leaf.guard,leaf.output);
                if(target.remaining!=node.remaining-1 || target.position!=leaf.position || !target.domain.contains(output)) return false;
            }
        }
    }
    const auto& root=c.nodes[c.root];
    return root.position==problem.root.position && root.remaining==c.horizon && root.domain.contains(alpha(problem.root));
}

RegionSearch searchRegions(const Problem& p,int horizon,const RegisteredRules& rules,Budget& budget,
                          const std::function<void(std::span<const int>)>& checkWitness) {
    RegionSearch result;Rooms rooms(p,horizon,rules,budget);
    try {
        int h=horizon;
        for(;;) {
            int root=rooms.decide(p.root.position,h,alpha(p.root));
            if(root>=0) {
                auto certificate=rooms.certificate(root,h);
                if(!verifyRegions(p,certificate,budget)) throw std::logic_error("regional false certificate rejected");
                result.negative=std::make_unique<RegionCertificate>(std::move(certificate));result.provedFalseThrough=h;break;
            }
            checkWitness(rooms.witness);
            result.commands=rooms.witness;h=int(result.commands.size())-1;
        }
    } catch(const Exhausted&) {
        // Already found command lists still have to pass raw replay by caller.
    }
    result.rooms=rooms.nodes.size();result.transitions=rooms.countTransitions();return result;
}
}
