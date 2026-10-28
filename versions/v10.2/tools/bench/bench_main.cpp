// tools/bench/bench_main.cpp —— 8.0 测评工具（离线，不连 ROS、不调模型）
//
// 两种比较方式：
//   --mode seat（默认，“性能”比较）：同一张地图、同一席位、同一批对手，
//       候选 A 与候选 B 各打一局，比较各自得分。A、B 从不同场，比的是“谁更能打”。
//   --mode duel：A、B 同场（旧 7.0 报告的做法），其余席位为对手。
// 第一阶段（--stage 1）：每张图 A、B 各自独立采集。
//
// 事件：--events none|standard|practice；oracle 在公告后 --latency Tick 交付结构化计划，
// --noise 0..3 模拟模型输出的常见毛病（见 referee.hpp）。
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <stdexcept>
#include <thread>

#include <nlohmann/json.hpp>

#include "opponent_adapter.hpp"
#include "referee.hpp"
#include "rmtc9/bot.hpp"
#include "rmtc9/parse.hpp"

using Json = nlohmann::json;
namespace {

class V9Policy final : public IPolicy {
public:
    V9Policy(int stage, int ticks) { bot_.configure({stage, ticks}); }
    rmtc9::Action act(const rmtc9::Perception& p) override { return bot_.decide(p).action; }
    bool plan(const std::string& content, const std::string& session, int tick, int maxTicks, int w, int h) override {
        rmtc9::Plan plan;
        if (!rmtc9::parse_plan(content, session, tick, maxTicks, w, h, &plan)) return false;
        bot_.set_plan(plan);
        return true;
    }
    const char* name() const override { return "v9"; }

private:
    rmtc9::Bot bot_;
};

// 合法贪心：看见就打相邻敌人，否则去最近资源或前沿（弱基线）
class GreedyBot final : public IPolicy {
public:
    rmtc9::Action act(const rmtc9::Perception& p) override {
        mem_.update(p);
        mask_.reset(mem_.width(), mem_.height());
        for (const auto& w : mem_.walls()) mask_.set(w, true);
        for (const auto& e : p.entities)
            if (e.type == rmtc9::EntityType::Robot && e.hp > 0) mask_.set(e.pos, true);
        field_ = rmtc9::bfs(mask_, p.self, 600);
        if (!p.alive) return rmtc9::Action::wait();
        for (int dir = 0; !p.protectedSpawn && dir < 4; ++dir)
            for (const auto& e : p.entities)
                if (e.type == rmtc9::EntityType::Robot && e.hp > 0 && !e.protectedSpawn &&
                    e.pos.x - p.self.x == rmtc9::kDirX[dir] && e.pos.y - p.self.y == rmtc9::kDirY[dir])
                    return rmtc9::Action::attack(rmtc9::kDirX[dir], rmtc9::kDirY[dir]);
        int bestDist = 1 << 20;
        rmtc9::Point goal{0, 0};
        bool found = false;
        for (const auto& kv : mem_.resources()) {
            const int d = field_.at(kv.second.pos);
            if (d > 0 && d < bestDist) { bestDist = d; goal = kv.second.pos; found = true; }
        }
        if (!found)
            for (const auto& c : mem_.frontiers()) {
                const int d = field_.at(c);
                if (d > 0 && d < bestDist) { bestDist = d; goal = c; found = true; }
            }
        if (!found) return rmtc9::Action::wait();
        const int8_t dir = field_.firstAt(goal);
        if (dir < 0) return rmtc9::Action::wait();
        return rmtc9::Action::move(rmtc9::kDirX[dir], rmtc9::kDirY[dir]);
    }
    const char* name() const override { return "greedy"; }

private:
    rmtc9::WorldMap mem_;
    rmtc9::BlockMask mask_;
    rmtc9::Field field_;
};

// 冲锋车：看见敌人就去对齐并开火（射程 1–2）；否则同贪心采集/探索。
// 只采集车：从不攻击。掉线车：一直 WAIT（程序崩溃或没接上）。
class StyleBot final : public IPolicy {
public:
    explicit StyleBot(char style) : style_(style) {}
    rmtc9::Action act(const rmtc9::Perception& p) override {
        if (style_ == 'w' || !p.alive) return rmtc9::Action::wait();
        mem_.update(p);
        mask_.reset(mem_.width(), mem_.height());
        for (const auto& w : mem_.walls()) mask_.set(w, true);
        for (const auto& e : p.entities)
            if (e.type == rmtc9::EntityType::Robot && e.hp > 0) mask_.set(e.pos, true);
        field_ = rmtc9::bfs(mask_, p.self, 600);
        if (style_ == 'r' && !p.protectedSpawn) {
            for (int dir = 0; dir < 4; ++dir)
                for (int step = 1; step <= 2; ++step)
                    for (const auto& e : p.entities)
                        if (e.type == rmtc9::EntityType::Robot && e.hp > 0 && !e.protectedSpawn &&
                            e.pos.x == p.self.x + rmtc9::kDirX[dir] * step && e.pos.y == p.self.y + rmtc9::kDirY[dir] * step)
                            return rmtc9::Action::attack(rmtc9::kDirX[dir], rmtc9::kDirY[dir]);
            int best = 1 << 20;
            rmtc9::Point goal{};
            for (const auto& e : p.entities) {
                if (e.type != rmtc9::EntityType::Robot || e.hp <= 0) continue;
                for (int dir = 0; dir < 4; ++dir) {
                    const rmtc9::Point c{e.pos.x + rmtc9::kDirX[dir] * 2, e.pos.y + rmtc9::kDirY[dir] * 2};
                    const int d = field_.at(c);
                    if (d > 0 && d < best) { best = d; goal = c; }
                }
            }
            if (best < (1 << 20)) {
                const int8_t dir = field_.firstAt(goal);
                if (dir >= 0) return rmtc9::Action::move(rmtc9::kDirX[dir], rmtc9::kDirY[dir]);
            }
        }
        int bestDist = 1 << 20;
        rmtc9::Point goal{0, 0};
        bool found = false;
        for (const auto& kv : mem_.resources()) {
            const int d = field_.at(kv.second.pos);
            if (d > 0 && d < bestDist) { bestDist = d; goal = kv.second.pos; found = true; }
        }
        if (!found)
            for (const auto& c : mem_.frontiers()) {
                const int d = field_.at(c);
                if (d > 0 && d < bestDist) { bestDist = d; goal = c; found = true; }
            }
        if (!found) return rmtc9::Action::wait();
        const int8_t dir = field_.firstAt(goal);
        if (dir < 0) return rmtc9::Action::wait();
        return rmtc9::Action::move(rmtc9::kDirX[dir], rmtc9::kDirY[dir]);
    }
    const char* name() const override { return "style"; }

private:
    char style_;
    rmtc9::WorldMap mem_;
    rmtc9::BlockMask mask_;
    rmtc9::Field field_;
};

std::unique_ptr<IPolicy> make(char version, int stage, int ticks) {
    if (version == 'r' || version == 'c' || version == 'w') return std::make_unique<StyleBot>(version);
    if (version == '9') return std::make_unique<V9Policy>(stage, ticks);
    if (version == '0') return make_v100_bot(stage, ticks);
    if (version == '2') return make_v101_bot(stage, ticks);
    if (version == '3') return make_v93_bot(stage, ticks);
    if (version == '1') return make_v91_bot(stage, ticks);
    if (version == '8') return make_v8_bot(stage, ticks);
    if (version == '7') return make_v7_bot(stage, ticks);
    if (version == 'g') return std::make_unique<GreedyBot>();
    throw std::runtime_error("policy must be 9, 8, 7, g, r, c or w");
}

struct Options {
    std::string mode = "seat";
    int stage = 2, maps = 50, robots = 4, w = 24, h = 24, vision = 4, threads = 0, latency = 2, noise = 0;
    unsigned seed = 83000;
    char a = '9', b = '2';
    std::string field = "888";  // seat 模式：其余席位的版本，按顺序填入
    std::string events = "none";
    std::string practice;       // 官方练习包配置（覆盖随机地图）
    std::string output;
};

char policy_argument(const std::string& v) {
    if (v=="10.2") return '9';
    if (v=="10.1") return '2';
    if (v=="10.0") return '0';
    if (v=="9.3") return '3';
    if (v=="9.1") return '1';
    if (v.size()==1 && std::string("9023187grcw").find(v[0])!=std::string::npos) return v[0];
    throw std::runtime_error("unknown policy version; use 10.2, 10.1, 10.0, 9.3, 9.1, 8 or 7");
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        if (i + 1 >= argc) throw std::runtime_error("missing value for " + key);
        const std::string v = argv[++i];
        if (key == "--mode") o.mode = v;
        else if (key == "--stage") o.stage = std::stoi(v);
        else if (key == "--matches" || key == "--maps") o.maps = std::stoi(v);
        else if (key == "--robots") o.robots = std::stoi(v);
        else if (key == "--width") o.w = std::stoi(v);
        else if (key == "--height") o.h = std::stoi(v);
        else if (key == "--vision") o.vision = std::stoi(v);
        else if (key == "--threads") o.threads = std::stoi(v);
        else if (key == "--latency") o.latency = std::stoi(v);
        else if (key == "--noise") o.noise = std::stoi(v);
        else if (key == "--seed") o.seed = static_cast<unsigned>(std::stoul(v));
        else if (key == "--a") o.a = policy_argument(v);
        else if (key == "--b") o.b = policy_argument(v);
        else if (key == "--field" || key == "--others") o.field = v;
        else if (key == "--events") o.events = v;
        else if (key == "--practice") o.practice = v;
        else if (key == "--output") o.output = v;
        else throw std::runtime_error("unknown argument: " + key);
    }
    if (o.mode != "seat" && o.mode != "duel") throw std::runtime_error("mode must be seat or duel");
    if ((o.stage != 1 && o.stage != 2) || (o.robots != 2 && o.robots != 4) || o.maps < 1 || o.maps > 5000 ||
        o.w < 8 || o.w > 32 || o.h < 8 || o.h > 32 || o.vision < 0 || o.vision > 4 || o.latency < 0 || o.noise < 0 || o.noise > 3)
        throw std::runtime_error("argument out of bounds");
    if (o.events != "none" && o.events != "standard" && o.events != "practice") throw std::runtime_error("events must be none|standard|practice");
    if (o.field.size() == 1) o.field = std::string(3, o.field[0]);
    for (char c : std::string{o.a, o.b} + o.field) make(c, 2, 500);
    return o;
}

