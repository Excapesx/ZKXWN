// tools/node_sim/node_sim_main.cpp —— 不装 ROS 也能把“真正的” rmtc_bot8 节点跑一整场
//
// 编译真实的 ros2_ws/src/rmtc_bot8/src/node.cpp、llm.cpp、curl_transport.cpp，
// 用 stub/rclcpp 替身把话题收发接到离线裁判上：
//   裁判状态 → 协议格式的感知 JSON（相对位置图字符串、event_text、score…）→ 节点回调
//   节点发布的动作 JSON → 校验 tick/session_id/字段 → 裁判执行
// 模型请求走真实的 libcurl，发往本地假网关 mock_gateway.py（它按公告里的事件编号
// 返回结构化事件，可设延迟与故障）。其余席位为冻结的 7.0。
//
// 用法：node_sim <seed> <maps> <tick_ms> [events: standard|practice|none]
// 需要环境变量 RMTC_AI_GATEWAY（假网关地址）与 RMTC_TEAM_TOKEN；由 scripts/node_sim.sh 设置。
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "opponent_adapter.hpp"
#include "rclcpp/rclcpp.hpp"
#include "referee.hpp"
#include "rmtc8/parse.hpp"
#ifdef NODE_SIM_V7
#include "rmtc_bot7/node.hpp"  // 对照：7.0 的节点（需要校内7.0 源码，见 scripts/node_sim.sh）
namespace node_ns = rmtc_bot7;
#else
#include "rmtc_bot8/node.hpp"
namespace node_ns = rmtc_bot8;
#endif

using Json = nlohmann::json;
using rmtc8::Point;

namespace {

std::string grid(Point self, Point e) {
    std::string s(89, '0');
    for (int r = 0; r < 8; ++r) s[static_cast<std::size_t>(r) * 10 + 9] = '\n';
    s[44] = '1';
    const int dx = e.x - self.x, dy = e.y - self.y;
    if ((dx || dy) && std::abs(dx) <= 4 && std::abs(dy) <= 4) s[static_cast<std::size_t>(4 + dy) * 10 + 4 + dx] = '2';
    return s;
}

const char* act_name(rmtc8::Act k) {
    switch (k) {
    case rmtc8::Act::Move: return "move";
    case rmtc8::Act::Attack: return "attack";
    case rmtc8::Act::Defend: return "defend";
    default: return "wait";
    }
}

// 按通信协议 §3 生成感知 JSON（只含公开字段）
std::string wire(const rmtc8::Perception& p, const bench::Robot& r, const std::string& lastAction, bool first) {
    Json self = {{"team", p.team}, {"robot_id", p.team}, {"position", {p.self.x, p.self.y}}, {"hp", p.hp}, {"alive", p.alive},
                 {"defending", p.defending}, {"respawn_in", p.respawnIn}, {"protected", p.protectedSpawn}};
    if (first) self["position_reason"] = "initial";
    else if (p.positionRespawn) self["position_reason"] = "respawn";
    if (first) self["last_action"] = nullptr;
    else self["last_action"] = {{"tick", p.lastActionTick}, {"action", lastAction}, {"moved", p.lastMoved}};
    Json entities = Json::array();
    for (const auto& e : p.entities) {
        if (e.type == rmtc8::EntityType::Robot)
            entities.push_back({{"type", "robot"}, {"id", e.id}, {"team", e.team}, {"robot_id", e.team}, {"position", grid(p.self, e.pos)},
                                {"hp", e.hp}, {"defending", e.defending}, {"protected", e.protectedSpawn}});
        else if (e.type == rmtc8::EntityType::Obstacle)
            entities.push_back({{"type", "obstacle"}, {"position", grid(p.self, e.pos)}});
        else
            entities.push_back({{"type", e.type == rmtc8::EntityType::ResHigh ? "resource_high" : "resource_normal"}, {"id", e.id},
                                {"position", grid(p.self, e.pos)}, {"value", e.value}});
    }
    Json missions = Json::array();
    for (const auto& m : p.missions) {
        Json mj = {{"id", m.id}, {"type", m.type}, {"target", m.target}};
        if (m.position) mj["position"] = {m.position->x, m.position->y};
        missions.push_back(mj);
    }
    (void)r;
    Json j = {{"tick", p.tick}, {"session_id", p.session}, {"vision_radius", p.vision}, {"self", self}, {"visible_entities", entities},
              {"score", {{"resource_value", p.resourceValue}, {"effective_damage", p.effectiveDamage}, {"mission_completed", p.missionsCompleted},
                         {"mission_completed_ids", p.completedIds}, {"invalid_actions", p.invalidActions}}},
              {"mission_text", p.missionText}, {"missions", missions}, {"map", {{"width", p.mapW}, {"height", p.mapH}}}, {"event_text", p.eventText}};
    return j.dump();
}

}  // namespace

