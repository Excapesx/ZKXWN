// Evaluation only: hidden referee state is recorded AFTER decisions, never fed to Bot.
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <nlohmann/json.hpp>
#include "rmtc9/bot.hpp"
#include "rmtc9/parse.hpp"
#include "referee.hpp"
#include "opponent_adapter.hpp"
using J=nlohmann::json;
int main(int argc,char**argv){
 if(argc!=5)return 2;
 int stage=std::stoi(argv[1]),size=std::stoi(argv[2]),maps=std::stoi(argv[3]);
 unsigned seed=std::stoul(argv[4]);int ticks=stage==1?100:500;
 for(int m=0;m<maps;++m)for(int seat=0;seat<(stage==1?1:4);++seat){
  auto map=bench::generate_map(size,size,stage==1?1:4,seed+m,stage);
  if(stage==2)map.events=bench::generate_events(map,seed+m,"standard");
  bench::Referee ref;ref.reset(map,ticks);rmtc9::Bot bot;bot.configure({stage,ticks});
  std::vector<std::unique_ptr<IPolicy>> rivals;
  for(int i=0;i<ref.robot_count();++i)rivals.push_back(make_v91_bot(stage,ticks));
  std::map<std::string,J> res;
  for(const auto&r:map.resources)res[r.id]={{"id",r.id},{"value",r.value},{"pos",{r.pos.x,r.pos.y}}, {"first_seen",-1},{"taken_tick",-1},{"collector",-1},{"attacks_while_known",0}};
  J reasons=J::object();int depleted=-1,attackWhileAvailable=0;std::vector<int> gained;
  for(int t=0;t<ticks;++t){
   if(stage==2&&t>=2&&bench::has_announcement(map,t-2)){
    auto content=bench::oracle_plan(map,t-2,0);rmtc9::Plan plan;
    if(!rmtc9::parse_plan(content,"bench-session",t-2,ticks,size,size,&plan))return 3;
    bot.set_plan(plan);for(auto& b:rivals)b->plan(content,"bench-session",t-2,ticks,size,size);
   }
   auto p=ref.perceive(seat);
   for(const auto&e:p.entities)if(e.type==rmtc9::EntityType::ResNormal||e.type==rmtc9::EntityType::ResHigh){
    auto& r=res.at(e.id);if(r["first_seen"]==-1){r["first_seen"]=t;r["first_distance_lower_bound"]=std::abs(e.pos.x-p.self.x)+std::abs(e.pos.y-p.self.y);}
   }
   auto d=bot.decide(p);ref.submit(seat,d.action);
   if(!ref.map().resources.empty()){
    auto key=d.reason;if(!reasons.contains(key))reasons[key]=0;reasons[key]=reasons[key].get<int>()+1;
    if(d.action.kind==rmtc9::Act::Attack)++attackWhileAvailable;
   }
   for(auto&[id,r]:res)if(r["first_seen"]!=-1&&r["taken_tick"]==-1&&d.action.kind==rmtc9::Act::Attack)r["attacks_while_known"]=r["attacks_while_known"].get<int>()+1;
   for(int i=0;i<ref.robot_count();++i)if(i!=seat)ref.submit(i,rivals[i]->act(ref.perceive(i)));
   ref.step();std::set<std::string> remaining;for(const auto&r:ref.map().resources)remaining.insert(r.id);
   for(const auto&r:map.resources)if(res[r.id]["taken_tick"]==-1&&!remaining.count(r.id)){
    auto& z=res[r.id];z["taken_tick"]=t;
    for(int i=0;i<ref.robot_count();++i)if(ref.robots()[i].alive&&ref.robots()[i].pos==r.pos){z["collector"]=i;if(i==seat)gained.push_back(t);break;}
   }
   if(depleted<0&&remaining.empty())depleted=t;
  }
  J out={{"map",m},{"seed",seed+m},{"seat",seat},{"stage",stage},{"size",size},{"score",ref.score(seat)}, {"resource",ref.robots()[seat].resourceValue},{"high_count",ref.robots()[seat].highCount},{"depletion_tick",depleted},{"attacks_before_depletion",attackWhileAvailable},{"reasons_before_depletion",reasons},{"pickup_ticks",gained}};
  out["resources"]=J::array();for(auto&[id,r]:res)out["resources"].push_back(r);
  std::cout<<out.dump()<<'\n';
 }
}
