// OWNER: B
#include "rmtc_bot/policies/stage2_policy.hpp"
namespace rmtc_bot {
Action Stage2Policy::decide(const Perception& p, WorldMemory& m, const std::optional<LLMPlan>& plan) {
    planner_.begin_tick(p,plan,config_.max_ticks);
    auto c=config_; c.stage=2;
    last_=engine_.decide(make_game_state(c,p,m,plan));
    return bridge_action(last_.action);
}
}
