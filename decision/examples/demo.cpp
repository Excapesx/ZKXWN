#include "rmtc_decision/engine.hpp"
#include "grid_planner.hpp"
#include <iostream>

using namespace rmtc::decision;

GameState example_state() {
    GameState s;
    s.session_id = "local-fixture";
    s.tick = 10;
    s.max_ticks = 500;
    s.stage = Stage::Battle;
    s.width = s.height = 10;
    s.self.position = {3, 3};
    return s;
}

void show(const char* label, GameState s, Mode mode = Mode::TaskFocus) {
    ExampleGridPlanner planner;
    DecisionEngine engine(planner, mode);
    const auto d = engine.decide(s);
    std::cout << label << ": " << (d.submit ? name(d.action.kind) : "do-not-submit");
    if (d.action.direction) std::cout << ' ' << name(*d.action.direction);
    std::cout << " | reason=" << d.reason;
    if (d.goal) std::cout << " | goal=(" << d.goal->x << ',' << d.goal->y << ')';
    std::cout << '\n';
}

int main() {
    auto s = example_state();
    s.self.alive = false; s.self.hp = 0;
    show("1. Dead", s);
    s = example_state();
    s.missions.push_back({"reach", MissionKind::Reach, 1, Point{6, 3}, {}});
    show("2. Reach mission", s);
    s.score.completed_mission_ids = {"reach"};
    s.resources.push_back({"near", {3, 2}, 10});
    show("3. Mission done, collect", s);
    s = example_state();
    s.self.hp = 15;
    s.enemies.push_back({"B", {5, 3}, 100, false, false, s.tick});
    show("4. Cannot dodge this tick's shot", s);
    s = example_state();
    s.enemies.push_back({"B", {5, 3}, 100, false, false, s.tick});
    show("5. Safe ordinary attack", s, Mode::Aggressive);
}
