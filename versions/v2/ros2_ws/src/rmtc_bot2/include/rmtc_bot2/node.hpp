#pragma once

#include <memory>

#include "rclcpp/rclcpp.hpp"

namespace rmtc_bot2 {
rclcpp::Node::SharedPtr make_bot2_node();
}
