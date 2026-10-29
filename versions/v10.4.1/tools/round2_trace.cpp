// Post-game audit only. Each policy receives Referee::perceive(seat), never truth.
#define main unused_benchmark_main
#include "bench/bench_main.cpp"
#undef main
int main(int argc,char** argv) {
 if(argc<5)return 2;
 unsigned seed=static_cast<unsigned>(std::stoul(argv[1]));int seat=std::stoi(argv[2]);
 char actual=policy_argument(argv[3]);std::string field=argv[4];
 int size=argc>5?std::stoi(argv[5]):18;std::string events=argc>6?argv[6]:"standard";
 auto map=bench::generate_map(size,size,4,seed,2);map.vision=4;
 if(events!="none")map.events=bench::generate_events(map,seed,events);
 bench::Referee ref;ref.reset(map,500);std::vector<std::unique_ptr<IPolicy>> bots;
 int other=0;for(int i=0;i<4;++i)bots.push_back(make(i==seat?actual:field[other++],2,500));
 rmtc9::Bot candidate;candidate.configure({2,500});auto baseline=make_v101_bot(2,500);
 for(int t=0;t<500;++t) {
  if(t>=2&&bench::has_announcement(map,t-2)) {
   auto text=bench::oracle_plan(map,t-2,0);
   for(auto& b:bots)b->plan(text,"bench-session",t-2,500,size,size);
   baseline->plan(text,"bench-session",t-2,500,size,size);
   rmtc9::Plan plan;if(rmtc9::parse_plan(text,"bench-session",t-2,500,size,size,&plan))candidate.set_plan(plan);
  }
  auto p=ref.perceive(seat);auto pred=candidate.decide(p);auto legacy=baseline->act(p);
  const auto& d=candidate.diagnostics();auto cs=d.candidates;
  std::stable_sort(cs.begin(),cs.end(),[](const auto&a,const auto&b){return a.priority>b.priority;});
  Json j={{"seed",seed},{"seat",seat},{"tick",t},{"actual_version",argv[3]},
   {"scope","shadow on actual public frames; only before first divergence is history shared"},
   {"self",{p.self.x,p.self.y}},{"hp",p.hp},{"resource",p.resourceValue},{"damage",p.effectiveDamage},
   {"candidate",rmtc9::action_json(pred.action,t,p.session)},{"reference101",rmtc9::action_json(legacy,t,p.session)},
   {"reason",pred.reason},{"value",pred.value},{"entities",Json::array()},{"candidates",Json::array()},
   {"frontiers",d.frontiers},{"target_switches",d.targetSwitches}};
  for(auto& e:p.entities)j["entities"].push_back({{"id",e.id},{"team",e.team},{"type",static_cast<int>(e.type)},{"pos",{e.pos.x,e.pos.y}},{"hp",e.hp},{"value",e.value}});
  if(d.selectedValid){const auto& c=d.selected;j["selected"]={{"id",c.id},{"kind",c.kind},{"target",{c.target.x,c.target.y}},{"eta",c.eta},{"resource_est",c.score.resource},{"task_est",c.score.taskConditional},{"route_ids",c.routeIds},{"route_etas",c.routeEtas}};}
  for(std::size_t k=0;k<std::min<std::size_t>(10,cs.size());++k){auto& c=cs[k];
   j["candidates"].push_back({{"kind",c.kind},{"id",c.id},{"target",{c.target.x,c.target.y}},
    {"eta",c.eta},{"rival_eta",c.rivalEta},{"age",c.age},{"stops",c.stops},
    {"points",c.score.points()},{"resource",c.score.resource},{"task",c.score.taskConditional},
    {"aux",c.score.auxiliary()},{"priority",c.priority},{"confidence",c.confidence},
    {"missed",c.missedResourceLoss},{"deferred",c.deferredTaskAux},{"choice",c.rivalCommitment},
    {"route_ids",c.routeIds},{"route_etas",c.routeEtas},{"reject",c.rejected}});
  }
  for(int i=0;i<4;++i){auto a=bots[i]->act(ref.perceive(i));ref.submit(i,a);if(i==seat)j["executed"]=rmtc9::action_json(a,t,p.session);}
  auto resources=ref.map().resources;ref.step();
  const auto& r=ref.robots()[seat];j["after"]={{"pos",{r.pos.x,r.pos.y}},{"resource",r.resourceValue},{"damage",r.damage},{"missions",r.missionsDone},{"deaths",r.deaths},{"score",ref.score(seat)},{"moved",r.lastMoved}};
  j["truth_removed_resources"]=Json::array();
  for(const auto& old:resources)if(std::none_of(ref.map().resources.begin(),ref.map().resources.end(),[&](const auto& now){return now.id==old.id;}))
   j["truth_removed_resources"].push_back({{"id",old.id},{"pos",{old.pos.x,old.pos.y}},{"value",old.value}});
  std::cout<<j.dump()<<'\n';
 }
}
