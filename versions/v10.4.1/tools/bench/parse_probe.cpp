// tools/bench/parse_probe.cpp —— 协议/计划解析的敌意输入探针（只读，不参与比赛）
//
// 目的：离线竞技场把 Perception 结构体直接喂给决策核心，**从不经过 JSON 解析**，
// 所以 2000 场对局无法证明"线上裁判发来畸形/极端报文时程序不会崩、不会卡"。
// 本探针直接调用 rmtc9::parse_perception / parse_plan，检查：
//   1. 不抛异常（noexcept 语义）；
//   2. 不崩溃、不无限循环（单次耗时上限）；
//   3. 返回 true 时 Perception::valid() 必须成立（内部一致性）；
//   4. 明确记录每个用例的结论，便于逐条复核。
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "rmtc9/parse.hpp"

namespace {

std::string grid9(int mx = 4, int my = 4, char self = '1', bool twin = false) {
    std::string g;
    for (int y = 0; y < 9; ++y) {
        for (int x = 0; x < 9; ++x) {
            char c = '0';
            if (x == 4 && y == 4) c = self;
            if (twin && x == mx && y == my) c = '2';
            g.push_back(c);
        }
        if (y != 8) g.push_back('\n');
    }
    return g;
}

std::string base() {
    return std::string("{\"tick\":7,\"session_id\":\"s1\",\"vision_radius\":4,") +
           "\"map\":{\"width\":12,\"height\":12}," +
           "\"self\":{\"team\":\"A\",\"position\":[5,5],\"hp\":100,\"alive\":true,\"position_reason\":\"\"}," +
           "\"visible_entities\":[{\"type\":\"robot\",\"id\":\"B\",\"team\":\"B\",\"hp\":20," +
           "\"protected\":false,\"defending\":false,\"position\":\"" + grid9(5, 4) + "\"}]," +
           "\"score\":{\"resource_value\":0,\"effective_damage\":0},\"missions\":[]," +
           "\"mission_text\":\"\",\"event_text\":\"\"}";
}

const std::string kPlan =
    "```json\n{\"events\":[{\"id\":\"e1\",\"kind\":\"inversion\",\"a\":[5,5],\"b\":[5,5],"
    "\"start\":0,\"end\":10,\"duration\":10}]}\n```";

int failures = 0;
double worstUs = 0;
std::string worstName;

void check(const std::string& name, bool crashFree, bool consistent, double us) {
    if (us > worstUs) { worstUs = us; worstName = name; }
    if (!crashFree || !consistent || us > 50000.0) {
        ++failures;
        std::printf("  [FAIL] %-42s %.1f us  crashFree=%d consistent=%d\n", name.c_str(), us,
                    crashFree ? 1 : 0, consistent ? 1 : 0);
    } else {
        std::printf("  [ok]   %-42s %8.1f us\n", name.c_str(), us);
    }
}

void probePerception(const std::string& name, const std::string& text) {
    rmtc9::Perception p;
    std::string err;
    const auto t0 = std::chrono::steady_clock::now();
    bool ok = false;
    bool crashFree = true;
    try {
        ok = rmtc9::parse_perception(text, &p, &err);
    } catch (...) {
        crashFree = false;
    }
    const double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    // parse 成功就必须给出一致结构（valid() 同时校验 session/tick/map/self 范围）
    const bool consistent = !ok || p.valid();
    check(name, crashFree, consistent, us);
}

void probePlan(const std::string& name, const std::string& text) {
    rmtc9::Plan plan;
    const auto t0 = std::chrono::steady_clock::now();
    bool crashFree = true;
    bool consistent = true;
    try {
        rmtc9::parse_plan(text, "s1", 7, 500, 12, 12, &plan);
        if (plan.valid) {
            for (const auto& h : plan.hazards) {
                if (h.end < h.start) consistent = false;
                if (h.a.x < 0 || h.a.y < 0 || h.b.x < 0 || h.b.y < 0) consistent = false;
            }
        }
    } catch (...) {
        crashFree = false;
    }
    const double us =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    check(name, crashFree, consistent, us);
}

}  // namespace

