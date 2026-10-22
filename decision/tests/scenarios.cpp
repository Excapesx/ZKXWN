#include "rmtc_decision/engine.hpp"
#include "grid_planner.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace rmtc::decision;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
GameState state(Stage stage = Stage::Battle) {
    GameState s;
    s.session_id = "fixture-one";
    s.tick = 10;
    s.max_ticks = stage == Stage::Collection ? 100 : 500;
    s.stage = stage;
    s.width = s.height = 16;
    s.self.position = {3, 3};
    return s;
}
Enemy enemy(const GameState& s, Point p, double hp = 100, bool defending = false, bool protected_spawn = false) {
    return {"B", p, hp, defending, protected_spawn, s.tick};
}
EffectSnapshot effects(const GameState& s) {
    EffectSnapshot result;
    result.session_id = s.session_id;
    result.validated = true;
    return result;
}
Decision choose(const GameState& s, Mode mode = Mode::TaskFocus) {
    ExampleGridPlanner planner;
    DecisionEngine engine(planner, mode);
    return engine.decide(s);
}
void expect_action(const Decision& d, ActionKind kind, std::optional<Direction> dir = {}) {
    require(d.submit, "expected a publishable action");
    require(d.action.kind == kind, "unexpected action kind");
    require(d.action.direction == dir, "unexpected direction");
}

class BrokenPlanner final : public Planner {
public:
    std::optional<Route> plan(const RouteQuery&) const override { return Route{20, Direction::Left}; }
};
class ThrowingPlanner final : public Planner {
public:
    std::optional<Route> plan(const RouteQuery&) const override { throw std::runtime_error("fixture planner error"); }
};