struct Sample {
    double score = 0, resource = 0, damage = 0, attack = 0, killCredit = 0;
    int missions = 0, deaths = 0, kills = 0, invalid = 0, eventDeaths = 0, plansOk = 0, plansBad = 0;
    int missNormal = 0, missHigh = 0, missReach = 0, rank = 0;
    int waits=0,moves=0,attacks=0,defends=0,blockedMoves=0,maxLiveWait=0;
};
Json encode(const Sample& s) {
    return {{"score", s.score}, {"resource", s.resource}, {"damage", s.damage}, {"attack_damage", s.attack},
            {"kill_credit", s.killCredit}, {"missions", s.missions}, {"deaths", s.deaths}, {"kills", s.kills},
            {"invalid", s.invalid}, {"event_deaths", s.eventDeaths}, {"plans_ok", s.plansOk}, {"plans_rejected", s.plansBad},
            {"mission_normal", s.missNormal}, {"mission_high", s.missHigh}, {"mission_reach", s.missReach},
            {"damage_capped", s.damage >= 500 ? 1 : 0}, {"rank", s.rank},
            {"waits",s.waits},{"moves",s.moves},{"attacks",s.attacks},{"defends",s.defends},
            {"blocked_moves",s.blockedMoves},{"max_live_wait_streak",s.maxLiveWait}};
}

