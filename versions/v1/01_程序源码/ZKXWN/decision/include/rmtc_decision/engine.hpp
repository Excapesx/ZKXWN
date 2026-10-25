#pragma once

#include "rmtc_decision/types.hpp"

namespace rmtc::decision {

// One instance per controlled seat. Call from one thread; AI results are snapshots.
class DecisionEngine {
public:
    explicit DecisionEngine(const Planner& planner, Mode mode = Mode::TaskFocus);
    Decision decide(const GameState& state);
    void reset();

private:
    const Planner& planner_;
    Mode mode_;
    std::string session_;
    std::vector<std::string> retired_sessions_;
    int last_tick_ = -1;
    Stage last_stage_ = Stage::Collection;
    std::optional<Decision> cached_;
    std::string previous_goal_key_;
};

} // namespace rmtc::decision

