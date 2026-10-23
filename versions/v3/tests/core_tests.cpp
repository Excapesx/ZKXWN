// tests/core_tests.cpp —— 核心库回归测试（不依赖 ROS / 裁判）
#include <algorithm>
#include <set>
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include "rmtc2/bot.hpp"
#include "rmtc2/grid.hpp"
#include "rmtc2/parse.hpp"
#include "rmtc2/frame_gate.hpp"
#include "rmtc2/combat.hpp"

namespace {
int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  [FAIL] %s\n", what);
    }
}
void check_near(double a, double b, double eps, const char* what) {
    ++g_checks;
    if (std::fabs(a - b) > eps) {
        ++g_failures;
        std::printf("  [FAIL] %s (%.6f vs %.6f)\n", what, a, b);
    }
}

std::string rel_grid(int dx, int dy) {
    std::string g(89, '0');
    for (int r = 0; r < 8; ++r) g[static_cast<std::size_t>(r) * 10 + 9] = '\n';
    g[4 * 10 + 4] = '1';
    if (dx != 0 || dy != 0) g[static_cast<std::size_t>(4 + dy) * 10 + (4 + dx)] = '2';
    return g;
}

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\n') out += "\\n";
        else out.push_back(c);
    }
    return out;
}

void test_parse() {
    std::printf("test_parse\n");
    const std::string json =
        "{\"tick\":148,\"session_id\":\"s1\",\"vision_radius\":4,"
        "\"self\":{\"team\":\"A\",\"position\":[8,8],\"hp\":70,\"alive\":true,\"defending\":false,"
        "\"respawn_in\":0,\"protected\":false},"
        "\"visible_entities\":["
        "{\"type\":\"robot\",\"id\":\"robot_B\",\"team\":\"B\",\"hp\":60,\"defending\":true,"
        "\"protected\":false,\"position\":\"" + escape(rel_grid(1, 0)) + "\"},"
        "{\"type\":\"resource_high\",\"id\":\"h01\",\"value\":20,\"position\":\"" + escape(rel_grid(0, 1)) + "\"},"
        "{\"type\":\"obstacle\",\"position\":\"" + escape(rel_grid(-2, 3)) + "\"},"
        "{\"type\":\"future_thing\",\"position\":\"" + escape(rel_grid(0, 0)) + "\"}"
        "],"
        "\"score\":{\"resource_value\":30,\"effective_damage\":12.5,\"mission_completed\":1,"
        "\"mission_completed_ids\":[\"m1\"],\"invalid_actions\":0},"
        "\"mission_text\":\"x\",\"event_text\":\"\","
        "\"missions\":[{\"id\":\"reach_target\",\"type\":\"reach\",\"target\":1,\"position\":[12,12]}],"
        "\"map\":{\"width\":24,\"height\":24}}";

    rmtc2::Perception p;
    std::string err;
    check(rmtc2::parse_perception(json, &p, &err), "perception parsed");
    check(p.tick == 148, "tick");
    check(p.team == "A", "self team");
    check(p.self.x == 8 && p.self.y == 8, "self position");
    check(p.entities.size() == 3, "unknown entity type skipped");
    if (p.entities.size() == 3) {
        check(p.entities[0].type == rmtc2::EntityType::Robot, "robot type");
        check(p.entities[0].pos.x == 9 && p.entities[0].pos.y == 8, "robot absolute position");
        check(p.entities[0].defending, "robot defending");
        check(p.entities[1].type == rmtc2::EntityType::ResHigh, "resource type");
        check(p.entities[2].pos.x == 6 && p.entities[2].pos.y == 11, "obstacle position");
    }
    check_near(p.effectiveDamage, 12.5, 1e-9, "fractional damage kept");
    check(p.completedIds.size() == 1 && p.completedIds[0] == "m1", "completed ids");

    auto frame = nlohmann::json::parse(json);
    for (int radius = 0; radius <= 4; ++radius) {
        frame["vision_radius"] = radius;
        check(rmtc2::parse_perception(frame.dump(), &p, &err) && p.vision == radius,
              "legal vision radius preserved");
    }
    for (const auto& value : nlohmann::json::array({-1, 5, 9, 1.5, "4", nullptr, true, 18446744073709551615ULL})) {
        frame["vision_radius"] = value;
        check(!rmtc2::parse_perception(frame.dump(), &p, &err), "invalid vision rejected");
    }

    rmtc2::Perception bad;
    check(!rmtc2::parse_perception("{\"tick\":1}", &bad, &err), "missing fields rejected");
    check(!rmtc2::parse_perception("不是 JSON", &bad, &err), "garbage rejected");
}

