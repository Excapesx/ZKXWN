// tools/bench/bench_main.cpp —— 5.0 对抗基准：同图、同规则、同裁判
//
// A 席默认 5.0（rmtc3::Bot），B 席默认 4.0（rmtc2::Bot，直接编译其源码）。
// 支持 --a/--b 切换成 4v4 对照，用来确认裁判本身对称、并测量随机波动。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numeric>
#include <map>
#include <string>
#include <vector>

#include "opponent_adapter.hpp"
#include "referee.hpp"
#include "rmtc3/bot.hpp"
#include "rmtc3/grid.hpp"

namespace {

class V5Policy final : public IPolicy {
public:
    V5Policy(int stage, int maxTicks, double aggression, int hunt) {
        rmtc3::Config c;
        c.stage = stage;
        c.maxTicks = maxTicks;
        bot_.configure(c);
        if (aggression > 0) bot_.set_profile(aggression, hunt);
    }
    rmtc3::Action act(const rmtc3::Perception& p) override {
        const rmtc3::Decision d = bot_.decide(p);
        ++reasons_[d.reason];
        return d.action;
    }
    const char* name() const override { return "v5"; }
    static void dump() {
        std::printf("  A(5) action mix:");
        for (const auto& kv : reasons_) std::printf(" %s=%d", kv.first.c_str(), kv.second);
        std::printf("\n");
    }

private:
    rmtc3::Bot bot_;
    static std::map<std::string, int> reasons_;
};
std::map<std::string, int> V5Policy::reasons_;

// "普通参赛队"基线：最近资源 + 贴身攻击，用来制造真实的资源争夺。
class GreedyBot final : public IPolicy {
public:
    rmtc3::Action act(const rmtc3::Perception& p) override {
        mem_.update(p);
        mask_.reset(mem_.width(), mem_.height());
        for (const auto& w : mem_.walls()) mask_.set(w, true);
        for (const auto& e : p.entities) {
            if (e.type == rmtc3::EntityType::Robot && e.hp > 0) mask_.set(e.pos, true);
        }
        field_ = rmtc3::bfs(mask_, p.self, 600);
        if (!p.alive) return rmtc3::Action::wait();
        for (int dir = 0; dir < 4; ++dir) {
            for (const auto& e : p.entities) {
                if (e.type != rmtc3::EntityType::Robot || e.hp <= 0) continue;
                if (e.pos.x - p.self.x == rmtc3::kDirX[dir] && e.pos.y - p.self.y == rmtc3::kDirY[dir]) {
                    return rmtc3::Action::attack(rmtc3::kDirX[dir], rmtc3::kDirY[dir]);
                }
            }
        }
        int bestDist = 1 << 20;
        rmtc3::Point goal{0, 0};
        bool found = false;
        for (const auto& kv : mem_.resources()) {
            const int d = field_.at(kv.second.pos);
            if (d > 0 && d < bestDist) {
                bestDist = d;
                goal = kv.second.pos;
                found = true;
            }
        }
        if (!found) {
            for (const auto& c : mem_.frontiers()) {
                const int d = field_.at(c);
                if (d > 0 && d < bestDist) {
                    bestDist = d;
                    goal = c;
                    found = true;
                }
            }
        }
        if (!found) return rmtc3::Action::wait();
        const int8_t dir = field_.firstAt(goal);
        if (dir < 0) return rmtc3::Action::wait();
        return rmtc3::Action::move(rmtc3::kDirX[dir], rmtc3::kDirY[dir]);
    }
    const char* name() const override { return "greedy"; }

private:
    rmtc3::WorldMap mem_;
    rmtc3::BlockMask mask_;
    rmtc3::Field field_;
};

struct Stats {
    std::vector<double> values;
    double mean() const {
        if (values.empty()) return 0;
        return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    }
    double quantile(double q) const {
        if (values.empty()) return 0;
        std::vector<double> v = values;
        std::sort(v.begin(), v.end());
        return v[static_cast<std::size_t>(q * (v.size() - 1))];
    }
    double max() const { return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end()); }
};