int main() {
    int passed = 0, failed = 0;
    const auto test = [&](const char* name, const std::function<void()>& body) {
        try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cout << "FAIL " << name << ": " << error.what() << '\n'; }
    };

    test("dead robot waits in both stages", [] {
        for (const auto stage : {Stage::Collection, Stage::Battle}) {
            auto s = state(stage); s.self.alive = false; s.self.hp = 0;
            expect_action(choose(s), ActionKind::Wait);
        }
    });
    test("zero HP wins over inconsistent alive flag", [] {
        auto s = state(); s.self.hp = 0;
        expect_action(choose(s), ActionKind::Wait);
    });
    test("invalid and nonfinite state is not submitted", [] {
        auto s = state(); s.session_id.clear(); require(!choose(s).submit, "empty session accepted");
        s = state(); s.width = 0; require(!choose(s).submit, "invalid map accepted");
        s = state(); s.self.position = {-1, 0}; require(!choose(s).submit, "outside self accepted");
        s = state(); s.self.hp = std::numeric_limits<double>::quiet_NaN();
        require(!choose(s).submit, "NaN accepted");
    });
    test("final state must not publish", [] {
        auto s = state(); s.tick = s.max_ticks;
        require(!choose(s).submit, "action at final state");
    });
    test("stage one prefers nearby normal over distant high resource", [] {
        auto s = state(Stage::Collection);
        s.resources = {{"small", {1, 3}, 10}, {"large", {11, 3}, 20}};
        const auto d = choose(s);
        expect_action(d, ActionKind::Move, Direction::Left);
        require(d.goal == Point{1, 3}, "wrong value per path cost");
    });
    test("stage one never attacks or defends", [] {
        auto s = state(Stage::Collection);
        s.enemies = {enemy(s, {5, 3})};
        expect_action(choose(s, Mode::Aggressive), ActionKind::Wait);
    });
    test("resource standing position waits for collection", [] {
        auto s = state(Stage::Collection); s.resources = {{"here", s.self.position, 10}};
        const auto d = choose(s); expect_action(d, ActionKind::Wait);
        require(d.reason == "resource", "did not select collection");
    });
    test("unreachable and out-of-map resource is ignored", [] {
        auto s = state(Stage::Collection);
        s.known_walls = {{1, 0}, {0, 1}};
        s.resources = {{"trapped", {0, 0}, 20}, {"invalid", {-10, 3}, 20}, {"ok", {3, 2}, 10}};
        const auto d = choose(s); expect_action(d, ActionKind::Move, Direction::Up);
        require(d.goal == Point{3, 2}, "unreachable goal selected");
    });
    test("route must fit remaining turns", [] {
        auto s = state(Stage::Collection); s.tick = 99;
        s.resources = {{"too-far", {6, 3}, 20}};
        expect_action(choose(s), ActionKind::Wait);
    });
    test("empty local resource view explores instead of assuming exhausted map", [] {
        auto s = state(Stage::Collection); s.exploration_targets = {{3, 1}};
        const auto d = choose(s); expect_action(d, ActionKind::Move, Direction::Up);
        require(d.reason == "explore", "did not explore");
    });
    test("combined resource and task gain can beat coordinate task", [] {
        auto s = state(); s.resources = {{"ordinary", {3, 2}, 10}};
        s.missions = {{"reach", MissionKind::Reach, 1, Point{4, 3}, {}},
                      {"two-normal", MissionKind::NormalResources, 2, {}, 1}};
        const auto d = choose(s); expect_action(d, ActionKind::Move, Direction::Up);
        require(d.reason == "resource", "coordinate task hard coded first");
    });
    test("completed task is not pursued again", [] {
        auto s = state(); s.missions = {{"reach", MissionKind::Reach, 1, Point{5, 3}, {}}};
        s.score.completed_mission_ids = {"reach"};
        expect_action(choose(s), ActionKind::Wait);
    });
    test("reach uses current mission coordinates", [] {
        auto s = state(); s.missions = {{"reach", MissionKind::Reach, 1, Point{3, 5}, {}}};
        const auto d = choose(s); expect_action(d, ActionKind::Move, Direction::Down);
        require(d.goal == Point{3, 5}, "task coordinates ignored");
    });
    test("resource value mission uses score progress", [] {
        auto s = state(); s.score.resource_value = 110;
        s.resources = {{"near", {3, 2}, 10}, {"far", {6, 3}, 20}};
        s.missions = {{"value", MissionKind::ResourceValue, 120, {}, {}}};
        expect_action(choose(s), ActionKind::Move, Direction::Up);
    });
    test("unfinished high resource mission affects target selection", [] {
        auto s = state(); s.score.resource_value = 120;
        s.resources = {{"ordinary", {3, 2}, 10}, {"high", {7, 3}, 20}};
        s.missions = {{"high-needed", MissionKind::HighResources, 1, {}, 0}};
        const auto d = choose(s); expect_action(d, ActionKind::Move, Direction::Right);
        require(d.goal == Point{7, 3}, "high-resource task ignored");
    });
    test("damage mission can reward an attack after damage score cap", [] {
        auto s = state(); s.score.effective_damage = 300;
        s.enemies = {enemy(s, {5, 3})}; s.resources = {{"near", {3, 2}, 10}};
        s.missions = {{"damage-needed", MissionKind::EffectiveDamage, 310, {}, {}}};
        expect_action(choose(s), ActionKind::Attack, Direction::Right);
    });
    test("protection prevents own attack", [] {
        auto s = state(); s.self.protected_spawn = true; s.enemies = {enemy(s, {5, 3})};
        require(choose(s, Mode::Aggressive).action.kind != ActionKind::Attack, "protected self attacked");
    });
    test("nearest protected enemy screens a farther target", [] {
        auto s = state(); s.enemies = {enemy(s, {4, 3}, 100, false, true), enemy(s, {5, 3})};
        require(choose(s, Mode::Aggressive).action.kind != ActionKind::Attack, "shot through protected vehicle");
    });
    test("ordinary attack crosses a static wall", [] {
        auto s = state(); s.known_walls = {{4, 3}}; s.enemies = {enemy(s, {5, 3})};
        expect_action(choose(s, Mode::Aggressive), ActionKind::Attack, Direction::Right);
    });
    test("defend survives a shot that movement cannot dodge", [] {
        auto s = state(); s.self.hp = 15; s.enemies = {enemy(s, {5, 3})};
        s.resources = {{"escape-resource", {3, 2}, 20}};
        expect_action(choose(s), ActionKind::Defend);
    });
    test("simultaneous kill cannot cancel enemy attack", [] {
        auto s = state(); s.self.hp = 15; s.enemies = {enemy(s, {5, 3}, 10)};
        expect_action(choose(s, Mode::Aggressive), ActionKind::Defend);
    });
    test("when death is unavoidable an attack can still gain damage", [] {
        auto s = state(); s.self.hp = 5; s.enemies = {enemy(s, {5, 3}, 10)};
        expect_action(choose(s), ActionKind::Attack, Direction::Right);
    });
    test("fresh opposing blocker screens incoming fire", [] {
        auto s = state(); s.self.hp = 15;
        s.enemies = {enemy(s, {4, 3}, 100, false, true), enemy(s, {5, 3})};
        expect_action(choose(s), ActionKind::Wait);
    });
    test("historical enemy is not a current attack target", [] {
        auto s = state(); auto e = enemy(s, {5, 3}); --e.observed_tick; s.enemies = {e};
        expect_action(choose(s, Mode::Aggressive), ActionKind::Wait);
    });
    test("modes share legality but choose different tradeoffs", [] {
        auto s = state(); s.enemies = {enemy(s, {5, 3})}; s.resources = {{"near", {3, 2}, 10}};
        expect_action(choose(s, Mode::TaskFocus), ActionKind::Move, Direction::Up);
        expect_action(choose(s, Mode::Aggressive), ActionKind::Attack, Direction::Right);
        expect_action(choose(s, Mode::Survival), ActionKind::Move, Direction::Up);
    });
    test("aggressive mode approaches a visible short-range target", [] {
        auto s = state(); s.enemies = {enemy(s, {7, 3})};
        const auto d = choose(s, Mode::Aggressive);
        expect_action(d, ActionKind::Move, Direction::Right);
        require(d.reason == "approach_enemy", "did not approach");
    });
    test("movement inversion changes command but not desired destination", [] {
        auto s = state(); s.resources = {{"up", {3, 2}, 10}}; s.effects = effects(s);
        s.effects->inverted_movement = TickWindow{10, 12};
        const auto d = choose(s); expect_action(d, ActionKind::Move, Direction::Down);
        require(d.expected_position == Point{3, 2}, "double inverted actual position");
    });
    test("illusion does not reverse attack direction", [] {
        auto s = state(); s.enemies = {enemy(s, {5, 3})}; s.effects = effects(s);
        s.effects->inverted_movement = TickWindow{10, 20};
        expect_action(choose(s, Mode::Aggressive), ActionKind::Attack, Direction::Right);
    });
    test("effect is inactive at the exclusive end tick", [] {
        auto s = state(); s.tick = 12; s.resources = {{"up", {3, 2}, 10}}; s.effects = effects(s);
        s.effects->inverted_movement = TickWindow{10, 12};
        expect_action(choose(s), ActionKind::Move, Direction::Up);
    });
    test("old-session and unvalidated effects cannot alter moves", [] {
        auto s = state(); s.resources = {{"up", {3, 2}, 10}}; s.effects = effects(s);
        s.effects->inverted_movement = TickWindow{0, 20}; s.effects->session_id = "other";
        expect_action(choose(s), ActionKind::Move, Direction::Up);
        s.effects->session_id = s.session_id; s.effects->validated = false;
        expect_action(choose(s), ActionKind::Move, Direction::Up);
    });
    test("known reflection avoids firing at a reported defender", [] {
        auto s = state(); s.enemies = {enemy(s, {5, 3}, 100, true)}; s.effects = effects(s);
        s.effects->reflection = TickWindow{0, 20};
        require(choose(s, Mode::Aggressive).action.kind != ActionKind::Attack, "ignored reflection");
    });
    test("known vulnerability changes whether defense can save the robot", [] {
        auto s = state(); s.self.hp = 15; s.enemies = {enemy(s, {5, 3})};
        expect_action(choose(s), ActionKind::Defend);
        s.effects = effects(s); s.effects->vulnerability = TickWindow{0, 20};
        expect_action(choose(s), ActionKind::Attack, Direction::Right);
    });
    test("world split forced wait is honored", [] {
        auto s = state(); s.resources = {{"up", {3, 2}, 20}}; s.effects = effects(s);
        s.effects->forced_wait = TickWindow{10, 11};
        expect_action(choose(s), ActionKind::Wait);
    });
    test("temporary obstruction expires without becoming a permanent wall", [] {
        auto s = state(); s.resources = {{"up", {3, 2}, 10}};
        s.temporary_blocks = {{{3, 2}, {10, 11}, s.session_id}};
        expect_action(choose(s), ActionKind::Wait);
        s.tick = 11; expect_action(choose(s), ActionKind::Move, Direction::Up);
        s.tick = 10; s.temporary_blocks[0].session_id = "other";
        expect_action(choose(s), ActionKind::Move, Direction::Up);
    });
    test("occupied cell is not entered or recorded as a permanent wall", [] {
        auto s = state(); s.self.protected_spawn = true;
        s.resources = {{"up", {3, 2}, 10}};
        s.enemies = {enemy(s, {3, 2}, 100, false, true)};
        expect_action(choose(s), ActionKind::Wait);
        ++s.tick; s.enemies.clear();
        expect_action(choose(s), ActionKind::Move, Direction::Up);
    });
    test("repeated tick reuses result without rerunning planner", [] {
        auto s = state(); s.resources = {{"up", {3, 2}, 10}};
        ExampleGridPlanner planner; DecisionEngine engine(planner);
        const auto first = engine.decide(s); const int calls = planner.calls;
        s.resources = {{"down", {3, 4}, 10}};
        const auto second = engine.decide(s);
        require(second.reused_tick && planner.calls == calls, "duplicate tick advanced policy");
        require(first.action.direction == second.action.direction, "duplicate tick changed result");
        --s.tick; require(!engine.decide(s).submit, "stale frame submitted");
    });
    test("new session resets cache and old target preference", [] {
        auto s = state(); s.resources = {{"up", {3, 2}, 10}};
        ExampleGridPlanner planner; DecisionEngine engine(planner);
        expect_action(engine.decide(s), ActionKind::Move, Direction::Up);
        s.session_id = "new"; s.tick = 0; s.resources = {{"down", {3, 4}, 10}};
        const auto d = engine.decide(s); expect_action(d, ActionKind::Move, Direction::Down);
        require(!d.reused_tick, "carried cache into new match");
    });
    test("respawn uses actual coordinates and keeps applicable illusion", [] {
        auto s = state(); s.self.alive = false; s.self.hp = 0;
        ExampleGridPlanner planner; DecisionEngine engine(planner);
        expect_action(engine.decide(s), ActionKind::Wait);
        ++s.tick; s.self.alive = true; s.self.hp = 100; s.self.position = {1, 1};
        s.resources = {{"right", {2, 1}, 10}}; s.effects = effects(s);
        s.effects->inverted_movement = TickWindow{0, 20};
        const auto d = engine.decide(s); expect_action(d, ActionKind::Move, Direction::Left);
        require(d.expected_position == Point{2, 1}, "assumed original spawn position");
    });
    test("late frame from retired match cannot switch the engine back", [] {
        auto s = state(); ExampleGridPlanner planner; DecisionEngine engine(planner);
        const auto old = s;
        engine.decide(s);
        s.session_id = "new-session"; s.tick = 0;
        engine.decide(s);
        const auto late = engine.decide(old);
        require(!late.submit && late.reason == "retired_session", "old match became current again");
        ++s.tick; require(engine.decide(s).submit, "lost current match");
    });
    test("bad planner cannot send a move into a known wall", [] {
        auto s = state(); s.known_walls = {{2, 3}}; s.resources = {{"right", {6, 3}, 20}};
        BrokenPlanner planner; DecisionEngine engine(planner);
        expect_action(engine.decide(s), ActionKind::Wait);
    });
    test("planner failure degrades without crashing policy", [] {
        auto s = state(); s.resources = {{"right", {6, 3}, 20}};
        ThrowingPlanner planner; DecisionEngine engine(planner);
        expect_action(engine.decide(s), ActionKind::Wait);
    });

    std::cout << "RESULT " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
