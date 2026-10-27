#include <cmath>
#include <iostream>
#include <set>
#include <nlohmann/json.hpp>
#include "rmtc9/parse.hpp"
#include "rmtc9/battle.hpp"
#include "rmtc9/bot.hpp"
#include "referee.hpp"
using namespace rmtc9;
int checks=0,failures=0;
void check(bool yes,const char* name){++checks;if(!yes){++failures;std::cout<<"FAIL "<<name<<"\n";}}
bool near(double a,double b){return std::abs(a-b)<1e-8;}
Perception frame(){Perception p;p.session="v6-test";p.team="A";p.tick=10;p.mapW=p.mapH=12;p.self={5,5};p.hp=100;p.alive=true;return p;}
Entity robot(const char* team,Point pos,int hp=100){Entity e;e.type=EntityType::Robot;e.team=team;e.id=team;e.pos=pos;e.hp=hp;return e;}
void physical_batches(){
 auto p=frame();p.entities={robot("B",{6,5},10),robot("C",{6,6})};Plan plan;HazardField hz;hz.reset(12,12);
 auto idle=forecast_battle(p,plan,hz,{&p.entities[0]},BattleMode::Hold);
 auto focused=forecast_battle(p,plan,hz,{&p.entities[0]},BattleMode::FocusSelf);
 auto opportunistic=forecast_battle(p,plan,hz,{&p.entities[0]},BattleMode::Opportunistic);
 check(near(idle.credit,70)&&idle.kills==1,"solo victim 10 attack + 60 award");
 check(near(focused.credit,70)&&near(focused.hpAfter,80),"diagonal rival cannot both hit self and shared victim");
 check(near(opportunistic.credit,35),"opportunistic rival actually shares one lethal batch");
 p.hp=10;p.effectiveDamage=100;auto dying=forecast_battle(p,plan,hz,{&p.entities[0]},BattleMode::FocusSelf);
 check(dying.fatal&&near(dying.credit,70),"mutual death keeps own attack and kill reward before penalty");
 check(near(forecast_adjusted_damage(100,dying.credit,0,1),110),"mutual adjusted damage order");
 p=frame();p.entities={robot("B",{6,5})};p.entities[0].defending=true;plan.reflect={0,20};plan.reflectDamage=20;
 auto reflected=forecast_battle(p,plan,hz,{&p.entities[0]},BattleMode::Hold);
 check(near(reflected.credit,0)&&near(reflected.hpAfter,80),"known reflection is not ordinary attack credit");
 plan=Plan{};p=frame();p.entities={robot("B",{6,5})};plan.vulnerable={0,20};
 auto vulnerable=forecast_battle(p,plan,hz,{&p.entities[0]},BattleMode::FocusSelf);
 check(near(vulnerable.credit,20)&&near(vulnerable.hpAfter,60),"own vulnerability does not double outgoing damage");
 p=frame();p.entities={robot("B",{6,5},20),robot("C",{7,5})};
 Hazard bomb;bomb.a=bomb.b={6,5};bomb.start=10;bomb.end=11;bomb.damage=20;plan=Plan{};plan.hazards={bomb};hz.apply(plan);
 auto deadEnemy=forecast_battle(p,plan,hz,{&p.entities[1]},BattleMode::FocusSelf);
 check(near(deadEnemy.credit,20)&&near(deadEnemy.hpAfter,80),"pre-event casualty cannot shoot or screen farther survivor");
 p=frame();p.entities={robot("B",{6,5}),robot("C",{7,5})};p.entities[0].protectedSpawn=true;hz.clear();
 auto screen=forecast_battle(p,Plan{},hz,{&p.entities[0]},BattleMode::FocusSelf);
 check(near(screen.credit,0)&&near(screen.hpAfter,100),"protected nearest target still screens rear enemy");
 for(int selfHP=1;selfHP<=100;++selfHP)for(int enemyHP=1;enemyHP<=100;enemyHP+=7){
  p=frame();p.hp=selfHP;p.entities={robot("B",{6,5},enemyHP)};
  const auto r=forecast_battle(p,Plan{},hz,{&p.entities[0]},BattleMode::FocusSelf);
  check(near(r.credit,std::min(20,enemyHP)+(enemyHP<=20?60:0)),"ordinary duel actual credit property");
  check(r.fatal==(selfHP<=20)&&near(r.hpAfter,std::max(0,selfHP-20)),"ordinary duel simultaneous hp property");
 }
}
void decision_regressions(){
 auto p=frame();p.hp=10;p.effectiveDamage=100;p.entities={robot("B",{6,5})};Bot b;b.configure({2,500});
 auto d=b.decide(p);check(d.action.kind==Act::Attack&&d.value<0,"negative salvage shot beats certain passive death");
 p.hp=20;Bot defending;defending.configure({2,500});check(defending.decide(p).action.kind==Act::Defend,"defend retains survival advantage");
 p=frame();Entity res;res.type=EntityType::ResNormal;res.id="underfoot";res.pos=p.self;res.value=10;p.entities={res};
 b.configure({2,500});check(b.decide(p).reason=="collect_underfoot","second stage can collect without leaving underfoot resource");
 p=frame();p.vision=0;p.self={0,0};p.mapW=p.mapH=8;p.tick=0;
 for(int stage:{1,2}){Bot explore;explore.configure({stage,100});auto z=p;std::set<Point> seen;
  for(z.tick=0;z.tick<100;++z.tick){seen.insert(z.self);auto a=explore.decide(z).action;if(a.kind==Act::Move)z.self=z.self+Point{a.dx,a.dy};check(z.self.x>=0&&z.self.x<8&&z.self.y>=0&&z.self.y<8,"zero vision remains legal");}
  check(seen.size()==64,"v6 retains complete zero-vision open-map probe");
 }
 p=frame();p.resourceValue=120;
 Entity high=res;high.id="high";high.value=20;high.pos={5,4};res.pos={6,5};
 Mission completed;completed.id="done";completed.type="normal_resources";completed.target=1;
 Mission unfinished=completed;unfinished.id="remaining";
 p.missions={completed,unfinished};p.completedIds={"done"};p.entities={res,high};
 b.configure({2,500});d=b.decide(p);
 check(d.action.kind==Act::Move&&d.action.dx==1,"all mission IDs considered after resource score cap");
 // A sighting which is disproved by a complete current view cannot remain a live target.
 b.configure({2,500});p=frame();p.entities={robot("B",{8,8})};b.decide(p);p.tick++;p.entities.clear();
 d=b.decide(p);check(d.reason!="sighting_search","visible absence invalidates enemy memory");
 p.session="new";p.tick=0;p.mapW=p.mapH=1;p.self={0,0};p.vision=0;d=b.decide(p);
 check(d.action.kind==Act::Wait,"new session clears enemy and chase state");
}
void referee_receipts(){
 bench::MapDef map;map.w=map.h=8;map.wall.assign(64,0);map.spawns={{2,2},{4,2}};map.vision=0;
 bench::Referee ref;ref.reset(map,100);ref.submit(0,Action::move(0,1));ref.step();auto p=ref.perceive(0);
 check(p.lastActionTick==0&&p.lastMoved&&p.self==Point{2,3},"benchmark forwards actual successful receipt");
 ref.submit(0,Action::move(1,0));ref.submit(1,Action::wait());ref.step();
 p=ref.perceive(0);check(p.lastActionTick==1&&p.lastMoved,"new receipt updates tick");
 // Explicit valid three-way conflict, not an invalid diagonal move.
 map.spawns={{2,3},{3,2},{4,3}};ref.reset(map,100);
 ref.submit(0,Action::move(1,0));ref.submit(1,Action::move(0,1));ref.submit(2,Action::move(-1,0));ref.step();
 for(int i=0;i<3;++i)check(!ref.perceive(i).lastMoved&&ref.perceive(i).lastActionTick==0,"conflict receipt reports failed move");
 map.spawns={{2,2},{3,2}};ref.reset(map,100);
 for(int tick=0;tick<5;++tick){ref.submit(0,Action::attack(1,0));ref.submit(1,Action::wait());ref.step();}
 check(!ref.robots()[1].alive&&ref.perceive(1).entities.empty(),"dead frame has no local entities");
 for(int tick=0;tick<3;++tick)ref.step();
 p=ref.perceive(1);check(p.alive&&p.positionRespawn&&p.protectedSpawn,"respawn receipt and protection");
 ref.step();check(!ref.perceive(1).positionRespawn,"respawn marker lasts one frame");
}
void duplicate_team_wire(){
 using J=nlohmann::json;
 J wire={{"tick",0},{"session_id","dup"},{"map",{{"width",12},{"height",12}}},{"vision_radius",4},
 {"self",{{"team","A"},{"position",{5,5}},{"hp",100},{"alive",true}}},
 {"score",{{"resource_value",0},{"effective_damage",0}}},{"visible_entities",J::array()}};
 std::string cell(89,'0');for(int row=0;row<8;++row)cell[row*10+9]='\n';cell[44]='1';cell[45]='2';
 wire["visible_entities"].push_back({{"type","robot"},{"team","B"},{"position",cell},{"hp",100}});
 Perception p;std::string err;
 check(parse_perception(wire.dump(),&p,&err),"one live team observation accepted");
 wire["visible_entities"].push_back(wire["visible_entities"][0]);
 check(parse_perception(wire.dump(),&p,&err),"duplicate observation is skipped, frame remains usable");check(p.entities.size()==1,"duplicate live team is counted once");
}
int main(){duplicate_team_wire();physical_batches();decision_regressions();referee_receipts();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