double mean(const std::vector<double>& a) { return a.empty() ? 0 : std::accumulate(a.begin(), a.end(), 0.0) / a.size(); }
double quantile(std::vector<double> a, double q) {
    if (a.empty()) return 0;
    std::sort(a.begin(), a.end());
    return a[static_cast<std::size_t>(q * (a.size() - 1))];
}

// 跑一局，返回每个席位的结果
std::vector<Sample> play(const bench::MapDef& map, int ticks, const std::vector<char>& versions, const Options& o,
                         std::vector<double>* times, int timedSeat) {
    bench::Referee ref;
    ref.reset(map, ticks);
    const int stage = map.stage;
    std::vector<std::unique_ptr<IPolicy>> bots;
    for (char v : versions) bots.push_back(make(v, stage, ticks));
    std::vector<Sample> out(versions.size());
    std::vector<int> waitStreak(versions.size(),0);
    std::vector<bool> triedMove(versions.size(),false);
    for (int t = 0; t < ticks; ++t) {
        // oracle：公告后 latency Tick 交付（模拟异步模型返回）
        if (stage == 2 && !map.events.empty() && t >= o.latency && bench::has_announcement(map, t - o.latency)) {
            const std::string content = bench::oracle_plan(map, t - o.latency, o.noise);
            for (std::size_t i = 0; i < bots.size(); ++i) {
                if (bots[i]->plan(content, "bench-session", t - o.latency, ticks, map.w, map.h)) ++out[i].plansOk;
                else ++out[i].plansBad;
            }
        }
        for (int i = 0; i < ref.robot_count(); ++i) {
            const auto p = ref.perceive(i);
            const auto begin = std::chrono::steady_clock::now();
            const auto action = bots[static_cast<std::size_t>(i)]->act(p);
            auto& sample=out[static_cast<std::size_t>(i)];
            if(action.kind==rmtc9::Act::Wait)++sample.waits;
            else if(action.kind==rmtc9::Act::Move)++sample.moves;
            else if(action.kind==rmtc9::Act::Attack)++sample.attacks;
            else ++sample.defends;
            waitStreak[i]=(p.alive&&action.kind==rmtc9::Act::Wait)?waitStreak[i]+1:0;
            sample.maxLiveWait=std::max(sample.maxLiveWait,waitStreak[i]);
            triedMove[i]=p.alive&&action.kind==rmtc9::Act::Move;
            if (times && i == timedSeat)
                times->push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count());
            ref.submit(i, action);
        }
        ref.step();
        for(int i=0;i<ref.robot_count();++i)
            if(triedMove[i]&&!ref.robots()[i].lastMoved)++out[i].blockedMoves;
    }
    for (int i = 0; i < ref.robot_count(); ++i) {
        const auto& r = ref.robots()[static_cast<std::size_t>(i)];
        auto& s = out[static_cast<std::size_t>(i)];
        s.score = ref.score(i);
        s.resource = r.resourceValue;
        s.damage = r.damage;
        s.attack = r.attackDamage;
        s.killCredit = r.killCredit;
        s.missions = r.missionsDone;
        s.deaths = r.deaths;
        s.kills = r.kills;
        s.invalid = r.invalid;
        s.eventDeaths = r.eventDeaths;
        for (const auto& id : r.doneIds) {
            if (id == "collect_two_normal") s.missNormal = 1;
            else if (id == "collect_high_value") s.missHigh = 1;
            else if (id == "reach_target") s.missReach = 1;
        }
    }
    // 名次（1 = 本局最高分；同分按资源价值再按有效伤害，与规则同分比较顺序一致的前几项）
    for (std::size_t i = 0; i < out.size(); ++i) {
        int better = 0;
        for (std::size_t j = 0; j < out.size(); ++j) {
            if (i == j) continue;
            const auto key = [&](const Sample& s) { return std::make_tuple(s.score, s.missions, s.resource, s.damage); };
            if (key(out[j]) > key(out[i])) ++better;
        }
        out[i].rank = better + 1;
    }
    return out;
}

