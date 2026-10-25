// SHARED
#pragma once
#include "rmtc_bot/planner.hpp"
#include "rmtc_bot/config.hpp"
#include "rmtc_decision/engine.hpp"

namespace rmtc_bot {
class Policy {
public:
    virtual ~Policy() = default;
    virtual Action decide(const Perception&, WorldMemory&, const std::optional<LLMPlan>&) = 0;
    const rmtc::decision::Decision& last_decision() const { return last_; }
protected:
    rmtc::decision::Decision last_;
};
rmtc::decision::GameState make_game_state(const Config&, const Perception&, const WorldMemory&,
                                        const std::optional<LLMPlan>&);
Action bridge_action(const rmtc::decision::Action&);
} // namespace rmtc_bot
