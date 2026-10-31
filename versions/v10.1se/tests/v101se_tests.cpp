// v10.1se semantic fallback tests.
//
// The LLM may return mission_goals in addition to events. The official
// structured `missions` field is always authoritative; the model-derived goals
// are only used when that field is empty.
#include <iostream>
#include <string>
#include <vector>

#include "rmtc9/bot.hpp"
#include "rmtc9/parse.hpp"

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

Perception frame(int tick = 0) {
    Perception p;
    p.session = "v101se";
    p.team = "A";
    p.tick = tick;
    p.mapW = p.mapH = 12;
    p.self = {5, 5};
    p.hp = 100;
    p.alive = true;
    p.vision = 4;
    return p;
}

Plan parse(const std::string& text) {
    Plan plan;
    const bool ok = parse_plan(text, "v101se", 0, 500, 12, 12, &plan);
    check(ok, "plan parses");
    return plan;
}

void mission_goal_variants() {
    const std::string text = R"({
      "events": [],
      "mission_goals": [
        {"id": "n3", "type": "normal_resources", "target": 3},
        {"type": "high_resources", "target": 2},
        {"type": "resource_value", "target": 150},
        {"type": "effective_damage", "target": 700},
        {"type": "reach", "position": [9, 5]},
        {"type": "future_objective", "target": 1},
        {"type": "reach", "position": [99, 99]}
      ]
    })";
    const Plan plan = parse(text);
    check(plan.valid, "plan with mission goals is valid");
    check(plan.events.empty(), "mission-only plan has no events");
    check(plan.missionGoals.size() == 5, "unknown and out-of-bounds goals are dropped");
    check(plan.missionGoals[0].id == "n3" && plan.missionGoals[0].type == "normal_resources" &&
              plan.missionGoals[0].target == 3, "normal goal keeps id/type/target");
    check(plan.missionGoals[3].type == "effective_damage" && plan.missionGoals[3].target == 700,
          "damage goal parsed");
    check(plan.missionGoals[4].type == "reach" && plan.missionGoals[4].position &&
              plan.missionGoals[4].position->x == 9 && plan.missionGoals[4].position->y == 5,
          "reach goal keeps position");
}

void events_and_goals_coexist() {
    const Plan plan = parse(R"({
      "events": [{"kind":"explosion","a":[1,1],"b":[2,2],"start":10,"end":11,"damage":80}],
      "mission_goals": [{"type":"reach","position":[3,3]}]
    })");
    check(plan.events.size() == 1, "event survives mission fallback");
    check(plan.missionGoals.size() == 1, "goal survives event");
}

void malformed_event_is_not_masked_by_goal() {
    Plan plan;
    const bool ok = parse_plan(R"({
      "events": [{"kind":"not_a_real_event","start":10}],
      "mission_goals": [{"type":"reach","position":[3,3]}]
    })", "v101se", 0, 500, 12, 12, &plan);
    check(!ok, "malformed event payload still triggers a bounded retry");
}

void empty_plan_is_valid() {
    const Plan plan = parse("{}");
    check(plan.valid, "empty object remains a valid empty plan");
    check(plan.missionGoals.empty(), "empty object has no fallback goals");
}

void structured_missions_win() {
    Bot bot;
    bot.configure({2, 500});
    Mission fallback;
    fallback.id = "llm_reach";
    fallback.type = "reach";
    fallback.target = 1;
    fallback.position = Point{9, 5};
    bot.set_fallback_missions({fallback});

    Perception p = frame(0);
    const Decision first = bot.decide(p);
    check(first.action.legal_shape(), "fallback-only frame has a legal action");
    check(bot.diagnostics().missionFallback, "fallback is used when structured missions are empty");

    p.tick = 1;
    p.missions = {Mission{"official_reach", "reach", 1, Point{1, 5}}};
    const Decision second = bot.decide(p);
    check(second.action.legal_shape(), "structured-mission frame has a legal action");
    check(!bot.diagnostics().missionFallback, "structured missions override the fallback");

    bot.configure({2, 500});
    p.tick = 2;
    p.missions.clear();
    bot.decide(p);
    check(!bot.diagnostics().missionFallback, "configure clears the fallback");
}

void fallback_filter() {
    Bot bot;
    bot.configure({2, 500});
    Mission badType{"bad", "future_objective", 1, Point{8, 8}};
    Mission badTarget{"bad_target", "normal_resources", 0, std::nullopt};
    Mission badReach{"bad_reach", "reach", 1, std::nullopt};
    Mission good{"good", "normal_resources", 2, std::nullopt};
    bot.set_fallback_missions({badType, badTarget, badReach, good});
    Perception p = frame(0);
    bot.decide(p);
    check(bot.diagnostics().missionFallback, "valid fallback survives filtering");
}
}  // namespace

int main() {
    mission_goal_variants();
    events_and_goals_coexist();
    malformed_event_is_not_masked_by_goal();
    empty_plan_is_valid();
    structured_missions_win();
    fallback_filter();

    std::cout << checks << " v10.1se checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