void test_action_json() {
    std::printf("test_action_json\n");
    const std::string s = rmtc2::action_json(rmtc2::Action::move(1, 0), 7, "sess");
    check(s.find("\"action\":\"move\"") != std::string::npos, "action move");
    check(s.find("\"direction\":\"right\"") != std::string::npos, "direction right");
    check(s.find("\"tick\":7") != std::string::npos, "tick echoed");
    check(s.find("\"session_id\":\"sess\"") != std::string::npos, "session echoed");
    const std::string d = rmtc2::action_json(rmtc2::Action::defend(), 3, "s");
    check(d.find("direction") == std::string::npos, "defend has no direction");
}

void test_plan_parse() {
    std::printf("test_plan_parse\n");
    const std::string content =
        "好的，我解析到：```json\n"
        "{\"hazards\":[{\"shape\":\"rect\",\"a\":[3,3],\"b\":[7,7],\"start\":100,\"end\":101,\"damage\":35},"
        "{\"shape\":\"cross\",\"a\":[5,5],\"start\":200,\"end\":230,\"wall\":true,\"lethal\":true}],"
        "\"effects\":{\"inverted\":[120,170],\"reflect\":[300,320],\"reflect_damage\":20},"
        "\"advice\":{\"target\":[12,12],\"priority\":2}}\n```";
    rmtc2::Plan plan;
    check(rmtc2::parse_plan(content, "sess", 90, 500, 24, 24, &plan), "plan parsed from fenced text");
    check(plan.hazards.size() == 2, "two hazards");
    check(plan.inverted.active(150) && !plan.inverted.active(170), "inverted window half-open");
    check(plan.advice && plan.advice->x == 12, "advice target");
    check(plan.advicePriority == 2, "advice priority");

    rmtc2::HazardField field;
    field.reset(24, 24);
    field.apply(plan);
    check(field.damageAt({5, 5}, 100) == 35, "rect damage inside");
    check(field.damageAt({8, 8}, 100) == 0, "rect damage outside");
    check(field.blockedAt({0, 5}, 210), "cross blocks whole row");
    check(field.blockedAt({5, 23}, 210), "cross blocks whole column");
    check(!field.blockedAt({4, 4}, 210), "cross leaves other cells");
    check(!field.blockedAt({0, 5}, 230), "cross expires at end");
}

// 与暴力枚举对比，验证定向越野求的是真最优
void test_tour_optimal() {
    std::printf("test_tour_optimal\n");
    std::mt19937 rng(7);
    for (int round = 0; round < 40; ++round) {
        const int n = 7;
        std::vector<std::vector<int>> between(static_cast<std::size_t>(n), std::vector<int>(static_cast<std::size_t>(n), -1));
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                between[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = 1 + static_cast<int>(rng() % 9);
            }
        }
        std::vector<int> start(static_cast<std::size_t>(n));
        std::vector<double> values(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            start[static_cast<std::size_t>(i)] = 1 + static_cast<int>(rng() % 6);
            values[static_cast<std::size_t>(i)] = static_cast<double>(1 + rng() % 3);
        }
        const int budget = 18;

        int bestBrute = 0;
        std::vector<int> perm(static_cast<std::size_t>(n));
        std::iota(perm.begin(), perm.end(), 0);
        do {
            int spent = 0;
            int value = 0;
            int prev = -1;
            for (int idx : perm) {
                const int step = prev < 0 ? start[static_cast<std::size_t>(idx)]
                                          : between[static_cast<std::size_t>(prev)][static_cast<std::size_t>(idx)];
                if (step < 0 || spent + step > budget) break;
                spent += step;
                value += static_cast<int>(values[static_cast<std::size_t>(idx)]);
                prev = idx;
            }
            bestBrute = std::max(bestBrute, value);
        } while (std::next_permutation(perm.begin(), perm.end()));

        const rmtc2::Tour tour = rmtc2::solve_tour(start, between, values, budget);
        check(tour.value == bestBrute, "tour value equals brute force optimum");
        check(tour.length <= budget, "tour fits budget");
        if (g_failures > 0) break;
    }
}

