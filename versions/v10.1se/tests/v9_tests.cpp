#include <iostream>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "rmtc9/bot.hpp"
#include "rmtc9/battle.hpp"
#include "rmtc9/parse.hpp"
using namespace rmtc9;
using J=nlohmann::json;
static int checks=0,failures=0;
static void check(bool yes,const char* name){++checks;if(!yes){++failures;std::cout<<"FAIL "<<name<<'\n';}}
static Perception frame(int t){Perception p;p.session="review";p.team="A";p.tick=t;p.mapW=p.mapH=12;p.vision=4;p.self={5,5};p.hp=100;p.alive=true;return p;}
static Entity robot(const char* name,Point at,int hp){Entity e;e.type=EntityType::Robot;e.id=e.team=name;e.pos=at;e.hp=hp;return e;}
static Plan parsed(const J& j){Plan p;check(parse_plan(j.dump(),"review",0,500,12,12,&p),"test plan accepted");return p;}

static void event_identity(){
 J e={{"id","boom"},{"kind","explosion"},{"a",{5,5}},{"b",{5,5}},{"start",20},{"end",21},{"damage",80}};
 auto plan=parsed({{"events",J::array({e,e})}});
 check(plan.events.size()==1,"duplicate ID kept once");
 EventTracker tracker;auto p=frame(20);auto active=tracker.apply(plan,p);HazardField hz;hz.reset(12,12);hz.apply(active);
 check(hz.damageAt(p.self,20)==80,"one explosion costs 80, not 160");
 Bot bot;bot.configure({2,500});bot.set_plan(plan);
 check(bot.decide(p).reason!="pre_event_forced_death","100HP is not forced dead by one 80HP event");
 J conflict=e;conflict["damage"]=200;
 auto first=parsed({{"events",J::array({e,conflict})}});
 check(first.events.size()==1&&first.events[0].damage==80,"same-response conflicting ID keeps first valid fact");
 e["id"]="independent";
 auto separate=parsed({{"events",J::array({e,conflict})}});
 check(separate.events.size()==2,"different IDs may be independent simultaneous events");
 e.erase("id");auto automatic=parsed({{"events",J::array({e,e})}});
 check(automatic.events.size()==1,"generated IDs deduplicate identical no-ID facts");
 auto again=merge_plans(plan,plan,19);check(again.events.size()==1,"later plan merge does not restore a duplicate");
}

static void parser_boundaries(){
 J fort={{"kind","fortress"},{"id","f"},{"a",{4,4}},{"b",{6,6}},{"start",10},{"end",60},{"damage",50},{"dwell",10}};
 auto p=parsed({{"events",J::array({fort})}});check(p.events[0].defendDamage==10,"missing fortress defend damage defaults to 10");
 fort["defend_damage"]=0;p=parsed({{"events",J::array({fort})}});check(p.events[0].defendDamage==0,"explicit zero remains zero");
 fort["defend_damage"]="bad";p=parsed({{"events",J::array({fort})}});check(p.events[0].defendDamage==10,"invalid fortress number falls back safely");
 J good={{"a",{1,1}},{"b",{2,2}},{"start",10},{"end",11},{"damage",30}};
 for(const char* field:{"wall","lethal","respects_protection"})
  for(const J& invalid:std::vector<J>{"yes",1,nullptr,J::array(),J::object()}){
   J bad=good;bad[field]=invalid;
   for(bool badFirst:{false,true}){
    J hazards=badFirst?J::array({bad,good}):J::array({good,bad});
    auto partial=parsed({{"hazards",hazards}});
    check(partial.hazards.size()==1&&partial.hazards[0].damage==30,"bad boolean affects only its own hazard");
   }
  }
 for(const J& window:std::vector<J>{J::array({1e300,20}),J::array({0,1e300}),J::array({-1e300,20}),J::array({0.5,20}),J::array({-1,20}),J::array({20,10}),J::array({0,"20"})}){
  Plan invalid;check(!parse_plan(J{{"effects",{{"inverted",window}}}}.dump(),"review",0,500,12,12,&invalid),"malformed standalone effect rejected");
  auto partial=parsed({{"hazards",J::array({good})},{"effects",{{"inverted",window}}}});
  check(partial.hazards.size()==1&&!partial.inverted.active(1),"valid hazard survives a malformed effect");
 }
 auto valid=parsed({{"effects",{{"inverted",{0,20}}}}});check(valid.inverted.active(0)&&!valid.inverted.active(20),"integer effect uses half-open interval");
}

