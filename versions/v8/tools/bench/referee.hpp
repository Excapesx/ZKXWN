// tools/bench/referee.hpp —— 8.0 离线仿真裁判（v1.1 规则，含随机事件）
//
// 按规则 PDF §5.2 的同 Tick 顺序结算：事件激活 → 事件死亡 → 固化动作 → 战斗
// （含击杀奖励、反伤、易伤、无双）→ 移动（幻术、临时墙）→ 入点事件（天使、辣椒）
// → 堡垒驻留与回血 → 死亡扣除 → 资源、任务、复活与保护倒计时。
//
// 事件参数有两套：standard（规则正文的标准数值）与 practice（官方练习包 stage2.json）。
// 事件的“理解”由 oracle 代替 LLM：在公告后若干 Tick 把结构化 JSON 交给双方各自的
// parse_plan。oracle 只在离线测评里用，参赛程序本身不接触任何结构化事件参数。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rmtc8/types.hpp"

namespace bench {

struct Resource {
    std::string id;
    rmtc8::Point pos;
    int value = 10;
};

enum class EventKind { Explosion, Illusion, Angel, Fortress, Split, Chili, Purity, Pan };

struct EventInst {
    std::string id;
    EventKind kind = EventKind::Explosion;
    int announce = 0;          // 公告 Tick
    int start = 0;             // 生效 Tick（含）
    int end = 0;               // 失效 Tick（不含）
    rmtc8::Point a, b;         // 矩形 / 交点 / 堡垒左上与右下
    std::vector<rmtc8::Point> points;  // 天使、辣椒点位
    int damage = 0;            // 爆炸、辣椒、无双普通伤害、反伤
    int defendDamage = 0;      // 无双防御伤害
    int effect = 0;            // 幻术持续、天使回血持续、易伤到期偏移
    int heal = 0;              // 天使每 Tick 回血
    int dwell = 0;             // 堡垒驻留要求
    int window = 0;            // 天使可进入窗口 / 易伤触发窗口（相对 start）
};

struct Robot {
    std::string team;
    rmtc8::Point spawn;
    rmtc8::Point pos;
    rmtc8::Point prevPos;
    int hp = 100;
    bool alive = true;
    int respawnIn = 0;
    int protectLeft = 0;
    bool defending = false;
    bool forcedWait = false;
    double resourceValue = 0;
    double damage = 0;          // 调整后有效伤害 D（含击杀奖励、死亡扣除）
    double attackDamage = 0;    // 诊断：实际攻击伤害
    double killCredit = 0;      // 诊断：击杀奖励
    int missionsDone = 0;
    int normalCount = 0;
    int highCount = 0;
    std::vector<std::string> doneIds;
    rmtc8::Action action;
    int deaths = 0;
    int kills = 0;
    int invalid = 0;
    int lastActionTick = -1;
    bool lastMoved = false;
    bool positionRespawn = false;
    // 事件效果（绝对到期 Tick，不含）
    int invertedUntil = -1;
    int healUntil = -1;
    int healRate = 0;
    int furyUntil = -1;
    int vulnerableUntil = -1;
    int vulnerableFrom = -1;
    int dwell = 0;
    int eventDeaths = 0;
};

struct MapDef {
    int stage = 2;              // 1：只允许 MOVE/WAIT、按 40×R/200 计分；2：对抗
    int w = 20;
    int h = 20;
    int vision = 4;
    std::vector<uint8_t> wall;  // 1 = 障碍
    std::vector<rmtc8::Point> spawns;
    std::vector<Resource> resources;
    std::vector<rmtc8::Mission> missions;
    std::string missionText;
    std::vector<EventInst> events;
};

MapDef generate_map(int w, int h, int robotCount, unsigned seed, int stage);
// 读取官方练习包的 stage1.json / stage2.json（地图、资源、任务、事件）
bool load_practice_config(const std::string& path, int robotCount, MapDef* out, std::string* err);
// 为随机地图生成一组事件；profile = "standard" 或 "practice"
std::vector<EventInst> generate_events(const MapDef& map, unsigned seed, const std::string& profile);
// 理想 LLM：到 tick 为止已公告事件的结构化计划（7.0/8.0 共用的 events 模式）。
// noise: 0 干净；1 同时给 events 和 hazards；2 夹带一条格式错误的事件；3 Markdown 包裹+说明文字
std::string oracle_plan(const MapDef& map, int tick, int noise);
// 某 Tick 是否有新公告（oracle 在公告后 latency Tick 交付）
bool has_announcement(const MapDef& map, int tick);

class Referee {
public:
    void reset(const MapDef& map, int maxTicks);
    int robot_count() const { return static_cast<int>(robots_.size()); }
    int tick() const { return tick_; }
    int max_ticks() const { return maxTicks_; }
    bool finished() const { return tick_ >= maxTicks_; }

    rmtc8::Perception perceive(int index) const;
    void submit(int index, const rmtc8::Action& action);
    void step();

    const std::vector<Robot>& robots() const { return robots_; }
    const MapDef& map() const { return map_; }
    double score(int index) const;
    int stage() const { return map_.stage; }

private:
    bool inside(rmtc8::Point p) const { return p.x >= 0 && p.y >= 0 && p.x < map_.w && p.y < map_.h; }
    bool wall(rmtc8::Point p) const { return !inside(p) || map_.wall[static_cast<std::size_t>(p.y) * map_.w + p.x] != 0; }
    bool temp_wall(rmtc8::Point p) const;
    bool occupied(rmtc8::Point p, int except) const;
    rmtc8::Point respawn_cell(const Robot& r) const;
    void announce(const std::string& text);

    MapDef map_;
    std::vector<Robot> robots_;
    int tick_ = 0;
    int maxTicks_ = 100;
    std::string session_ = "bench-session";
    std::vector<std::string> notices_;
};

}  // namespace bench
