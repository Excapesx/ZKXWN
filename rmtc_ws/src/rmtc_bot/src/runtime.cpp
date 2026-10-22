// SHARED
#include "rmtc_bot/runtime.hpp"
#include "rmtc_bot/perception.hpp"
#include <algorithm>
#include <limits>
namespace rmtc_bot {
BotRuntime::BotRuntime(Config config, std::shared_ptr<HttpTransport> transport)
    : config_(std::move(config)), planner_(memory_), collection_(config_,planner_), battle_(config_,planner_),
      llm_(config_,std::move(transport)) { validate_config(config_); }
void BotRuntime::merge_plan(const LLMPlan& incoming, int tick) {
    if(!plan_) { plan_=incoming; return; }
    auto old=plan_->regions;
    auto effects=plan_->effects;
    plan_=incoming;
    // Keep still-relevant event windows when a later model answer discusses another event.
    for(const auto& r:old) if(r.end_tick>tick) {
        const auto same=[&](const AvoidRegion& v) { return v.shape==r.shape && v.rect.x1==r.rect.x1 && v.rect.y1==r.rect.y1 && v.rect.x2==r.rect.x2 && v.rect.y2==r.rect.y2 && v.start_tick==r.start_tick && v.end_tick==r.end_tick; };
        if(std::none_of(plan_->regions.begin(),plan_->regions.end(),same)) plan_->regions.push_back(r);
    }
    const auto keep=[tick](auto& dst,const auto& src) { if(!dst && src && src->end>tick) dst=src; };
    keep(plan_->effects.inverted_movement,effects.inverted_movement);
    keep(plan_->effects.forced_wait,effects.forced_wait);
    keep(plan_->effects.vulnerability,effects.vulnerability);
    if(!plan_->effects.reflection && effects.reflection && effects.reflection->end>tick) {
        plan_->effects.reflection=effects.reflection; plan_->effects.reflection_damage=effects.reflection_damage;
    }
}
std::optional<std::string> BotRuntime::handle(const std::string& text) {
    Perception p;
    try { p=parse_perception(text); } catch(const std::exception&) { reason_="invalid_perception"; return {}; }
    if(p.tick>std::numeric_limits<int>::max() || (!p.self_team.empty() && p.self_team!=config_.team)) {
        reason_="wrong_seat_or_tick"; return {};
    }
    // Fail safe: never silently play stage 2 with the stage-1 policy or vice versa.
    if(config_.stage==1 && !p.missions.empty()) { reason_="stage1_but_missions_present"; return {}; }
    if(config_.stage==2 && p.missions.empty()) { reason_="stage2_but_no_missions"; return {}; }
    if(p.session_id!=session_) {
        if(retired_.count(p.session_id)) { reason_="retired_session"; return {}; }
        if(!session_.empty()) retired_.insert(session_);
        session_=p.session_id; last_tick_=-1; model_tick_=-1; cached_action_.reset(); plan_.reset();
        requested_=false; llm_success_seen_=false; last_llm_attempt_tick_=-1000;
        event_text_.clear(); mission_text_.clear();
        memory_.reset(p.map_width,p.map_height); llm_.reset_session(session_);
    }
    const int tick=static_cast<int>(p.tick);
    if(tick<last_tick_) { reason_="stale_tick"; return {}; }
    if(tick==last_tick_) { reason_="cached_tick"; return cached_action_; }
    last_tick_=tick; cached_action_.reset();
    last_receipt_=p.last_action;
    if(tick>=config_.max_ticks) { reason_="final_tick"; return {}; }
    memory_.update(p);
    if(config_.stage==2) {
        if(auto result=llm_.latest(); result && result->session_id==session_ && result->source_tick<=tick && result->source_tick>model_tick_) {
            merge_plan(*result,tick); model_tick_=result->source_tick;
        }
        if(config_.llm_enabled) {
            if(llm_.http_successes()>0) llm_success_seen_=true;
            const bool changed = event_text_!=p.event_text || mission_text_!=p.mission_text;
            if(changed) { event_text_=p.event_text; mission_text_=p.mission_text; }
            const std::string status=llm_.status();
            const bool hard_blocked = status=="quota_exhausted" || status=="authentication_failed";
            // If the opening call fails, retry every 25 ticks until we have one
            // successful request or the gateway hard-blocks the token.
            const bool retry_without_success = !llm_success_seen_ && !hard_blocked &&
                (tick-last_llm_attempt_tick_ >= 25);
            if(!requested_ || changed || retry_without_success) {
                llm_.submit(make_llm_request(p,config_.max_ticks));
                requested_=true;
                last_llm_attempt_tick_=tick;
            }
        }
        if(plan_) {
            auto& regions=plan_->regions;
            regions.erase(std::remove_if(regions.begin(),regions.end(),[tick](const auto& r){return r.end_tick<=tick;}),regions.end());
            memory_.apply_plan(*plan_,tick);
        }
    }
    Policy& policy=config_.stage==1 ? static_cast<Policy&>(collection_) : static_cast<Policy&>(battle_);
    Action action=policy.decide(p,memory_,plan_);
    const auto& decision=policy.last_decision(); reason_=decision.reason;
    if(!decision.submit) return {};
    if(!action_allowed(action,p,config_.stage)) { action={}; reason_="legality_guard_wait"; }
    cached_action_=action_to_json(action,p.tick,p.session_id);
    return cached_action_;
}
}
