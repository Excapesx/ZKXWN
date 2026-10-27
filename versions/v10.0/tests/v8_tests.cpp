// v8_tests.cpp — 8.0 新增行为的契约测试（不依赖 ROS）。
//
// 覆盖：容错的计划解析、不开火对手的在线识别、公告累积与请求节流、
// 第一阶段观察点路线不摆动、第二阶段开局路线不压过补刀、
// 以及测评裁判的 v1.1 结算顺序（击杀奖励、死亡扣除、事件）。
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "referee.hpp"
#include "rmtc9/announce.hpp"
#include "rmtc9/bot.hpp"
#include "rmtc9/parse.hpp"

using namespace rmtc9;

static int checks = 0, failures = 0;
static void check(bool yes, const char* name) {
    ++checks;
    if (!yes) { ++failures; std::cout << "FAIL " << name << "\n"; }
}

static Perception base(int tick = 0) {
    Perception p;
    p.session = "v8-test";
    p.team = "A";
    p.tick = tick;
    p.mapW = p.mapH = 12;
    p.vision = 4;
    p.self = {5, 5};
    p.hp = 100;
    p.alive = true;
    p.positionInitial = tick == 0;
    return p;
}

static Entity robot(const std::string& team, Point pos, int hp = 100) {
    Entity e;
    e.type = EntityType::Robot;
    e.team = team;
    e.id = "robot_" + team;
    e.pos = pos;
    e.hp = hp;
    return e;
}

// ---------------------------------------------------------------------------
// 计划解析：坏条目只丢那一条
// ---------------------------------------------------------------------------
static void tolerant_plans() {
    Plan plan;
    // 模型同时写了 events 与 hazards：7.0 整份拒收；8.0 用 events，忽略重复的 hazards
    check(parse_plan(R"({"events":[{"id":"e1","kind":"explosion","a":[1,1],"b":[3,3],"start":20,"end":21,"damage":80}],
                         "hazards":[{"shape":"rect","a":[1,1],"b":[3,3],"start":20,"end":21,"damage":80}]})",
                     "s", 0, 500, 12, 12, &plan) && plan.events.size() == 1 && plan.hazards.empty(),
          "events and hazards together: events used, hazards ignored");
    // 一条认不出的事件夹在中间：其余照用
    check(parse_plan(R"({"events":[{"id":"a","kind":"split","a":[4,4],"b":[4,4],"start":30,"end":60},
                                   {"id":"note","kind":"announcement","a":[0,0],"b":[0,0],"start":1,"end":2},
                                   {"id":"c","kind":"chili","a":[6,6],"b":[6,6],"start":40,"end":70,"damage":50}]})",
                     "s", 0, 500, 12, 12, &plan) && plan.events.size() == 2,
          "one unknown event skipped, others kept");
    // 规则原文名字归一（只归一模型输出的 kind 字段）
    check(parse_plan(R"({"events":[{"id":"p","kind":"pepper","a":[2,2],"start":40,"end":70,"damage":30}]})",
                     "s", 0, 500, 12, 12, &plan) && plan.events.size() == 1 && plan.events[0].kind == "chili" &&
              plan.events[0].b == Point{2, 2},
          "rule-name alias normalised, missing b defaults to a");
    // 缺伤害数值时用规则公布的标准值（爆炸 80）
    check(parse_plan(R"({"events":[{"id":"b","kind":"explosion","a":[1,1],"b":[2,2],"start":20}]})",
                     "s", 0, 500, 12, 12, &plan) && plan.events.size() == 1 && plan.events[0].damage == 80 &&
              plan.events[0].end == 21,
          "missing explosion numbers use the published standard values");
    // 一条可用的都没有 → 返回失败，节点会有限重试
    check(!parse_plan(R"({"events":[{"kind":"???"}]})", "s", 0, 500, 12, 12, &plan), "no usable entry -> failure for retry");
    check(!parse_plan(R"({"events":"later"})", "s", 0, 500, 12, 12, &plan), "wrong top-level type rejected");
    check(parse_plan("{}", "s", 0, 500, 12, 12, &plan) && plan.valid, "empty object stays a valid no-op plan");
    check(parse_plan("说明文字\n```json\n{\"events\":[]}\n```", "s", 0, 500, 12, 12, &plan) && plan.valid, "markdown-wrapped JSON accepted");
}

