// SHARED
#include "rmtc_bot/ros_io.hpp"
#include <cstdio>
int main(int argc,char** argv) {
    rclcpp::init(argc,argv);
    try { rclcpp::spin(std::make_shared<rmtc_bot::BotNode>()); }
    catch(const std::exception&) {
        std::fputs("RMTC node stopped: check configuration and ROS environment.\n",stderr);
        rclcpp::shutdown(); return 1;
    }
    rclcpp::shutdown(); return 0;
}
