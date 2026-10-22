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
};
} // namespace rmtc_bot

