// OWNER: A (strategy delegates to shared decision engine)
#pragma once
#include "rmtc_bot/policy.hpp"
namespace rmtc_bot {
class Stage1Policy final : public Policy {
public:
    Stage1Policy(Config config, Planner& planner)
        : config_(std::move(config)), planner_(planner), engine_(planner_, decision_mode(config_.decision_type)) {}
    Action decide(const Perception&, WorldMemory&, const std::optional<LLMPlan>&) override;
private:
    Config config_;
    Planner& planner_;
    rmtc::decision::DecisionEngine engine_;
};
}
