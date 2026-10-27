#include <iostream>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <nlohmann/json.hpp>
#include "rmtc9/bot.hpp"
#include "rmtc9/parse.hpp"
#include "rmtc9/combat.hpp"
#include "referee.hpp"
using namespace rmtc9;
using J=nlohmann::json;
static int checks=0,failures=0;
static void check(bool v,const char* label){++checks;if(!v){++failures;std::cout<<"FAIL "<<label<<'\n';}}
static Perception frame(){Perception p;p.session="v92";p.team="A";p.mapW=p.mapH=7;p.self={3,3};p.tick=300;p.vision=4;p.alive=true;p.hp=100;p.effectiveDamage=515;return p;}
static Entity enemy(Point q,int hp=40){Entity e;e.type=EntityType::Robot;e.id=e.team="B";e.pos=q;e.hp=hp;return e;}
static void cap_deadlock(){
 auto p=frame();p.entities={enemy({6,6})};Bot bot;bot.configure({2,500});auto d=bot.decide(p);
 check(bot.diagnostics().frontiers==0,"fully visible map has no exploration frontier");
 check(bot.diagnostics().combatObjective,"515 still needs one-death buffer");
 check(d.action.kind==Act::Move,"visible enemy beyond attack range does not deadlock after 500");
 p.entities.clear();bot.configure({2,500});d=bot.decide(p);check(d.action.kind==Act::Move,"known map without visible enemy still has patrol objective");
 p=frame();p.effectiveDamage=600;p.entities={enemy({6,6})};bot.configure({2,500});d=bot.decide(p);
 check(!bot.diagnostics().combatObjective&&d.reason=="score_guard","sufficient buffer allows explicit low-risk score preservation");
 Mission m;m.id="more";m.type="effective_damage";m.target=700;p.missions={m};bot.configure({2,500});d=bot.decide(p);
 check(bot.diagnostics().combatObjective&&d.action.kind==Act::Move,"unfinished damage mission overrides buffer threshold");
 p=frame();p.entities={enemy({3,5},20)};bot.configure({2,500});d=bot.decide(p);
 check(d.action.kind==Act::Attack,"capped damage still takes favorable finishing shot for buffer");
 check(combat_points(515)==15&&forecast_adjusted_damage(515,0,0,1)==455,"score cap does not cap raw damage or hide a death deduction");
}
static void fortress(){
 auto p=frame();p.tick=10;p.self={4,2};p.effectiveDamage=0;
 Entity wall;wall.type=EntityType::Obstacle;wall.pos={2,2};p.entities={wall};
 Plan plan;plan.valid=true;plan.session=p.session;Event e;e.kind="fortress";e.id="offset";e.a={1,1};e.b={3,3};e.start=10;e.end=60;e.dwell=10;e.damage=50;e.defendDamage=10;plan.events={e};
 Bot bot;bot.configure({2,500});bot.set_plan(plan);auto d=bot.decide(p);
 check(d.action.kind==Act::Move&&d.action.dx==-1&&d.action.dy==0&&d.reason=="fortress_entry","offset fortress chooses nearby edge even when center is a wall");
 p.tick=11;p.self={3,2};d=bot.decide(p);check(d.reason=="fortress_dwell"&&d.action.kind==Act::Defend,"inside announced fortress holds capture instead of leaving for patrol");
 p.tick=61;d=bot.decide(p);check(d.reason!="fortress_dwell","expired fortress does not pin robot");
 // Eight ticks of valid progress must not be discarded just because fewer
 // than ten ticks remain. The last ten-tick capture window is still usable.
 p=frame();p.self={3,2};p.effectiveDamage=0;plan.events[0].start=10;plan.events[0].end=21;
 bot.configure({2,500});bot.set_plan(plan);
 for(p.tick=10;p.tick<20;++p.tick) {
   d=bot.decide(p);check(d.reason=="fortress_dwell","capture preserves accumulated dwell near event expiry");
 }
 p.tick=20;d=bot.decide(p);check(d.reason!="fortress_dwell","completed capture transitions to fury policy");
}
static void importer(){
 auto dir=std::filesystem::temp_directory_path()/("rmtc92_import_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 std::filesystem::create_directory(dir);auto path=dir/"input.json";
 J j=J::parse(R"({"stage":2,"map":{"width":8,"height":8,"obstacles":[],"resources":[]},
 "robots":[{"spawn":[0,0]},{"spawn":[7,7]}],"missions":[],
 "events":{"enabled":true,"announcement_delay_ticks":5,"definitions":{"unrivaled":{
 "rect":{"top_left":[1,2],"bottom_right":[3,4]},"duration_ticks":50,"stay_ticks":10}},
 "schedule":[{"event":"unrivaled","announce_tick":10}]}})");
 auto load=[&](const J& value,bench::MapDef* map){std::ofstream f(path);f<<value.dump();f.close();std::string error;return bench::load_practice_config(path.string(),2,map,&error);};
 bench::MapDef map;check(load(j,&map),"offset fortress config accepted by offline loader");
 check(map.events.size()==1&&map.events[0].a==Point{1,2}&&map.events[0].b==Point{3,4},"loader preserves announced rectangle rather than recentering it");
 for(const J& bad:std::vector<J>{J(-1),J(257),J(2.5),J("8")}){auto b=j;b["map"]["width"]=bad;check(!load(b,&map),"invalid map dimensions rejected before allocation");}
 {auto b=j;b["map"]["obstacles"]=J::array({{-1,0}});check(!load(b,&map),"negative obstacle rejected before indexing");}
 {auto b=j;b["map"]["resources"]=J::array({{{"id","r"},{"position",{8,0}},{"value",10}}});check(!load(b,&map),"off-map resource rejected");}
 {auto b=j;b["robots"][1]["spawn"]={0,0};check(!load(b,&map),"duplicate spawn rejected");}
 {auto b=j;b["events"]["definitions"]["unrivaled"]["rect"]["bottom_right"]={2,99};check(!load(b,&map),"off-map fortress rejected");}
 std::filesystem::remove(path);
 std::filesystem::remove(dir);
}
int main(){cap_deadlock();fortress();importer();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
