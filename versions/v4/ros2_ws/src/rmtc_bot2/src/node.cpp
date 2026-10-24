// rmtc_bot2/node.cpp —— ROS 2 接入层
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

#include "rmtc2/bot.hpp"
#include "rmtc2/parse.hpp"
#include "rmtc2/frame_gate.hpp"
#include "rmtc_bot2/llm.hpp"
#include "rmtc_bot2/node.hpp"

namespace rmtc_bot2 {
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
    int huntRadius = 5;
};

Profile profile_from_name(const std::string& name) {
    if (name == "collect") return {0.7, 3};
    if (name == "fight") return {1.4, 8};
    if(name == "balanced") return {1.0, 5};
    throw std::runtime_error("Unknown decision_type");
}
}  // namespace

class Bot2Node : public rclcpp::Node {
public:
    Bot2Node() : Node("rmtc_bot2") {
        seat_ = declare_parameter<std::string>("team", env("RMTC_TEAM", "A"));
        stage_ = declare_parameter<int>("stage", env_int("RMTC_STAGE", 1));
        maxTicks_ = declare_parameter<int>("max_ticks",
                                          env_int("RMTC_MAX_TICKS", stage_ == 1 ? 100 : 500));
        teamName_ = declare_parameter<std::string>("team_name", env("RMTC_TEAM_NAME", "HERO-4.0"));
        const std::string gateway = declare_parameter<std::string>("gateway", env("RMTC_AI_GATEWAY"));
        const std::string token = env("RMTC_TEAM_TOKEN");  // 令牌只从环境读，不进参数
        const std::string model = declare_parameter<std::string>("model", env("RMTC_MODEL"));
        const int maxTokens = declare_parameter<int>("max_tokens", env_int("RMTC_MAX_TOKENS", 0));
        const int timeoutMs = declare_parameter<int>("http_timeout_ms", env_int("RMTC_HTTP_TIMEOUT_MS", 2500));

        if (seat_.size() != 1 || seat_[0] < 'A' || seat_[0] > 'D') {
            throw std::runtime_error("team must be A/B/C/D");
        }
        if(teamName_.empty()||teamName_.size()>96||teamName_.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("Invalid team_name");
        if(maxTicks_<=0||maxTicks_>1000000)throw std::runtime_error("Invalid max_ticks");
        if (stage_ != 1 && stage_ != 2) throw std::runtime_error("stage must be 1 or 2");

        rmtc2::Config cfg;
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
        registrationTimer_ = create_wall_timer(std::chrono::seconds(1), [this] { publish_registration(); });
        publish_registration();
        RCLCPP_INFO(get_logger(), "Ready: seat=%s stage=%d max_ticks=%d profile=%s", seat_.c_str(), stage_,
                    maxTicks_, decisionType.c_str());
    }

    ~Bot2Node() override { llm_.stop(); }

private:
    void publish_registration() {
        std_msgs::msg::String msg;
        msg.data = Json{{"team", seat_}, {"name", teamName_}}.dump();
        registration_->publish(msg);
    }

    void on_perception(const std::string& text) {
        const auto callbackStart=std::chrono::steady_clock::now();
        rmtc2::Perception p;
        std::string err;
        if (!rmtc2::parse_perception(text, &p, &err)) {
            ++parseErrors_;
            if (parseErrors_ % 20 == 1) {
                RCLCPP_WARN(get_logger(), "感知解析失败(%s)，累计 %d 次", err.c_str(), parseErrors_);
            }
            return;
        }
        if(p.team != seat_ || p.tick >= maxTicks_) return;
        const auto frame=frames_.accept(p.session,p.tick);
        if(frame==rmtc2::FrameGate::Result::Reject) return;
        if(frame==rmtc2::FrameGate::Result::NewSession) {
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
            bot_.configure(rmtc2::Config{stage_, maxTicks_});
            lastEventText_.clear();
            lastMissionText_.clear();
            firstSuccessSeen_ = false;
            lastRequestTick_ = -1000;
            RCLCPP_INFO(get_logger(), "新场次 %s", session_.c_str());
        }

        drain_llm();
        maybe_request_llm(p);

        rmtc2::Decision d;
        try { d=bot_.decide(p); }
        catch(const std::exception&) { d={rmtc2::Action::wait(),"decision_exception",0}; }
        rmtc2::Action action = rmtc2::sanitize_action(d.action,p,stage_);
        // 出口合法性自检：任何不合法就退化成 wait，绝不送 E_STATE
        if (!p.alive || p.hp <= 0) action = rmtc2::Action::wait();
        if (p.protectedSpawn && action.kind == rmtc2::Act::Attack) action = rmtc2::Action::wait();
        if (stage_ == 1 && action.kind != rmtc2::Act::Move) action = rmtc2::Action::wait();
        if (!action.legal_shape()) action = rmtc2::Action::wait();

        lastAction_ = rmtc2::action_json(action, p.tick, p.session);
        publish_action(lastAction_);
        if(p.invalidActions>lastInvalid_){RCLCPP_WARN(get_logger(),"裁判累计拒绝动作=%d（请查回执）",p.invalidActions);}
        lastInvalid_=p.invalidActions;
        if (p.tick % 25 == 0) {
            const double latency=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-callbackStart).count();
            RCLCPP_INFO(get_logger(),"decision_ms=%.3f receipt_tick=%d moved=%d local_ai_success=%d",latency,p.lastActionTick,p.lastMoved,firstSuccessSeen_);
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

    void maybe_request_llm(const rmtc2::Perception& p) {
        if (!llm_.enabled() || llm_.hard_blocked()) return;
        if (stage_ != 2) return;  // 第一阶段没有随机事件，不需要调用

        const std::string events = p.eventText;
        const std::string missions = p.missionText;
        const bool changed=events!=lastEventText_ || missions!=lastMissionText_;
        // 开场必须成功调用一次（任务分资格）；失败则每 20 Tick 重试，直到成功或硬阻塞
        const bool needQualification = !firstSuccessSeen_ && (p.tick - lastRequestTick_ >= 20);
        if(changed) { planAttempts_=0; planRetry_=false; }
        const bool retryPlan=planRetry_ && planAttempts_<3 && p.tick-lastRequestTick_>=20;
        if (!changed && !needQualification && !retryPlan) return;
        ++planAttempts_; planRetry_=false;

        lastEventText_=events;
        lastMissionText_=missions;
        lastRequestTick_ = p.tick;

        Json user;
        user["tick"] = p.tick;
        user["max_tick"] = maxTicks_;
        user["map"] = {{"width", p.mapW}, {"height", p.mapH}};
        user["self"] = {{"position", Json::array({p.self.x, p.self.y})}, {"hp", p.hp}};
        user["mission_text"] = missions;
        user["event_text"] = events;
        user["previous_plan"] = previousPlan_;
        user["score"] = {{"resource_value", p.resourceValue}, {"effective_damage", p.effectiveDamage}};

        Json schema;
        schema["hazards"] =
            "array of {shape:'rect'|'cross', a:[x,y], b:[x,y], start:tick, end:tick, "
            "damage:HP per tick, wall:bool, lethal:bool}. start is inclusive, end is exclusive.";
        schema["effects"] = {{"inverted", "[start,end] movement direction inverted for us"},
                             {"vulnerable", "[start,end] damage taken doubled"},
                             {"reflect", "[start,end] attacking a defending target reflects damage"},
                             {"reflect_damage", "HP reflected"}};
        schema["advice"] = {{"target", "[x,y] recommended objective"}, {"priority", "1..3"}};

        schema["events"]="array of {id:string,kind:explosion|inversion|heal|fortress|split|chili|reflect|vulnerability,a:[x,y],b:[x,y],start:int,end:int,damage:int,defend_damage:int,duration:int,heal:int,dwell:int,trigger_end:int}. Regions inclusive, time half-open. For split a is intersection; inversion duration is acquired buff duration; heal duration is acquired healing duration; fortress damage and defend_damage are its attack damage; vulnerability trigger_end is movement-trigger cutoff.";
        Json body;
        body["instruction"] =
            "Convert the announcement into this schema. Prefer typed events; set hazards=[] and effects={} when using events, to avoid double-counting. Output only the JSON object. "
            "Use absolute map coordinates and half-open tick intervals [start,end). "
            "Never invent unspecified numbers. Omit unsupported events. Inversion affects movement only. Explosion acts once before movement; split kills at activation and its wall persists. "
            "Keep every still-active hazard from the previous announcement if it is still relevant.";
        body["schema"] = schema;
        body["request"] = user;
        llm_.submit(body.dump(), p.tick, p.session);
    }

    void drain_llm() {
        LlmResult result;
        while (llm_.poll(&result)) {
            if(result.session != session_) continue;
            if(result.httpSuccess&&result.valid) firstSuccessSeen_=true;
            if(result.sourceTick != lastRequestTick_) continue; // obsolete announcement revision
            if (!result.valid) { planRetry_=true; continue; }
            rmtc2::Plan plan;
            if (rmtc2::parse_plan(result.content, session_, result.sourceTick, maxTicks_, mapW_, mapH_, &plan)) {
                bot_.set_plan(plan);
                previousPlan_=result.content; planRetry_=false;
                RCLCPP_INFO(get_logger(), "事件计划已更新：%zu 个危险区", plan.hazards.size());
            } else {
                planRetry_=true;
                RCLCPP_WARN(get_logger(), "模型输出无法解析，最多两次计划重试，保留旧计划");
            }
        }
        // Qualification comes only from this session result, never a lifetime counter.
    }

    std::string seat_;
    std::string teamName_;
    int stage_ = 1;
    int maxTicks_ = 100;
    rmtc2::Bot bot_;
    LlmWorker llm_{http_post};

    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr action_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr registration_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr perception_;
    rclcpp::TimerBase::SharedPtr registrationTimer_;

    rmtc2::FrameGate frames_;
    bool planRetry_=false;
    int planAttempts_=0;
    std::string previousPlan_;
    std::string session_;
    int lastTick_ = -1;
    std::string lastAction_;
    std::string lastEventText_;
    std::string lastMissionText_;
    int lastRequestTick_ = -1000;
    bool firstSuccessSeen_ = false;
    int parseErrors_ = 0;
    int lastInvalid_=0;
    int mapW_ = 64;
    int mapH_ = 64;
};

rclcpp::Node::SharedPtr make_bot2_node() { return std::make_shared<Bot2Node>(); }

}  // namespace rmtc_bot2
