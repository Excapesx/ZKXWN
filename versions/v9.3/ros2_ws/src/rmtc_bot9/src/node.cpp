// rmtc_bot9/node.cpp —— ROS 2 接入层
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <stdexcept>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

#include <nlohmann/json.hpp>

#include "rmtc9/bot.hpp"
#include "rmtc9/parse.hpp"
#include "rmtc9/frame_gate.hpp"
#include "rmtc9/announce.hpp"
#include "rmtc_bot9/llm.hpp"
#include "rmtc_bot9/node.hpp"

namespace rmtc_bot9 {
namespace {
using Json = nlohmann::json;

std::string env(const char* key, const std::string& fallback = {}) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

int env_int(const char* key, int fallback) {
    const std::string v = env(key);
    if (v.empty()) return fallback;
    try {
        std::size_t consumed=0;const int value=std::stoi(v,&consumed);
        if(consumed!=v.size())throw std::invalid_argument("trailing input");
        return value;
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("Invalid integer environment: ")+key);
    }
}

struct Profile {
    double aggression = 1.0;
    int huntRadius = 10;
};

// 现场可切换的决策类型（赛前登记的枚举）。只改强度参数，不改算法结构。
// 8.0：balanced 的追猎半径为整个视野（10），collect 退回 7.0 的保守值，fight 更激进。
Profile profile_from_name(const std::string& name) {
    if (name == "collect") return {0.7, 5};
    if (name == "fight") return {1.4, 10};
    if(name == "balanced") return {1.0, 10};
    throw std::runtime_error("Unknown decision_type");
}
}  // namespace

