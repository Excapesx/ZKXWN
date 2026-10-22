// OWNER: A
#pragma once
#include "rmtc_bot/memory.hpp"

namespace rmtc_bot {
class Planner final : public rmtc::decision::Planner {
public:
    explicit Planner(const WorldMemory& memory) : memory_(memory) {}
    void begin_tick(const Perception& p, const std::optional<LLMPlan>& plan, int max_ticks);
    std::optional<rmtc::decision::Route> plan(const rmtc::decision::RouteQuery& query) const override;
    std::optional<std::string> next_step(Vec2 from, Vec2 to) const;
private:
    const WorldMemory& memory_;
    int tick_ = 0, max_ticks_ = 100;
    std::vector<AvoidRegion> regions_;
    std::vector<Vec2> occupied_;
    // Single-threaded, per-tick static BFS tree shared by all goal queries.
    mutable bool cache_valid_ = false;
    mutable Vec2 cache_from_;
    mutable int cache_width_ = 0;
    mutable std::vector<unsigned char> cache_blocked_;
    mutable std::vector<int> distances_;
    mutable std::vector<std::optional<rmtc::decision::Direction>> first_steps_;
};
} // namespace rmtc_bot

