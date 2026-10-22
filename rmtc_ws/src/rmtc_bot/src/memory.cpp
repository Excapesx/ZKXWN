// OWNER: A
#include "rmtc_bot/memory.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace rmtc_bot {
void WorldMemory::reset(int w, int h) {
    if (w < 1 || h < 1 || static_cast<std::int64_t>(w) * h > 1000000) throw std::runtime_error("Invalid memory dimensions");
    width_ = w; height_ = h; last_tick_ = -1; last_resource_value_ = 0; vision_radius_ = 4;
    grid_.assign(h, std::vector<Cell>(w)); resources_.clear();
    initial_spawn_.reset(); normal_progress_.reset(); high_progress_.reset(); session_.clear();
}
bool WorldMemory::in_bounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
CellState WorldMemory::at(int x, int y) const { return in_bounds(x, y) ? grid_[y][x].state : CellState::Obstacle; }
void WorldMemory::mark_obstacle(int x, int y) {
    if (in_bounds(x,y)) grid_[y][x].state = CellState::Obstacle;
}
void WorldMemory::mark_free(int x, int y) {
    if (in_bounds(x,y)) { auto& c = grid_[y][x]; c.state = CellState::Free; c.wall_first_seen = -1; c.temporary_until = -1; }
}
bool WorldMemory::is_visited(int x, int y) const { return in_bounds(x,y) && grid_[y][x].visited; }
void WorldMemory::mark_visited(int x, int y) { if (in_bounds(x,y)) grid_[y][x].visited = true; }
void WorldMemory::update(const Perception& p) {
    if (p.session_id != session_ || p.map_width != width_ || p.map_height != height_) {
        reset(p.map_width, p.map_height); session_ = p.session_id;
        if (p.score.resource_value == 0) { normal_progress_ = 0; high_progress_ = 0; }
    }
    if (p.tick <= last_tick_) return;
    vision_radius_ = p.vision_radius;
    if (p.position_reason == "initial" && !initial_spawn_) initial_spawn_ = p.self_pos;
    if (last_tick_ >= 0 && p.tick != last_tick_ + 1) { normal_progress_.reset(); high_progress_.reset(); }
    const double delta = p.score.resource_value - last_resource_value_;
    if (last_tick_ >= 0 && delta > 0) {
        const auto collected = std::find_if(resources_.begin(), resources_.end(), [&](const auto& entry) {
            return entry.second.pos == p.self_pos && std::abs(entry.second.value - delta) < 1e-6;
        });
        if (collected != resources_.end()) {
            auto& progress = collected->second.value >= 20 ? high_progress_ : normal_progress_;
            if (progress) ++*progress;
        } else { normal_progress_.reset(); high_progress_.reset(); }
    }
    for (auto& row : grid_) for (auto& c : row) {
        if (c.temporary_until >= 0 && p.tick >= c.temporary_until) {
            c.state = CellState::Unknown; c.temporary_until = -1; c.wall_first_seen = -1;
        }
    }
    last_tick_ = static_cast<int>(p.tick); last_resource_value_ = p.score.resource_value;
    // A dead robot has no surrounding observation. Empty entities must not erase its map.
    if (!p.alive || p.hp <= 0) return;
    const auto visible = [&](Vec2 point) {
        return std::abs(point.x-p.self_pos.x) <= p.vision_radius && std::abs(point.y-p.self_pos.y) <= p.vision_radius;
    };
    std::set<std::string> observed_resources;
    std::vector<Vec2> observed_walls;
    for (const auto& e : p.entities) {
        if (e.type == EntityType::Obstacle) observed_walls.push_back(e.pos);
        else if (e.type == EntityType::ResourceNormal || e.type == EntityType::ResourceHigh) {
            observed_resources.insert(e.id); resources_[e.id] = e;
        }
    }
    for (auto it = resources_.begin(); it != resources_.end();) {
        if (visible(it->second.pos) && !observed_resources.count(it->first)) it = resources_.erase(it);
        else ++it;
    }
    // Free-space inference uses a complete, valid visible-entity list, NOT its '0' characters.
    for (int y = std::max(0, p.self_pos.y-p.vision_radius); y <= std::min(height_-1,p.self_pos.y+p.vision_radius); ++y)
        for (int x = std::max(0,p.self_pos.x-p.vision_radius); x <= std::min(width_-1,p.self_pos.x+p.vision_radius); ++x) {
            auto& c = grid_[y][x];
            if (std::find(observed_walls.begin(), observed_walls.end(), Vec2{x,y}) != observed_walls.end()) {
                if (c.state != CellState::Obstacle) c.wall_first_seen = last_tick_;
                c.state = CellState::Obstacle;
            } else mark_free(x,y);
        }
    mark_free(p.self_pos.x,p.self_pos.y); mark_visited(p.self_pos.x,p.self_pos.y);
}
void WorldMemory::apply_plan(const LLMPlan& plan, int tick) {
    if (plan.session_id != session_) return;
    for (const auto& r : plan.regions) if (r.wall) {
        for (int y=0; y<height_; ++y) for (int x=0; x<width_; ++x) {
            auto& c = grid_[y][x];
            if (r.contains({x,y}) && c.state == CellState::Obstacle && c.wall_first_seen >= r.start_tick && c.wall_first_seen < r.end_tick) {
                if (tick >= r.end_tick) { c.state = CellState::Unknown; c.temporary_until = -1; c.wall_first_seen = -1; }
                else c.temporary_until = r.end_tick;
            }
        }
    }
}
std::vector<Vec2> WorldMemory::walls() const {
    std::vector<Vec2> result;
    for (int y=0;y<height_;++y) for(int x=0;x<width_;++x) if(at(x,y)==CellState::Obstacle) result.push_back({x,y});
    return result;
}
std::vector<Vec2> WorldMemory::frontiers() const {
    std::vector<Vec2> result;
    for(int y=0;y<height_;++y) for(int x=0;x<width_;++x) {
        if (at(x,y)!=CellState::Free) continue;
        for (const auto d : {rmtc::decision::Direction::Up,rmtc::decision::Direction::Right,
                             rmtc::decision::Direction::Down,rmtc::decision::Direction::Left}) {
            const auto n=rmtc::decision::step({x,y},d);
            if(in_bounds(n.x,n.y)&&at(n.x,n.y)==CellState::Unknown) {
                // With radius zero the sole known frontier can be our current
                // cell, which is not a movement goal. Probe its unknown neighbors.
                if(vision_radius_==0) result.push_back(n);
                else { result.push_back({x,y}); break; }
            }
        }
    }
    std::sort(result.begin(),result.end(),[](Vec2 a,Vec2 b){return a.y<b.y||(a.y==b.y&&a.x<b.x);});
    result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}
std::optional<Vec2> WorldMemory::nearest_unvisited(int x,int y) const {
    std::optional<Vec2> best; int best_distance=std::numeric_limits<int>::max();
    for(const auto p:frontiers()) {
        const int d=std::abs(p.x-x)+std::abs(p.y-y);
        if(d>0 && d<best_distance) { best=p; best_distance=d; }
    }
    return best;
}
std::vector<rmtc::decision::Resource> WorldMemory::resources() const {
    std::vector<rmtc::decision::Resource> result;
    for(const auto& entry:resources_) result.push_back({entry.first,entry.second.pos,entry.second.value});
    return result;
}
} // namespace rmtc_bot

