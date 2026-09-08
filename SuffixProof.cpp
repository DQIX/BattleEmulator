#include "SuffixProof.h"
#include "BattleEmulator.h"
#include "lcg.h"
#include <cmath>
#include <numeric>

namespace proof {
namespace {
bool exactStep(const Problem& p,const RegisteredRules& rules,BattleState& state,int command,Budget& budget) {
    auto point=alpha(state);
    if(selectable(point,command).empty()) return false;
    auto leaves=step(rules,point,state.position,command,budget);
    if(leaves.size()!=1) throw std::logic_error("singleton rule execution is not deterministic");
    const auto& leaf=leaves[0];
    if(leaf.terminal==Terminal::Invalid) throw std::logic_error("Invalid on selectable replay input");
    budget.tick();const auto beforeTurn=(state.nowState>>12)&0xfffff;
    int32_t gene[350]={command};
    auto raw=state;bool rejectedFlee=false;
    BattleEmulator::Main(&raw.position,1,gene,raw.players,nullptr,p.seed,nullptr,nullptr,-2,&raw.nowState,true,false,&rejectedFlee);
    if(rejectedFlee!=(leaf.terminal==Terminal::ForbiddenFlee)) throw std::logic_error("skipTurn assertion correspondence");
    if(rejectedFlee) return false;
    state=raw;
    if(state.position!=leaf.position || !(alpha(state)==image(point,leaf.output)) ||
        ((state.nowState>>12)&0xfffff)!=beforeTurn+1)
        throw std::logic_error("RuleProgram disagrees with exact BattleEmulator replay");
    return true;
}
struct Choice {int source=0,edge=0,target=-1;};
struct Cursor {int edge=0,target=-2;std::size_t next=0;};
struct CandidateFrame {int turn=0,source=0;std::vector<Cursor> cursors;};
class CandidateIterator {
    const Model& model;
    const DynamicProgram& dp;
    Budget& budget;
    std::vector<std::vector<int>> destinations;
    std::vector<CandidateFrame> stack;
    std::vector<Choice> path;
    std::vector<Choice> yielded;
    void scan() {
        budget.tick();if(++budget.candidateScans>budget.limits.maxCandidateScans) throw Exhausted{};
    }
    void advance(CandidateFrame& f,Cursor& c,bool initial) {
        const auto& e=model.edges[f.source][c.edge];
        if(initial && e.goal) {c.target=-1;return;}
        if(!e.completion) {
            c.target=initial && e.target>=0 && dp.distance[f.turn+1][e.target]>=0?e.target:-2;
            return;
        }
        const auto& order=destinations[f.turn+1];
        while(c.next<order.size()) {
            scan();int q=order[c.next++];int p=model.cells[q].position;
            if(p>=e.firstPosition && p<=e.lastPosition) {c.target=q;return;}
        }
        c.target=-2;
    }
    CandidateFrame frame(int turn,int source) {
        CandidateFrame f{turn,source,{}};
        for(int i=0;i<int(model.edges[source].size());++i) {
            scan();if(model.edges[source][i].dead) continue;
            Cursor c{i,-2,0};advance(f,c,true);if(c.target!=-2) f.cursors.push_back(c);
        }
        return f;
    }
    auto order(const CandidateFrame& f,const Cursor& c) const {
        const auto& e=model.edges[f.source][c.edge];
        int rank=int(std::find(selectionOrder.begin(),selectionOrder.end(),e.command)-selectionOrder.begin());
        int length=c.target==-1?1:1+dp.distance[f.turn+1][c.target];
        CellKey target=c.target==-1?CellKey{}:model.cells[c.target];
        return std::tuple(length,e.completion?1:0,rank,c.edge,c.target==-1?0:1,target.position,target.local);
    }
public:
    CandidateIterator(const Model& m,const DynamicProgram& d,Budget& b):model(m),dp(d),budget(b) {
        destinations.resize(m.horizon+1);
        for(int t=0;t<=m.horizon;++t) {
            for(int q:m.support[t]) {scan();if(dp.distance[t][q]>=0) destinations[t].push_back(q);}
            std::sort(destinations[t].begin(),destinations[t].end(),[&](int x,int y){
                scan();return std::tuple(dp.distance[t][x],m.cells[x])<std::tuple(dp.distance[t][y],m.cells[y]);
            });
        }
        if(m.horizon>0) stack.push_back(frame(0,m.root));
    }
    const std::vector<Choice>* next() {
        while(!stack.empty()) {
            auto& f=stack.back();int best=-1;
            for(int i=0;i<int(f.cursors.size());++i) {
                scan();if(f.cursors[i].target==-2) continue;
                if(best<0 || order(f,f.cursors[i])<order(f,f.cursors[best])) best=i;
            }
            if(best<0) {stack.pop_back();if(!path.empty()) path.pop_back();continue;}
            const auto c=f.cursors[best];Choice choice{f.source,c.edge,c.target};
            int nextTurn=f.turn+1;advance(f,f.cursors[best],false);
            if(choice.target==-1) {yielded=path;yielded.push_back(choice);return &yielded;}
            path.push_back(choice);stack.push_back(frame(nextTurn,choice.target));
        }
        return nullptr;
    }
};
struct Repair {
    int turn=0;
    CellKey cell;
    int command=0;
    Box domain;
    bool completion=false;
    std::unique_ptr<Predicate> predicate;
    Box point;
};
std::unique_ptr<Predicate> separating(const Box& expected,const Box& actual) {
    for(int j=0;j<4;++j) {
        if(actual.resource[j].lo<expected.resource[j].lo) return std::make_unique<Predicate>(Predicate{j,add(expected.resource[j].lo,-1),0});
        if(actual.resource[j].hi>expected.resource[j].hi) return std::make_unique<Predicate>(Predicate{j,expected.resource[j].hi,0});
    }
    for(int j=0;j<6;++j) if(actual.mode[j]&~expected.mode[j]) return std::make_unique<Predicate>(Predicate{j+4,0,expected.mode[j]});
    return nullptr;
}
struct Replay {
    bool win=false;
    std::vector<int> commands;
    std::unique_ptr<Repair> repair;
};
Replay replayCandidate(const Problem& p,const RegisteredRules& rules,const Model& model,
                       const Partitions& partitions,const std::vector<Choice>& path,Budget& budget) {
    Replay result;auto state=p.root;
    for(int t=0;t<int(path.size());++t) {
        const auto choice=path[t];const auto& edge=model.edges[choice.source][choice.edge];
        const auto point=alpha(state);const int beforePosition=state.position;
        const auto actual=CellKey{state.position,partitions.at(state.position).classify(point)};
        if(!result.repair && state.position==model.cells[choice.source].position) {
            if(auto predicate=separating(edge.guard,point))
                result.repair=std::make_unique<Repair>(Repair{t,actual,edge.command,{},false,std::move(predicate),point});
        }
        if(state.players[0].hp==0 || !exactStep(p,rules,state,edge.command,budget)) {
            if(!result.repair && edge.completion)
                result.repair=std::make_unique<Repair>(Repair{t,model.cells[choice.source],edge.command,edge.guard,true,{},point});
            return result;
        }
        result.commands.push_back(edge.command);
        const bool win=state.players[1].hp==0;
        const bool dead=state.players[0].hp==0;
        bool destinationMatches=choice.target==-1?win:(!win && !dead &&
            model.cells[choice.target]==CellKey{state.position,partitions.at(state.position).classify(alpha(state))});
        if(!result.repair && edge.completion && !destinationMatches)
            result.repair=std::make_unique<Repair>(Repair{t,model.cells[choice.source],edge.command,edge.guard,true,{},point});
        if(!edge.completion && beforePosition==model.cells[choice.source].position && edge.guard.contains(point) && !destinationMatches)
            throw std::logic_error("verified detailed edge disagrees with replay destination");
        if(win) {result.win=true;return result;}
        if(dead) return result;
    }
    return result;
}

struct Plane {double intercept=0;std::array<double,3> slope{};};
Int edgeWeight(const Edge& e,Prices p) {
    Int w=mul(p.Q,e.gain[0]);for(int j=0;j<3;++j) w=add(w,mul(p.resource[j],e.gain[j+1]));return w;
}
Plane maximizingPlane(const Problem& p,const Model& m,const DynamicProgram& dp,Prices prices,Budget& budget) {
    std::array<Int,4> gain{0,p.root.players[0].hp,p.root.players[0].mp,p.root.players[0].medicinal_herbs_count};
    int q=m.root;
    for(int t=0;t<m.horizon;++t) {
        bool found=false;
        for(const auto& e:m.edges[q]) {
            budget.tick();if(e.dead) continue;
            Bound continuation=e.goal?0:unreachable;int target=-1;
            if(e.completion) {
                for(int out:m.support[t+1]) {
                    budget.tick();int pos=m.cells[out].position;
                    if(pos<e.firstPosition || pos>e.lastPosition) continue;
                    const auto value=dp.bound[t+1][out];
                    if(value>continuation) {continuation=value;target=out;}
                }
            } else if(!e.goal) {continuation=dp.bound[t+1][e.target];target=e.target;}
            if(continuation!=unreachable && extendBound(edgeWeight(e,prices),continuation)==dp.bound[t][q]) {
                for(int j=0;j<4;++j) gain[j]=add(gain[j],e.gain[j]);
                q=target;found=true;break;
            }
        }
        if(!found) throw std::logic_error("max-plus predecessor missing");
        if(q==-1) break;
    }
    return {double(mul(prices.Q,gain[0])),{double(gain[1]),double(gain[2]),double(gain[3])}};
}
Prices nextPrices(const std::vector<Plane>& planes,Budget& budget) {
    // Four-variable cutting-plane proposal. Floating point proposes prices
    // only; every accepted bound is recalculated with checked integer max-plus.
    struct Constraint {std::array<double,4> a;double rhs;};
    std::vector<Constraint> constraints;
    for(const auto& p:planes) constraints.push_back({{-p.slope[0],-p.slope[1],-p.slope[2],1},p.intercept});
    for(int j=0;j<3;++j) {
        Constraint lo{{0,0,0,0},0},hi{{0,0,0,0},-1048576};lo.a[j]=1;hi.a[j]=-1;
        constraints.push_back(lo);constraints.push_back(hi);
    }
    double best=std::numeric_limits<double>::infinity();std::array<double,4> winner{};
    const int n=int(constraints.size());
    for(int a=0;a<n;++a) for(int b=a+1;b<n;++b) for(int c=b+1;c<n;++c) for(int d=c+1;d<n;++d) {
        budget.tick();std::array<int,4> ids{a,b,c,d};double matrix[4][5]{};
        for(int i=0;i<4;++i) {for(int j=0;j<4;++j) matrix[i][j]=constraints[ids[i]].a[j];matrix[i][4]=constraints[ids[i]].rhs;}
        bool valid=true;
        for(int col=0;col<4;++col) {
            int pivot=col;for(int row=col+1;row<4;++row) if(std::abs(matrix[row][col])>std::abs(matrix[pivot][col])) pivot=row;
            if(std::abs(matrix[pivot][col])<1e-10) {valid=false;break;}
            for(int j=0;j<5;++j) std::swap(matrix[col][j],matrix[pivot][j]);
            double scale=matrix[col][col];for(int j=col;j<5;++j) matrix[col][j]/=scale;
            for(int row=0;row<4;++row) if(row!=col) {
                scale=matrix[row][col];for(int j=col;j<5;++j) matrix[row][j]-=scale*matrix[col][j];
            }
        }
        if(!valid) continue;
        std::array<double,4> x;for(int j=0;j<4;++j) x[j]=matrix[j][4];
        for(const auto& constraint:constraints) {
            budget.tick();double value=0;for(int j=0;j<4;++j) value+=constraint.a[j]*x[j];
            if(value<constraint.rhs-1e-5) {valid=false;break;}
        }
        if(valid && x[3]<best) {best=x[3];winner=x;}
    }
    Prices result;
    if(std::isfinite(best)) for(int j=0;j<3;++j) result.resource[j]=Int(std::llround(std::clamp(winner[j],0.0,1048576.0)));
    return result;
}

Result run(const Problem& p,int horizon,const RegisteredRules& rules,Budget& budget) {
    Result result;result.problem=p;validateProblem(p,horizon,rules);
    if(p.root.players[1].hp==0) {result.status=Status::Optimal;result.minimum=0;return result;}
    if(horizon==0 || p.root.players[0].hp==0) {
        result.certificate=std::make_unique<Certificate>(Certificate{rules.version,p,horizon,{},{},{},{},rules.identity});
        if(!verifyFalse(p,*result.certificate,budget)) throw std::logic_error("trivial false rejected");
        result.status=Status::ProvedFalse;result.provedFalseThrough=horizon;return result;
    }
    lcg::init(p.seed,true);
    // A local Box proof can close a suffix without rebuilding the deliberately
    // coarse all-mode graph. The same registered transitions and raw witness
    // replay bind both sides of the adjacent-turn result.
    if(budget.limits.regional) {
        auto regional=searchRegions(p,horizon,rules,budget,[&](std::span<const int> commands) {
            auto raw=p.root;
            for(std::size_t i=0;i<commands.size();++i) {
                if(raw.players[0].hp==0 || !exactStep(p,rules,raw,commands[i],budget) ||
                   (raw.players[1].hp==0 && i+1!=commands.size()))
                    throw std::logic_error("regional witness rejected by exact replay");
            }
            if(raw.players[1].hp!=0) throw std::logic_error("regional witness did not win");
        });
        result.statistics.cells=regional.rooms;result.statistics.edges=regional.transitions;
        if(!regional.commands.empty()) {
            result.commands=std::move(regional.commands);result.status=Status::Win;
        }
        if(regional.negative) {
            result.regionCertificate=std::move(regional.negative);
            result.provedFalseThrough=regional.provedFalseThrough;
            if(result.commands.empty()) result.status=Status::ProvedFalse;
            else if(result.provedFalseThrough==int(result.commands.size())-1) {
                result.status=Status::Optimal;result.minimum=int(result.commands.size());
            }
        }
        if(result.status!=Status::Unknown) return result;
        result.detail="regional proof time/work/space budget exhausted";
        return result;
    }
    Partitions partitions(p,horizon,rules);Coverage coverage;
    std::set<std::vector<int>> failed;
    std::vector<int> witness;
    try {
        for(;;) {
            if(budget.prices>=budget.limits.maxPrices) throw Exhausted{};
            auto model=rebuildSupport(p,horizon,rules,partitions,coverage,budget);
            result.statistics.cells=model.cells.size();result.statistics.edges=0;
            for(const auto& edges:model.edges) result.statistics.edges+=edges.size();
            Prices prices;
            auto dp=maxPlus(model,prices,budget);
            ++budget.prices;
            std::vector<Plane> planes;
            bool falseCandidate=excludesWin(p,dp,prices);
            // Up to four price proposals per snapshot, all charged globally.
            for(int trial=0;!falseCandidate && trial<3 && budget.prices<budget.limits.maxPrices;++trial) {
                planes.push_back(maximizingPlane(p,model,dp,prices,budget));
                auto proposed=nextPrices(planes,budget);if(proposed==prices) break;
                prices=proposed;dp=maxPlus(model,prices,budget);++budget.prices;
                falseCandidate=excludesWin(p,dp,prices);
            }
            if(falseCandidate) {
                Certificate certificate{rules.version,p,horizon,std::move(partitions),std::move(coverage),prices,std::move(dp.bound),rules.identity};
                if(!verifyFalse(p,certificate,budget)) throw std::logic_error("independent false verification rejected");
                result.certificate=std::make_unique<Certificate>(std::move(certificate));result.provedFalseThrough=horizon;
                result.commands=witness;result.status=witness.empty()?Status::ProvedFalse:Status::Optimal;
                if(!witness.empty()) result.minimum=int(witness.size());
                return result;
            }
            CandidateIterator iterator(model,dp,budget);
            bool rebuild=false;
            while(!rebuild) {
                std::vector<Repair> repairs;
                bool exhausted=false;
                for(unsigned batch=0;batch<8;) {
                    auto path=iterator.next();if(!path) {exhausted=true;break;}
                    std::vector<int> commands;for(auto choice:*path) commands.push_back(model.edges[choice.source][choice.edge].command);
                    budget.tick(commands.size()+1);
                    if(failed.contains(commands)) {
                        // A repeated failed command list is not replayed. Its
                        // root is still exact, so a root completion can be
                        // detailed without storing concrete path histories.
                        const auto& first=model.edges[path->front().source][path->front().edge];
                        if(first.completion) {
                            repairs.push_back({0,model.cells[model.root],first.command,first.guard,true,{},alpha(p.root)});
                            if(repairs.size()>=8) break;
                        }
                        continue;
                    }
                    if(budget.candidates>=budget.limits.maxCandidates) throw Exhausted{};
                    ++budget.candidates;++batch;
                    auto replay=replayCandidate(p,rules,model,partitions,*path,budget);
                    if(replay.win) {
                        witness=std::move(replay.commands);result.commands=witness;result.status=Status::Win;
                        horizon=int(witness.size())-1;
                        partitions.lastPosition=p.root.position+horizon*rules.maxDraws;
                        partitions.trees.resize(partitions.lastPosition-partitions.firstPosition+1);
                        ++partitions.version;
                        if(horizon==0) {
                            Certificate certificate{rules.version,p,0,{},{},{},{},rules.identity};
                            if(!verifyFalse(p,certificate,budget)) throw std::logic_error("zero bound rejected");
                            result.certificate=std::make_unique<Certificate>(std::move(certificate));result.status=Status::Optimal;
                            result.minimum=1;result.provedFalseThrough=0;return result;
                        }
                        rebuild=true;break;
                    }
                    failed.insert(std::move(commands));if(replay.repair) repairs.push_back(std::move(*replay.repair));
                    budget.memory(model.bytes()+coverage.bytes()+failed.size()*std::size_t(budget.limits.maxTurns+1)*8);
                }
                if(rebuild) break;
                std::stable_sort(repairs.begin(),repairs.end(),[](const auto& a,const auto& b){return a.turn<b.turn;});
                if(budget.refinements<budget.limits.maxRefinements) for(const auto& repair:repairs) {
                    bool changed=false;
                    if(repair.completion) {
                        // Keep the expensive all-mode expansion local to the
                        // observed mismatch. This partitions the full BaseBox;
                        // both children and every required output remain covered.
                        // No combat value is rounded or used for a FALSE bound.
                        for(int j=0;j<6 && !changed;++j) {
                            if(repair.domain.mode[j]!=repair.point.mode[j])
                                changed=partitions.refine(repair.cell,{j+4,0,repair.point.mode[j]},budget.limits.maxCellsPerPosition);
                        }
                        if(!changed) changed=coverage.expand(repair.cell.position,repair.command,repair.domain,rules,budget);
                    } else if(repair.predicate)
                        changed=partitions.refine(repair.cell,*repair.predicate,budget.limits.maxCellsPerPosition);
                    if(changed) {++budget.refinements;rebuild=true;break;}
                }
                if(!rebuild && exhausted) {result.detail="candidate iterator exhausted without a verified conclusion";return result;}
            }
        }
    } catch(const Exhausted&) {
        result.detail="cumulative time/work/memory/candidate budget exhausted";
    } catch(const std::bad_alloc&) {
        result.detail="allocation budget exhausted";
    } catch(const std::exception& e) {
        result.status=Status::Error;result.detail=e.what();
    }
    result.commands=witness;return result;
}
Result solve(const Problem& initial,std::span<const int> prefix,int horizon,const Limits& limits) {
    const auto started=std::chrono::steady_clock::now();Budget budget(limits);Result result;
    try {
        budget.tick();auto rules=battleRules();
        if(horizon<0 || horizon>limits.maxTurns) throw std::invalid_argument("explicit suffix horizon exceeds configured limit");
        validateProblem(initial,int(prefix.size()),*rules);lcg::init(initial.seed,true);
        auto p=initial;
        for(int command:prefix) {
            if(p.root.players[1].hp==0 || p.root.players[0].hp==0 || !exactStep(initial,*rules,p.root,command,budget))
                throw std::invalid_argument("invalid prefix or command after terminal state");
        }
        result=run(p,horizon,*rules,budget);result.prefix.assign(prefix.begin(),prefix.end());
    } catch(const Exhausted&) {result.detail="input/prefix budget exhausted";}
      catch(const std::bad_alloc&) {result.detail="allocation budget exhausted";}
      catch(const std::exception& e) {result.status=Status::Error;result.detail=e.what();}
    result.statistics.work=budget.work;result.statistics.peakBytes=budget.peakBytes;
    result.statistics.candidates=budget.candidates;result.statistics.refinements=budget.refinements;
    result.statistics.prices=budget.prices;result.statistics.candidateScans=budget.candidateScans;
    result.statistics.elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started);
    return result;
}
}
Result solveSuffix(const Problem& p,int horizon,const Limits& limits) {return solve(p,{},horizon,limits);}
Result extendPrefix(const Problem& p,std::span<const int> prefix,int horizon,const Limits& limits) {return solve(p,prefix,horizon,limits);}
bool replayWin(const Problem& p,std::span<const int> commands,const Limits& limits) {
    try {
        Budget budget(limits);auto rules=battleRules();validateProblem(p,int(commands.size()),*rules);lcg::init(p.seed,true);
        auto state=p.root;if(state.players[1].hp==0) return commands.empty();
        for(std::size_t i=0;i<commands.size();++i) {
            if(state.players[0].hp==0 || !exactStep(p,*rules,state,commands[i],budget)) return false;
            if(state.players[1].hp==0) return i+1==commands.size();
        }
    } catch(...) {return false;}
    return false;
}
}
