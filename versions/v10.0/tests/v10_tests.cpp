#include <iostream>
#include "rmtc9/bot.hpp"
#include "rmtc9/resource_race.hpp"
#include "rmtc9/receipt_audit.hpp"
using namespace rmtc9;
static int failures=0;
static void check(bool ok,const char* message) {
    if(!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
int main() {
    // A later second stop must not inherit its direct-from-start ETA.
    auto tour=solve_arrival_tour({1,1},{{0,2},{2,0}},{5,10},{-1,1},3);
    check(tour.order.size()==2 && tour.order.front()==1,
          "take contested valuable stop before its deadline");
    check(tour.length<=3 && tour.value<=15,"no invented value or time budget");
    auto impossible=solve_arrival_tour({4},{{0}},{20},{-1},3);
    check(impossible.order.empty(),"unreachable-in-budget resource is excluded");
    check(resource_race(1,4).availability>resource_race(4,1).availability,
          "rival timing must affect attainable value");
    check(resource_race(-1,2).availability==0,"unreachable goal earns no value");
    for(int ours=0;ours<20;++ours) {
        double previous=1;
        for(int extra=0;extra<10;++extra) {
            const auto race=resource_race(ours+extra,ours+2);
            check(race.availability>=0 && race.availability<=previous,
                  "arriving later cannot increase expected availability");
            previous=race.availability;
        }
    }
    ReceiptAudit audit;
    auto r=audit.observe("s1",0,-1);
    check(!r.missingReceipt,"initial frame needs no prior receipt"); audit.sent(0);
    r=audit.observe("s1",1,0);check(!r.missingReceipt,"contiguous acknowledgement matches");audit.sent(1);
    r=audit.observe("s1",2,0);check(r.missingReceipt,"stale acknowledgement is visible");audit.sent(2);
    r=audit.observe("s1",5,4);check(!r.missingReceipt && r.gap==2,"observation gaps are not lost-action claims");
    r=audit.observe("s2",0,-1);check(!r.missingReceipt && r.expectedTick==-1,"new session resets receipt history");
    audit.sent(25,"move");r=audit.observe("s2",26,25,"wait");
    check(r.missingReceipt,"matching tick with a different executed action is visible");
    Perception p; p.session="tail";p.team="A";p.tick=91;p.mapW=p.mapH=20;
    p.vision=4;p.self={5,5};p.alive=true;p.hp=100;
    Entity a;a.type=EntityType::ResNormal;a.id="near";a.pos={6,5};a.value=10;
    Entity b=a;b.id="far";b.pos={9,5};p.entities={a,b};
    Bot bot;bot.configure({1,100});auto d=bot.decide(p);
    check(d.action.kind==Act::Move && d.action.dx==1 && d.action.dy==0,
          "tail spends remaining ticks on reachable known resources");
    p.session="fight";p.tick=20;p.self={5,5};p.hp=20;p.resourceValue=60;p.effectiveDamage=530;
    Entity e;e.type=EntityType::Robot;e.team=e.id="B";e.pos={5,4};e.hp=100;p.entities={e};
    bot=Bot{};bot.configure({2,500});d=bot.decide(p);
    check(d.action.kind==Act::Defend,"moving after attacks cannot evade a lethal incoming shot");
    p.session="race-diagnostic";p.tick=0;p.self={4,4};p.hp=100;p.resourceValue=0;p.effectiveDamage=0;p.vision=4;
    a.pos={4,6};e.pos={4,7};e.hp=100;p.entities={a,e};
    bot=Bot{};bot.configure({2,500});d=bot.decide(p);
    check(d.action.legal_shape() && bot.diagnostics().resourceLate==1,
          "visible rival arriving before us is logged without hidden state");
    check(!bot.diagnostics().resourceRaces.empty() && bot.diagnostics().resourceRaces.front().ourEta==2 &&
          bot.diagnostics().resourceRaces.front().rivalEta==1,
          "race trace exposes both arrival estimates");
    std::cout << "v10 race/tail/receipt: " << failures << " failures\n";
    return failures?1:0;
}