static void timed_wait(){
 for(int expiry=1;expiry<=12;++expiry){
  Plan plan;plan.valid=true;plan.session="review";Hazard wall;wall.a=wall.b={1,0};wall.start=0;wall.end=expiry;wall.wall=true;plan.hazards={wall};
  HazardField hz;hz.reset(3,1);hz.apply(plan);BlockMask mask;mask.reset(3,1);
  for(int now=0;now<=expiry+1;++now){
   auto f=bfs_timed(mask,hz,{0,0},now,20);
   check(f.at({2,0})==(now<expiry?expiry-now+2:2),"time-expanded distance includes required waiting");
   check(f.firstAt({2,0})==(now<expiry?kFirstWait:1),"first action is WAIT until movement is legal");
  }
 }
 Plan plan;plan.valid=true;plan.session="review";Hazard wall;wall.a=wall.b={1,0};wall.end=5;wall.wall=true;plan.hazards={wall};
 Bot bot;bot.configure({2,500});bot.set_plan(plan);Point at{0,0};
 for(int tick=1;tick<=6;++tick){
  auto p=frame(tick);p.mapW=3;p.mapH=1;p.self=at;
  Entity resource;resource.type=EntityType::ResHigh;resource.id="r";resource.pos={2,0};resource.value=20;p.entities={resource};
  if(tick<5){Entity obstacle;obstacle.type=EntityType::Obstacle;obstacle.pos={1,0};p.entities.push_back(obstacle);}
  auto d=bot.decide(p);
  check(d.action.legal_shape(),"WAIT sentinel never used as a direction index");
  if(tick<5)check(d.action.kind!=Act::Move,"corridor bot holds while wall is active");
  if(d.action.kind==Act::Move)at={at.x+d.action.dx,at.y+d.action.dy};
 }
 check(at==Point{2,0},"corridor bot moves and reaches resource after expiry");
}

static void screened_shooter(){
 Bot bot;bot.configure({2,500});
 for(int t=10;t<14;++t){auto p=frame(t);p.hp=20;p.effectiveDamage=100;
  p.entities={robot("B",{7,5},100),robot("C",{6,5},100-20*(t-10))};bot.decide(p);
 }
 auto p=frame(14);p.hp=20;p.effectiveDamage=150;p.entities={robot("B",{7,5},100)};
 check(bot.decide(p).action.kind==Act::Defend,"removed screen does not turn a live shooter into a passive enemy");
 HazardField hz;hz.reset(12,12);Plan plan;std::set<std::string> quietTeams{"B"};
 check(forecast_battle(p,plan,hz,{&p.entities[0]},BattleMode::FocusSelf,&quietTeams).fatal,
       "FocusSelf keeps a lethal shooter even if a caller passes quiet history");
 // Even genuine quiet history is not a guarantee of the next enemy action.
 Bot quiet;quiet.configure({2,500});
 for(int t=0;t<9;++t){p=frame(t);p.hp=20;p.effectiveDamage=150;p.entities={robot("B",{7,5},100)};
  check(quiet.decide(p).action.kind==Act::Defend,"lethal risk retained after quiet observations");}
 // A protected observer cannot infer that an aligned enemy never shoots.
 Bot protectedBot;protectedBot.configure({2,500});
 for(int t=0;t<6;++t){p=frame(t);p.hp=20;p.effectiveDamage=150;p.protectedSpawn=true;p.entities={robot("B",{7,5},100)};protectedBot.decide(p);}
 p=frame(6);p.hp=20;p.effectiveDamage=150;p.entities={robot("B",{7,5},100)};
 check(protectedBot.decide(p).action.kind==Act::Defend,"protected history is not evidence of harmlessness");
}
int main(){event_identity();parser_boundaries();timed_wait();screened_shooter();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
