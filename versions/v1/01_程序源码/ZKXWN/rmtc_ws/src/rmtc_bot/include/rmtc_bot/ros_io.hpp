// OWNER: A
#pragma once
#include "rmtc_bot/runtime.hpp"
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
namespace rmtc_bot {
class BotNode final : public rclcpp::Node {
public:
    BotNode();
private:
    std::unique_ptr<BotRuntime> runtime_;
    rclcpp::Publisher<std_msgs::msg::String>::SharedPtr actions_, registration_;
    rclcpp::Subscription<std_msgs::msg::String>::SharedPtr perception_;
};
}