void test_bot_smoke() {
    std::printf("test_bot_smoke\n");
    rmtc2::Config cfg;
    cfg.stage = 1;
    cfg.maxTicks = 100;
    rmtc2::Bot bot;
    bot.configure(cfg);

    rmtc2::Perception p;
    p.tick = 0;
    p.session = "s";
    p.mapW = 12;
    p.mapH = 12;
    p.vision = 4;
    p.self = {1, 1};
    p.hp = 100;
    p.alive = true;
    p.positionInitial = true;
    p.team = "A";
    rmtc2::Entity res;
    res.type = rmtc2::EntityType::ResHigh;
    res.id = "h1";
    res.pos = {4, 1};
    res.value = 20;
    p.entities.push_back(res);

    const rmtc2::Decision d = bot.decide(p);
    check(d.action.kind == rmtc2::Act::Move, "stage1 moves toward resource");
    check(d.action.dx == 1 && d.action.dy == 0, "stage1 moves right");
    check(d.reason == "stage1_tour", "stage1 reason");
}


void test_v3_regressions() {
    using namespace rmtc2;
    std::printf("test_v3_regressions\n");
    FrameGate gate;
    check(gate.accept("old",450)==FrameGate::Result::NewSession,"initial match");
    check(gate.accept("new",0)==FrameGate::Result::NewSession,"new session tick zero accepted");
    check(gate.accept("old",451)==FrameGate::Result::Reject,"delayed retired session rejected");
    check(gate.accept("new",0)==FrameGate::Result::Duplicate,"only same session can replay");
    check(gate.accept("new",1)==FrameGate::Result::NewTick,"advance new match");
    check(gate.accept("new",0)==FrameGate::Result::Reject,"stale tick rejected");
    Plan plan; plan.valid=true; plan.session="s";
    Hazard a; a.a={2,2};a.b={2,2};a.start=10;a.end=20;a.damage=10;
    Hazard b=a;b.start=30;b.end=40;b.lethal=true;
    plan.hazards={a,b};
    HazardField hz;hz.reset(5,5);hz.apply(plan);
    check(hz.damageAt({2,2},10)==10,"hazard inclusive start");
    check(hz.damageAt({2,2},20)==0,"hazard exclusive end");
    check(hz.damageAt({2,2},25)==0,"separate hazards preserve safe gap");
    check(!hz.lethalAt({2,2},15),"later lethal flag not active early");
    check(hz.lethalAt({2,2},30),"later lethal flag activates");
    b.start=15;b.end=18;b.damage=30;plan.hazards={a,b};hz.apply(plan);
    check(hz.damageAt({2,2},16)==40,"distinct concurrent hazard estimates add");
    check(hz.damageAt({2,2},18)==10,"overlap ends independently");
    auto batch=resolve_damage_batch(20,{{"A",20,true},{"B",20,true}});
    check_near(batch.awards.at("A").attack_credit,10,1e-9,"overkill actual proportional damage");
    check_near(batch.awards.at("A").kill_credit,30,1e-9,"shared kill award");
    auto dedup=resolve_damage_batch(10,{{"A",10,true},{"A",10,false},{"B",10,true}});
    check_near(dedup.awards.at("A").kill_credit,30,1e-9,"kill contributors deduplicate teams");
    check_near(forecast_adjusted_damage(100,20,30,1),90,1e-9,"award before death deduction");
    check_near(forecast_adjusted_damage(0,20,0,1),0,1e-9,"death floor zero");
    check_near(combat_points(forecast_adjusted_damage(550,0,0,1)),14.7,1e-9,"excess credit cushions later death");
    Plan bad;
    check(!parse_plan("{}","s",0,500,10,10,&bad),"arbitrary empty object not a plan");
    check(!parse_plan("{\"hazards\":[],\"effects\":3}","s",0,500,10,10,&bad),"wrong plan types rejected");
    Bot bot; bot.configure(Config{2,500});
    Perception obs;obs.session="s";obs.tick=10;obs.mapW=10;obs.mapH=10;obs.self={4,4};obs.team="A";
    obs.hp=20;obs.alive=true;obs.effectiveDamage=100;
    Entity enemy;enemy.type=EntityType::Robot;enemy.team="B";enemy.hp=100;enemy.pos={5,4};
    obs.entities={enemy};
    check(bot.decide(obs).action.kind==Act::Defend,"defend survives lethal retaliation before movement");
    obs.hp=100;
    check(bot.decide(obs).action.kind==Act::Attack,"nonlethal retaliation does not suppress all attacks");
    obs.protectedSpawn=true;
    check(bot.decide(obs).action.kind!=Act::Attack,"protected bot plans a legal nonattack action");
    obs.protectedSpawn=false;
    obs.hp=20;obs.effectiveDamage=0;obs.entities[0].hp=20;
    Mission damageMission;damageMission.id="damage";damageMission.type="effective_damage";damageMission.target=20;
    obs.missions={damageMission};
    const auto trade=bot.decide(obs);
    check(trade.action.kind==Act::Attack && trade.value<1.0,"fatal trade gets no survival-gated mission reward");
    obs.hp=100;obs.effectiveDamage=0;obs.missions.clear();obs.entities[0].hp=10;
    Entity rival=enemy;rival.team="C";rival.pos={5,5};obs.entities.push_back(rival);
    Bot sharedBot;sharedBot.configure(Config{2,500});
    const auto sharedShot=sharedBot.decide(obs);
    check_near(sharedShot.value,1.25,1e-9,"attack uses shared actual contribution and kill award");
    Perception malformed;
    check(!parse_perception(R"({"tick":1,"session_id":"s","map":{"width":10,"height":10},"self":{"position":[1,1],"hp":100,"alive":"wrong"}})",&malformed,nullptr),"wrong bool type returns failure without throwing");
    obs.entities.clear();obs.hp=100;
    Hazard danger;danger.a=obs.self;danger.b=obs.self;danger.start=10;danger.end=20;danger.lethal=true;
    Plan escape;escape.session="s";escape.valid=true;escape.hazards={danger};bot.set_plan(escape);
    check(bot.decide(obs).action.kind==Act::Move,"escape from hazardous BFS origin");
}