class Bot9Node : public rclcpp::Node {
public:
    Bot9Node() : Node("rmtc_bot9") {
        seat_ = declare_parameter<std::string>("team", env("RMTC_TEAM", "A"));
        stage_ = declare_parameter<int>("stage", env_int("RMTC_STAGE", 1));
        maxTicks_ = declare_parameter<int>("max_ticks",
                                          env_int("RMTC_MAX_TICKS", stage_ == 1 ? 100 : 500));
        teamName_ = declare_parameter<std::string>("team_name", env("RMTC_TEAM_NAME", "HERO-9.3"));
        const std::string gateway = declare_parameter<std::string>("gateway", env("RMTC_AI_GATEWAY"));
        const std::string token = env("RMTC_TEAM_TOKEN");  // 令牌只从环境读，不进参数
        const std::string model = declare_parameter<std::string>("model", env("RMTC_MODEL"));
        const int maxTokens = declare_parameter<int>("max_tokens", env_int("RMTC_MAX_TOKENS", 1024));
        const int timeoutMs = declare_parameter<int>("http_timeout_ms", env_int("RMTC_HTTP_TIMEOUT_MS", 15000));

        if (seat_.size() != 1 || seat_[0] < 'A' || seat_[0] > 'D') {
            throw std::runtime_error("team must be A/B/C/D");
        }
        if(teamName_.empty()||teamName_.size()>96||teamName_.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("Invalid team_name");
        if(maxTicks_<=0||maxTicks_>1000000)throw std::runtime_error("Invalid max_ticks");
        if (stage_ != 1 && stage_ != 2) throw std::runtime_error("stage must be 1 or 2");

        // Check both sources, even when a ROS override masks a stale environment value.
        if (!env("RMTC_MODEL").empty() || !model.empty()) {
            throw std::runtime_error("Custom model is forbidden; clear RMTC_MODEL and the ROS model parameter");
        }
        if (maxTokens < 0) throw std::runtime_error("max_tokens must be nonnegative (0 means 1024)");

        rmtc9::Config cfg;
        cfg.stage = stage_;
        cfg.maxTicks = maxTicks_;
        bot_.configure(cfg);

        // 现场只允许改 decision_type，且必须来自已登记枚举
        const std::string decisionFile = env("RMTC_DECISION_CONFIG");
        std::string decisionType = "balanced";
        if (!decisionFile.empty()) {
            std::ifstream in(decisionFile);
            if (in) {
                try {
                    const Json j = Json::parse(in);
                    if (j.is_object() && j.size() == 1 && j.contains("decision_type") &&
                        j["decision_type"].is_string()) {
                        decisionType = j["decision_type"].get<std::string>();
                    } else throw std::runtime_error("decision_config requires exactly decision_type");
                } catch (const std::exception&) {
                    throw std::runtime_error("Invalid decision_config JSON");
                }
            } else throw std::runtime_error("Cannot open decision_config");
        }
        const Profile profile = profile_from_name(decisionType);
        bot_.set_profile(profile.aggression, profile.huntRadius);

        llm_.configure(gateway, token, model, maxTokens, timeoutMs);
        RCLCPP_INFO(get_logger(), "HERO 9.3.0; official default model; max_tokens=%d", maxTokens == 0 ? 1024 : maxTokens);
        if (llm_.enabled() && stage_==2) {
            llm_.start();
            RCLCPP_INFO(get_logger(), "LLM 已启用（凭据及完整网关地址不写日志）");
        }

        const auto live = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().durability_volatile();
        const auto retained = rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();
        const std::string prefix = "/team_" + seat_;

        action_ = create_publisher<std_msgs::msg::String>(prefix + "/action", live);
        registration_ = create_publisher<std_msgs::msg::String>(prefix + "/registration", retained);
        perception_ = create_subscription<std_msgs::msg::String>(
            prefix + "/perception", live,
            [this](std_msgs::msg::String::ConstSharedPtr msg) { on_perception(msg->data); });
        // Retained String topics have no session identifier. They are diagnostic
        // only: receipt after a reset does not establish which match produced them.
        // Only text inside an accepted, session-scoped perception enters a plan.
        const auto events = rclcpp::QoS(rclcpp::KeepLast(128)).reliable().transient_local();
        eventSub_ = create_subscription<std_msgs::msg::String>(
            "/referee/event_text", events,
            [this](std_msgs::msg::String::ConstSharedPtr msg) {
                (void)msg;
                if(!warnedUnscopedEvents_) {
                    warnedUnscopedEvents_=true;
                    RCLCPP_INFO(get_logger(), "无场次公告话题仅作诊断，决策等待当前场次感知确认");
                }
            });
        missionSub_ = create_subscription<std_msgs::msg::String>(
            "/referee/mission", retained,
            [this](std_msgs::msg::String::ConstSharedPtr msg) {
                (void)msg;
                if(!warnedUnscopedMission_) {
                    warnedUnscopedMission_=true;
                    RCLCPP_INFO(get_logger(), "无场次任务话题仅作诊断，决策使用当前场次感知任务文本");
                }
            });
        registrationTimer_ = create_wall_timer(std::chrono::seconds(1), [this] { publish_registration(); });
        publish_registration();
        RCLCPP_INFO(get_logger(), "Ready: seat=%s stage=%d max_ticks=%d profile=%s", seat_.c_str(), stage_,
                    maxTicks_, decisionType.c_str());
    }

    ~Bot9Node() override { llm_.stop(); }

private:
    void publish_registration() {
        std_msgs::msg::String msg;
        msg.data = Json{{"team", seat_}, {"name", teamName_}}.dump();
        registration_->publish(msg);
    }

    void on_perception(const std::string& text) {
        const auto callbackStart=std::chrono::steady_clock::now();
        rmtc9::Perception p;
        std::string err;
        if (!rmtc9::parse_perception(text, &p, &err)) {
            ++parseErrors_;
            if (parseErrors_ % 20 == 1) {
                RCLCPP_WARN(get_logger(), "感知解析失败(%s)，累计 %d 次", err.c_str(), parseErrors_);
            }
            return;
        }
        // `self.team` is normally the seat letter, but some deployments put the
        // long-term team id there. Only reject a different *seat*; an id-like team
        // string is compatible with the seat configured on this node.
        const bool looksLikeSeat = p.team.size() == 1 && p.team[0] >= 'A' && p.team[0] <= 'D';
        if ((looksLikeSeat && p.team != seat_) || p.tick >= maxTicks_) return;
        const auto frame=frames_.accept(p.session,p.tick);
        if(frame==rmtc9::FrameGate::Result::Reject) return;
        if(frame==rmtc9::FrameGate::Result::NewSession) {
            lastTick_=-1; lastAction_.clear();lastInvalid_=0;
        }
        if (p.tick == lastTick_ && !lastAction_.empty()) {
            // 同 Tick 重发：不推进策略，重发缓存动作
            publish_action(lastAction_);
            return;
        }
        lastTick_ = p.tick;
        mapW_ = p.mapW;
        mapH_ = p.mapH;

        if (p.session != session_) {
            session_ = p.session;
            llm_.reset_session(session_);
            previousPlan_.clear(); planRetry_=false; planAttempts_=0;
            bot_.configure(rmtc9::Config{stage_, maxTicks_});
            lastMissionText_.clear();
            announcements_.reset();
            requestedVersion_ = 0;
            lastPlanRequestTick_ = -1000;
            appliedPlanTick_ = -1;
            firstSuccessSeen_ = false;
            qualificationAttempts_ = 0;
            lastRequestTick_ = -1000;
            RCLCPP_INFO(get_logger(), "新场次 %s", session_.c_str());
        }

        drain_llm();
        maybe_request_llm(p);

        rmtc9::Decision d;
        try { d=bot_.decide(p); }
        catch(const std::exception&) { d={rmtc9::Action::wait(),"decision_exception",0}; }
        rmtc9::Action action = rmtc9::sanitize_action(d.action,p,stage_);
        // 出口合法性自检：任何不合法就退化成 wait，绝不送 E_STATE
        if (!p.alive || p.hp <= 0) action = rmtc9::Action::wait();
        if (p.protectedSpawn && action.kind == rmtc9::Act::Attack) action = rmtc9::Action::wait();
        if (stage_ == 1 && action.kind != rmtc9::Act::Move) action = rmtc9::Action::wait();
        if (!action.legal_shape()) action = rmtc9::Action::wait();

        lastAction_ = rmtc9::action_json(action, p.tick, p.session);
        publish_action(lastAction_);
        if(p.invalidActions>lastInvalid_){RCLCPP_WARN(get_logger(),"裁判累计拒绝动作=%d（请查回执）",p.invalidActions);}
        lastInvalid_=p.invalidActions;
        if (p.tick % 25 == 0) {
            const double latency=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-callbackStart).count();
            RCLCPP_INFO(get_logger(),"decision_ms=%.3f receipt_tick=%d moved=%d local_ai_success=%d",latency,p.lastActionTick,p.lastMoved,firstSuccessSeen_);
            const auto& diag=bot_.diagnostics();
            RCLCPP_INFO(get_logger(),"frontiers=%d goals=%d unsafe_goals=%d enemies=%d combat_objective=%d wait_streak=%d",
                        diag.frontiers,diag.goals,diag.unsafeGoals,diag.visibleEnemies,diag.combatObjective,diag.consecutiveWaits);
            RCLCPP_INFO(get_logger(), "tick=%d action=%s reason=%s llm=%s D=%.1f R=%.1f", p.tick,
                        lastAction_.c_str(), d.reason.c_str(), llm_.status().c_str(), p.effectiveDamage,
                        p.resourceValue);
        }
    }

