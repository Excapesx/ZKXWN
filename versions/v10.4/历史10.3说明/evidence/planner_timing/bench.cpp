#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include "rmtc9/opportunity_evaluator.hpp"
int main(){using namespace rmtc9;Perception p;p.session="timing";p.team="A";p.mapW=p.mapH=20;p.self={10,10};p.hp=100;p.alive=true;p.vision=4;p.resourceValue=110;
 Point positions[]={{8,8},{9,8},{10,8},{12,8},{8,10},{9,10},{11,10},{12,10},{8,12},{9,12},{11,12},{12,12}};
 for(int i=0;i<12;++i){Entity e;e.id="r"+std::to_string(i);e.pos=positions[i];e.value=i%3?10:20;e.type=e.value==20?EntityType::ResHigh:EntityType::ResNormal;p.entities.push_back(e);}
 Mission normal;normal.id="normal";normal.type="normal_resources";normal.target=2;Mission high=normal;high.id="high";high.type="high_resources";high.target=1;p.missions={normal,high};
 WorldMap mem;mem.update(p);BlockMask mask;mask.reset(20,20);auto field=bfs(mask,p.self,500);std::vector<Field> enemies={bfs(mask,{6,10},500),bfs(mask,{14,10},500)};
 std::vector<double> us;double guard=0;int maxexp=0;
 for(int i=0;i<210;++i){auto t=std::chrono::steady_clock::now();auto r=resource_opportunities(p,mem,mask,field,enemies,500);double elapsed=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-t).count();if(i>=10)us.push_back(elapsed);maxexp=std::max(maxexp,r.expansions);for(auto& c:r.goals)guard+=c.priority;}
 std::sort(us.begin(),us.end());std::cout<<nlohmann::json({{"scope","synthetic 12-resource public-memory planner only; not production latency distribution"},{"samples",us.size()},{"p50_us",us[us.size()/2]},{"p99_us",us[static_cast<size_t>(.99*(us.size()-1))]},{"max_us",us.back()},{"max_expansions",maxexp},{"guard",guard}}).dump(2)<<'\n';
}