void test_zero_vision_exploration() {
    using namespace rmtc2;
    for (int stage : {1, 2}) {
        Bot bot; bot.configure(Config{stage, 100});
        Perception p; p.session="zero"; p.team="A"; p.mapW=8; p.mapH=8;
        p.vision=0; p.self={0,0}; p.hp=100; p.alive=true;
        std::set<std::pair<int,int>> visited;
        bool legal=true;
        for (p.tick=0; p.tick<100; ++p.tick) {
            visited.insert({p.self.x,p.self.y});
            const auto a=bot.decide(p).action;
            if (a.kind==Act::Move) {
                p.self.x+=a.dx; p.self.y+=a.dy;
                legal=legal && p.self.x>=0 && p.self.x<8 && p.self.y>=0 && p.self.y<8;
                if (!legal) break;
            }
        }
        check(legal, "zero vision probes stay inside map");
        check(visited.size()==64, "zero vision explores entire open map without getting stuck");
        check(bot.decide(p).action.kind==Act::Wait, "finished exploration waits");
        p.session="single"; p.tick=0; p.mapW=1; p.mapH=1; p.self={0,0};
        check(bot.decide(p).action.kind==Act::Wait, "single-cell map cannot probe out of bounds");
    }
}

}  // namespace

int main() {
    test_zero_vision_exploration();
    test_v3_regressions();
    test_parse();
    test_action_json();
    test_plan_parse();
    test_tour_optimal();
    test_bot_smoke();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
