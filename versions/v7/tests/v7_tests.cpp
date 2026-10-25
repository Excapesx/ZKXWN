// v7_tests.cpp — tests for the 7.0 changes that are not covered by the 6.0 suite.
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

#include "rmtc7/battle.hpp"
#include "rmtc7/bot.hpp"
#include "rmtc7/events.hpp"
#include "rmtc7/grid.hpp"
#include "rmtc7/parse.hpp"

using namespace rmtc7;
using Json = nlohmann::json;

static int checks = 0, failures = 0;
static void check(bool yes, const char* name) {
    ++checks;
    if (!yes) { ++failures; std::cout << "FAIL " << name << "\n"; }
}

static Perception base() {
    Perception p;
    p.session = "v7-test";
    p.team = "A";
    p.tick = 0;
    p.mapW = p.mapH = 12;
    p.vision = 4;
    p.self = {5, 5};
    p.hp = 100;
    p.alive = true;
    return p;
}

static void timed_bfs() {
    // A wall at (1,0) is active only from tick 5 to 15. Starting at tick 4,
    // the direct path arrives at tick 5 and must not be used.
    Plan plan;
    Hazard wall;
    wall.a = wall.b = {1, 0};
    wall.start = 5;
    wall.end = 15;
    wall.wall = true;
    plan.hazards = {wall};
    plan.valid = true;
    HazardField hz;
    hz.reset(8, 8);
    hz.apply(plan);
    BlockMask mask;
    mask.reset(8, 8);
    const Field f = bfs_timed(mask, hz, {0, 0}, 4, 20);
    check(f.at({2, 0}) > 0, "timed bfs reaches far side");
    check(f.firstAt({2, 0}) != 1, "timed bfs does not step into a wall active on arrival");
    check(f.firstAt({2, 0}) == 2 || f.firstAt({2, 0}) == 0 || f.firstAt({2, 0}) == 3,
          "timed bfs chooses a legal first step");
    // A wall that is already gone by arrival must not be avoided.
    const Field g = bfs_timed(mask, hz, {0, 0}, 20, 20);
    check(g.firstAt({2, 0}) == 1, "expired wall no longer blocks");
}

static void split_wait() {
    Perception p = base();
    p.tick = 10;
    p.positionRespawn = true;
    Plan plan;
    plan.valid = true;
    plan.session = p.session;
    Event e;
    e.id = "split";
    e.kind = "split";
    e.a = e.b = {6, 6};
    e.start = 10;
    e.end = 20;
    plan.events = {e};
    Bot bot;
    bot.configure({2, 500});
    bot.set_plan(plan);
    const auto d = bot.decide(p);
    check(d.action.kind == Act::Wait && d.reason == "split_forced_wait",
          "split respawn tick is forced WAIT");
}

static void tolerant_wire() {
    Json wire = {{"tick", 0}, {"session_id", "tol"},
                 {"map", {{"width", 12}, {"height", 12}}}, {"vision_radius", 9},
                 {"self", {{"team", "T001"}, {"position", {5, 5}}, {"hp", 120}, {"alive", true}}},
                 {"visible_entities", Json::array()}};
    std::string cell(89, '0');
    for (int row = 0; row < 8; ++row) cell[row * 10 + 9] = '\n';
    cell[44] = '1'; cell[45] = '2';
    wire["visible_entities"].push_back({{"type", "robot"}, {"team", "B"}, {"position", cell}, {"hp", 60}});
    // A bad entity must not discard the whole frame.
    wire["visible_entities"].push_back({{"type", "robot"}, {"team", "C"}, {"hp", 60}});
    wire["visible_entities"].push_back({{"type", "future_entity"}, {"position", cell}});
    Perception p;
    std::string err;
    check(parse_perception(wire.dump(), &p, &err), "tolerant frame accepted");
    check(p.team == "T001", "long-term team id accepted");
    check(p.hp == 100, "hp is clamped to the rule maximum");
    check(p.vision == 4, "vision is clamped to 4");
    check(p.entities.size() == 1, "bad entities are skipped, good one kept");
    check(!p.observationComplete, "skipped observation marks incomplete");

    Json missing = {{"tick", 1}, {"session_id", "tol"},
                    {"map", {{"width", 12}, {"height", 12}}},
                    {"self", {{"team", "A"}, {"position", {5, 5}}, {"hp", 0}, {"alive", true}}}};
    check(parse_perception(missing.dump(), &p, &err), "missing optional arrays accepted");
    check(!p.alive, "alive=true with hp=0 is treated as dead");
}

