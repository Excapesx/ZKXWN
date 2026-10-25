#include <iostream>
#include "rmtc6/bot.hpp"
#include "rmtc2/bot.hpp"
int main(){
 rmtc2::Perception old;old.session="same";old.tick=10;old.mapW=old.mapH=12;old.self={5,5};old.team="A";old.hp=10;old.alive=true;old.effectiveDamage=100;
 rmtc2::Entity enemy;enemy.type=rmtc2::EntityType::Robot;enemy.team="B";enemy.pos={6,5};enemy.hp=100;old.entities={enemy};
 rmtc6::Perception next;next.session=old.session;next.tick=old.tick;next.mapW=old.mapW;next.mapH=old.mapH;next.self={5,5};next.team="A";next.hp=10;next.alive=true;next.effectiveDamage=100;
 rmtc6::Entity e;e.type=rmtc6::EntityType::Robot;e.team="B";e.pos={6,5};e.hp=100;next.entities={e};
 rmtc2::Bot v4;v4.configure({2,500});rmtc6::Bot v6;v6.configure({2,500});
 auto a=v4.decide(old);auto b=v6.decide(next);
 std::cout<<"HP10, D100, adjacent enemy HP100, no hazards. Incoming attack precedes movement.\n";
 std::cout<<"v4 action="<<static_cast<int>(a.action.kind)<<" reason="<<a.reason<<" value="<<a.value<<"\n";
 std::cout<<"v6 action="<<static_cast<int>(b.action.kind)<<" reason="<<b.reason<<" value="<<b.value<<"\n";
 std::cout<<"Act enum: wait=0 move=1 attack=2 defend=3. Enemy action remains a scenario, not observed truth.\n";
 return a.action.kind!=rmtc2::Act::Attack && b.action.kind==rmtc6::Act::Attack?0:1;
}

