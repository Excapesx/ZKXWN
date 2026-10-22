// SHARED
#pragma once
#include "rmtc_bot/llm_client.hpp"
#include "rmtc_bot/policies/stage1_policy.hpp"
#include "rmtc_bot/policies/stage2_policy.hpp"
#include <set>
namespace rmtc_bot {
class BotRuntime {
public:
    explicit BotRuntime(Config config, std::shared_ptr<HttpTransport> transport = {});
    std::optional<std::string> handle(const std::string& perception_json);
    const std::string& reason() const { return reason_; }
    std::string llm_status() const { return llm_.status(); }
    const WorldMemory& memory() const { return memory_; }
    const std::optional<LastAction>& last_receipt() const { return last_receipt_; }
private:
    void merge_plan(const LLMPlan&, int tick);
    Config config_;
    WorldMemory memory_;
    Planner planner_;
    Stage1Policy collection_;
    Stage2Policy battle_;
    LLMClient llm_;
    std::set<std::string> retired_;
    std::string session_, event_text_, mission_text_, reason_;
    int last_tick_=-1, model_tick_=-1;
    int last_llm_attempt_tick_=-1000;
    bool requested_=false;
    bool llm_success_seen_=false;
    std::optional<std::string> cached_action_;
    std::optional<LLMPlan> plan_;
    std::optional<LastAction> last_receipt_;
};
}
