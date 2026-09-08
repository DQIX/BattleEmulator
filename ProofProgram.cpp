#include "ProofProgram.h"
#include <bitset>
#include <map>
#include <numeric>
#include <set>

namespace proof {
namespace {
using Slots=std::bitset<slotCount>;
struct Summary { Int steps=0,draws=0,depth=0;Slots needs,writes; };
void slot(int i) { if(i<0 || i>=slotCount) throw std::invalid_argument("invalid rule slot"); }
bool resourceSlot(int i) { return i<4 || i==26 || i==27; }
int resourceAxis(int i) { return i<4 ? i : 0; }
void assignType(int dst,int src) {
    slot(dst);slot(src);
    if(resourceSlot(src) && (!resourceSlot(dst) || resourceAxis(src)!=resourceAxis(dst)))
        throw std::invalid_argument("resource coordinate transfer");
    if(resourceSlot(dst) && src>=4 && src<10)
        throw std::invalid_argument("mode assigned to resource");
    if(dst>=4 && dst<10 && src>=4 && src<10 && dst!=src)
        throw std::invalid_argument("mode coordinate transfer");
}
}

std::shared_ptr<const RegisteredRules> registerRules(RuleProgram p) {
    if(p.routines.empty() || p.routines.size()>128 || p.entry<0 || p.entry>=int(p.routines.size()))
        throw std::invalid_argument("invalid rule entry/count");
    std::vector<unsigned char> routineColor(p.routines.size());
    std::vector<Summary> summaries(p.routines.size());
    std::function<Summary(int)> routine=[&](int f)->Summary {
        if(f<0 || f>=int(p.routines.size())) throw std::invalid_argument("unresolved rule call");
        if(routineColor[f]==1) throw std::invalid_argument("recursive rule call graph");
        if(routineColor[f]==2) return summaries[f];
        routineColor[f]=1;
        const auto& code=p.routines[f].code;
        if(code.empty() || code.size()>16384) throw std::invalid_argument("invalid routine size");
        std::vector<unsigned char> color(code.size());
        std::vector<Summary> at(code.size());
        std::function<Summary(int)> visit=[&](int pc)->Summary {
            if(pc<0 || pc>=int(code.size())) throw std::invalid_argument("unresolved rule successor");
            if(color[pc]==1) throw std::invalid_argument("cyclic rule CFG");
            if(color[pc]==2) return at[pc];
            color[pc]=1;
            const auto& i=code[pc];
            Summary here; here.steps=1;
            Slots reads,writes;
            std::vector<int> next;
            switch(i.op) {
                case Op::Set:slot(i.dst);writes.set(i.dst);next={pc+1};break;
                case Op::Copy:
                    assignType(i.dst,i.x);reads.set(i.x);writes.set(i.dst);next={pc+1};break;
                case Op::Add:
                    assignType(i.dst,i.x);slot(i.y);
                    if(resourceSlot(i.y) || (i.immediate!=1 && i.immediate!=-1))
                        throw std::invalid_argument("non-affine rule addition");
                    reads.set(i.x);reads.set(i.y);writes.set(i.dst);next={pc+1};break;
                case Op::CompareLE:
                    slot(i.x);slot(i.y);
                    if(resourceSlot(i.y)) throw std::invalid_argument("nonconstant comparison RHS");
                    reads.set(i.x);reads.set(i.y);next={i.yes,i.no};break;
                case Op::Jump:next={i.yes};break;
                case Op::Call: {
                    if(i.x==p.entry) throw std::invalid_argument("call to turn routine");
                    auto callee=routine(i.x);
                    reads=callee.needs;writes=callee.writes;
                    here.steps=add(1,callee.steps);here.draws=callee.draws;here.depth=add(1,callee.depth);
                    next={pc+1};break;
                }
                case Op::Return:
                    if(f==p.entry) throw std::invalid_argument("RETURN in turn routine");break;
                case Op::Finish:
                    if(f!=p.entry) throw std::invalid_argument("FINISH in subroutine");
                    for(int j=0;j<10;++j) reads.set(j);break;
                case Op::Reject:break;
                case Op::RandomSkip:
                    if(i.immediate<0 || i.immediate>5000) throw std::invalid_argument("invalid RNG skip");
                    here.draws=i.immediate;next={pc+1};break;
                case Op::NativeCall:
                    slot(i.dst);
                    if(resourceSlot(i.dst) || i.args.size()!=nativeArity(i.native))
                        throw std::invalid_argument("native result/arity");
                    for(auto a:i.args) {
                        slot(a);if(resourceSlot(a)) throw std::invalid_argument("resource-native argument");
                        reads.set(a);
                    }
                    writes.set(i.dst);here.draws=nativeDraws(i.native);next={pc+1};break;
                default:throw std::invalid_argument("unknown rule opcode");
            }
            Slots futureNeeds,futureWrites;
            Int tailSteps=0,tailDraws=0,tailDepth=0;
            bool first=true;
            for(auto n:next) {
                auto tail=visit(n);
                tailSteps=std::max(tailSteps,tail.steps);tailDraws=std::max(tailDraws,tail.draws);
                tailDepth=std::max(tailDepth,tail.depth);futureNeeds|=tail.needs;
                if(first) futureWrites=tail.writes;else futureWrites&=tail.writes;
                first=false;
            }
            here.steps=add(here.steps,tailSteps);here.draws=add(here.draws,tailDraws);
            here.depth=std::max(here.depth,tailDepth);
            here.needs=reads|(futureNeeds&~writes);here.writes=writes|futureWrites;
            color[pc]=2;return at[pc]=here;
        };
        auto result=visit(0);
        // No generator assertion of unreachability exempts malformed code.
        for(int pc=0;pc<int(code.size());++pc) visit(pc);
        routineColor[f]=2;return summaries[f]=result;
    };
    auto bound=routine(p.entry);
    for(int f=0;f<int(p.routines.size());++f) routine(f);
    Slots initial;for(int j=0;j<=10;++j) initial.set(j);
    if((bound.needs&~initial).any()) throw std::invalid_argument("uninitialized turn slot");
    if(bound.draws>4999 || bound.depth>128 || bound.steps>1'000'000)
        throw std::invalid_argument("rule execution bounds exceed profile");
    // Version is private to the fixed registry. The certificate cannot replace
    // this program, its numerical functions or its kernel-computed bounds.
    return std::make_shared<const RegisteredRules>(RegisteredRules{
        std::move(p),int(bound.draws),std::uint64_t(bound.steps),unsigned(bound.depth),1});
}

namespace {
struct Pc { int routine=0,index=0; };
struct Frame {
    Pc pc;
    std::vector<Pc> stack;
    std::array<Value,slotCount> value;
    Box domain;
    int position=0;
    std::uint64_t steps=0;
};
// Split a native argument by its finite mode image, never by resource values.
// Every nonempty inverse image is enqueued at the same prescribed instruction.
bool concretize(Frame& f,int slotId,std::vector<Frame>& pending,Budget& budget) {
    const auto v=f.value[slotId];
    if(v.kind==Value::Constant) return false;
    if(v.kind==Value::Resource) throw std::logic_error("symbolic resource in concrete operation");
    auto possibilities=v.values(f.domain);
    for(auto it=possibilities.rbegin();it!=possibilities.rend();++it) {
        auto x=*it;
        budget.tick(modeSizes[v.axis]);
        auto child=f;
        child.domain=restrictValue(child.domain,v,x,true);
        child.domain=restrictValue(child.domain,v,add(x,-1),false);
        if(child.domain.empty()) throw std::logic_error("empty mode-image branch");
        child.value[slotId]=Value::constant(x);
        // This is operand resolution, not an additional rule instruction.
        --child.steps;
        pending.push_back(std::move(child));
    }
    return true;
}
}
std::vector<Leaf> step(const RegisteredRules& rules,const Box& input,int position,int command,Budget& budget) {
    std::vector<Leaf> leaves;
    if(input.empty()) return leaves;
    if(position<1 || position>4999-rules.maxDraws) throw std::invalid_argument("RNG range at rule root");
    Frame initial;
    initial.pc={rules.program.entry,0};initial.position=position;initial.domain=input;
    for(int j=0;j<4;++j) initial.value[j]=Value::resource(j);
    for(int j=0;j<6;++j) initial.value[j+4]=Value::mode(j);
    initial.value[10]=Value::constant(command);
    std::vector<Frame> pending;pending.push_back(std::move(initial));
    while(!pending.empty()) {
        budget.memory(pending.capacity()*sizeof(Frame)+leaves.capacity()*sizeof(Leaf));
        Frame f=std::move(pending.back());pending.pop_back();
        bool done=false;
        while(!done) {
            budget.tick();
            if(++f.steps>rules.maxSteps) throw std::logic_error("rule step bound violated");
            const auto& i=rules.program.routines.at(f.pc.routine).code.at(f.pc.index);
            switch(i.op) {
                case Op::Set:f.value[i.dst]=Value::constant(i.immediate);++f.pc.index;break;
                case Op::Copy:f.value[i.dst]=f.value[i.x];++f.pc.index;break;
                case Op::Add:
                    if(concretize(f,i.y,pending,budget)) {done=true;break;}
                    f.value[i.dst]=f.value[i.x].translated(mul(f.value[i.y].offset,i.immediate));
                    ++f.pc.index;break;
                case Op::CompareLE: {
                    if(concretize(f,i.y,pending,budget)) {done=true;break;}
                    auto yes=restrictValue(f.domain,f.value[i.x],f.value[i.y].offset,true);
                    auto no=restrictValue(f.domain,f.value[i.x],f.value[i.y].offset,false);
                    budget.tick(2);
                    if(!no.empty()) {auto child=f;child.domain=no;child.pc.index=i.no;pending.push_back(std::move(child));}
                    if(!yes.empty()) {f.domain=yes;f.pc.index=i.yes;} else done=true;
                    break;
                }
                case Op::Jump:f.pc.index=i.yes;break;
                case Op::Call:
                    if(f.stack.size()>=rules.maxDepth) throw std::logic_error("rule stack bound violated");
                    f.stack.push_back({f.pc.routine,f.pc.index+1});f.pc={i.x,0};break;
                case Op::Return:
                    if(f.stack.empty()) throw std::logic_error("empty rule return stack");
                    f.pc=f.stack.back();f.stack.pop_back();break;
                case Op::RandomSkip:f.position+=int(i.immediate);++f.pc.index;break;
                case Op::NativeCall: {
                    bool split=false;
                    for(auto a:i.args) if(concretize(f,a,pending,budget)) {split=true;break;}
                    if(split) {done=true;break;}
                    std::vector<Int> args;for(auto a:i.args) args.push_back(f.value[a].offset);
                    auto before=f.position;
                    budget.tick(32); // finite native internal-work contract
                    auto value=evaluateNative(i.native,args,f.position);
                    if(f.position<before || f.position-before>int(nativeDraws(i.native)))
                        throw std::logic_error("native RNG contract violated");
                    f.value[i.dst]=Value::constant(value);++f.pc.index;break;
                }
                case Op::Reject: {
                    Leaf leaf;leaf.guard=f.domain;leaf.position=f.position;leaf.terminal=Terminal::Invalid;
                    std::copy_n(f.value.begin(),10,leaf.output.begin());
                    if(leaves.size()>=budget.limits.maxLeavesPerRoot) throw Exhausted{};
                    leaves.push_back(std::move(leaf));done=true;break;
                }
                case Op::Finish: {
                    if(!f.stack.empty()) throw std::logic_error("unfinished rule calls");
                    // Split terminal classification too; no mixed live/goal leaf.
                    for(int e=0;e<2;++e) {
                        auto d=restrictValue(f.domain,f.value[0],0,e==0);
                        if(d.empty()) continue;
                        for(int a=0;a<2;++a) {
                            auto part=restrictValue(d,f.value[1],0,a==0);
                            if(part.empty()) continue;
                            Leaf leaf;leaf.guard=part;leaf.position=f.position;
                            std::copy_n(f.value.begin(),10,leaf.output.begin());
                            auto out=image(part,leaf.output);
                            for(auto r:out.resource) if(r.lo<0) throw std::logic_error("negative terminal resource");
                            leaf.terminal=e==0?Terminal::Goal:(a==0?Terminal::Dead:Terminal::Continue);
                            if(leaves.size()>=budget.limits.maxLeavesPerRoot) throw Exhausted{};
                            leaves.push_back(std::move(leaf));
                        }
                    }
                    done=true;break;
                }
                default:throw std::logic_error("unknown registered opcode");
            }
            if(f.position<position || f.position>position+rules.maxDraws || f.position>4999)
                throw std::logic_error("rule RNG bound violated");
        }
    }
    return leaves;
}

int Assembler::declare(const std::string& name) {
    program.routines.push_back({name,{}});return int(program.routines.size()-1);
}
void Assembler::begin(int routine) {
    if(current!=-1) throw std::logic_error("nested assembler routine");
    current=routine;labels.clear();unresolved.clear();
}
void Assembler::label(const std::string& name) {
    for(const auto& [n,p]:labels) if(n==name) throw std::logic_error("duplicate assembler label");
    labels.emplace_back(name,int(program.routines.at(current).code.size()));
}
void Assembler::emit(Instruction i) {program.routines.at(current).code.push_back(std::move(i));}
void Assembler::set(int d,Int n,const char* source) {Instruction i;i.op=Op::Set;i.dst=d;i.immediate=n;i.source=source;emit(i);}
void Assembler::copy(int d,int x) {Instruction i;i.op=Op::Copy;i.dst=d;i.x=x;emit(i);}
void Assembler::add(int d,int x,int y,int sign) {Instruction i;i.op=Op::Add;i.dst=d;i.x=x;i.y=y;i.immediate=sign;emit(i);}
void Assembler::addConstant(int d,int x,Int n) {set(63,n);add(d,x,63);}
void Assembler::branch(int x,int y,const std::string& yes,const std::string& no) {
    const auto pc=int(program.routines.at(current).code.size());
    Instruction i;i.op=Op::CompareLE;i.x=x;i.y=y;emit(i);
    unresolved.emplace_back(pc*2,yes);unresolved.emplace_back(pc*2+1,no);
}
void Assembler::branchConstant(int x,Int n,const std::string& yes,const std::string& no) {set(63,n);branch(x,63,yes,no);}
void Assembler::jump(const std::string& name) {
    const auto pc=int(program.routines.at(current).code.size());
    Instruction i;i.op=Op::Jump;emit(i);unresolved.emplace_back(pc*2,name);
}
void Assembler::call(int r) {Instruction i;i.op=Op::Call;i.x=r;emit(i);}
void Assembler::native(int d,Native n,std::initializer_list<int> args) {
    Instruction i;i.op=Op::NativeCall;i.dst=d;i.native=n;i.args=args;emit(i);
}
void Assembler::skip(int n) {Instruction i;i.op=Op::RandomSkip;i.immediate=n;emit(i);}
void Assembler::ret() {Instruction i;i.op=Op::Return;emit(i);}
void Assembler::finish() {Instruction i;i.op=Op::Finish;emit(i);}
void Assembler::reject() {Instruction i;i.op=Op::Reject;emit(i);}
void Assembler::end() {
    for(const auto& [encoded,name]:unresolved) {
        auto found=std::find_if(labels.begin(),labels.end(),[&](const auto& p){return p.first==name;});
        if(found==labels.end()) throw std::logic_error("unresolved assembler label: "+name);
        auto& i=program.routines.at(current).code.at(encoded/2);
        (encoded%2?i.no:i.yes)=found->second;
    }
    current=-1;
}
RuleProgram Assembler::take() {if(current!=-1) throw std::logic_error("unfinished assembler");return std::move(program);}
}