// ---------------------------------------------------------------------------
// 不开火对手：7.0 会对着它原地防御几百 Tick
// ---------------------------------------------------------------------------
static void passive_enemy() {
    Bot bot;
    bot.configure({2, 500});
    std::string first, last;
    Decision lastDecision;
    for (int t = 0; t < 8; ++t) {
        Perception p = base(t);
        p.positionInitial = false;
        p.hp = 20;  // 一枪就死的血量
        p.effectiveDamage = 150; // Death now loses credit; this checks a meaningful survival decision.
        p.self = {5, 7};
        p.lastActionTick = t - 1;
        p.entities = {robot("B", {5, 5}, 100)};  // 同列相隔 2 格，但从不开火
        const Decision d = bot.decide(p);
        if (t == 0) first = d.reason;
        last = d.reason;
        lastDecision = d;
    }
    check(first == "defend_threat" || first == "attack", "first tick: the aligned enemy is treated as a lethal threat");
    // 9.0 intentionally replaces the old unsafe contract: quiet history is not
    // enough to erase a lethal shot. Nonlethal opportunity costs can still adapt.
    check(lastDecision.action.kind == Act::Defend,
          "quiet history does not erase a lethal incoming shot");
}

// ---------------------------------------------------------------------------
// 公告累积与请求节流
// ---------------------------------------------------------------------------
static void announcements() {
    AnnouncementLog log;
    check(log.ingest("第 10 Tick：预告 A\n第 10 Tick：预告 B") == 2 && log.version() == 1, "two new lines");
    check(log.ingest("第 10 Tick：预告 B\n第 12 Tick：生效 A") == 1 && log.version() == 2, "rolling window: only the new line counts");
    check(log.ingest("第 10 Tick：预告 A") == 0 && log.version() == 2, "repeated text does not bump the version");
    const auto recent = log.recent(2, 10000);
    check(recent.size() == 2 && recent.back() == "第 12 Tick：生效 A", "recent keeps chronological order");
    log.reset();
    check(log.lines().empty() && log.version() == 0, "new match clears the log");

    PlanScheduler s;
    check(!s.want_plan(false, 100, 0, 500, 0), "no new announcement -> no request");
    check(s.want_plan(true, 100, 90, 500, 0), "new announcement after the minimum interval");
    check(!s.want_plan(true, 100, 99, 500, 0), "minimum interval respected");
    check(s.interval(100, 500, 85) >= 80, "interval stretches when the budget is nearly used");
    check(!s.want_plan(true, 400, 0, 500, 90), "budget exhausted -> no more plan requests");
}

// ---------------------------------------------------------------------------
// 第二阶段：能开火时开局路线不抢决策
// ---------------------------------------------------------------------------
static void route_does_not_override_attack() {
    Bot bot;
    bot.configure({2, 500});
    Perception p = base(0);
    p.entities = {robot("B", {6, 5}, 20)};  // 相邻、一枪可杀
    const Decision d = bot.decide(p);
    check(d.action.kind == Act::Attack && d.action.dx == 1, "adjacent kill shot beats early exploration");
}

// ---------------------------------------------------------------------------
// 第一阶段：观察点路线不会原地来回摆动
// ---------------------------------------------------------------------------
static void stage1_no_oscillation() {
    int worst = 0;
    for (unsigned seed = 0; seed < 40; ++seed) {
        for (int vision : {3, 4}) {
            auto map = bench::generate_map(20, 20, 1, 7100 + seed, 1);
            map.vision = vision;
            bench::Referee ref;
            ref.reset(map, 100);
            Bot bot;
            bot.configure({1, 100});
            std::vector<Point> trail;
            int run = 0;
            for (int t = 0; t < 100; ++t) {
                const auto p = ref.perceive(0);
                ref.submit(0, bot.decide(p).action);
                ref.step();
                trail.push_back(ref.robots()[0].pos);
                const std::size_t n = trail.size();
                if (n >= 3 && trail[n - 1] == trail[n - 3] && trail[n - 1] != trail[n - 2]) ++run; else run = 0;
                worst = std::max(worst, run);
            }
        }
    }
    check(worst <= 6, "stage 1 never ping-pongs between two cells for more than 6 ticks");
}

// ---------------------------------------------------------------------------
// 测评裁判：v1.1 结算顺序
// ---------------------------------------------------------------------------
static bench::MapDef open_map(int w, int h, std::vector<Point> spawns) {
    bench::MapDef m;
    m.stage = 2;
    m.w = w;
    m.h = h;
    m.wall.assign(static_cast<std::size_t>(w) * h, 0);
    m.spawns = std::move(spawns);
    return m;
}

