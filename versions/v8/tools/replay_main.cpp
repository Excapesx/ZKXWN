#include <iostream>
#include <fstream>
#include <chrono>
#include <nlohmann/json.hpp>
#include "rmtc8/bot.hpp"
#include "rmtc8/parse.hpp"
#include "rmtc8/frame_gate.hpp"
int main(int argc,char** argv) {
 using namespace rmtc8;using Json=nlohmann::json;
 int stage=argc>1?std::atoi(argv[1]):2;
 if(stage!=1&&stage!=2){std::cerr<<"Usage: rmtc_replay 1|2 < public_perceptions.jsonl\n";return 2;}
 Bot bot;FrameGate gate;std::string line,cache;int errors=0;
 while(std::getline(std::cin,line)) {
   try {
     if(line.size()>1024*1024)throw std::runtime_error("oversized input");
     auto start=std::chrono::steady_clock::now();Json wrapper=Json::parse(line);
     Json observation=wrapper.contains("perception")?wrapper.at("perception"):wrapper;
     Perception p;std::string why;
     if(!parse_perception(observation.dump(),&p,&why)){++errors;std::cout<<Json{{"rejected",why}}.dump()<<"\n";continue;}
     auto state=gate.accept(p.session,p.tick);
     if(state==FrameGate::Result::Reject){std::cout<<"{\"rejected\":\"stale_frame\"}\n";continue;}
     if(state==FrameGate::Result::Duplicate&&!cache.empty()){std::cout<<cache<<"\n";continue;}
     cache.clear();
     if(state==FrameGate::Result::NewSession)bot.configure(Config{stage,stage==1?100:500});
     if(wrapper.contains("plan")){Plan plan;if(parse_plan(wrapper.at("plan").dump(),p.session,p.tick,stage==1?100:500,p.mapW,p.mapH,&plan))bot.set_plan(plan);else throw std::runtime_error("invalid supplied plan");}
     auto decision=bot.decide(p);auto action=sanitize_action(decision.action,p,stage);
     Json out={{"action",Json::parse(action_json(action,p.tick,p.session))},{"reason",decision.reason},{"utility",decision.value},
               {"decision_ms",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()}};
     cache=out.dump();std::cout<<cache<<"\n";
   }catch(const std::exception&){++errors;std::cout<<"{\"rejected\":\"invalid_input\"}\n";}
 }
 return errors?1:0;
}
