#include <iostream>
#include <random>
#include <set>
#include <nlohmann/json.hpp>
#include "rmtc7/bot.hpp"
#include "rmtc7/events.hpp"
#include "rmtc7/parse.hpp"
#include "rmtc7/frame_gate.hpp"
#include "referee.hpp"
using namespace rmtc7;using Json=nlohmann::json;
int checks=0,failures=0;
void check(bool b,const char* why){++checks;if(!b){++failures;std::cout<<"FAIL "<<why<<"\n";}}
Perception frame(int tick=0){Perception p;p.session="s";p.tick=tick;p.team="A";p.mapW=p.mapH=12;p.self={5,5};p.hp=100;p.alive=true;p.positionInitial=tick==0;return p;}
Entity robot(std::string team,Point pos,int hp=100){Entity e;e.type=EntityType::Robot;e.team=team;e.pos=pos;e.hp=hp;e.id=team;return e;}
Json observation(){return Json{{"tick",1},{"session_id","s"},{"vision_radius",4},{"map",{{"width",12},{"height",12}}},{"self",{{"team","A"},{"position",{5,5}},{"hp",100},{"alive",true}}},{"visible_entities",Json::array()},{"score",{{"resource_value",0},{"effective_damage",0}}}};}
std::string grid(int x,int y){std::string s(89,'0');for(int r=0;r<8;++r)s[r*10+9]='\n';s[44]='1';if(x||y)s[(4+y)*10+4+x]='2';return s;}
void parser_boundaries(){
 Perception p;std::string err;auto good=observation();check(parse_perception(good.dump(),&p,&err),"minimal official fields");
 auto bad=good;bad["self"]["position"]={4294967301LL,5};check(!parse_perception(bad.dump(),&p,&err),"coordinate wrap rejected");
 bad=good;bad["tick"]=18446744073709551615ULL;check(!parse_perception(bad.dump(),&p,&err),"unsigned overflow rejected");
 bad=good;bad["vision_radius"]=5;check(parse_perception(bad.dump(),&p,&err)&&p.vision==4,"vision clamped to rule maximum");
 bad=good;bad.erase("visible_entities");check(parse_perception(bad.dump(),&p,&err)&&!p.observationComplete,"missing observation keeps memory");
 bad=good;bad["self"]["alive"]=false;check(parse_perception(bad.dump(),&p,&err)&&!p.alive,"inconsistent life state treated as dead");
 bad=good;bad["visible_entities"].push_back({{"type","resource_normal"},{"id","n"},{"value",10},{"position",grid(1,0)}});
 std::string doubleGrid=grid(1,0);doubleGrid[46]='2';bad["visible_entities"][0]["position"]=doubleGrid;check(parse_perception(bad.dump(),&p,&err)&&p.entities.empty(),"multiple entity markers skipped");
 bad["visible_entities"][0]["position"]=grid(1,0);bad["vision_radius"]=0;check(parse_perception(bad.dump(),&p,&err)&&p.entities.empty(),"entity outside declared vision skipped");
 Plan plan;check(!parse_plan(R"({"events":[{"id":"x","kind":"unknown","a":[1,1],"b":[1,1],"start":10,"end":20}]})","s",0,500,12,12,&plan),"unknown event kind rejected");
 check(!parse_plan(R"({"hazards":[{"shape":"rect","a":[-2,1],"b":[2,2],"start":1,"end":2,"damage":10}]})","s",0,500,12,12,&plan),"event coordinates not silently clipped");
 for(int n=0;n<200;++n){bad=good;bad["self"]["position"]={static_cast<long long>(2147483648LL+n),5};check(!parse_perception(bad.dump(),&p,&err),"coordinate overflow property");}
}
void event_rules(){
 EventTracker tracker;Plan plan;plan.valid=true;plan.session="s";
 Event e;e.id="illusion";e.kind="inversion";e.a={4,4};e.b={6,6};e.start=2;e.end=12;e.duration=10;plan.events={e};
 auto p=frame(2);auto active=tracker.apply(plan,p);check(active.inverted.active(2),"inversion activates on current location");
 p.tick=3;p.self={10,10};active=tracker.apply(plan,p);check(active.inverted.active(3),"inversion survives leaving region");
 p.tick=4;p.alive=false;p.hp=0;active=tracker.apply(plan,p);check(active.inverted.active(4),"inversion survives death");
 p.tick=12;check(!tracker.apply(plan,p).inverted.active(12),"half-open effect expiry");
 e.kind="explosion";e.id="boom";e.damage=80;e.start=20;e.end=21;plan.events={e};tracker.reset();p=frame(19);tracker.apply(plan,p);p.tick=20;active=tracker.apply(plan,p);
 HazardField hz;hz.reset(12,12);hz.apply(active);check(hz.damageAt({5,5},20)==80,"explosion once before move");check(hz.damageAt({5,5},20,true)==0,"explosion respects spawn protection");check(hz.damageAt({5,5},21)==0,"explosion not continuous");
 e.kind="split";e.id="wall";e.a=e.b={5,5};e.start=20;e.end=30;plan.events={e};tracker.reset();p=frame(0);p.self={1,1};tracker.apply(plan,p);p=frame(20);active=tracker.apply(plan,p);hz.apply(active);
 check(hz.lethalAt({5,5},20),"split death on activation");check(!hz.lethalAt({5,5},21)&&hz.blockedAt({5,5},21),"split wall not repeatedly lethal");
 e.kind="vulnerability";e.id="v";e.start=10;e.end=40;e.triggerEnd=30;plan.events={e};tracker.reset();p=frame(11);p.lastActionTick=10;p.lastMoved=true;check(tracker.apply(plan,p).vulnerable.active(11),"actual moved receipt triggers next-tick vulnerability");
 tracker.reset();p.lastMoved=false;check(!tracker.apply(plan,p).vulnerable.active(11),"failed move does not trigger vulnerability");
 e.kind="heal";e.id="heal";e.a=e.b={5,5};e.duration=10;e.heal=10;plan.events={e};tracker.reset();p.lastMoved=true;active=tracker.apply(plan,p);check(active.healUntil==20&&active.healRate==10,"entry refresh uses absolute expiry");
 e.kind="fortress";e.id="f";e.start=0;e.end=10;e.dwell=2;e.damage=50;e.defendDamage=10;e.a={4,4};e.b={6,6};plan.events={e};tracker.reset();tracker.apply(plan,frame(0));tracker.apply(plan,frame(1));active=tracker.apply(plan,frame(2));check(active.fury.active(2),"fortress consecutive post-move dwell grants fury");
 tracker.reset();tracker.apply(plan,frame(0));check(!tracker.apply(plan,frame(2)).fury.active(2),"missing frame does not invent dwell");
 Plan older=plan,newer=plan;older.events[0].id="old";newer.events.clear();auto merged=merge_plans(older,newer,1);check(merged.events.size()==1,"active typed event preserved across partial updates");
 Hazard z;z.a=z.b={2,2};z.start=1;z.end=10;z.damage=20;newer.events.clear();newer.hazards={z,z};Plan blank;merged=merge_plans(blank,newer,0);check(merged.hazards.size()==1,"duplicate region not double damage");
}
void decision_rules(){
 Bot bot;bot.configure({2,500});auto p=frame();p.entities={robot("B",{6,5},20)};Plan plan;plan.session="s";plan.valid=true;plan.inverted={0,20};bot.set_plan(plan);
 auto d=bot.decide(p);check(d.action.kind==Act::Attack&&d.action.dx==1,"illusion must not invert attack");
 bot.configure({2,500});Event boom;boom.id="b";boom.kind="explosion";boom.a=boom.b={5,5};boom.start=1;boom.end=2;boom.damage=100;plan=Plan{};plan.valid=true;plan.session="s";plan.events={boom};bot.set_plan(plan);p=frame(0);d=bot.decide(p);check(d.action.kind==Act::Move,"leave before next-tick explosion");
 p=frame(1);d=bot.decide(p);check(d.action.kind==Act::Wait,"active pre-move explosion cannot be escaped now");
 bot.configure({2,500});plan=Plan{};plan.session="s";plan.valid=true;Event fortress;fortress.id="f";fortress.kind="fortress";fortress.a={4,4};fortress.b={6,6};fortress.start=0;fortress.end=10;fortress.dwell=2;fortress.damage=50;fortress.defendDamage=10;plan.events={fortress};bot.set_plan(plan);
 bot.decide(frame(0));bot.decide(frame(1));p=frame(2);p.entities={robot("B",{6,6},40)};d=bot.decide(p);check(d.action.kind==Act::Attack,"fury attacks diagonal target");
 p.effectiveDamage=490;p.entities.push_back(robot("C",{4,4},40));d=bot.decide(p);check(d.value<2.0,"multi-victim score capped once");
 WorldMap memory;p=frame(0);Entity wall;wall.type=EntityType::Obstacle;wall.pos={6,5};p.entities={wall};memory.update(p);p.tick=1;p.observationComplete=false;p.entities.clear();memory.update(p);check(memory.isWall({6,5}),"unknown entity frame does not erase remembered wall");
 p=frame(0);p.session="fresh";p.self={2,2};memory.update(p);check(memory.spawn()&&*memory.spawn()==Point{2,2},"spawn reset per match");
 check(sanitize_action(Action::attack(1,0),p,1).kind==Act::Wait,"stage one attack guard");p.protectedSpawn=true;check(sanitize_action(Action::attack(1,0),p,2).kind==Act::Wait,"protection guard");
}
void simulator_rules(){
 bench::MapDef map;map.w=map.h=8;map.wall.assign(64,0);map.spawns={{2,3},{3,2},{4,3}};
 bench::Referee ref;ref.reset(map,100);ref.submit(0,Action::move(1,0));ref.submit(1,Action::move(0,1));ref.submit(2,Action::move(-1,0));ref.step();
 for(int i=0;i<3;++i)check(ref.robots()[i].pos==map.spawns[i],"all three same-cell contenders fail");
}
void underfoot(){Bot b;b.configure({1,100});auto p=frame();Entity e;e.type=EntityType::ResNormal;e.id="r";e.pos=p.self;e.value=10;p.entities={e};check(b.decide(p).action.kind==Act::Wait,"stage one collects underfoot before leaving");}
void late_effects(){
    EventTracker tracker;Plan plan;plan.valid=true;plan.session="s";Event e;e.id="v";e.kind="vulnerability";e.start=10;e.end=40;e.triggerEnd=30;plan.events={e};
    auto p=frame(11);p.hp=0;p.alive=false;p.lastActionTick=10;p.lastMoved=true;
    check(tracker.apply(plan,p).vulnerable.active(11),"post-move death retains acquired vulnerability");
    WorldMap memory;p=frame(10);Entity wall;wall.type=EntityType::Obstacle;wall.pos={6,5};p.entities={wall};memory.update(p);
    Hazard h;h.a=h.b={6,5};h.start=10;h.end=20;h.wall=true;plan.events.clear();plan.hazards={h};p.tick=11;memory.update(p,&plan);
    check(!memory.isWall({6,5}),"late temporary-wall interpretation removes ambiguous permanent wall");
}
void more_boundaries(){
 Plan parsed;
 check(parse_plan(R"({"events":[{"id":"boom","kind":"explosion","a":[1,1],"b":[3,3],"start":20,"end":21,"damage":80}]})","s",0,500,12,12,&parsed),"typed event wire schema parsed");
 check(!parse_plan(R"({"events":[{"id":"x","kind":"split","a":[1,1],"b":[1,1],"start":20,"end":40},{"id":"x","kind":"split","a":[1,1],"b":[1,1],"start":20,"end":40}]})","s",0,500,12,12,&parsed),"duplicate event ID rejected");
 check(!parse_plan(std::string(100000,'{'),"s",0,500,12,12,&parsed),"malformed nesting bounded");
 Plan previous;previous.valid=true;previous.session="s";Event heal;heal.kind="heal";heal.id="h";heal.start=10;heal.end=20;heal.duration=10;heal.heal=10;previous.events={heal};Plan next;next.valid=true;next.session="s";
 check(merge_plans(previous,next,25).events.size()==1,"point expiry does not discard active healing tail");
 WorldMap mem;auto p=frame();Entity resource;resource.type=EntityType::ResNormal;resource.id="r";resource.pos={6,5};resource.value=10;p.entities={resource};mem.update(p);p.tick=1;p.entities.clear();p.observationComplete=false;mem.update(p);check(mem.resources().size()==1,"incomplete frame cannot erase remembered resource");
 Bot bot;bot.configure({1,100});p=frame();p.entities={resource};auto first=bot.decide(p);check(first.action.kind==Act::Move&&first.action.dx==1,"initial route to resource");p.tick=1;p.lastActionTick=0;p.lastMoved=false;auto retry=bot.decide(p);check(retry.action.kind!=Act::Move||retry.action.dx!=1,"failed edge backoff avoids repeated collision");
 std::mt19937 random(20261024);
 for(int sample=0;sample<30;++sample){int n=16;std::vector<Point> points;for(int i=0;i<n;++i)points.push_back({static_cast<int>(random()%20),static_cast<int>(random()%20)});
   std::vector<int> start(n);std::vector<double> value(n);std::vector<std::vector<int>> dist(n,std::vector<int>(n));
   for(int i=0;i<n;++i){start[i]=manhattan({0,0},points[i]);value[i]=i<12?10:20;for(int j=0;j<n;++j)dist[i][j]=manhattan(points[i],points[j]);}
   auto tour=solve_tour(start,dist,value,50);int length=0,total=0,last=-1;std::set<int> seen;
   for(int id:tour.order){check(id>=0&&id<n&&seen.insert(id).second,"large route unique valid indices");length+=last<0?start[id]:dist[last][id];total+=static_cast<int>(value[id]);last=id;}
   check(length==tour.length&&length<=50&&total==tour.value,"large route budget and reward match independent recomputation");
 }
}
void upstream_regressions(){
 Perception parsed;std::string err;auto wire=observation();
 for(int radius=0;radius<=4;++radius){wire["vision_radius"]=radius;check(parse_perception(wire.dump(),&parsed,&err)&&parsed.vision==radius,"upstream legal vision preserved");}
 for(int value:{-1,5,9}){
  wire["vision_radius"]=value;const int expected=std::max(0,std::min(4,value));
  check(parse_perception(wire.dump(),&parsed,&err)&&parsed.vision==expected,"upstream out-of-range vision clamped");
 }
 for(const auto& value:Json::array({1.5,"4",nullptr,true,18446744073709551615ULL})){
  wire["vision_radius"]=value;check(!parse_perception(wire.dump(),&parsed,&err),"upstream wrong-typed vision rejected");
 }
 for(int stage:{1,2}){
  Bot bot;bot.configure({stage,100});Perception p;p.session="zero";p.team="A";p.mapW=p.mapH=8;p.vision=0;p.self={0,0};p.hp=100;p.alive=true;
  std::set<std::pair<int,int>> visited;bool legal=true;
  for(p.tick=0;p.tick<100;++p.tick){
   visited.insert({p.self.x,p.self.y});auto a=bot.decide(p).action;
   if(a.kind==Act::Move){p.self.x+=a.dx;p.self.y+=a.dy;legal=legal&&p.self.x>=0&&p.self.x<8&&p.self.y>=0&&p.self.y<8;if(!legal)break;}
  }
  check(legal,"upstream zero vision probes stay in map");check(visited.size()==64,"upstream zero vision covers open map");
  check(bot.decide(p).action.kind==Act::Wait,"upstream terminal exploration waits");
  p.session="single";p.tick=0;p.mapW=p.mapH=1;p.self={0,0};check(bot.decide(p).action.kind==Act::Wait,"upstream single cell stays inside");
 }
}
int main(){upstream_regressions();underfoot();late_effects();more_boundaries();parser_boundaries();event_rules();decision_rules();simulator_rules();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}
