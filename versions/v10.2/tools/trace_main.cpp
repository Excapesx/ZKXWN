#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include "bench/opponent_adapter.hpp"
#include "bench/referee.hpp"
#include "rmtc9/bot.hpp"
#include "rmtc9/parse.hpp"
#include "rmtc9/policy_variant.hpp"
using J=nlohmann::json;
int main(int argc,char** argv){
 unsigned seed=argc>1?static_cast<unsigned>(std::stoul(argv[1])):420000;
 int seat=argc>2?std::stoi(argv[2]):0;if(seat<0||seat>3)return 2;
 auto map=bench::generate_map(18,18,4,seed,2);map.vision=4;map.events=bench::generate_events(map,seed,"standard");
 bench::Referee ref;ref.reset(map,500);std::vector<std::unique_ptr<IPolicy>> bots;
 int index=0;for(int i=0;i<4;++i){if(i==seat)bots.push_back(make_v101_bot(2,500));else bots.push_back(index++<2?make_v7_bot(2,500):make_v8_bot(2,500));}
 rmtc9::Bot shadow;shadow.configure({2,500});int divergences=0;
 for(int t=0;t<500;++t){
  if(t>=2&&bench::has_announcement(map,t-2)){
   auto text=bench::oracle_plan(map,t-2,0);for(auto& b:bots)b->plan(text,"bench-session",t-2,500,18,18);
   rmtc9::Plan plan;if(rmtc9::parse_plan(text,"bench-session",t-2,500,18,18,&plan))shadow.set_plan(plan);
  }
  for(int i=0;i<4;++i){auto p=ref.perceive(i);auto action=bots[i]->act(p);
   if(i==seat){auto alternative=shadow.decide(p);
    if((action.kind!=alternative.action.kind||action.dx!=alternative.action.dx||action.dy!=alternative.action.dy)&&t<100&&divergences++<8){
     const auto& d=shadow.diagnostics();J j={{"scope","shadow decisions on baseline public observations; only first divergence shares action history"},
      {"tick",t},{"variant",rmtc9::kAblationLevel},{"resource",p.resourceValue},{"damage",p.effectiveDamage},{"hp",p.hp},
      {"reference",rmtc9::action_json(action,t,p.session)},{"candidate",rmtc9::action_json(alternative.action,t,p.session)},
      {"reason",alternative.reason},{"value",alternative.value},{"public_entities",J::array()},{"candidates",J::array()}};
     for(auto& e:p.entities)j["public_entities"].push_back({{"id",e.id},{"team",e.team},{"type",static_cast<int>(e.type)},{"pos",{e.pos.x,e.pos.y}},{"hp",e.hp},{"value",e.value}});
     auto cs=d.candidates;std::stable_sort(cs.begin(),cs.end(),[](const auto&a,const auto&b){return a.priority>b.priority;});
     for(std::size_t k=0;k<std::min<std::size_t>(8,cs.size());++k){auto& c=cs[k];j["candidates"].push_back({{"kind",c.kind},{"id",c.id},{"eta",c.eta},{"stops",c.stops},{"target",{c.target.x,c.target.y}},
      {"points",c.score.points()},{"aux",c.score.auxiliary()},{"priority",c.priority},{"confidence",c.confidence},{"reject",c.rejected}});}
     std::cout<<j.dump()<<'\n';
    }
   }ref.submit(i,action);
  }ref.step();
 }
}
