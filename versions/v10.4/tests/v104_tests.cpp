#include <cmath>
#include <iostream>
#include "rmtc9/phase_probe.hpp"
#include "rmtc9/phase_policy.hpp"
#include "rmtc9/parse.hpp"
using namespace rmtc9;
int main(){int checks=0,bad=0;auto check=[&](bool value,const char* why){++checks;if(!value){++bad;std::cerr<<why<<'\n';}};
 auto near=[](double a,double b){return std::abs(a-b)<1e-9;};
 for(double raw:{1.2,.7,1.8,2.6}) {
  check(phase_multiplier(raw,PhaseComponent::Resource,{1,0})==raw,"C0 preserves original floating value");
  check(near(phase_multiplier(raw,PhaseComponent::Resource,{.5,0}),1+.5*(raw-1)),"C1 weakens attraction and suppression symmetrically");
  check(phase_multiplier(raw,PhaseComponent::Resource,{0,0})==1,"C2 removes only numerical preference");
 }
 check(phase_multiplier(1.2,PhaseComponent::Resource,{0,1})==1.2 && phase_multiplier(.7,PhaseComponent::Pursuit,{0,1})==1,"single-resource restoration does not restore pursuit");
 check(phase_multiplier(.7,PhaseComponent::Pursuit,{0,2})==.7 && phase_multiplier(2.6,PhaseComponent::Mission,{0,2})==1,"single-pursuit restoration leaves mission neutral");
 Perception p;p.session="v104";p.team="A";p.mapW=p.mapH=12;p.self={4,4};p.hp=100;p.alive=true;p.vision=4;
 Entity resource;resource.type=EntityType::ResNormal;resource.id="n";resource.value=10;resource.pos={5,4};p.entities={resource};
 Mission reach;reach.id="reach";reach.type="reach";reach.position={4,7};p.missions={reach};
 for(int tick:{0,99,100,320,321,349,350,410,411,499}) {
  p.tick=tick;Bot before;before.configure({2,500});auto a=PhaseProbe::evaluate(before,p,1),b=PhaseProbe::evaluate(before,p,.5),c=PhaseProbe::evaluate(before,p,0);
  check(before.map().width()==1,"probe does not consume the live bot observation");
  check(a.diagnostics.resourceFocus==b.diagnostics.resourceFocus&&a.diagnostics.resourceFocus==c.diagnostics.resourceFocus&&a.diagnostics.routeGateEligible==b.diagnostics.routeGateEligible&&a.diagnostics.routeGateEligible==c.diagnostics.routeGateEligible&&a.diagnostics.collectionWindow==b.diagnostics.collectionWindow&&a.diagnostics.collectionWindow==c.diagnostics.collectionWindow,"numeric intervention preserves structural gates");
  double old=tick>410?2.6:tick>320?1.8:1;bool found=false;
  for(const auto& x:b.diagnostics.phaseCandidates)if(x.kind=="reach"){check(near(x.phaseMultiplier,1+.5*(old-1)),"strict mission time boundary applies correct half multiplier");found=true;}
  if(tick<499)check(found,"trace records a reachable coordinate candidate");
  for(const auto& x:c.diagnostics.phaseCandidates)if(x.phaseBeforeValid)check(x.phaseMultiplier==1,"C2 candidate has no surviving fixed multiplier");
 }
 p.tick=0;Bot before;before.configure({2,500});auto first=PhaseProbe::evaluate(before,p,1);PhaseProbe::evaluate(before,p,0);auto again=PhaseProbe::evaluate(before,p,1);
 check(action_json(first.decision.action,0,p.session)==action_json(again.decision.action,0,p.session)&&near(first.decision.value,again.decision.value),"counterfactual order cannot contaminate C0");
 p.tick=500;for(double alpha:{1.,.5,0.})check(PhaseProbe::evaluate(before,p,alpha).decision.reason=="finished","deadline survives numeric ablation");
 p.tick=0;p.positionRespawn=true;Plan plan;plan.valid=true;plan.session=p.session;Event split;split.id="split";split.kind="split";split.start=0;split.end=5;split.a=split.b={4,4};plan.events={split};before.set_plan(plan);
 for(double alpha:{1.,.5,0.})check(PhaseProbe::evaluate(before,p,alpha).decision.reason=="split_forced_wait","forced post-split WAIT survives numeric ablation");
 p.positionRespawn=false;p.hp=20;p.effectiveDamage=530;Entity foe;foe.type=EntityType::Robot;foe.id=foe.team="B";foe.pos={4,3};foe.hp=100;p.entities={foe};before=Bot{};before.configure({2,500});
 for(double alpha:{1.,.5,0.})check(PhaseProbe::evaluate(before,p,alpha).decision.action.kind==Act::Defend,"lethal attack-before-move safety survives numeric ablation");
 p.hp=100;p.protectedSpawn=true;
 for(double alpha:{1.,.5,0.})check(PhaseProbe::evaluate(before,p,alpha).decision.action.kind!=Act::Attack,"spawn protection still forbids attack");
 p.protectedSpawn=false;p.entities={resource};before=Bot{};before.configure({1,100});
 auto stage1=PhaseProbe::evaluate(before,p,1);
 for(double alpha:{.5,0.})check(action_json(stage1.decision.action,0,p.session)==action_json(PhaseProbe::evaluate(before,p,alpha).decision.action,0,p.session),"stage1 action unaffected by phase weights");
 std::cout<<checks<<" third-round checks, "<<bad<<" failures\n";return bad?1:0;
}
