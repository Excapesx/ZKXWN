#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rmtc::decision {

struct Point {
    int x = 0;
    int y = 0;
};
inline bool operator==(Point a, Point b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Point a, Point b) { return !(a == b); }

enum class Stage { Collection, Battle };
enum class Mode { TaskFocus, Aggressive, Survival };
enum class Direction { Up, Right, Down, Left };
enum class ActionKind { Wait, Move, Attack, Defend };
enum class MissionKind { NormalResources, HighResources, ResourceValue, EffectiveDamage, Reach };

struct Action {
    ActionKind kind = ActionKind::Wait;
    // A MOVE direction is already a command direction, including illusion mapping.
    std::optional<Direction> direction;
};

struct SelfState {
    Point position;
    double hp = 100;
    bool alive = true;
    bool protected_spawn = false;
    std::optional<Point> initial_spawn;
};

struct Score {
    double resource_value = 0;
    double effective_damage = 0;
    std::vector<std::string> completed_mission_ids;
};

struct Resource {
    std::string id;
    Point position;
    double value = 10;
};

struct Enemy {
    std::string id;
    Point position;
    double hp = 100;
    bool defending = false;  // Last reported state, not a promise about its next action.
    bool protected_spawn = false;
    int observed_tick = -1; // Historical enemy positions must not become current targets.
};

struct Mission {
    std::string id;
    MissionKind kind = MissionKind::Reach;
    double target = 1;
    std::optional<Point> position;
    // Optional adapter estimate from lawful observations, NOT an official JSON field.
    std::optional<double> observed_progress;
};

struct TickWindow {
    int start = 0;
    int end = 0; // [start, end): effect is inactive at end.
    bool contains(int tick) const { return start >= 0 && start <= tick && tick < end; }
};

struct TemporaryBlock {
    Point position;
    TickWindow window;
    std::string session_id;
};

// A typed adapter input, not a parser of natural-language announcements.
// Supply only validated model-derived effects (fixtures are permitted in local tests).
struct EffectSnapshot {
    std::string session_id;
    bool validated = false;
    std::optional<TickWindow> inverted_movement;
    std::optional<TickWindow> forced_wait;
    std::optional<TickWindow> vulnerability;
    std::optional<TickWindow> reflection;
    double reflection_damage = 20;
};

struct DangerZone {
    enum class Shape { Rectangle, Cross };
    Shape shape = Shape::Rectangle;
    Point first, last;
    TickWindow window;
    std::string session_id;
    bool validated = false;
    bool before_movement = true;
    bool respects_protection = true;
    bool excludes_initial_spawn = false;
    bool instant_death = false;
    double damage = 0;
    double advisory_risk = 50; // Heuristic when a model recommends avoidance without damage.
};

struct GameState {
    std::string session_id;
    int tick = 0;
    // These two values come from launch/configuration, not an assumed perception field.
    int max_ticks = 0;
    Stage stage = Stage::Collection;
    int width = 0;
    int height = 0;
    SelfState self;
    Score score;
    std::vector<Mission> missions;
    std::vector<Resource> resources; // Base must remove disproven/collected memories.
    std::vector<Enemy> enemies;
    std::vector<Point> known_walls;
    std::vector<TemporaryBlock> temporary_blocks;
    std::vector<Point> exploration_targets;
    std::optional<EffectSnapshot> effects;
    std::vector<DangerZone> dangers;
    std::optional<Point> advisory_target;
    int advisory_priority = 0;
};

struct RouteQuery {
    Point from;
    Point goal;
    int width = 0;
    int height = 0;
    std::vector<Point> blocked;
};

struct Route {
    int distance = 0;
    // Desired world displacement, before illusion mapping.
    std::optional<Direction> first_step;
    bool wait_first = false; // An explicit timed route can start by waiting.
};

class Planner {
public:
    virtual ~Planner() = default;
    virtual std::optional<Route> plan(const RouteQuery& query) const = 0;
};

struct Decision {
    bool submit = false; // False for invalid/stale/final state; do not publish an action.
    Action action;
    std::optional<Point> goal;
    std::optional<Point> expected_position;
    std::string reason;
    double utility = 0; // Heuristic comparison value, not a score prediction.
    bool reused_tick = false;
};

Point step(Point point, Direction direction);
Direction opposite(Direction direction);
const char* name(Direction direction);
const char* name(ActionKind kind);

} // namespace rmtc::decision

