// OWNER: A
#include "rmtc_bot/policies/stage1_policy.hpp"
namespace rmtc_bot {
Action Stage1Policy::decide(const Perception& p, WorldMemory& m, const std::optional<LLMPlan>&) {
    planner_.begin_tick(p,std::nullopt,config_.max_ticks);
    auto c=config_; c.stage=1;
    last_=engine_.decide(make_game_state(c,p,m,std::nullopt));
    return bridge_action(last_.action);
}
}