Json summarize(const std::vector<Sample>& ss, const std::vector<double>& times) {
    Json j;
    for (const char* key : {"score", "resource", "damage", "attack_damage", "kill_credit", "missions", "deaths", "kills", "invalid", "event_deaths", "plans_ok", "plans_rejected",
                            "mission_normal", "mission_high", "mission_reach", "damage_capped", "rank",
                            "waits","moves","attacks","defends","blocked_moves","max_live_wait_streak"}) {
        std::vector<double> values;
        for (const auto& s : ss) values.push_back(encode(s)[key].get<double>());
        j[key] = mean(values);
    }
    int full = 0;
    for (const auto& s : ss) if (s.missions >= 3) ++full;
    j["all_missions_rate"] = ss.empty() ? 0.0 : static_cast<double>(full) / ss.size();
    int top2 = 0, first = 0;
    for (const auto& s : ss) { if (s.rank <= 2) ++top2; if (s.rank == 1) ++first; }
    j["top2_rate"] = ss.empty() ? 0.0 : static_cast<double>(top2) / ss.size();
    j["first_rate"] = ss.empty() ? 0.0 : static_cast<double>(first) / ss.size();
    j["decision_us"] = {{"p50", quantile(times, 0.5)}, {"p99", quantile(times, 0.99)},
                        {"max", times.empty() ? 0 : *std::max_element(times.begin(), times.end())}};
    return j;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const Options o = parse(argc, argv);
        std::optional<bench::MapDef> imported;
        if(!o.practice.empty()) {
            bench::MapDef map;std::string error;
            if(!bench::load_practice_config(o.practice,o.stage==1?1:o.robots,&map,&error))throw std::runtime_error(error);
            if(map.stage!=o.stage)throw std::runtime_error("configuration stage differs from --stage");
            if(o.events=="none")map.events.clear();
            imported=std::move(map);
        }
        std::ofstream log;
        if (!o.output.empty()) {
            log.open(o.output, std::ios::binary);
            if (!log) throw std::runtime_error("cannot open output");
        }
        const int ticks = o.stage == 1 ? 100 : 500;
        struct MapResult { std::vector<Sample> a, b; std::vector<double> ta, tb; std::vector<Json> lines; };
        std::vector<MapResult> results(static_cast<std::size_t>(o.maps));
        std::atomic<int> next{0};
        const auto worker = [&]() {
            for (int m = next++; m < o.maps; m = next++) {
                bench::MapDef map;
                if (imported) {
                    map=*imported;
                } else {
                    map = bench::generate_map(o.w, o.h, o.stage == 1 ? 1 : o.robots, o.seed + static_cast<unsigned>(m), o.stage);
                    map.vision = o.vision;
                    if (o.stage == 2 && o.events != "none") map.events = bench::generate_events(map, o.seed + static_cast<unsigned>(m), o.events);
                }
                auto& R = results[static_cast<std::size_t>(m)];
                if (o.stage == 1) {
                    R.a.push_back(play(map, ticks, {o.a}, o, &R.ta, 0)[0]);
                    R.b.push_back(play(map, ticks, {o.b}, o, &R.tb, 0)[0]);
                    R.lines.push_back({{"map", m}, {"a", encode(R.a.back())}, {"b", encode(R.b.back())}});
                    continue;
                }
                const int n = o.robots;
                if (o.mode == "seat") {
                    for (int seat = 0; seat < n; ++seat) {
                        std::vector<char> vs(static_cast<std::size_t>(n));
                        int k = 0;
                        for (int i = 0; i < n; ++i) vs[static_cast<std::size_t>(i)] = i == seat ? '?' : o.field[static_cast<std::size_t>(k++ % o.field.size())];
                        vs[static_cast<std::size_t>(seat)] = o.a;
                        const Sample sa = play(map, ticks, vs, o, &R.ta, seat)[static_cast<std::size_t>(seat)];
                        vs[static_cast<std::size_t>(seat)] = o.b;
                        const Sample sb = play(map, ticks, vs, o, &R.tb, seat)[static_cast<std::size_t>(seat)];
                        R.a.push_back(sa);
                        R.b.push_back(sb);
                        R.lines.push_back({{"map", m}, {"seat", seat}, {"a", encode(sa)}, {"b", encode(sb)}, {"difference", sa.score - sb.score}});
                    }
                } else {
                    for (int sa = 0; sa < n; ++sa)
                        for (int sb = 0; sb < n; ++sb) {
                            if (sa == sb) continue;
                            std::vector<char> vs(static_cast<std::size_t>(n));
                            int k = 0;
                            for (int i = 0; i < n; ++i) vs[static_cast<std::size_t>(i)] = o.field[static_cast<std::size_t>(k++ % o.field.size())];
                            vs[static_cast<std::size_t>(sa)] = o.a;
                            vs[static_cast<std::size_t>(sb)] = o.b;
                            std::vector<double> tmp;
                            const auto r = play(map, ticks, vs, o, &tmp, sa);
                            R.ta.insert(R.ta.end(), tmp.begin(), tmp.end());
                            R.a.push_back(r[static_cast<std::size_t>(sa)]);
                            R.b.push_back(r[static_cast<std::size_t>(sb)]);
                            R.lines.push_back({{"map", m}, {"a_seat", sa}, {"b_seat", sb}, {"a", encode(R.a.back())}, {"b", encode(R.b.back())},
                                               {"difference", R.a.back().score - R.b.back().score}});
                            if (n == 2) break;
                        }
                }
            }
        };
        const int threads = o.threads > 0 ? o.threads : std::max(1u, std::thread::hardware_concurrency());
        std::vector<std::thread> pool;
        std::exception_ptr failure;
        std::mutex failMutex;
        for (int t = 0; t < threads; ++t)
            pool.emplace_back([&] {
                try { worker(); } catch (...) { std::lock_guard<std::mutex> lock(failMutex); failure = std::current_exception(); }
            });
        for (auto& t : pool) t.join();
        if (failure) std::rethrow_exception(failure);

        std::vector<Sample> as, bs;
        std::vector<double> ta, tb, mapDiff;
        int wins = 0, draws = 0, losses = 0;
        for (const auto& R : results) {
            double d = 0;
            for (std::size_t k = 0; k < R.a.size(); ++k) {
                const double diff = R.a[k].score - R.b[k].score;
                d += diff;
                if (diff > 1e-8) ++wins; else if (diff < -1e-8) ++losses; else ++draws;
            }
            mapDiff.push_back(R.a.empty() ? 0 : d / R.a.size());
            as.insert(as.end(), R.a.begin(), R.a.end());
            bs.insert(bs.end(), R.b.begin(), R.b.end());
            ta.insert(ta.end(), R.ta.begin(), R.ta.end());
            tb.insert(tb.end(), R.tb.begin(), R.tb.end());
            if (log) for (const auto& line : R.lines) log << line.dump() << '\n';
        }
        // 按地图整组自助抽样，不把同图的席位轮换当成独立样本
        std::mt19937 random(932001);
        std::uniform_int_distribution<std::size_t> pick(0, mapDiff.size() - 1);
        std::vector<double> boot;
        for (int s = 0; s < 2000; ++s) {
            double sum = 0;
            for (std::size_t i = 0; i < mapDiff.size(); ++i) sum += mapDiff[pick(random)];
            boot.push_back(sum / mapDiff.size());
        }
        long long invalid = 0;
        for (const auto& s : as) invalid += s.invalid;
        for (const auto& s : bs) invalid += s.invalid;
        const Json out = {{"mode", o.mode}, {"stage", o.stage}, {"seed", o.seed}, {"maps", o.maps}, {"games", as.size()},
                          {"robots", o.stage==1?1:o.robots}, {"width", imported?imported->w:o.w},
                          {"height", imported?imported->h:o.h}, {"vision", imported?imported->vision:o.vision}, {"field", o.field},
                          {"unique_map_count", imported?1:o.maps},
                          {"events", o.events}, {"noise", o.noise}, {"latency", o.latency}, {"practice", o.practice},
                          {"a_version", o.a=='9'?"10.2":o.a=='2'?"10.1":o.a=='0'?"10.0":o.a=='3'?"9.3":o.a=='1'?"9.1":std::string(1,o.a)}, {"b_version", o.b=='9'?"10.2":o.b=='2'?"10.1":o.b=='0'?"10.0":o.b=='3'?"9.3":o.b=='1'?"9.1":std::string(1,o.b)},
                          {"a", summarize(as, ta)}, {"b", summarize(bs, tb)}, {"wins", wins}, {"draws", draws}, {"losses", losses},
                          {"win_rate", as.empty() ? 0.0 : static_cast<double>(wins) / as.size()},
                          {"mean_difference", mean(mapDiff)}, {"map_bootstrap_95", {quantile(boot, .025), quantile(boot, .975)}},
                          {"ab_invalid_total", invalid}};
        std::cout << out.dump(2) << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "arena: " << e.what() << '\n';
        return 2;
    }
}
