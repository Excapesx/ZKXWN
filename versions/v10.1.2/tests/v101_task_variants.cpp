// Task-variation regression tests for the 10.1 decision core.
// These tests do not claim the fixed-task benchmark covers every combination;
// they exercise the five protocol task types with non-default targets/positions.
#include <iostream>
#include <string>
#include <vector>

#include "rmtc9/bot.hpp"

using namespace rmtc9;

namespace {
int checks = 0;
int failures = 0;

void check(bool ok, const char* what) {
    ++checks;
    if (!ok) {
        ++failures;
        std::cerr << "FAIL " << what << '\n';
    }
}

Perception frame() {
    Perception p;
    p.session = "task-variants";
    p.team = "A";
    p.tick = 0;
    p.mapW = p.mapH = 12;
    p.self = {5, 5};
    p.hp = 100;
    p.alive = true;
    p.vision = 4;
    p.resourceValue = 0;
    p.effectiveDamage = 0;
    return p;
}

Entity resource(Point pos, int value, const std::string& id) {
    Entity e;
    e.type = value >= 20 ? EntityType::ResHigh : EntityType::ResNormal;
    e.pos = pos;
    e.value = value;
    e.id = id;
    return e;
}

Entity enemy(Point pos, int hp = 100) {
    Entity e;
    e.type = EntityType::Robot;
    e.pos = pos;
    e.hp = hp;
    e.team = "B";
    e.id = "B";
    return e;
}

Mission mission(const std::string& id, const std::string& type, double target) {
    Mission m;
    m.id = id;
    m.type = type;
    m.target = target;
    return m;
}

Decision decide(Bot& bot, const Perception& p) {
    bot = Bot{};
    bot.configure({2, 500});
    return bot.decide(p);
}

void resource_variant(const char* label, const char* type, double target,
                      int value, int count) {
    Bot bot;
    Perception p = frame();
    p.missions = {mission("dynamic", type, target)};
    for (int i = 0; i < count; ++i) {
        p.entities.push_back(resource({6 + i, 5}, value, "r" + std::to_string(i)));
    }
    const Decision d = decide(bot, p);
    check(d.action.legal_shape(), (std::string(label) + " legal action").c_str());
    check(d.action.kind == Act::Move || d.action.kind == Act::Wait,
          (std::string(label) + " does not attack without a target").c_str());
    check(d.action.kind != Act::Wait || d.reason != "score_guard",
          (std::string(label) + " still pursues the task").c_str());
}

void reach_variant(bool multiple) {
    Bot bot;
    Perception p = frame();
    Mission a = mission("reach_a", "reach", 1);
    a.position = {9, 5};
    p.missions = {a};
    if (multiple) {
        Mission b = mission("reach_b", "reach", 1);
        b.position = {5, 9};
        p.missions.push_back(b);
    }
    const Decision d = decide(bot, p);
    check(d.action.legal_shape(), "reach variant legal action");
    check(d.action.kind == Act::Move, "reach variant moves toward a coordinate");
    check((d.action.dx != 0) || (d.action.dy != 0), "reach variant has direction");
}

void damage_variant() {
    Bot bot;
    Perception p = frame();
    p.missions = {mission("damage_700", "effective_damage", 700)};
    p.entities.push_back(enemy({7, 5}, 100));
    const Decision d = decide(bot, p);
    check(d.action.legal_shape(), "damage variant legal action");
    check(d.action.kind == Act::Attack || d.action.kind == Act::Move,
          "damage variant keeps a combat/approach action");
}

void unknown_type_is_safe() {
    Bot bot;
    Perception p = frame();
    p.missions = {mission("future_objective", "future_objective", 1)};
    p.entities.push_back(resource({6, 5}, 10, "n"));
    const Decision d = decide(bot, p);
    check(d.action.legal_shape(), "unknown task type produces a legal action");
    // Unknown types must not turn into a false reach/collection goal.
    check(d.action.kind != Act::Attack, "unknown task type does not invent an attack");
}

void multiple_mixed_tasks() {
    Bot bot;
    Perception p = frame();
    p.missions = {
        mission("n3", "normal_resources", 3),
        mission("h2", "high_resources", 2),
        mission("value150", "resource_value", 150),
        mission("reach", "reach", 1),
    };
    p.missions[3].position = {9, 5};
    p.entities = {
        resource({6, 5}, 10, "n0"),
        resource({5, 6}, 10, "n1"),
        resource({6, 6}, 20, "h0"),
    };
    const Decision d = decide(bot, p);
    check(d.action.legal_shape(), "mixed task variant legal action");
    check(d.action.kind == Act::Move || d.action.kind == Act::Wait,
          "mixed task variant keeps a legal non-combat action");
}
}  // namespace

int main() {
    resource_variant("normal3", "normal_resources", 3, 10, 3);
    resource_variant("high2", "high_resources", 2, 20, 2);
    resource_variant("value150", "resource_value", 150, 20, 1);
    reach_variant(false);
    reach_variant(true);
    damage_variant();
    unknown_type_is_safe();
    multiple_mixed_tasks();

    std::cout << checks << " task-variation checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