    void publish_action(const std::string& payload) {
        std_msgs::msg::String msg;
        msg.data = payload;
        action_->publish(msg);
    }

    void maybe_request_llm(const rmtc9::Perception& p) {
        if (!llm_.enabled() || llm_.hard_blocked()) return;
        if (stage_ != 2) return;  // 第一阶段没有随机事件，不需要调用

        // Text is tied to p.session by the accepted perception, not callback time.
        announcements_.ingest(p.eventText);
        const std::string missions = p.missionText;
        const bool hasNew = announcements_.version() != requestedVersion_ || missions != lastMissionText_;
        // 任务分资格只要求“本场本席位一次 HTTP 2xx 成功调用”：5 Tick 重试、最多 20 次。
        const bool needQualification = !firstSuccessSeen_ && qualificationAttempts_ < 20 &&
                                       (p.tick - lastRequestTick_ >= 5);
        const bool wantPlan = scheduler_.want_plan(hasNew, p.tick, lastPlanRequestTick_, maxTicks_, llm_.http_successes());
        const bool retryPlan = planRetry_ && planAttempts_ < 3 && p.tick - lastRequestTick_ >= 10 &&
                               llm_.http_successes() < scheduler_.quotaBudget;
        if (!wantPlan && !needQualification && !retryPlan) return;
        if (wantPlan) { planAttempts_ = 0; planRetry_ = false; lastPlanRequestTick_ = p.tick; }
        if (needQualification) ++qualificationAttempts_;
        ++planAttempts_;
        planRetry_ = false;
        requestedVersion_ = announcements_.version();
        lastMissionText_ = missions;
        lastRequestTick_ = p.tick;

        Json user;
        user["session_id"] = p.session;
        user["stage"] = stage_;
        user["announcement_source"] = "accepted_perception.event_text";
        user["tick"] = p.tick;
        user["max_tick"] = maxTicks_;
        user["map"] = {{"width", p.mapW}, {"height", p.mapH}};
        user["self"] = {{"position", Json::array({p.self.x, p.self.y})}, {"hp", p.hp}};
        user["mission_text"] = missions;
        user["announcements"] = announcements_.recent(48, 12000);  // 本场全部公告（去重、按时间顺序）
        user["previous_answer"] = previousPlan_;

        Json schema;
        schema["events"] =
            "array; one object per announced event, fields: id (stable string), kind, a [x,y], b [x,y], start, end, and the kind's numbers. "
            "explosion: a,b = rectangle corners (inclusive), start = blast tick, end = start+1, damage = HP lost. "
            "inversion: a,b = rectangle, start = activation tick, end = start+1, duration = ticks the movement reversal lasts. "
            "heal: ONE object per point (a=b=point), start, end = tick the point disappears, heal = HP per tick, duration = ticks the healing buff lasts. "
            "fortress: a,b = corners of the fort, start, end, dwell = consecutive ticks needed, damage, defend_damage = buffed attack damage vs normal/defending targets. "
            "split: a=b=cross intersection, start = activation tick, end = tick the walls disappear. "
            "chili: ONE object per point (a=b=point), start, end, damage = HP lost by robots on the fire line. "
            "reflect: start, end, damage = HP the attacker loses when attacking a defending robot. "
            "vulnerability: start, end = tick the vulnerability expires, trigger_end = end of the movement window.";
        Json body;
        body["instruction"] =
            "Read `announcements` and output ONLY one JSON object of the form {\"events\":[...]} following `schema`. "
            "Include every announced event whose effects may still matter at or after `tick`; output {\"events\":[]} if none. "
            "Use absolute map coordinates [x,y] (x to the right, y downwards) and half-open tick intervals [start,end). "
            "Use only numbers stated in the announcements; if a number is missing, omit that field. "
            "Fortress regions may be off-center under rules v1.2: use the announced rectangle, never guess a map center or fixed coordinate. "
            "Do not output hazards, effects, comments or any text outside the JSON object.";
        body["schema"] = schema;
        body["request"] = user;
        llm_.submit(body.dump(), p.tick, p.session);
    }

