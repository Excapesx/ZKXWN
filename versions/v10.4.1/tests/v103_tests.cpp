#include <cmath>
#include <iostream>
#include "rmtc9/bot.hpp"
using namespace rmtc9;
int main(){
 int n=0,bad=0;auto check=[&](bool b,const char* why){++n;if(!b){++bad;std::cerr<<why<<'\n';}};
 check(choice_confidence(6,2,0,.25)>choice_confidence(6,2,0,.85),"a rival with another closer option is not treated as committed");
 check(choice_confidence(3,3,0,.85)<choice_confidence(2,3,0,.85),"same-tick contention remains less certain than arriving ahead");
 check(choice_confidence(3,-1,60,0)<choice_confidence(3,-1,0,0),"old resource observation is discounted");
 check(irreversible_resource_loss(2.5,2,-1,0,0,1)==0,"no visible contest cannot establish irreversible one-tick loss");
 check(irreversible_resource_loss(2.5,2,3,0,.85,2)>irreversible_resource_loss(2.5,2,3,0,.85,1),"two-tick delay evaluates its actual ETA boundary");
 check(irreversible_resource_loss(0,2,3,0,.85,2)==0,"score-capped pickup has no direct resource opportunity loss");
 Perception p;p.session="v103";p.team="A";p.mapW=p.mapH=12;p.self={1,1};p.hp=100;p.alive=true;p.vision=4;
 p.resourceValue=110;Resource a{"a",{2,1},20,0},b{"b",{3,1},10,0};
 CandidateAudit capRoute;capRoute.id="b";capRoute.routeIds={"a","b"};capRoute.rivalEta=3;capRoute.rivalCommitment=.85;
 check(direct_target_delay_loss(p,capRoute,b,2,2)==0,"incidental earlier pickup can consume the cap, so no independent later-target loss is charged");
 std::vector<RoutePickup> route{{a,3,1,.85},{b,4,-1,0}};
 auto score=weighted_route_score(p,0,0,route,{});
 double ca=choice_confidence(3,1,0,.85),cb=choice_confidence(4,-1,0,0);
 check(std::abs(score.resource-2.5*(1-(1-ca)*(1-cb)))<1e-9,"failure to collect first stop leaves score room for the next stop");
 Mission m;m.id="two";m.type="normal_resources";m.target=2;p.resourceValue=0;p.missions={m,m};
 a.value=b.value=10;route={{a,1,-1,0},{b,2,-1,0}};score=weighted_route_score(p,0,0,route,{});
 check(std::abs(score.taskConditional-5*.95*.95)<1e-9,"collection mission requires both pickups in the same weighted outcome; duplicate ID earns once");
 auto repeated=route;repeated.push_back(route.front());
 check(std::abs(weighted_route_score(p,0,0,repeated,{}).points()-score.points())<1e-9,"a repeated pickup ID cannot increase expected route points");
 Mission reach;reach.id="reach";reach.type="reach";reach.position={3,1};p.missions.push_back(reach);
 score=weighted_route_score(p,0,0,route,{{3,1}});
 check(std::abs(score.reachTaskConditional-5)<1e-9,"reaching a coordinate is independent of whether a contested resource survives");
 Entity ea;ea.type=EntityType::ResNormal;ea.id="a";ea.pos={2,1};ea.value=10;Entity eb=ea;eb.id="b";eb.pos={4,1};
 p.entities={ea,eb};p.missions.clear();WorldMap mem;mem.update(p);BlockMask mask;mask.reset(12,12);auto f=bfs(mask,p.self,500);
 auto plans=short_resource_routes(p,mem,mask,f,{},500,nullptr);bool incidental=false;
 for(const auto& c:plans.goals)if(c.id=="b"&&c.stops==1){incidental=c.routeIds.size()==2&&c.routeIds[0]=="a"&&c.routeEtas[0]==1&&c.routeEtas[1]==3;}
 check(incidental,"a singleton destination counts the intermediate resource at its real arrival tick");
 check(plans.goals.size()<=36&&plans.expansions<=2000&&plans.bfsRuns<=12,"planning stays within documented budgets");
 auto deadline=short_resource_routes(p,mem,mask,f,{},0,nullptr);check(deadline.goals.empty(),"deadline rejects all pickups");
 HazardField hz;hz.reset(12,12);Plan plan;plan.valid=true;plan.session=p.session;
 Hazard z;z.a=z.b={2,1};z.start=1;z.end=2;z.lethal=true;plan.hazards={z};hz.apply(plan);
 auto dangerous=short_resource_routes(p,mem,mask,f,{},500,&hz);bool unsafe=false;
 for(const auto& c:dangerous.goals)if(c.id=="b"&&c.stops==1)unsafe=true;
 check(!unsafe,"a lethal intermediate route cell excludes the route even if its endpoint is safe");
 p.entities.clear();p.tick=1;mem.update(p);check(mem.resources().empty(),"fresh empty observation invalidates prior targets");
 std::cout<<n<<" second-round checks, "<<bad<<" failures\n";return bad?1:0;
}
