// rmtc91/types.hpp —— 2.0 的公共数据模型（纯 C++17，无 ROS 依赖）
//
// 设计原则：
//   1. 只表达"协议允许知道的东西"，不引入任何隐藏真值字段；
//   2. 所有坐标都是绝对坐标，方向用 (dx,dy) 表示，避免 up/down 映射错位；
//   3. 结构体只做数据载体，不做策略判断。
#pragma once

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace rmtc91 {

struct Point {
    int x = 0;
    int y = 0;
};
inline bool operator==(Point a, Point b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Point a, Point b) { return !(a == b); }
inline bool operator<(Point a, Point b) { return a.y != b.y ? a.y < b.y : a.x < b.x; }
inline Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
inline int manhattan(Point a, Point b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

// 四个方向，索引 0..3 固定顺序（与 first-step 编码共用）
extern const int kDirX[4];
extern const int kDirY[4];
extern const char* const kDirName[4];

enum class Act { Wait, Move, Attack, Defend };

struct Action {
    Act kind = Act::Wait;
    int dx = 0;
    int dy = 0;

    static Action wait() { return {}; }
    static Action move(int dx_, int dy_) {
        Action a;
        a.kind = Act::Move;
        a.dx = dx_;
        a.dy = dy_;
        return a;
    }
    static Action attack(int dx_, int dy_) {
        Action a;
        a.kind = Act::Attack;
        a.dx = dx_;
        a.dy = dy_;
        return a;
    }
    static Action defend() {
        Action a;
        a.kind = Act::Defend;
        return a;
    }
    bool directional() const { return kind == Act::Move || kind == Act::Attack; }
    bool legal_shape() const {
        if (!directional()) return kind == Act::Defend || kind == Act::Wait;
        return (std::abs(dx) + std::abs(dy)) == 1;
    }
};

// 把方向索引 0..3 转成动作；索引非法返回 false
bool dir_action(int index, bool attack, Action* out);

enum class EntityType { Robot, ResNormal, ResHigh, Obstacle };struct Entity {
    EntityType type = EntityType::Obstacle;
    std::string id;
    std::string team;
    Point pos;
    int hp = 0;
    int value = 0;
    bool defending = false;
    bool protectedSpawn = false;
};

struct Mission {
    std::string id;
    std::string type;  // normal_resources / high_resources / resource_value / effective_damage / reach
    double target = 1;
    std::optional<Point> position;
};

// 一帧感知。字段名与协议保持一一对应，缺失字段在解析层给安全默认值。
struct Perception {
    int tick = 0;
    std::string session;
    int mapW = 0;
    int mapH = 0;
    int vision = 4;
    Point self;
    std::string team;  // 本队席位标识（A/B/C/D）
    int hp = 0;
    bool alive = false;
    bool defending = false;
    bool protectedSpawn = false;
    int respawnIn = 0;
    bool positionInitial = false;
    bool positionRespawn = false;
    bool observationComplete = true;
    int lastActionTick = -1;
    bool lastMoved = false;
    int invalidActions = 0;
    std::vector<Entity> entities;
    double resourceValue = 0;
    double effectiveDamage = 0;
    int missionsCompleted = 0;
    std::vector<std::string> completedIds;
    std::string eventText;
    std::string missionText;
    std::vector<Mission> missions;

    bool valid() const {
        return !session.empty() && session.size()<=256 && tick >= 0 && tick<=1000000 && mapW > 0 && mapH > 0 && mapW<=256 && mapH<=256 && vision >= 0 && vision<=4 &&
               self.x >= 0 && self.y >= 0 && self.x < mapW && self.y < mapH;
    }
};

// ---------------------------------------------------------------------------
// 事件解释（由 LLM 给出，本地只做校验和过期处理，不做关键词匹配）
// ---------------------------------------------------------------------------

struct Hazard {
    bool cross = false;   // true: 整行十字；false: 矩形
    Point a;              // 矩形左上 / 十字交点
    Point b;              // 矩形右下（cross 时等于 a）
    int start = 0;        // 生效起点（含）
    int end = 0;          // 失效点（不含）
    int damage = 0;       // 每 Tick 环境伤害
    bool lethal = false;  // 直接阵亡（世界分裂/分裂墙）
    bool wall = false;    // 阻断移动
    bool respectsProtection = false;
    bool active(int tick) const { return start <= tick && tick < end; }
    bool contains(Point p) const {
        if (cross) return p.x == a.x || p.y == a.y;
        return p.x >= a.x && p.x <= b.x && p.y >= a.y && p.y <= b.y;
    }
};

struct Window {
    int start = -1;
    int end = -1;
    bool active(int tick) const { return start >= 0 && start <= tick && tick < end; }
};

// Typed facts supplied by the LLM only; never inferred from announcement keywords.
struct Event {
    std::string id, kind;
    Point a,b;
    int start=0,end=0;
    int damage=0, defendDamage=0, duration=0, heal=0, dwell=0, triggerEnd=0;
    bool active(int t) const { return start<=t && t<end; }
    bool contains(Point p) const { return p.x>=a.x && p.x<=b.x && p.y>=a.y && p.y<=b.y; }
};
struct Plan {
    std::string session;
    int sourceTick = 0;
    bool valid = false;
    std::vector<Hazard> hazards;
    std::vector<Event> events;
    Window fury;
    int furyDamage=0, furyDefendDamage=0, healUntil=-1, healRate=0;
    Window inverted;          // 幻术：移动方向反转
    Window vulnerable;        // 易伤：伤害 ×2
    Window reflect;           // 洁身自好：攻击防御目标反伤
    int reflectDamage = 20;
    std::optional<Point> advice;  // 模型建议的目标点
    int advicePriority = 0;
};

struct Config {
    int stage = 1;
    int maxTicks = 100;
    // 规则 4.2 公布的每场资源总量（标准：普通 12×10、高价值 4×20）。
    // 赛前若调整，主办方会同步公布；这里只作为“还有几个没露面”的先验。
    int totalNormal = 12;
    int totalHigh = 4;
};

}  // namespace rmtc91
