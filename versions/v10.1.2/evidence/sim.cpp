#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include "referee.hpp"
#include "opponent_adapter.hpp"
#include "rmtc9/bot.hpp"
#include "rmtc9/parse.hpp"
using J=nlohmann::json;
J run(const bench::MapDef& map,int seat,bool candidate){
 const int ticks=map.stage==1?100:500;bench::Referee ref;ref.reset(map,ticks);
 rmtc9::Bot current;current.configure({map.stage,ticks});
 std::vector<std::unique_ptr<IPolicy>> old;for(int i=0;i<ref.robot_count();++i)old.push_back(make_v1011_bot(map.stage,ticks));
 for(int t=0;t<ticks;++t){
  if(t>=2 && bench::has_announcement(map,t-2)){
   auto text=bench::oracle_plan(map,t-2,0);rmtc9::Plan plan;
   if(rmtc9::parse_plan(text,"bench-session",t-2,ticks,map.w,map.h,&plan))current.set_plan(plan);
   for(auto& bot:old)bot->plan(text,"bench-session",t-2,ticks,map.w,map.h);
  }
  for(int i=0;i<ref.robot_count();++i){auto p=ref.perceive(i);ref.submit(i,candidate&&i==seat?current.decide(p).action:old[i]->act(p));}ref.step();
 }
 const auto& r=ref.robots()[seat];return J{{"score",ref.score(seat)},{"resource",r.resourceValue},{"damage",r.damage},{"missions",r.missionsDone},{"deaths",r.deaths},{"invalid",r.invalid},{"done",r.doneIds}};
}
int main(){for(int scene=0;scene<3;++scene){const int stage=scene==2?1:2;for(int m=0;m<10;++m){
 auto map=bench::generate_map(18,18,stage==1?1:4,103150000+scene*1000+m,stage);
 if(scene==1)map.events=bench::generate_events(map,103150000+scene*1000+m,"standard");
 for(int seat=0;seat<(stage==1?1:4);++seat)std::cout<<J{{"scene",scene},{"map",m},{"seat",seat},{"baseline",run(map,seat,false)},{"candidate",run(map,seat,true)}}.dump()<<'\n';
 }} }

