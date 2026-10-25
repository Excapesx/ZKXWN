// rmtc_bot8/main.cpp
#include <cstdio>
#include <exception>

#include "rclcpp/rclcpp.hpp"
#include "rmtc_bot8/node.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    try {
        auto node = rmtc_bot8::make_bot2_node();
        rclcpp::spin(node);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "rmtc_bot8 stopped: %s\n", e.what());
        rclcpp::shutdown();
        return 1;
    }
    rclcpp::shutdown();
    return 0;
}