struct MatchResult {
    double score[4] = {0, 0, 0, 0};
    double resource[4] = {0, 0, 0, 0};
    double damage[4] = {0, 0, 0, 0};
    int missions[4] = {0, 0, 0, 0};
    int deaths[4] = {0, 0, 0, 0};
    int kills[4] = {0, 0, 0, 0};
    double to[4][4] = {{0}};
};

MatchResult run_match(const bench::MapDef& map, int maxTicks, std::vector<std::unique_ptr<IPolicy>>& policies,
                      std::vector<std::vector<double>>* decisionUs) {
    bench::Referee ref;
    ref.reset(map, maxTicks);
    MatchResult out;
    for (int t = 0; t < maxTicks; ++t) {
        for (int i = 0; i < ref.robot_count(); ++i) {
            const rmtc3::Perception p = ref.perceive(i);
            const auto begin = std::chrono::steady_clock::now();
            const rmtc3::Action a = policies[static_cast<std::size_t>(i)]->act(p);
            const auto end = std::chrono::steady_clock::now();
            if (decisionUs) {
                (*decisionUs)[static_cast<std::size_t>(i)].push_back(
                    std::chrono::duration<double, std::micro>(end - begin).count());
            }
            ref.submit(i, a);
        }
        ref.step();
    }
    for (int i = 0; i < ref.robot_count() && i < 4; ++i) {
        out.score[i] = ref.score(i);
        out.resource[i] = ref.robots()[static_cast<std::size_t>(i)].resourceValue;
        out.damage[i] = ref.robots()[static_cast<std::size_t>(i)].damage;
        out.missions[i] = ref.robots()[static_cast<std::size_t>(i)].missionsDone;
        out.deaths[i] = ref.robots()[static_cast<std::size_t>(i)].deaths;
        out.kills[i] = ref.robots()[static_cast<std::size_t>(i)].kills;
        for (int j = 0; j < 4; ++j) out.to[i][j] = ref.robots()[static_cast<std::size_t>(i)].dmgTo[j];
    }
    return out;
}

struct Args {
    int stage = 2;
    int matches = 300;
    int robots = 4;
    char a = '5';
    char b = '4';
    unsigned seed = 20261024u;
    double aggression = 0.0;
    int hunt = 0;
};

std::unique_ptr<IPolicy> make(char kind, int stage, int maxTicks, const Args& args) {
    if (kind == '5') return std::make_unique<V5Policy>(stage, maxTicks, args.aggression, args.hunt);
    return make_v4_bot(stage, maxTicks);
}

}  // namespace

