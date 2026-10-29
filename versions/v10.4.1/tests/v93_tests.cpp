#include <iostream>
#include "rmtc9/bot.hpp"

using namespace rmtc9;
static int failures = 0;
static void check(bool ok, const char* why) {
    if (!ok) { ++failures; std::cerr << "FAIL " << why << '\n'; }
}
static Perception opening() {
    Perception p;
    p.session = "v93-shortest-path"; p.team = "A";
    p.mapW = p.mapH = 8; p.self = {1, 0}; p.tick = 0;
    p.vision = 2; p.alive = true; p.hp = 100;
    Entity r; r.type = EntityType::ResHigh; r.id = "r";
    r.pos = {3, 2}; r.value = 20; p.entities = {r};
    return p;
}
int main() {
    // Both right and down reach the resource in four moves. Down reveals four
    // cells, right only three at the top edge. This fixture fails on v9.2.
    Bot bot; bot.configure({2, 500}); auto p = opening();
    auto d = bot.decide(p);
    check((d.reason == "resource" || d.reason == "resource_plan") && d.action.kind == Act::Move &&
          d.action.dx == 0 && d.action.dy == 1, "choose more informative equally short route");
    const Point next{p.self.x + d.action.dx, p.self.y + d.action.dy};
    check(manhattan(next, {3,2}) == 3, "information preference does not lengthen the route");

    Entity wall; wall.type = EntityType::Obstacle; wall.pos = {1,1};
    p = opening(); p.entities.push_back(wall); bot = Bot{}; bot.configure({2,500}); d = bot.decide(p);
    check(d.action.kind == Act::Move && d.action.dx == 1 && d.action.dy == 0,
          "information preference never selects the blocked first step");

    Entity foe; foe.type = EntityType::Robot; foe.id = foe.team = "B";
    foe.pos = {0,2}; foe.hp = 100;
    p = opening(); p.entities.push_back(foe); bot = Bot{}; bot.configure({2,500}); d = bot.decide(p);
    check((d.reason == "resource" || d.reason == "resource_plan") && d.action.dx == 1 && d.action.dy == 0,
          "visible opponent preserves the original risk-scored path");

    p = opening(); bot = Bot{}; bot.configure({2,500});
    Plan plan; plan.valid = true; plan.session = p.session;
    Event event; event.kind = "explosion"; event.id = "future";
    event.a = event.b = {6,6}; event.start = 100; event.end = 101; event.damage = 50;
    plan.events.push_back(event); bot.set_plan(plan); d = bot.decide(p);
    check((d.reason == "resource" || d.reason == "resource_plan") && d.action.dx == 1 && d.action.dy == 0,
          "hazard field preserves time-dependent routing");

    p = opening(); bot = Bot{}; bot.configure({1,100}); d = bot.decide(p);
    check(d.action.kind == Act::Move && d.action.dx == 1 && d.action.dy == 0,
          "stage one retains its existing route selection");
    std::cout << "v9.3 shortest-path exploration: " << failures << " failures\n";
    return failures ? 1 : 0;
}
