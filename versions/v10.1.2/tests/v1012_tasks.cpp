#include <iostream>
#include "rmtc9/bot.hpp"
#include "referee.hpp"
using namespace rmtc9;
int failures=0;
void check(bool ok,const char* message){if(!ok){++failures;std::cerr<<message<<'\n';}}
Perception frame(int tick=0){Perception p;p.session="tasks";p.team="A";p.tick=tick;p.mapW=p.mapH=12;p.self={5,5};p.hp=100;p.alive=true;p.vision=4;return p;}
Mission reach(Point at){Mission m;m.id="official-reach";m.type="reach";m.position=at;return m;}
int main(){
 Bot b;b.configure({2,500});auto p=frame();p.missions={reach(p.self)};
 check(b.decide(p).reason=="reach_settle","underfoot task needs settlement");
 p.tick=1;check(b.decide(p).reason!="reach_settle","missing confirmation must not cause endless parking");
 p.session="new-match";p.tick=0;check(b.decide(p).reason=="reach_settle","new match resets provisional settlement");
 b.configure({2,500});p=frame();p.missions={reach(p.self)};p.completedIds={"official-reach"};
 check(b.decide(p).reason!="reach_settle","official completion suppresses settlement");
 b.configure({2,500});p=frame(499);p.missions={reach({6,5})};auto d=b.decide(p);
 check(d.reason=="reach" && d.action.kind==Act::Move && d.action.dx==1,"final tick arrival remains feasible at public coordinate");
 b.configure({2,500});p=frame(499);p.missions={reach({8,5})};
 check(b.decide(p).reason!="reach","unreachable-before-end mission must not win goal ranking");
 b.configure({2,500});p=frame();p.hp=10;p.missions={reach(p.self)};
 Entity enemy;enemy.type=EntityType::Robot;enemy.team=enemy.id="B";enemy.pos={5,4};enemy.hp=100;p.entities={enemy};
 check(b.decide(p).reason!="reach_settle","settlement never claims a fatal defense is safe");
 b.configure({2,500});p=frame();p.missions={reach(p.self)};enemy.hp=20;p.entities={enemy};
 check(b.decide(p).action.kind==Act::Attack,"a safe finishing shot can also settle the reach task");
 bench::MapDef map;map.stage=2;map.w=map.h=8;map.wall.assign(64,0);map.spawns={{1,1}};
 Mission normal;normal.id="normal";normal.type="normal_resources";normal.target=2;
 Mission high;high.id="high";high.type="high_resources";high.target=1;
 map.missions={normal,high,reach({6,1})};
 map.resources={{"n1",{2,1},10},{"n2",{3,1},10},{"h1",{4,1},20}};
 bench::Referee ref;ref.reset(map,100);b.configure({2,100});
 for(int t=0;t<100;++t){ref.submit(0,b.decide(ref.perceive(0)).action);ref.step();}
 check(ref.robots()[0].missionsDone==3,"referee confirms all three tasks at non-default coordinate");
 check(ref.robots()[0].invalid==0,"three-task execution has no invalid actions");
 return failures?1:0;
}
