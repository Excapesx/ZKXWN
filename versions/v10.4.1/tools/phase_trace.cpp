// Offline same-state numeric counterfactual. Hidden state is read only AFTER
// policies have acted, for auditing the resulting simulated match.
#define main unused_benchmark_main
#include "bench/bench_main.cpp"
#undef main
using namespace rmtc9;
std::pair<double,unsigned> weights(const std::string& label) {
 if(label=="C0")return {1,0};if(label=="C1")return {.5,0};if(label=="C2")return {0,0};
 if(label=="C_R")return {0,1};if(label=="C_P")return {0,2};if(label=="C_M")return {0,4};
 PhaseWeights w;return {w.alpha,w.restoreMask};
}
std::string key(const CandidateAudit& c) {
 return Json::array({c.kind,c.id,c.target.x,c.target.y,c.eta,c.stops,c.routeIds,c.routeEtas}).dump();
}
std::vector<std::string> ranking(const DecisionDiagnostics& d) {
 auto all=d.phaseCandidates;std::stable_sort(all.begin(),all.end(),[](const auto&a,const auto&b){return a.priority>b.priority;});
 std::vector<std::string> keys;std::map<std::string,int> duplicates;
 for(const auto& c:all)if(c.rejected.empty()){auto k=key(c);keys.push_back(k+"#"+std::to_string(duplicates[k]++));}
 return keys;
}
bool common_order_changed(const std::vector<std::string>& a,const std::vector<std::string>& b) {
 std::set<std::string> sa(a.begin(),a.end()),sb(b.begin(),b.end());std::vector<std::string> x,y;
 for(const auto& k:a)if(sb.count(k))x.push_back(k);for(const auto& k:b)if(sa.count(k))y.push_back(k);return x!=y;
}
Json encode_probe(const PhaseProbeResult& r,bool full) {
 const auto& d=r.diagnostics;Json out={{"action",action_json(r.decision.action,0,"")},{"reason",r.decision.reason},{"value",r.decision.value},
  {"resource_scale",d.resourceScale},{"pursuit_scale",d.pursuitScale},{"opening_focus",d.resourceFocus},
  {"route_gate",d.routeGateEligible},{"collection_window",d.collectionWindow},{"candidate_count",d.phaseCandidates.size()},
  {"ranking",ranking(d)},{"candidates",Json::array()}};
 out["selected_key"]=d.selectedValid?key(d.selected):"action:"+r.decision.reason;
 out["selected_target"]=d.selectedValid?Json::array({d.selected.target.x,d.selected.target.y}):Json(nullptr);
 if(d.selectedValid)out["selected"]={{"id",d.selected.id},{"kind",d.selected.kind},{"eta",d.selected.eta},{"route_ids",d.selected.routeIds},{"route_etas",d.selected.routeEtas}};
 auto all=d.phaseCandidates;std::stable_sort(all.begin(),all.end(),[](const auto&a,const auto&b){return a.priority>b.priority;});
 for(std::size_t i=0;i<all.size()&&(full||i<8);++i) {
  const auto& c=all[i];out["candidates"].push_back({{"key",key(c)},{"kind",c.kind},{"id",c.id},{"target",{c.target.x,c.target.y}},
   {"raw_rule_gain",c.nominalGain},{"estimated_resource",c.score.resource},{"estimated_combat",c.score.combat},{"conditional_task",c.score.taskConditional},
   {"auxiliary",c.score.auxiliary()},{"eta",c.eta},{"risk_loss",c.deathLoss},{"delay_aux_loss",c.delayAuxLoss},{"opportunity_loss",c.missedResourceLoss},
   {"phase_component",c.phaseComponent},{"condition",c.phaseCondition},{"original_multiplier",c.phaseOriginal},{"applied_multiplier",c.phaseMultiplier},
   {"before_phase_valid",c.phaseBeforeValid},{"before_phase",c.phaseBeforeValid?Json(c.phaseBefore):Json(nullptr)},
   {"after_phase",c.phaseAfter},{"candidate_generation_dependency",c.phaseFeedsGeneration},{"final_priority",c.priority},
   {"route_ids",c.routeIds},{"route_etas",c.routeEtas},{"rejected",c.rejected}});
 }
 return out;
}
int main(int argc,char** argv) {
 if(argc<5)return 2;unsigned seed=static_cast<unsigned>(std::stoul(argv[1]));int seat=std::stoi(argv[2]);
 std::string actual=argv[3],field=argv[4];int size=argc>5?std::stoi(argv[5]):18;std::string events=argc>6?argv[6]:"standard";
 auto map=bench::generate_map(size,size,4,seed,2);map.vision=4;if(events!="none")map.events=bench::generate_events(map,seed,events);
 bench::Referee ref;ref.reset(map,500);std::vector<std::unique_ptr<IPolicy>> bots(4);int idx=0;
 for(int i=0;i<4;++i)if(i!=seat)bots[i]=make(field[idx++],2,500);
 Bot live;live.configure({2,500});auto [alpha,restore]=weights(actual);PhaseProbe::configure_offline(live,alpha,restore);
 auto frozen=make_v103_bot(2,500);
 for(int t=0;t<500;++t) {
  if(t>=2&&bench::has_announcement(map,t-2)) {
   auto text=bench::oracle_plan(map,t-2,0);for(auto& b:bots)if(b)b->plan(text,"bench-session",t-2,500,size,size);
   frozen->plan(text,"bench-session",t-2,500,size,size);Plan plan;if(parse_plan(text,"bench-session",t-2,500,size,size,&plan))live.set_plan(plan);
  }
  auto p=ref.perceive(seat);std::array<PhaseProbeResult,3> probes={PhaseProbe::evaluate(live,p,1),PhaseProbe::evaluate(live,p,.5),PhaseProbe::evaluate(live,p,0)};
  const auto liveDecision=live.decide(p);const auto original=frozen->act(p);
  if(actual=="C0" && action_json(original,t,p.session)!=action_json(liveDecision.action,t,p.session))throw std::runtime_error("C0/frozen103 action mismatch");
  Json j={{"scope","same public frame and deep-copied pre-observation bot state; actual rollout is separate"},{"seed",seed},{"seat",seat},{"tick",t},{"actual",actual},
   {"self",{p.self.x,p.self.y}},{"hp",p.hp},{"alive",p.alive},{"resource",p.resourceValue},{"damage",p.effectiveDamage},
   {"receipt",{{"tick",p.lastActionTick},{"kind",p.lastActionKind},{"moved",p.lastMoved}}},
   {"executed",action_json(liveDecision.action,t,p.session)},{"reason",liveDecision.reason},{"probes",Json::object()}};
  auto baseRank=ranking(probes[0].diagnostics);bool interesting=false;
  for(int i=1;i<3;++i) {
   const auto& d=probes[i].diagnostics;const auto& base=probes[0].diagnostics;
   if(d.routeGateEligible!=base.routeGateEligible||d.collectionWindow!=base.collectionWindow||d.resourceFocus!=base.resourceFocus)throw std::runtime_error("numeric experiment changed structural gate");
   auto rank=ranking(d);auto x=rank,y=baseRank;std::sort(x.begin(),x.end());std::sort(y.begin(),y.end());
   bool action=action_json(probes[i].decision.action,0,"")!=action_json(probes[0].decision.action,0,"");
   bool changed=common_order_changed(baseRank,rank),setChanged=x!=y;
   bool winner=base.selectedValid!=d.selectedValid ||
      (base.selectedValid && key(base.selected)!=key(d.selected)) || probes[i].decision.reason!=probes[0].decision.reason;
   bool target=base.selectedValid!=d.selectedValid || (base.selectedValid&&base.selected.target!=d.selected.target);
   auto label=i==1?"C1":"C2";j[std::string(label)+"_rank_changed"]=changed;j[std::string(label)+"_action_changed"]=action;
   j[std::string(label)+"_winner_changed"]=winner;j[std::string(label)+"_target_changed"]=target;
   j[std::string(label)+"_candidate_set_changed"]=setChanged;interesting=interesting||changed||action||setChanged;
  }
  for(int i=0;i<3;++i)j["probes"][i==0?"C0":i==1?"C1":"C2"]=encode_probe(probes[i],interesting);
  j["public_entities"]=Json::array();for(const auto& e:p.entities)j["public_entities"].push_back({{"id",e.id},{"team",e.team},{"type",static_cast<int>(e.type)},{"pos",{e.pos.x,e.pos.y}},{"hp",e.hp},{"value",e.value}});
  for(int i=0;i<4;++i)ref.submit(i,i==seat?liveDecision.action:bots[i]->act(ref.perceive(i)));
  auto resources=ref.map().resources;ref.step();const auto& r=ref.robots()[seat];
  j["after"]={{"pos",{r.pos.x,r.pos.y}},{"resource",r.resourceValue},{"damage",r.damage},{"missions",r.missionsDone},{"deaths",r.deaths},{"score",ref.score(seat)},{"moved",r.lastMoved},{"invalid",r.invalid}};
  j["truth_removed_resources"]=Json::array();for(const auto& old:resources)if(std::none_of(ref.map().resources.begin(),ref.map().resources.end(),[&](const auto& now){return old.id==now.id;}))j["truth_removed_resources"].push_back({{"id",old.id},{"pos",{old.pos.x,old.pos.y}},{"value",old.value}});
  std::cout<<j.dump()<<'\n';
 }
}
