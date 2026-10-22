// OWNER: A
#include "rmtc_bot/ros_io.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdlib>
namespace rmtc_bot {
BotNode::BotNode() : Node("rmtc_bot") {
    auto c=load_config_from_env();
    c.team=declare_parameter<std::string>("team",c.team);
    c.team_name=declare_parameter<std::string>("team_name",c.team_name);
    c.stage=declare_parameter<int>("stage",c.stage);
    // -1 derives the official length from stage after ROS parameter overrides.
    const int maximum=declare_parameter<int>("max_ticks",-1);
    if(maximum>=0) c.max_ticks=maximum;
    else if(!std::getenv("RMTC_MAX_TICKS")) c.max_ticks=c.stage==1?100:500;
    // decision_type is read only from RMTC_DECISION_CONFIG (decision_config.json).
    // Do not expose it as a ROS parameter: on-site switching must follow the
    // registered JSON-only procedure.
    c.llm_enabled=declare_parameter<bool>("llm_enabled",c.llm_enabled);
    c.gateway=declare_parameter<std::string>("gateway",c.gateway);
    c.model=declare_parameter<std::string>("model",c.model);
    c.request_timeout_ms=declare_parameter<int>("http_timeout_ms",c.request_timeout_ms);
    c.max_tokens=declare_parameter<int>("max_tokens",c.max_tokens);
    // Authentication is read from environment only, never from ROS parameters.
    validate_config(c);
    auto transport=make_curl_transport();
    if(c.llm_enabled&&!transport) throw std::runtime_error("LLM enabled but CURL transport unavailable");
    runtime_=std::make_unique<BotRuntime>(c,std::move(transport));
    const auto prefix="/team_"+c.team;
    auto live=rclcpp::QoS(rclcpp::KeepLast(1)).reliable().durability_volatile();
    auto retained=rclcpp::QoS(rclcpp::KeepLast(1)).reliable().transient_local();
    actions_=create_publisher<std_msgs::msg::String>(prefix+"/action",live);
    registration_=create_publisher<std_msgs::msg::String>(prefix+"/registration",retained);
    perception_=create_subscription<std_msgs::msg::String>(prefix+"/perception",live,
        [this](std_msgs::msg::String::ConstSharedPtr msg) {
            const auto started=std::chrono::steady_clock::now();
            try {
                const auto result=runtime_->handle(msg->data);
                // A resumed referee may reopen the same tick. Resubmit the cached
                // action without advancing memory or policy (protocol section 4).
                if(result) {
                    std_msgs::msg::String action; action.data=*result; actions_->publish(action);
                    RCLCPP_DEBUG(get_logger(),"published_action=%s",result->c_str());
                }
                if(runtime_->reason()=="invalid_perception")
                    RCLCPP_WARN(get_logger(),"Invalid perception schema; no action published");
                const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
                RCLCPP_DEBUG(get_logger(),"decision=%s callback_us=%lld llm=%s",runtime_->reason().c_str(),static_cast<long long>(elapsed),runtime_->llm_status().c_str());
                if(runtime_->last_receipt() && runtime_->reason()!="cached_tick") {
                    const auto& receipt=*runtime_->last_receipt();
                    RCLCPP_DEBUG(get_logger(),"receipt_tick=%lld moved=%d",static_cast<long long>(receipt.tick),receipt.moved?1:0);
                }
            } catch(const std::exception&) { RCLCPP_ERROR(get_logger(),"Callback failed; no action published for this message"); }
        });
    std_msgs::msg::String name;
    name.data=nlohmann::json{{"team",c.team},{"name",c.team_name}}.dump(); registration_->publish(name);
    RCLCPP_INFO(get_logger(),"Ready: seat=%s stage=%d max_ticks=%d llm=%s",c.team.c_str(),c.stage,c.max_ticks,c.llm_enabled?"enabled":"disabled");
}
}