static void referee_rules() {
    {   // 同 Tick 互相击毁：先计伤害与击杀奖励，再各扣 60
        auto m = open_map(8, 8, {{2, 2}, {3, 2}});
        bench::Referee ref;
        ref.reset(m, 100);
        for (int t = 0; t < 5; ++t) {
            ref.submit(0, Action::attack(1, 0));
            ref.submit(1, Action::attack(-1, 0));
            ref.step();
        }
        const auto& a = ref.robots()[0];
        const auto& b = ref.robots()[1];
        check(!a.alive && !b.alive, "simultaneous mutual kill");
        check(std::abs(a.damage - 100.0) < 1e-9 && std::abs(b.damage - 100.0) < 1e-9, "mutual kill: 100 damage + 60 kill - 60 death");
    }
    {   // 爆炸致死：只扣死亡，不发击杀奖励；出生保护免疫
        auto m = open_map(8, 8, {{2, 2}, {6, 6}});
        bench::EventInst e;
        e.id = "boom";
        e.kind = bench::EventKind::Explosion;
        e.announce = 0;
        e.start = 1;
        e.end = 2;
        e.a = {1, 1};
        e.b = {3, 3};
        e.damage = 120;
        m.events = {e};
        bench::Referee ref;
        ref.reset(m, 100);
        ref.step();
        ref.step();
        check(!ref.robots()[0].alive && ref.robots()[0].eventDeaths == 1, "explosion kills inside the rectangle");
        check(ref.robots()[1].damage == 0 && ref.robots()[1].kills == 0, "explosion kill grants no kill bonus");
    }
    {   // 世界分裂：线上车辆立即满血复活、本 Tick 强制 WAIT（不记无效动作）
        auto m = open_map(9, 9, {{1, 1}, {7, 7}});
        bench::EventInst e;
        e.id = "split";
        e.kind = bench::EventKind::Split;
        e.start = 1;
        e.end = 4;
        e.a = e.b = {4, 7};
        m.events = {e};
        bench::Referee ref;
        ref.reset(m, 100);
        ref.submit(1, Action::move(-1, 0));
        ref.step();  // tick0：车 1 走到 (6,7)，在分裂十字线 y=7 上
        ref.submit(1, Action::move(-1, 0));
        ref.step();  // tick1：分裂激活
        const auto& r = ref.robots()[1];
        check(r.eventDeaths == 1 && r.alive && r.hp == 100 && r.pos == Point{7, 7}, "split kills and respawns at spawn");
        check(r.invalid == 0 && !r.lastMoved, "forced WAIT is not an invalid action");
        check(ref.perceive(1).positionRespawn, "respawn receipt after split");
    }
    {   // 洁身自好：攻击防御者 → 目标不受伤，攻击者吃反伤
        auto m = open_map(8, 8, {{2, 2}, {3, 2}});
        bench::EventInst e;
        e.id = "purity";
        e.kind = bench::EventKind::Purity;
        e.start = 0;
        e.end = 10;
        e.damage = 20;
        m.events = {e};
        bench::Referee ref;
        ref.reset(m, 100);
        ref.submit(0, Action::attack(1, 0));
        ref.submit(1, Action::defend());
        ref.step();
        check(ref.robots()[0].hp == 80 && ref.robots()[1].hp == 100 && ref.robots()[0].damage == 0, "reflection hurts the attacker only");
    }
    {   // 辣椒：踩点触发整行整列火线，致死时触发者拿击杀奖励（无攻击伤害）
        auto m = open_map(9, 9, {{1, 4}, {6, 4}});
        bench::EventInst e;
        e.id = "chili";
        e.kind = bench::EventKind::Chili;
        e.start = 0;
        e.end = 20;
        e.points = {{2, 4}};
        e.damage = 100;
        m.events = {e};
        bench::Referee ref;
        ref.reset(m, 100);
        ref.submit(0, Action::move(1, 0));
        ref.step();
        check(!ref.robots()[1].alive && std::abs(ref.robots()[0].damage - 60.0) < 1e-9, "chili kill: trigger gets the 60 kill bonus only");
    }
    {   // 第一阶段只允许 MOVE/WAIT
        auto m = open_map(8, 8, {{2, 2}});
        m.stage = 1;
        bench::Referee ref;
        ref.reset(m, 100);
        ref.submit(0, Action::defend());
        ref.step();
        check(ref.robots()[0].invalid == 1, "stage 1 rejects DEFEND as an invalid action");
    }
}

int main() {
    tolerant_plans();
    passive_enemy();
    announcements();
    route_does_not_override_attack();
    stage1_no_oscillation();
    referee_rules();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
