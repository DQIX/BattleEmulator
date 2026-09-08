#include "SuffixProof.h"
#include <iostream>
#include <sstream>

int main(int argc,char** argv) {
    try {
        proof::Problem problem{0,proof::initialState()};proof::Limits limits;
        std::vector<int> prefix;int horizon=30;
        for(int i=1;i<argc;++i) {
            std::string arg=argv[i];
            auto value=[&]()->std::string {
                if(++i>=argc) throw std::invalid_argument("missing option value");return argv[i];
            };
            if(arg=="--seed") problem.seed=std::stoull(value(),nullptr,0);
            else if(arg=="--horizon") horizon=std::stoi(value());
            else if(arg=="--ms") limits.time=std::chrono::milliseconds(std::stoll(value()));
            else if(arg=="--work") limits.maxWork=std::stoull(value());
            else if(arg=="--mib") limits.maxBytes=std::stoull(value())*1024*1024;
            else if(arg=="--prefix") {
                std::istringstream input(value());int c;while(input>>c) prefix.push_back(c);
            } else if(arg=="--resources") {
                std::istringstream input(value());
                if(!(input>>problem.root.players[1].hp>>problem.root.players[0].hp>>problem.root.players[0].mp>>problem.root.players[0].medicinal_herbs_count))
                    throw std::invalid_argument("resources are E A M I");
            } else if(arg=="--position") problem.root.position=std::stoi(value());
            else throw std::invalid_argument("unknown argument: "+arg);
        }
        limits.maxTurns=horizon;
        auto result=proof::extendPrefix(problem,prefix,horizon,limits);
        const char* names[]={"OPTIMAL","WIN","PROVED_FALSE","UNKNOWN","ERROR"};
        std::cout<<names[int(result.status)]<<" seed="<<problem.seed<<" prefix_turns="<<prefix.size()
            <<" N="<<(result.commands.empty()?result.minimum:int(result.commands.size()))
            <<" false_through="<<result.provedFalseThrough<<" ms="<<result.statistics.elapsed.count()
            <<" work="<<result.statistics.work<<" bytes="<<result.statistics.peakBytes
            <<" rooms="<<result.statistics.cells<<" transitions="<<result.statistics.edges<<'\n';
        std::cout<<"commands:";for(int c:result.commands) std::cout<<' '<<c;std::cout<<'\n';
        if(!result.detail.empty()) std::cout<<result.detail<<'\n';
        return result.status==proof::Status::Error?1:0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
