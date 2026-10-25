// OWNER: A
#pragma once
#include "rmtc_bot/types.hpp"
#include <map>

namespace rmtc_bot {
enum class CellState { Unknown, Free, Obstacle };
struct Cell {
    CellState state = CellState::Unknown;
    bool visited = false;
    int wall_first_seen = -1, temporary_until = -1;
};
class WorldMemory {
public:
    void reset(int width, int height);
    void update(const Perception& perception);
    void apply_plan(const LLMPlan& plan, int tick);
    bool in_bounds(int x, int y) const;
    CellState at(int x, int y) const;
    void mark_obstacle(int x, int y);
    void mark_free(int x, int y);
    bool is_visited(int x, int y) const;
    void mark_visited(int x, int y);
    std::optional<Vec2> nearest_unvisited(int x, int y) const;
    const std::vector<std::vector<Cell>>& grid() const { return grid_; }
    int width() const { return width_; }
    int height() const { return height_; }
    std::vector<Vec2> walls() const;
    std::vector<Vec2> frontiers() const;
    std::vector<rmtc::decision::Resource> resources() const;
    std::optional<Vec2> initial_spawn() const { return initial_spawn_; }
    std::optional<double> normal_progress() const { return normal_progress_; }
    std::optional<double> high_progress() const { return high_progress_; }
    int last_tick() const { return last_tick_; }
private:
    std::vector<std::vector<Cell>> grid_;
    std::map<std::string, Entity> resources_;
    int width_ = 0, height_ = 0, last_tick_ = -1;
    int vision_radius_ = 4;
    double last_resource_value_ = 0;
    std::string session_;
    std::optional<Vec2> initial_spawn_;
    std::optional<double> normal_progress_, high_progress_;
};
} // namespace rmtc_bot

