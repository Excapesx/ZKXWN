#include <cmath>
#include <iostream>
#include <limits>
#include "rmtc9/bot.hpp"
#include "rmtc9/policy_variant.hpp"
#include "rmtc9/phase_policy.hpp"
using namespace rmtc9;
static int count=0,failures=0;
static void check(bool ok,const char* why){++count;if(!ok){++failures;std::cerr<<"FAIL "<<why<<'\n';}}
static bool near(double a,double b){return std::abs(a-b)<1e-8;}
static Perception frame(){Perception p;p.session="v102";p.team="A";p.mapW=p.mapH=12;p.self={4,4};p.hp=100;p.alive=true;p.vision=4;return p;}
int main(){
 auto huge=phase_policy(0,std::numeric_limits<int>::max(),0,100);
 check(huge.openingEnd==100 && huge.endgameStart==std::numeric_limits<int>::max()-150,
       "phase budgeting avoids integer overflow for an extreme announced limit");
 auto p=frame();Resource a{"a",{5,4},10},b{"b",{6,4},10},h{"h",{4,6},20};
 p.resourceValue=20;auto x=collection_score(p,0,0,{a});p.resourceValue=80;auto y=collection_score(p,0,0,{a});
 check(near(x.resource,2.5)&&near(y.resource,2.5),"linear resource marginal does not grow just because the deficit is larger");
 p.resourceValue=110;check(near(collection_score(p,0,0,{h}).resource,2.5),"collection clips at 120 for the entire branch");
 p.resourceValue=120;check(near(collection_score(p,0,0,{a}).resource,0),"score-capped resource is not new own score");
 p.resourceValue=0;Mission m;m.id="normal";m.type="normal_resources";m.target=2;p.missions={m,m};
 x=collection_score(p,0,0,{a});y=collection_score(p,0,0,{a,b});
 check(x.taskConditional==0 && x.progressAux>0,"partial mission is auxiliary, never completed points");
 check(y.taskConditional==5 && y.resource==5,"two resources finish one mission exactly once, including duplicate mission IDs");
 check(collection_score(p,0,0,{a,a,b}).utility()==y.utility(),"duplicate resource in a route cannot print extra rewards");
 p.completedIds={"normal"};check(collection_score(p,0,0,{a,b}).taskConditional==0,"completed ID earns no second reward");
 Mission reach;reach.id="reach";reach.type="reach";reach.position={6,4};
 p.completedIds.clear();p.missions={m,reach};
 Resource waypoint{"virtual",{6,4},0};
 auto combined=collection_score(p,0,0,{a,waypoint});
 check(combined.resource==2.5 && combined.taskConditional==5,"coordinate waypoint does not manufacture a second collected resource");
 check(collection_score(p,0,0,{a,b}).taskConditional==10,"a collected resource may finish both real task IDs once");
 check(near(death_score_loss(530),.9),"530 damage dying once loses .9 score");
 check(near(death_score_loss(560),0),"560 buffer has no direct score loss on a single death");
 check(buffer_value(500,560)>0 && combat_points(500)==combat_points(560),"buffer is separate future protection utility");
 check(resource_confidence(2,5,0)>resource_confidence(6,5,0),"arriving after a visible rival discounts the opportunity");
 check(resource_confidence(2,-1,60)<resource_confidence(2,-1,0),"old public memory is less confident than a fresh observation");
 p=frame();Entity r;r.id="a";r.type=EntityType::ResNormal;r.value=10;r.pos=a.pos;p.entities={r};
 WorldMap mem;mem.update(p);BlockMask mask;mask.reset(12,12);auto field=bfs(mask,p.self,500);
 auto plans=resource_opportunities(p,mem,mask,field,{},500);
 check(plans.goals.size()==1 && plans.expansions<=2000 && plans.bfsRuns<=12,"planner exposes a bounded opportunity using public memory");
 check(plans.goals.front().score.resource<=2.5 && plans.goals.front().confidence<1,"confidence never manufactures more direct score than the rule allows");
 p.missions={reach};auto joint=resource_opportunities(p,mem,mask,field,{},500);
 bool jointFound=false;for(const auto& c:joint.goals)if(c.kind=="resource_plan" && c.stops>=2 && c.score.taskConditional==5 && c.score.resource>0)jointFound=true;
 check(jointFound,"resource then coordinate task is evaluated as one joint route");
 auto late=resource_opportunities(p,mem,mask,field,{},0);check(late.goals.empty(),"deadline excludes every impossible route");
 p.entities.clear();++p.tick;mem.update(p);check(mem.resources().empty(),"fresh empty view invalidates a formerly seen resource");
 check(mem.disappearedElsewhere()==1,"observed disappearance is recorded without claiming who collected it");
 p=frame();p.entities={r};Bot bot;bot.configure({2,500});auto decision=bot.decide(p);
 check(decision.action.legal_shape(),"production decision remains legal under new score estimates");
 check(bot.diagnostics().resourceSpace==30 && bot.diagnostics().combatSpace==15,"public score spaces are measured in rule points");
 check(!bot.diagnostics().candidates.empty(),"production decision supplies a bounded candidate audit");
 if(kUseOpportunities)check(bot.diagnostics().plannerExpansions>0,"opportunity ablation actually reaches the production planner");
 if(kUseOpportunities){
  p=frame();p.tick=20;p.resourceValue=40;p.missions={reach};r.pos={4,5};p.entities={r};
  bot=Bot{};bot.configure({2,500});decision=bot.decide(p);
  check(decision.action.kind==Act::Move && decision.action.dy==1,
        "recoverable coordinate reward does not preempt the resource-first joint route");
  bool reserved=false;for(const auto& c:bot.diagnostics().candidates)if(c.deferredTaskAux>0 && c.score.taskConditional==5)reserved=true;
  check(reserved,"deferred task preference is explicit auxiliary, not removed rule points");
 }
 p.hp=20;Entity foe;foe.type=EntityType::Robot;foe.id=foe.team="B";foe.pos={4,3};foe.hp=100;
 p.entities={foe};p.effectiveDamage=530;p.session="lethal";bot=Bot{};bot.configure({2,500});decision=bot.decide(p);
 check(decision.action.kind==Act::Defend,"movement cannot dodge a lethal shot that resolves before moving");
 p=frame();bot=Bot{};bot.configure({2,500});Plan plan;plan.valid=true;plan.session=p.session;
 Event e;e.id="blast";e.kind="explosion";e.a=e.b=p.self;e.start=1;e.end=2;e.damage=100;plan.events={e};bot.set_plan(plan);decision=bot.decide(p);
 check(decision.reason=="escape_hazard","announced lethal event preempts ordinary opportunity gains");
 p=frame();p.tick=499;r.pos={8,4};p.entities={r};bot=Bot{};bot.configure({2,500});bot.decide(p);
 for(const auto& c:bot.diagnostics().candidates)if(c.kind=="resource_plan" && c.id=="a")check(c.rejected=="deadline" || c.eta<=1,"late candidate never claims unreachable end-game collection");
 std::cout<<count<<" score/opportunity checks, "<<failures<<" failures; variant="<<kAblationLevel<<'\n';return failures?1:0;
}