static void empty_plan() {
    Plan plan;
    check(parse_plan("{}", "s", 0, 500, 24, 24, &plan), "empty plan object accepted");
    check(plan.valid && plan.hazards.empty() && plan.events.empty(), "empty plan is a valid no-op");
    check(parse_plan("{\"hazards\":3}", "s", 0, 500, 24, 24, &plan) == false,
          "wrongly typed hazards still rejected");
}

static void untracked_collection() {
    WorldMap map;
    map.reset(12, 12);
    Perception p = base();
    p.resourceValue = 0;
    map.update(p);
    p.tick = 1;
    p.resourceValue = 10;  // a normal resource, never seen before
    map.update(p);
    check(map.normalCollected() == 1, "untracked normal resource counted");
    p.tick = 2;
    p.resourceValue = 30;  // a high resource
    map.update(p);
    check(map.highCollected() == 1, "untracked high resource counted");
}

static void protected_blocker() {
    Perception p = base();
    p.entities.clear();
    Entity b;
    b.type = EntityType::Robot; b.team = "B"; b.id = "B"; b.pos = {6, 5}; b.hp = 100; b.protectedSpawn = true;
    Entity c;
    c.type = EntityType::Robot; c.team = "C"; c.id = "C"; c.pos = {7, 5}; c.hp = 100;
    p.entities = {b, c};
    Plan plan;
    HazardField hz;
    hz.reset(12, 12);
    const auto f = forecast_battle(p, plan, hz, {}, BattleMode::FocusSelf);
    check(!f.fatal && std::abs(f.hpAfter - 100) < 1e-9, "protected nearest target screens the rear enemy");
}

static void heal_entry() {    EventTracker tracker;
    Plan plan;
    plan.valid = true;
    Event e;
    e.id = "heal"; e.kind = "heal"; e.a = e.b = {5, 5};
    e.start = 0; e.end = 20; e.duration = 8; e.heal = 5;
    plan.events = {e};
    Perception p = base();
    p.self = {4, 4};
    p.positionInitial = true;
    tracker.apply(plan, p);
    p.tick = 1;
    p.self = {5, 5};
    p.lastActionTick = 0;
    p.lastMoved = true;
    const Plan first = tracker.apply(plan, p);
    check(first.healUntil == 8, "entering a heal point grants the buff");
    p.tick = 2;
    p.self = {5, 5};
    p.lastActionTick = 1;
    p.lastMoved = true;
    const Plan second = tracker.apply(plan, p);
    check(second.healUntil <= 8, "moving inside a heal point does not refresh it");
}

static void event_goals() {
    {
        Perception p = base();
        p.self = {1, 1};
        p.hp = 30;
        Plan plan;
        plan.valid = true;
        plan.session = p.session;
        Event e;
        e.id = "heal"; e.kind = "heal"; e.a = e.b = {6, 6};
        e.start = 0; e.end = 20; e.duration = 8; e.heal = 5;
        plan.events = {e};
        Bot bot;
        bot.configure({2, 500});
        bot.set_plan(plan);
        const auto d = bot.decide(p);
        check(d.reason == "heal_entry", "low HP prioritises the heal point");
    }
    {
        Perception p = base();
        p.self = {1, 1};
        p.vision = 0;  // remove information-goal competition; isolate the fortress goal
        Plan plan;
        plan.valid = true;
        plan.session = p.session;
        Event e;
        e.id = "fortress"; e.kind = "fortress"; e.a = {5, 5}; e.b = {7, 7};
        e.start = 0; e.end = 24; e.dwell = 4; e.damage = 35; e.defendDamage = 7;
        plan.events = {e};
        Bot bot;
        bot.configure({2, 500});
        bot.set_plan(plan);
        const auto d = bot.decide(p);
        check(d.reason == "fortress_entry", "fortress is a high-priority objective");
    }
}

int main() {
    timed_bfs();
    split_wait();
    tolerant_wire();
    empty_plan();
    untracked_collection();
    protected_blocker();
    heal_entry();
    event_goals();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