int main(int argc, char** argv) {
    const unsigned seed = argc > 1 ? static_cast<unsigned>(std::stoul(argv[1])) : 83000;
    const int maps = argc > 2 ? std::stoi(argv[2]) : 3;
    const int tickMs = argc > 3 ? std::stoi(argv[3]) : 20;
    const std::string events = argc > 4 ? argv[4] : "standard";
    setenv("RMTC_TEAM", "A", 1);
    setenv("RMTC_STAGE", "2", 1);
    setenv("RMTC_MAX_TICKS", "500", 1);
    Json report = Json::array();
    for (int m = 0; m < maps; ++m) {
        auto map = bench::generate_map(24, 24, 4, seed + static_cast<unsigned>(m), 2);
        if (events != "none") map.events = bench::generate_events(map, seed + static_cast<unsigned>(m), events);
        {   // 假网关按事件编号查表
            bench::MapDef all = map;
            for (auto& e : all.events) { e.announce = 0; e.end = 100000; }
            std::ofstream out("node_sim_events.json");
            out << bench::oracle_plan(all, 0, 0);
        }
        rclcpp::Bus::get() = rclcpp::Bus{};
        auto node = node_ns::make_bot2_node();
        auto& bus = rclcpp::Bus::get();
        std::function<void(const std::string&)> deliver;
        for (const auto& s : bus.subs) if (s.first == "/team_A/perception") deliver = s.second;
        auto actionPub = std::static_pointer_cast<rclcpp::Publisher<std_msgs::msg::String>>(bus.pubs["/team_A/action"].front());
        int eventQos = 0;
        for (const auto& q : bus.qos) if (q.first == "/referee/event_text") eventQos = q.second;

        bench::Referee ref;
        ref.reset(map, 500);
        std::vector<std::unique_ptr<IPolicy>> others;
        for (int i = 1; i < 4; ++i) others.push_back(make_v7_bot(2, 500));
        std::string lastAction = "wait";
        int published = 0, missing = 0, badEcho = 0, schemaErrors = 0;
        for (int t = 0; t < 500; ++t) {
            if (t >= 2 && bench::has_announcement(map, t - 2)) {
                const std::string content = bench::oracle_plan(map, t - 2, 0);
                for (auto& o : others) o->plan(content, "bench-session", t - 2, 500, map.w, map.h);
            }
            const auto p = ref.perceive(0);
            const std::size_t before = actionPub->sent.size();
            deliver(wire(p, ref.robots()[0], lastAction, t == 0));
            rmtc8::Action action = rmtc8::Action::wait();
            if (actionPub->sent.size() == before) {
                ++missing;
            } else {
                ++published;
                const Json a = Json::parse(actionPub->sent.back().data);
                if (a.value("tick", -1) != t || a.value("session_id", std::string{}) != p.session) ++badEcho;
                const std::string kind = a.value("action", std::string{});
                const std::string dir = a.value("direction", std::string{});
                int dx = 0, dy = 0;
                if (dir == "up") dy = -1; else if (dir == "down") dy = 1; else if (dir == "left") dx = -1; else if (dir == "right") dx = 1;
                if (kind == "move" && (dx || dy)) action = rmtc8::Action::move(dx, dy);
                else if (kind == "attack" && (dx || dy)) action = rmtc8::Action::attack(dx, dy);
                else if (kind == "defend") action = rmtc8::Action::defend();
                else if (kind != "wait") ++schemaErrors;
                if (kind != "move" && kind != "attack" && !dir.empty()) ++schemaErrors;
            }
            lastAction = act_name(action.kind);
            ref.submit(0, action);
            for (int i = 1; i < 4; ++i) ref.submit(i, others[static_cast<std::size_t>(i - 1)]->act(ref.perceive(i)));
            ref.step();
            std::this_thread::sleep_for(std::chrono::milliseconds(tickMs));
        }
        Json row = {{"map", m}, {"published", published}, {"missing_actions", missing}, {"bad_tick_or_session", badEcho},
                    {"schema_errors", schemaErrors}, {"invalid_actions", ref.robots()[0].invalid}, {"event_topic_qos_depth", eventQos}};
        Json scores = Json::array();
        for (int i = 0; i < 4; ++i) scores.push_back(ref.score(i));
        row["scores"] = scores;
        row["node_resource"] = ref.robots()[0].resourceValue;
        row["node_damage"] = ref.robots()[0].damage;
        row["node_missions"] = ref.robots()[0].missionsDone;
        row["node_event_deaths"] = ref.robots()[0].eventDeaths;
        report.push_back(row);
        node.reset();  // 析构时停止 LLM 线程
    }
    std::printf("%s\n", report.dump(2).c_str());
    return 0;
}
