#include <iostream>
#include <string>
#include "rmtc9/bot.hpp"
#include "rmtc9/phase_policy.hpp"

using namespace rmtc9;
static int checks=0, failures=0;
static void check(bool ok,const char* why) {
    ++checks;
    if(!ok){++failures;std::cerr<<"FAIL "<<why<<'\n';}
}
static Perception frame(int tick=0) {
    Perception p;p.session="phase";p.team="A";p.tick=tick;p.mapW=p.mapH=12;
    p.self={5,5};p.hp=100;p.alive=true;p.vision=2;
    Entity r;r.id="near";r.type=EntityType::ResNormal;r.value=10;r.pos={6,5};
    p.entities={r};return p;
}
static Decision decide(Bot& b,const Perception& p,int total=500,int stage=2) {
    b=Bot{};b.configure({stage,total});return b.decide(p);
}
int main() {
    Bot b;auto p=frame();auto early=decide(b,p);
    check(early.action.kind==Act::Move && early.action.dx==1,"opening moves to an attainable resource");
    check(b.diagnostics().resourceFocus,"resource focus requires a real reachable resource");
    p.tick=1;p.hp=0;p.alive=false;b.decide(p);
    check(!b.diagnostics().resourceFocus && b.diagnostics().resourceScale==1,
          "dead-frame diagnostics do not reuse the prior live collection focus");
    p=frame(100);auto middle=decide(b,p);
    check(middle.action.kind==Act::Move && early.value>middle.value,"known resource value is more urgent before opening deadline");
    check(std::string(b.diagnostics().strategyPhase)=="adaptive","tick100 uses adaptive objectives");
    p=frame(350);decide(b,p);
    check(std::string(b.diagnostics().strategyPhase)=="finish" && b.diagnostics().pursuitScale<1,
          "late reachable scoring target reduces speculative pursuit");
    p=frame();p.entities.clear();decide(b,p);
    check(!b.diagnostics().resourceFocus,"clock alone never invents abundant resources");
    p=frame();p.hp=40;decide(b,p);
    check(!b.diagnostics().resourceFocus && b.diagnostics().resourceScale==1,"low HP disables opening bonus");
    p=frame();p.resourceValue=120;decide(b,p);
    check(!b.diagnostics().resourceFocus && b.diagnostics().resourceScale==1,"full resource score disables opening bonus");
    p=frame();p.vision=3;p.entities.front().pos={8,5};
    Entity foe;foe.type=EntityType::Robot;foe.id=foe.team="B";foe.pos={5,4};foe.hp=20;
    p.entities.push_back(foe);auto shot=decide(b,p);
    check(shot.action.kind==Act::Attack,"reachable ordinary resource preserves the finishing shot fixture");
    check(b.diagnostics().resourceScale==1 && !b.diagnostics().resourceFocus,"finishing shot disables collection bias");
    p.hp=20;foe.hp=100;p.entities.back()=foe;p.effectiveDamage=530;
    auto danger=decide(b,p);
    check(danger.action.kind==Act::Defend,"opening movement cannot evade attacks that settle first");
    p=frame();b=Bot{};b.configure({2,500});
    Plan plan;plan.valid=true;plan.session=p.session;Event e;e.kind="explosion";e.id="blast";
    e.a=e.b=p.self;e.start=1;e.end=2;e.damage=100;plan.events={e};b.set_plan(plan);
    auto escape=b.decide(p);
    check(escape.reason=="escape_hazard" && escape.action.kind==Act::Move,
          "announced next-tick lethal event interrupts opening collection");
    p=frame(500);auto finished=decide(b,p);
    check(finished.action.kind==Act::Wait,"final frame never starts a late target");
    p=frame();decide(b,p,100);
    check(b.diagnostics().openingEnd==20 && b.diagnostics().endgameStart==70,"shorter announced duration scales phase budgets");
    decide(b,p,500,1);
    check(std::string(b.diagnostics().strategyPhase)=="none","stage one does not enter stage-two phases");
    // A new match must not inherit a late-game phase from the previous match.
    b=Bot{};b.configure({2,500});p=frame(400);b.decide(p);p=frame();p.session="next";b.decide(p);
    check(std::string(b.diagnostics().strategyPhase)=="opening","new session starts from its own public tick");
    std::cout<<checks<<" phase/safety checks, "<<failures<<" failures\n";
    return failures?1:0;
}
