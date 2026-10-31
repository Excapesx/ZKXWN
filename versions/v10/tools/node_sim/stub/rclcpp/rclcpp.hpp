// Minimal rclcpp stand-in used ONLY by tools/node_sim to compile and drive the real node.cpp without ROS 2.
// Not part of the contestant build (ros2_ws uses the real rclcpp).
#pragma once
#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "std_msgs/msg/string.hpp"
namespace rclcpp {
struct KeepLast { explicit KeepLast(int d) : depth(d) {} int depth; };
struct QoS { explicit QoS(KeepLast k) : depth(k.depth) {} QoS& reliable() { return *this; } QoS& durability_volatile() { return *this; } QoS& transient_local() { latched = true; return *this; } int depth; bool latched = false; };
struct Logger {};
template <class M> struct Publisher { using SharedPtr = std::shared_ptr<Publisher>; std::string topic; std::vector<M> sent; void publish(const M& m) { sent.push_back(m); } };
template <class M> struct Subscription { using SharedPtr = std::shared_ptr<Subscription>; std::string topic; std::function<void(typename M::ConstSharedPtr)> cb; QoS qos{KeepLast(1)}; };
struct TimerBase { using SharedPtr = std::shared_ptr<TimerBase>; std::function<void()> cb; };
struct Bus { std::map<std::string, std::vector<std::shared_ptr<void>>> pubs; std::vector<std::pair<std::string, std::function<void(const std::string&)>>> subs; std::vector<std::pair<std::string, int>> qos; static Bus& get() { static Bus b; return b; } };
class Node : public std::enable_shared_from_this<Node> {
public:
  using SharedPtr = std::shared_ptr<Node>;
  explicit Node(const std::string&) {}
  virtual ~Node() = default;
  template <class T> T declare_parameter(const std::string& name, const T& def) { (void)name; return def; }
  template <class M> typename Publisher<M>::SharedPtr create_publisher(const std::string& topic, const QoS&) {
    auto p = std::make_shared<Publisher<M>>(); p->topic = topic; Bus::get().pubs[topic].push_back(p); return p; }
  template <class M, class F> typename Subscription<M>::SharedPtr create_subscription(const std::string& topic, const QoS& q, F f) {
    auto s = std::make_shared<Subscription<M>>(); s->topic = topic; s->cb = f; s->qos = q;
    Bus::get().subs.push_back({topic, [f](const std::string& d) { auto m = std::make_shared<M>(); m->data = d; f(m); }});
    Bus::get().qos.push_back({topic, q.depth}); return s; }
  template <class D, class F> TimerBase::SharedPtr create_wall_timer(D, F f) { auto t = std::make_shared<TimerBase>(); t->cb = f; return t; }
  Logger get_logger() const { return {}; }
};
inline void init(int, char**) {}
inline void spin(const std::shared_ptr<Node>&) {}
inline void shutdown() {}
}
#define RCLCPP_INFO(logger, ...) do { (void)(logger); if (getenv("STUB_LOG")) { std::fprintf(stderr, __VA_ARGS__); std::fputc('\n', stderr); } } while (0)
#define RCLCPP_WARN(logger, ...) RCLCPP_INFO(logger, __VA_ARGS__)
