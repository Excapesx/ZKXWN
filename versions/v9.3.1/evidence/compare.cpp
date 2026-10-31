#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
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
 std::vector<std::unique_ptr<IPolicy>> old;for(int i=0;i<ref.robot_count();++i)old.push_back(make_v93_bot(map.stage,ticks));
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
int main(){std::atomic<int> next{0};std::mutex output;
auto worker=[&]{for(int job=next++;job<60;job=next++){int scene=job/20,m=job%20,stage=scene==2?1:2;
auto map=bench::generate_map(stage==1?20:24,stage==1?20:24,stage==1?1:4,103190000+scene*1000+m,stage);
if(scene==1)map.events=bench::generate_events(map,103190000+scene*1000+m,"standard");
for(int seat=0;seat<(stage==1?1:4);++seat){auto a=run(map,seat,false),b=run(map,seat,true);std::lock_guard<std::mutex> lock(output);std::cout<<J{{"scene",scene},{"map",m},{"seat",seat},{"v9_3",a},{"v9_3_1",b}}.dump()<<std::endl;}
}};std::vector<std::thread> workers;for(int i=0;i<4;++i)workers.emplace_back(worker);for(auto& t:workers)t.join();}
