#include <iostream>
#include "rmtc2/bot.hpp"
int main(){using namespace rmtc2;Perception p;p.session="s";p.mapW=p.mapH=12;p.self={5,5};p.team="A";p.hp=100;p.alive=true;
Entity e;e.type=EntityType::Robot;e.team="B";e.pos={6,5};e.hp=20;p.entities={e};
Bot bot;bot.configure({2,500});Plan plan;plan.valid=true;plan.session="s";plan.inverted={0,20};bot.set_plan(plan);auto result=bot.decide(p);
std::cout<<"inversion_attack kind="<<static_cast<int>(result.action.kind)<<" dx="<<result.action.dx<<" dy="<<result.action.dy<<"\n";
bot.configure({2,500});p.entities.clear();plan=Plan{};plan.valid=true;plan.session="s";Hazard h;h.a=h.b={5,5};h.start=0;h.end=1;h.damage=100;h.lethal=true;plan.hazards={h};bot.set_plan(plan);result=bot.decide(p);
std::cout<<"active_pre_move_fatal kind="<<static_cast<int>(result.action.kind)<<" reason="<<result.reason<<"\n";
}
