#include "SuffixProof.h"
#include "BattleEmulator.h"
#include "lcg.h"
#include <iostream>

namespace {
using namespace proof;
void require(bool test,const char* message) {if(!test) throw std::runtime_error(message);}
std::uint64_t randomState=0x912759bd12345678ull;
std::uint64_t nextRandom() {randomState^=randomState<<13;randomState^=randomState>>7;return randomState^=randomState<<17;}
Limits testLimits() {
    Limits l;
    l.time=std::chrono::seconds(60);
    l.maxWork=200'000'000;
    l.maxBytes=512u*1024u*1024u;
    l.maxCellsPerPosition=32;
    l.maxPrices=32;
    return l;
}
void checkRegistration() {
    RuleProgram invalid;invalid.routines.push_back({"cycle",{{}}});
    invalid.routines[0].code[0].op=Op::Jump;invalid.routines[0].code[0].yes=0;
    bool rejected=false;try {registerRules(invalid);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"cycle was registered");
    invalid.routines[0].code[0].op=Op::Reject;rejected=false;
    try {registerRules(invalid);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"generic Reject was registered");
    auto rules=battleRules();require(rules->maxDraws>0 && rules->maxDepth>0,"missing computed execution bounds");
    std::cout<<"registered: RNG <= "<<rules->maxDraws<<", steps <= "<<rules->maxSteps<<", depth <= "<<rules->maxDepth<<'\n';
}
void compare(const RegisteredRules& rules,std::uint64_t seed,const BattleState& source,int command,Budget& budget) {
    const auto point=alpha(source);if(selectable(point,command).empty()) return;
    auto leaves=step(rules,point,source.position,command,budget);
    require(leaves.size()==1,"concrete execution must have one terminal leaf");
    require(leaves[0].terminal!=Terminal::Invalid,"legal input became Invalid");
    auto actual=source;int32_t gene[350]={command};
    bool rejectedFlee=false;
    BattleEmulator::Main(&actual.position,1,gene,actual.players,nullptr,seed,nullptr,nullptr,-2,&actual.nowState,true,false,&rejectedFlee);
    require(rejectedFlee==(leaves[0].terminal==Terminal::ForbiddenFlee),"asserted FLEE mismatch was hidden");
    if(rejectedFlee) return;
    if(actual.position!=leaves[0].position || !(alpha(actual)==image(point,leaves[0].output))) {
        const auto predicted=image(point,leaves[0].output);
        std::cerr<<"differential mismatch seed="<<seed<<" p="<<source.position<<" command="<<command
                 <<" input E/A/M/I="<<source.players[1].hp<<'/'<<source.players[0].hp<<'/'<<source.players[0].mp<<'/'<<source.players[0].medicinal_herbs_count
                 <<" output p="<<actual.position<<" expected="<<leaves[0].position<<'\n';
        for(int j=0;j<4;++j) std::cerr<<"resource "<<j<<": "<<alpha(actual).resource[j].lo<<" expected "<<predicted.resource[j].lo<<'\n';
        for(int j=0;j<6;++j) std::cerr<<"mode "<<j<<": "<<int(alpha(actual).mode[j])<<" expected "<<int(predicted.mode[j])<<'\n';
        throw std::runtime_error("symbolic rules differ from the original battle emulator");
    }
}
void checkBattleCorrespondence() {
    auto rules=battleRules();auto limits=testLimits();Budget budget(limits);
    for(int trial=0;trial<1024;++trial) {
        auto seed=nextRandom();lcg::init(seed,true);auto s=initialState();
        auto& a=s.players[0];auto& e=s.players[1];
        s.position=1+nextRandom()%3500;s.nowState=((nextRandom()%20)<<12)|((nextRandom()%6)<<8);
        a.hp=1+nextRandom()%65;e.hp=1+nextRandom()%456;a.mp=nextRandom()%23;a.medicinal_herbs_count=nextRandom()%8;
        a.specialCharge=nextRandom()%2;a.specialChargeTurn=nextRandom()%7;
        a.paralysis=nextRandom()%2;a.paralysisTurns=int(nextRandom()%7)-2;
        a.acrobaticStar=nextRandom()%2;a.acrobaticStarTurn=1+nextRandom()%6;
        e.rage=nextRandom()%2;e.rageTurns=1+nextRandom()%4;a.inactive=nextRandom()%2;
        for(int command:selectionOrder) compare(*rules,seed,s,command,budget);
    }
    std::cout<<"8192 independently executed battle/selection comparisons passed\n";
}
void checkIntervalCoverage() {
    auto rules=battleRules();auto limits=testLimits();Budget budget(limits);
    for(int trial=0;trial<16;++trial) {
        auto seed=nextRandom();lcg::init(seed,true);auto s=initialState();s.position=1+nextRandom()%3000;
        s.players[0].acrobaticStar=trial%2;s.players[0].acrobaticStarTurn=3;
        s.players[1].rage=trial%3==0;s.players[1].rageTurns=2;
        Problem p{seed,s};auto domain=alpha(s);domain.resource=baseBox(p).resource;
        for(int command:selectionOrder) {
            auto input=selectable(domain,command);if(input.empty()) continue;
            auto leaves=step(*rules,input,s.position,command,budget);
            for(int sample=0;sample<32;++sample) {
                auto concrete=s;concrete.players[1].hp=1+nextRandom()%456;concrete.players[0].hp=1+nextRandom()%65;
                concrete.players[0].mp=int(input.resource[2].lo+nextRandom()%(input.resource[2].hi-input.resource[2].lo+1));
                concrete.players[0].medicinal_herbs_count=int(input.resource[3].lo+nextRandom()%(input.resource[3].hi-input.resource[3].lo+1));
                auto point=alpha(concrete);unsigned count=0;
                for(const auto& leaf:leaves) if(leaf.guard.contains(point)) {
                    ++count;
                    auto single=step(*rules,point,s.position,command,budget);
                    require(single.size()==1 && single[0].terminal==leaf.terminal,"interval terminal disagreement");
                    require(leaf.terminal!=Terminal::Invalid,"Invalid interval leaf");
                    if(leaf.terminal!=Terminal::ForbiddenFlee)
                        require(single[0].position==leaf.position && image(point,single[0].output)==image(point,leaf.output),"interval update disagreement");
                }
                require(count==1,"nonempty input is missing or multiply covered");
                compare(*rules,seed,concrete,command,budget);
            }
        }
    }
    std::cout<<"interval guards cover all sampled concrete inputs exactly once\n";
}
void checkMaxPlusAndPartitions() {
    auto limits=testLimits();Budget budget(limits);Problem p{12345,initialState()};
    auto rules=battleRules();Partitions family(p,1,*rules);const auto other=family.at(2).nodes;
    require(family.refine({1,0},{0,227,0},8),"partition split failed");
    require(family.at(2).nodes==other,"partition refinement leaked to another RNG position");
    family.at(1).verify(baseBox(p),8,budget);
    Model m;m.horizon=2;m.firstPosition=1;m.lastPosition=3;m.root=0;
    m.cells={{1,0},{2,0},{3,0}};m.cellIndex={{0},{1},{2}};m.support={{0},{1},{2}};m.edges.resize(3);
    Edge flee;flee.command=53;flee.target=1;m.edges[0].push_back(flee);
    Edge win;win.goal=true;win.gain={456,0,0,0};m.edges[1].push_back(win);
    auto dp=maxPlus(m,{},budget);require(dp.root==256*456,"zero-weight FLEE edge lost");
    require(!excludesWin(p,dp,{}),"winning graph was declared false");
    m.edges[1][0].goal=false;m.edges[1][0].target=2;
    dp=maxPlus(m,{},budget);require(dp.root==unreachable,"live B0 must be negative infinity");
    std::cout<<"position-local partitions and max-plus terminal initialization passed\n";
}
void checkSearchAndCertificates() {
    auto limits=testLimits();limits.maxTurns=4;limits.regional=false;
    Problem p{0x1234567,initialState()};p.root.players[1].hp=200;
    auto negative=solveSuffix(p,1,limits);
    require(negative.status==Status::ProvedFalse && negative.certificate!=nullptr,negative.detail.c_str());
    Budget verifyBudget(limits);require(verifyFalse(p,*negative.certificate,verifyBudget),"fresh verifier rejected valid false");
    auto damaged=*negative.certificate;damaged.coverage.records.erase(damaged.coverage.records.begin());
    bool rejected=false;try {Budget b(limits);rejected=!verifyFalse(p,damaged,b);} catch(const std::exception&) {rejected=true;}
    require(rejected,"missing legal command root was accepted");
    damaged=*negative.certificate;damaged.ruleIdentity.program.routines[0].code[0].immediate++;
    Budget identityBudget(limits);require(!verifyFalse(p,damaged,identityBudget),"same version with different program accepted");
    auto wrong=p;wrong.root.players[0].specialChargeTurn-=1;Budget wrongBudget(limits);
    require(!verifyFalse(wrong,*negative.certificate,wrongBudget),"raw root binding omitted inactive timer");
    p.root.players[1].hp=1;
    auto positive=solveSuffix(p,1,limits);
    require(positive.status==Status::Optimal && positive.minimum==1,positive.detail.c_str());
    require(replayWin(p,positive.commands,limits),"returned witness failed independent replay");
    auto atPrefixEnd=extendPrefix(p,positive.commands,0,limits);
    require(atPrefixEnd.status==Status::Optimal && atPrefixEnd.minimum==0,"prefix-final victory not recognized");
    p.root.players[1].hp=18;
    auto multi=solveSuffix(p,3,limits);
    std::cout<<"multi-turn status="<<int(multi.status)<<" N="<<multi.minimum<<" candidates="<<multi.statistics.candidates
        <<" refinements="<<multi.statistics.refinements<<" work="<<multi.statistics.work<<" detail="<<multi.detail<<std::endl;
    require(multi.status==Status::Optimal && multi.minimum>=1,"multi-turn unknown-root proof did not finish");
    require(replayWin(p,multi.commands,limits),"multi-turn witness replay failed");
    limits.regional=true;
    auto regional=solveSuffix(p,3,limits);
    require(regional.status==Status::Optimal && regional.regionCertificate!=nullptr,regional.detail.c_str());
    Budget rb(limits);require(verifyRegions(p,*regional.regionCertificate,rb),"independent regional verification failed");
    require(replayWin(p,regional.commands,limits),"regional witness replay failed");
    auto badRegion=*regional.regionCertificate;badRegion.nodes[badRegion.root].children.fill(-1);
    Budget br(limits);require(!verifyRegions(p,badRegion,br),"missing regional obligations accepted");
    limits.time=std::chrono::milliseconds(0);auto unknown=solveSuffix(p,1,limits);
    require(unknown.status==Status::Unknown,"deadline exhaustion was not UNKNOWN");
    std::cout<<"unknown-root search, adjacent false proof, witness and prefix binding passed\n";
}
}
int main() {
    try {
        checkRegistration();checkBattleCorrespondence();checkIntervalCoverage();
        checkMaxPlusAndPartitions();checkSearchAndCertificates();std::cout<<"all proof checks passed\n";return 0;
    } catch(const proof::Exhausted&) {std::cerr<<"test work budget exhausted\n";return 1;}
      catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
