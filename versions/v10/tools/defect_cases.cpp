// tools/defect_cases.cpp —— 同一输入下 7.0 与 9.0 的实际输出对照（历史8.0说明/7.0检查报告.md 的证据）
//
// 编译：见 scripts/defect_cases.sh。7.0 用 baseline_v7（与校内7.0 源码逐字相同）。
#include <cstdio>
#include <string>
#include <vector>

#include "rmtc7/bot.hpp"
#include "rmtc7/parse.hpp"
#include "rmtc9/bot.hpp"
#include "rmtc9/parse.hpp"

namespace {

struct Case { const char* name; const char* json; };

template <class PlanT, class Fn>
void plan_row(const char* name, const char* json, Fn parse7, Fn parse8) {
    (void)sizeof(PlanT);
    std::printf("%-46s 7.0=%s  9.0=%s\n", name, parse7(json).c_str(), parse8(json).c_str());
}

std::string p7(const char* json) {
    rmtc7::Plan p;
    if (!rmtc7::parse_plan(json, "s", 0, 500, 24, 24, &p)) return "整份拒收";
    return "接受(" + std::to_string(p.events.size()) + "事件," + std::to_string(p.hazards.size()) + "危险区)";
}
std::string p8(const char* json) {
    rmtc9::Plan p;
    if (!rmtc9::parse_plan(json, "s", 0, 500, 24, 24, &p)) return "整份拒收";
    return "接受(" + std::to_string(p.events.size()) + "事件," + std::to_string(p.hazards.size()) + "危险区)";
}

template <class B, class P, class E, class A>
int defend_ticks(int ticks) {
    B bot;
    bot.configure({2, 500});
    int defend = 0;
    for (int t = 0; t < ticks; ++t) {
        P p;
        p.session = "s"; p.team = "A"; p.tick = t; p.mapW = p.mapH = 24; p.vision = 4;
        p.self = {19, 7}; p.hp = 20; p.alive = true; p.lastActionTick = t - 1;
        p.effectiveDamage = 160;  // 已有伤害：此时死亡会真扣 60，7.0 才会选择防御
        // 复现离线对局里的真实场面：A 同列相隔 2 格、D 在斜对角，两者都不开火
        E a, d;
        a.type = decltype(a.type)::Robot; a.team = "B"; a.id = "robot_B"; a.pos = {19, 5}; a.hp = 100;
        d.type = decltype(d.type)::Robot; d.team = "D"; d.id = "robot_D"; d.pos = {18, 4}; d.hp = 100;
        p.entities = {a, d};
        if (bot.decide(p).action.kind == A::Defend) ++defend;
    }
    return defend;
}

}  // namespace

int main() {
    std::printf("== 1. 模型输出的常见毛病：7.0 只要一处不合规就整份作废\n");
    const Case cases[] = {
        {"同时写了 events 和 hazards（同一公告写两遍）",
         R"({"events":[{"id":"e1","kind":"explosion","a":[3,3],"b":[7,7],"start":40,"end":41,"damage":80}],"hazards":[{"shape":"rect","a":[3,3],"b":[7,7],"start":40,"end":41,"damage":80}]})"},
        {"多写了一条 kind 不认识的条目",
         R"({"events":[{"id":"s","kind":"split","a":[4,4],"b":[4,4],"start":30,"end":60},{"id":"x","kind":"announcement","a":[0,0],"b":[0,0],"start":1,"end":2}]})"},
        {"两个辣椒点用了同一个 id",
         R"({"events":[{"id":"c","kind":"chili","a":[2,2],"b":[2,2],"start":40,"end":70,"damage":50},{"id":"c","kind":"chili","a":[9,9],"b":[9,9],"start":40,"end":70,"damage":50}]})"},
        {"用了规则原文名字 pepper 而不是 chili",
         R"({"events":[{"id":"p","kind":"pepper","a":[2,2],"b":[2,2],"start":40,"end":70,"damage":50}]})"},
        {"堡垒只给了中心点、漏写 dwell",
         R"({"events":[{"id":"f","kind":"fortress","a":[12,12],"b":[12,12],"start":100,"end":150,"damage":50,"defend_damage":10}]})"},
        {"正常、干净的输出（对照）",
         R"({"events":[{"id":"e1","kind":"explosion","a":[3,3],"b":[7,7],"start":40,"end":41,"damage":80}]})"},
    };
    for (const auto& c : cases) std::printf("  %-44s 7.0=%-22s 9.0=%s\n", c.name, p7(c.json).c_str(), p8(c.json).c_str());

    std::printf("\n== 2. 原地防御死锁：20 HP、D=160；B 同列相隔 2 格、D 在斜对角，两者都从不开火（例如只打相邻目标的程序）\n");
    const int n = 200;
    const int d7 = defend_ticks<rmtc7::Bot, rmtc7::Perception, rmtc7::Entity, rmtc7::Act>(n);
    const int d8 = defend_ticks<rmtc9::Bot, rmtc9::Perception, rmtc9::Entity, rmtc9::Act>(n);
    std::printf("  %d Tick 里选择 DEFEND 的次数：7.0=%d  9.0=%d\n", n, d7, d8);
    return 0;
}
