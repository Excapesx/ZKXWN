// tools/bench/referee.hpp —— v1.1 规则的最小可信仿真裁判
//
// 只实现可判定的机制：移动争格、攻击射线、防御减伤、死亡复活、
// 资源收集、任务判定、有效伤害 D 的加减顺序、两阶段计分。
// 随机事件不在此实现（基准对局统一关闭事件，双方条件相同）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rmtc2/types.hpp"

namespace bench {

struct Resource {
    std::string id;
    rmtc2::Point pos;
    int value = 10;
};

struct Robot {
    std::string team;
    rmtc2::Point spawn;
    rmtc2::Point pos;
    int hp = 100;
    bool alive = true;
    int respawnIn = 0;
    int protectLeft = 0;
    bool defending = false;
    double resourceValue = 0;
    double damage = 0;
    int missionsDone = 0;
    int normalCount = 0;
    int highCount = 0;
    std::vector<std::string> doneIds;
    rmtc2::Action action;
    int deaths = 0;
    int kills = 0;
    int invalid = 0;
};

struct MapDef {
    int w = 20;
    int h = 20;
    std::vector<uint8_t> wall;  // 1 = 障碍
    std::vector<rmtc2::Point> spawns;
    std::vector<Resource> resources;
    std::vector<rmtc2::Mission> missions;
    std::string missionText;
};

MapDef generate_map(int w, int h, int robotCount, unsigned seed, int stage);

class Referee {
public:
    void reset(const MapDef& map, int maxTicks);
    int robot_count() const { return static_cast<int>(robots_.size()); }
    int tick() const { return tick_; }
    int max_ticks() const { return maxTicks_; }
    bool finished() const { return tick_ >= maxTicks_; }

    rmtc2::Perception perceive(int index) const;
    void submit(int index, const rmtc2::Action& action);
    void step();

    const std::vector<Robot>& robots() const { return robots_; }
    const MapDef& map() const { return map_; }
    double score(int index) const;

private:
    bool inside(rmtc2::Point p) const { return p.x >= 0 && p.y >= 0 && p.x < map_.w && p.y < map_.h; }
    bool wall(rmtc2::Point p) const { return !inside(p) || map_.wall[static_cast<std::size_t>(p.y) * map_.w + p.x] != 0; }
    void build_relative(rmtc2::Point self, rmtc2::Point entity, std::string* out) const;

    MapDef map_;
    std::vector<Robot> robots_;
    int tick_ = 0;
    int maxTicks_ = 100;
    std::string session_ = "bench-session";
};

}  // namespace bench