int main() {
    std::printf("== parse_perception: 合法基线 ==\n");
    probePerception("baseline_valid", base());

    std::printf("== parse_perception: 结构缺字段 ==\n");
    probePerception("empty_string", "");
    probePerception("whitespace", "   \n\t ");
    probePerception("null_literal", "null");
    probePerception("array_literal", "[]");
    probePerception("number_literal", "0");
    probePerception("no_session", R"({"tick":1,"map":{"width":12,"height":12},"self":{"position":[1,1],"hp":1}})");
    probePerception("no_map", R"({"tick":1,"session_id":"s","self":{"position":[1,1]}})");
    probePerception("no_self", R"({"tick":1,"session_id":"s","map":{"width":12,"height":12}})");
    probePerception("bom_prefix", "\xEF\xBB\xBF" + base());
    probePerception("trailing_garbage", base() + " trailing{}");
    probePerception("dup_keys", R"({"tick":1,"tick":2,"session_id":"s","map":{"width":12,"height":12},"self":{"position":[1,1]}})");

    std::printf("== parse_perception: 数值/坐标越界 ==\n");
    std::string m = base();
    probePerception("map_zero", [&] { std::string s = m; s.replace(s.find("\"width\":12"), 10, "\"width\":0"); return s; }());
    probePerception("map_negative", [&] { std::string s = m; s.replace(s.find("\"width\":12"), 10, "\"width\":-5"); return s; }());
    probePerception("map_1e9", [&] { std::string s = m; s.replace(s.find("\"width\":12"), 10, "\"width\":1000000000"); return s; }());
    probePerception("tick_negative", [&] { std::string s = m; s.replace(s.find("\"tick\":7"), 8, "\"tick\":-1"); return s; }());
    probePerception("tick_1e18", [&] { std::string s = m; s.replace(s.find("\"tick\":7"), 8, "\"tick\":1000000000000000000"); return s; }());
    probePerception("self_out_of_map", [&] { std::string s = m; s.replace(s.find("[5,5]"), 5, "[99,99]"); return s; }());
    probePerception("self_wrong_arity", [&] { std::string s = m; s.replace(s.find("[5,5]"), 5, "[5]"); return s; }());
    probePerception("self_three", [&] { std::string s = m; s.replace(s.find("[5,5]"), 5, "[5,5,5]"); return s; }());
    probePerception("self_float", [&] { std::string s = m; s.replace(s.find("[5,5]"), 5, "[5.7,5.2]"); return s; }());
    probePerception("hp_negative", [&] { std::string s = m; s.replace(s.find("\"hp\":100"), 8, "\"hp\":-30"); return s; }());
    probePerception("hp_huge", [&] { std::string s = m; s.replace(s.find("\"hp\":100"), 8, "\"hp\":2147483647"); return s; }());

    std::printf("== parse_perception: 9x9 相对位置图畸形 ==\n");
    auto withGrid = [&](const std::string& g) {
        std::string s = base();
        const auto at = s.find("\"position\":\"");
        const auto from = at + 12;
        const auto to = s.find('"', from);
        s.replace(from, to - from, g);
        return s;
    };
    probePerception("grid_no_self_marker", withGrid([] { std::string g; for (int y = 0; y < 9; ++y) { g += std::string(9, '0'); if (y != 8) g += '\n'; } return g; }()));
    probePerception("grid_two_self_markers", withGrid([] { std::string g = grid9(2, 2, '1', true); g[0] = '1'; return g; }()));
    probePerception("grid_short_80", withGrid(grid9().substr(0, 80)));
    probePerception("grid_long_200", withGrid(grid9() + std::string(111, '0')));
    probePerception("grid_no_newlines", withGrid([] { std::string g; for (int i = 0; i < 81; ++i) g += (i == 40 ? '1' : '0'); return g; }()));
    probePerception("grid_crlf", withGrid([] { std::string g; for (int y = 0; y < 9; ++y) { for (int x = 0; x < 9; ++x) g += ((x == 4 && y == 4) ? '1' : '0'); g += "\r\n"; } return g; }()));
    probePerception("grid_unknown_chars", withGrid([] { std::string g; for (int i = 0; i < 81; ++i) g += "abz#"[i % 4]; return g; }()));
    probePerception("grid_utf8_digits", withGrid("１２３４５６７８９\n" + grid9().substr(10)));

    std::printf("== parse_perception: 体积/嵌套 ==\n");
    {
        std::string big = base();
        const auto at = big.find("\"event_text\":\"\"");
        big.replace(at, 17, "\"event_text\":\"" + std::string(200000, 'x') + "\"");
        probePerception("event_text_200KB", big);
    }
    {
        std::string s = base();
        const auto at = s.find("\"visible_entities\":[");
        s.insert(at + 20, std::string(2000, '{'));
        probePerception("nested_braces_2000", s);
    }
    {
        std::string s = base();
        const auto at = s.find("[]");
        std::string arr = "[";
        for (int i = 0; i < 20000; ++i) { if (i) arr += ','; arr += "{\"type\":\"robot\",\"id\":\"" + std::to_string(i) + "\",\"team\":\"B\",\"hp\":1,\"position\":\"" + grid9() + "\"}"; }
        arr += "]";
        s.replace(at, 2, arr);
        probePerception("entities_20000", s);
    }
    {
        std::string s(500000, '{');
        probePerception("unclosed_braces_500K", s);
    }

    std::printf("== parse_plan: 合法与脏输出 ==\n");
    probePlan("plan_clean_fenced", kPlan);
    probePlan("plan_empty", "");
    probePlan("plan_plain_text", "抱歉，我无法回答。");
    probePlan("plan_unclosed", "{\"events\":[{\"id\":\"e\",\"kind\":\"inversion\",\"a\":[1,1],\"b\":[1,1],\"start\":0,\"end\":5}");
    probePlan("plan_nested_5000", std::string(5000, '[') + std::string(5000, ']'));
    probePlan("plan_braces_500K", std::string(500000, '{'));
    probePlan("plan_start_gt_end", R"({"events":[{"id":"e","kind":"explosion","a":[1,1],"b":[3,3],"start":50,"end":10}]})");
    probePlan("plan_reverse_rect", R"({"events":[{"id":"e","kind":"explosion","a":[9,9],"b":[2,2],"start":1,"end":5}]})");
    probePlan("plan_out_of_map", R"({"events":[{"id":"e","kind":"explosion","a":[-5,-5],"b":[999,999],"start":1,"end":5}]})");
    probePlan("plan_dup_ids", R"({"events":[{"id":"e","kind":"explosion","a":[1,1],"b":[2,2],"start":1,"end":5},{"id":"e","kind":"split","a":[3,3],"b":[3,3],"start":1,"end":5}]})");
    probePlan("plan_nan", R"({"events":[{"id":"e","kind":"explosion","a":[1,1],"b":[2,2],"start":NaN,"end":5}]})");
    probePlan("plan_top_array", R"([{"id":"e","kind":"explosion","a":[1,1],"b":[2,2],"start":1,"end":5}])");
    {
        std::string arr = "{\"events\":[";
        for (int i = 0; i < 5000; ++i) { if (i) arr += ','; arr += "{\"id\":\"e" + std::to_string(i) + "\",\"kind\":\"explosion\",\"a\":[1,1],\"b\":[2,2],\"start\":1,\"end\":5}"; }
        arr += "]}";
        probePlan("plan_events_5000", arr);
    }

    std::printf("\n最慢用例：%s %.1f us\n", worstName.c_str(), worstUs);
    std::printf("失败用例：%d\n", failures);
    return failures == 0 ? 0 : 1;
}