int main(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--stage") == 0 && i + 1 < argc) args.stage = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--matches") == 0 && i + 1 < argc) args.matches = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--robots") == 0 && i + 1 < argc) args.robots = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) args.seed = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--a") == 0 && i + 1 < argc) args.a = argv[++i][0];
        else if (std::strcmp(argv[i], "--b") == 0 && i + 1 < argc) args.b = argv[++i][0];
        else if (std::strcmp(argv[i], "--agg") == 0 && i + 1 < argc) args.aggression = std::atof(argv[++i]);
        else if (std::strcmp(argv[i], "--hunt") == 0 && i + 1 < argc) args.hunt = std::atoi(argv[++i]);
    }

    double toA[4] = {0, 0, 0, 0}, toB[4] = {0, 0, 0, 0};
    int toN = 0;
    Stats sa, sb, sra, srb, sda, sdb, sma, smb, ska, skb, sxa, sxb, us, usb;
    int wins = 0, draws = 0, losses = 0;

    for (int m = 0; m < args.matches; ++m) {
        if (args.stage == 1) {
            const bench::MapDef map = bench::generate_map(20, 20, 1, args.seed + static_cast<unsigned>(m), 1);
            for (int side = 0; side < 2; ++side) {
                std::vector<std::unique_ptr<IPolicy>> pol;
                pol.push_back(make(side == 0 ? args.a : args.b, 1, 100, args));
                const MatchResult r = run_match(map, 100, pol, nullptr);
                Stats& sc = side == 0 ? sa : sb;
                Stats& rs = side == 0 ? sra : srb;
                Stats& dm = side == 0 ? sda : sdb;
                sc.values.push_back(r.score[0]);
                rs.values.push_back(r.resource[0]);
                dm.values.push_back(r.damage[0]);
                if (side == 1) {
                    const double x = sa.values.back(), y = sb.values.back();
                    if (x > y + 1e-9) ++wins;
                    else if (y > x + 1e-9) ++losses;
                    else ++draws;
                }
            }
            continue;
        }
        const bench::MapDef map = bench::generate_map(24, 24, args.robots, args.seed + static_cast<unsigned>(m), 2);
        for (int rotation = 0; rotation < 2; ++rotation) {
            std::vector<std::unique_ptr<IPolicy>> pol;
            const int iA = rotation == 0 ? 0 : 1;
            const int iB = rotation == 0 ? 1 : 0;
            pol.push_back(make(rotation == 0 ? args.a : args.b, 2, 500, args));
            pol.push_back(make(rotation == 0 ? args.b : args.a, 2, 500, args));
            if (args.robots >= 4) {
                pol.push_back(std::make_unique<GreedyBot>());
                pol.push_back(std::make_unique<GreedyBot>());
            }
            std::vector<std::vector<double>> times(4);
            const MatchResult r = run_match(map, 500, pol, &times);
            sa.values.push_back(r.score[iA]);
            sra.values.push_back(r.resource[iA]);
            sda.values.push_back(r.damage[iA]);
            sma.values.push_back(r.missions[iA]);
            ska.values.push_back(r.kills[iA]);
            sxa.values.push_back(r.deaths[iA]);
            sb.values.push_back(r.score[iB]);
            srb.values.push_back(r.resource[iB]);
            sdb.values.push_back(r.damage[iB]);
            smb.values.push_back(r.missions[iB]);
            skb.values.push_back(r.kills[iB]);
            sxb.values.push_back(r.deaths[iB]);
            for (double v : times[static_cast<std::size_t>(iA)]) us.values.push_back(v);
            for (double v : times[static_cast<std::size_t>(iB)]) usb.values.push_back(v);
            for (int k = 0; k < 4; ++k) { toA[k] += r.to[iA][k]; toB[k] += r.to[iB][k]; }
            ++toN;
            if (r.score[iA] > r.score[iB] + 1e-9) ++wins;
            else if (r.score[iB] > r.score[iA] + 1e-9) ++losses;
            else ++draws;
        }
    }

    std::printf("\n===== %s =====\n", args.stage == 1 ? "STAGE 1 (independent collection)"
                                                      : (args.robots >= 4 ? "STAGE 2 (4-team, C/D = greedy)" : "STAGE 2 (1v1 duel)"));
    std::printf("  A(%c)  score mean=%6.2f p10=%6.2f p50=%6.2f p90=%6.2f | res=%6.1f dmg=%6.1f mis=%.2f kill=%.2f death=%.2f\n",
                args.a, sa.mean(), sa.quantile(0.10), sa.quantile(0.50), sa.quantile(0.90), sra.mean(), sda.mean(),
                sma.mean(), ska.mean(), sxa.mean());
    std::printf("  B(%c)  score mean=%6.2f p10=%6.2f p50=%6.2f p90=%6.2f | res=%6.1f dmg=%6.1f mis=%.2f kill=%.2f death=%.2f\n",
                args.b, sb.mean(), sb.quantile(0.10), sb.quantile(0.50), sb.quantile(0.90), srb.mean(), sdb.mean(),
                smb.mean(), skb.mean(), sxb.mean());
    std::printf("  head-to-head: A win=%d draw=%d loss=%d  (A win rate %.1f%%)\n", wins, draws, losses,
                100.0 * wins / std::max(1, wins + draws + losses));
    if (toN > 0) {
        std::printf("  damage dealt per victim seat (avg): A -> [%.0f %.0f %.0f %.0f]  B -> [%.0f %.0f %.0f %.0f]\n",
                    toA[0] / toN, toA[1] / toN, toA[2] / toN, toA[3] / toN,
                    toB[0] / toN, toB[1] / toN, toB[2] / toN, toB[3] / toN);
    }
    V5Policy::dump();
    std::printf("  decision time: A p50=%.1fus p99=%.1fus max=%.1fus | B p50=%.1fus p99=%.1fus max=%.1fus\n",
                us.quantile(0.50), us.quantile(0.99), us.max(), usb.quantile(0.50), usb.quantile(0.99), usb.max());
    return 0;
}