    void drain_llm() {
        LlmResult result;
        while (llm_.poll(&result)) {
            if(result.session != session_) continue;
            // A successful HTTP 2xx call is the qualification record even when the
            // model returned empty content; content quality only affects the plan.
            if(result.httpSuccess) firstSuccessSeen_=true;
            // 8.0：只丢弃“比已采用计划还旧”的结果。7.0 要求 sourceTick 等于最近一次请求，
            // 公告频繁变化时结果一个个被当成过时，可能整场都用不上计划。
            if(result.sourceTick < appliedPlanTick_) continue;
            if (!result.valid) { planRetry_=true; continue; }
            rmtc9::Plan plan;
            if (rmtc9::parse_plan(result.content, session_, result.sourceTick, maxTicks_, mapW_, mapH_, &plan)) {
                bot_.set_plan(plan);
                appliedPlanTick_ = result.sourceTick;
                previousPlan_ = result.content.size() > 8000 ? result.content.substr(0, 8000) : result.content;
                planRetry_=false;
                RCLCPP_INFO(get_logger(), "事件计划已更新：%zu 个事件", plan.events.size());
            } else {
                planRetry_=true;
                RCLCPP_WARN(get_logger(), "模型输出无法解析，最多三次计划重试，保留旧计划");
            }
        }
        // Qualification comes only from this session result, never a lifetime counter.
    }

    std::string seat_;
    std::string teamName_;
    int stage_ = 1;
    int maxTicks_ = 100;
    rmtc9::Bot bot_;
    LlmWorker llm_{http_post};

    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr action_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr registration_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr perception_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr eventSub_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr missionSub_;
    rclcpp::TimerBase::SharedPtr registrationTimer_;

    rmtc9::FrameGate frames_;
    bool planRetry_=false;
    int planAttempts_=0;
    std::string previousPlan_;
    std::string session_;
    int lastTick_ = -1;
    std::string lastAction_;
    std::string lastMissionText_;
    rmtc9::AnnouncementLog announcements_;
    rmtc9::PlanScheduler scheduler_;
    bool warnedUnscopedEvents_=false, warnedUnscopedMission_=false;
    unsigned requestedVersion_ = 0;
    int lastPlanRequestTick_ = -1000;
    int appliedPlanTick_ = -1;
    int lastRequestTick_ = -1000;
    bool firstSuccessSeen_ = false;
    int qualificationAttempts_ = 0;
    int parseErrors_ = 0;
    int lastInvalid_=0;
    int mapW_ = 64;
    int mapH_ = 64;
};

rclcpp::Node::SharedPtr make_bot9_node() { return std::make_shared<Bot9Node>(); }

}  // namespace rmtc_bot9
