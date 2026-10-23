// tools/bench/bench_main.cpp —— 对抗基准：2.0 vs 1.0（同图同规则）
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "legacy_adapter.hpp"
#include "referee.hpp"
#include "rmtc2/bot.hpp"
#include "rmtc2/grid.hpp"

namespace {

class Bot2Policy final : public IPolicy {
public:
    Bot2Policy(int stage, int maxTicks) {
        rmtc2::Config c;
        c.stage = stage;
        c.maxTicks = maxTicks;
        bot_.configure(c);
    }
    rmtc2::Action act(const rmtc2::Perception& p) override { return bot_.decide(p).action; }
    const char* name() const override { return "bot2"; }

private:
    rmtc2::Bot bot_;
};

// "普通参赛队"基线：最近资源 + 贴身攻击，用来制造真实的资源争夺
class GreedyBot final : public IPolicy {
public:
    rmtc2::Action act(const rmtc2::Perception& p) override {
        mem_.update(p);
        mask_.reset(mem_.width(), mem_.height());
        for (const auto& w : mem_.walls()) mask_.set(w, true);
        for (const auto& e : p.entities) {
            if (e.type == rmtc2::EntityType::Robot && e.hp > 0) mask_.set(e.pos, true);
        }
        field_ = rmtc2::bfs(mask_, p.self, 600);
        if (!p.alive) return rmtc2::Action::wait();

        // 贴身就打
        for (int dir = 0; dir < 4; ++dir) {
            for (const auto& e : p.entities) {
                if (e.type != rmtc2::EntityType::Robot || e.hp <= 0) continue;
                if (e.pos.x - p.self.x == rmtc2::kDirX[dir] && e.pos.y - p.self.y == rmtc2::kDirY[dir]) {
                    return rmtc2::Action::attack(rmtc2::kDirX[dir], rmtc2::kDirY[dir]);
                }
            }
        }
        // 最近资源
        int bestDist = 1 << 20;
        rmtc2::Point goal{0, 0};
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
        if (!found) return rmtc2::Action::wait();
        const int8_t dir = field_.firstAt(goal);
        if (dir < 0) return rmtc2::Action::wait();
        return rmtc2::Action::move(rmtc2::kDirX[dir], rmtc2::kDirY[dir]);
    }
    const char* name() const override { return "greedy"; }

private:
    rmtc2::WorldMap mem_;
    rmtc2::BlockMask mask_;
    rmtc2::Field field_;
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
        const std::size_t idx = static_cast<std::size_t>(q * (v.size() - 1));
        return v[idx];
    }
};

struct MatchResult {
    double score[4] = {0, 0, 0, 0};
    double resource[4] = {0, 0, 0, 0};
    double damage[4] = {0, 0, 0, 0};
    int missions[4] = {0, 0, 0, 0};
    int deaths[4] = {0, 0, 0, 0};
    int kills[4] = {0, 0, 0, 0};
    int missionFlags[4][4] = {{0}};
    double compRes[4] = {0, 0, 0, 0};
    double compDmg[4] = {0, 0, 0, 0};
    double compMis[4] = {0, 0, 0, 0};
};

MatchResult run_match(const bench::MapDef& map, int maxTicks, std::vector<std::unique_ptr<IPolicy>>& policies,
                      std::vector<std::vector<double>>* decisionUs) {
    bench::Referee ref;
    ref.reset(map, maxTicks);
    MatchResult out;
    for (int t = 0; t < maxTicks; ++t) {
        for (int i = 0; i < ref.robot_count(); ++i) {
            const rmtc2::Perception p = ref.perceive(i);
            const auto begin = std::chrono::steady_clock::now();
            const rmtc2::Action a = policies[static_cast<std::size_t>(i)]->act(p);
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
        out.compRes[i] = 30.0 * std::min(1.0, out.resource[i] / 120.0);
        out.compDmg[i] = 15.0 * std::min(1.0, out.damage[i] / 500.0);
        out.compMis[i] = 5.0 * out.missions[i];
        out.deaths[i] = ref.robots()[static_cast<std::size_t>(i)].deaths;
        out.kills[i] = ref.robots()[static_cast<std::size_t>(i)].kills;
        for (std::size_t k = 0; k < map.missions.size() && k < 4; ++k) {
            const auto& done = ref.robots()[static_cast<std::size_t>(i)].doneIds;
            out.missionFlags[i][k] = std::find(done.begin(), done.end(), map.missions[k].id) != done.end() ? 1 : 0;
        }
    }
    return out;
}

void report(const char* title, const Stats& a, const Stats& b, const Stats& ra, const Stats& rb,
            const Stats& da, const Stats& db, const Stats& ma, const Stats& mb, const Stats& ka,
            const Stats& kb, const Stats& xa, const Stats& xb, int wins, int draws, int losses,
            const Stats& us, const Stats& usl, const Stats& cra, const Stats& cda, const Stats& cma,
            const Stats& crb, const Stats& cdb, const Stats& cmb) {
    std::printf("\n===== %s =====\n", title);
    std::printf("  A(bot2)    score mean=%6.2f  p10=%6.2f  p90=%6.2f | res=%6.1f dmg=%6.1f mis=%.2f kill=%.2f death=%.2f\n",
                a.mean(), a.quantile(0.10), a.quantile(0.90), ra.mean(), da.mean(), ma.mean(), ka.mean(), xa.mean());
    std::printf("  B(legacy)  score mean=%6.2f  p10=%6.2f  p90=%6.2f | res=%6.1f dmg=%6.1f mis=%.2f kill=%.2f death=%.2f\n",
                b.mean(), b.quantile(0.10), b.quantile(0.90), rb.mean(), db.mean(), mb.mean(), kb.mean(), xb.mean());
    std::printf("  components     bot2: res=%.2f dmg=%.2f mis=%.2f  | legacy: res=%.2f dmg=%.2f mis=%.2f  (sum=%.2f / %.2f)\n",
                cra.mean(), cda.mean(), cma.mean(), crb.mean(), cdb.mean(), cmb.mean(),
                cra.mean() + cda.mean() + cma.mean(), crb.mean() + cdb.mean() + cmb.mean());
    std::printf("  head-to-head: win=%d draw=%d loss=%d  (win rate %.1f%%)\n", wins, draws, losses,
                100.0 * wins / std::max(1, wins + draws + losses));
    const auto pmax = [](const Stats& s) { return s.values.empty() ? 0.0 : *std::max_element(s.values.begin(), s.values.end()); };
    std::printf("  bot2   decision time: p50=%.1fus p95=%.1fus p99=%.1fus max=%.1fus\n", us.quantile(0.50),
                us.quantile(0.95), us.quantile(0.99), pmax(us));
    std::printf("  legacy decision time: p50=%.1fus p95=%.1fus p99=%.1fus max=%.1fus\n", usl.quantile(0.50),
                usl.quantile(0.95), usl.quantile(0.99), pmax(usl));
}

}  // namespace

int main(int argc, char** argv) {
    int stage = 2;
    int matches = 100;
    int robots = 4;
    unsigned seed = 20261023;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--stage") == 0 && i + 1 < argc) stage = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--matches") == 0 && i + 1 < argc) matches = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = static_cast<unsigned>(std::atoi(argv[++i]));
        else if (std::strcmp(argv[i], "--robots") == 0 && i + 1 < argc) robots = std::atoi(argv[++i]);
    }

    Stats sa, sb, sra, srb, sda, sdb, sma, smb, us, usLegacy;
    Stats ska, skb, sxa, sxb;
    Stats cra, cda, cma, crb, cdb, cmb;
    int misA[4] = {0, 0, 0, 0};
    int misB[4] = {0, 0, 0, 0};
    int misTotal = 0;
    int wins = 0, draws = 0, losses = 0;

    for (int m = 0; m < matches; ++m) {
        if (stage == 1) {
            const bench::MapDef map = bench::generate_map(20, 20, 1, seed + static_cast<unsigned>(m), 1);
            std::vector<std::vector<double>> dummy(1);
            {
                std::vector<std::unique_ptr<IPolicy>> pol;
                pol.push_back(std::make_unique<Bot2Policy>(1, 100));
                const MatchResult r = run_match(map, 100, pol, &dummy);
                sa.values.push_back(r.score[0]);
                sra.values.push_back(r.resource[0]);
                sda.values.push_back(r.damage[0]);
                sma.values.push_back(r.missions[0]);
                ska.values.push_back(r.kills[0]);
                sxa.values.push_back(r.deaths[0]);
            }
            {
                std::vector<std::unique_ptr<IPolicy>> pol;
                pol.push_back(make_legacy_bot(1, 100, 'A'));
                const MatchResult r = run_match(map, 100, pol, nullptr);
                sb.values.push_back(r.score[0]);
                srb.values.push_back(r.resource[0]);
                sdb.values.push_back(r.damage[0]);
                smb.values.push_back(r.missions[0]);
                skb.values.push_back(r.kills[0]);
                sxb.values.push_back(r.deaths[0]);
            }
        } else {
            const bench::MapDef map = bench::generate_map(24, 24, robots, seed + static_cast<unsigned>(m), 2);
            // 两套席位轮换，抵消出生点差异
            for (int rotation = 0; rotation < 2; ++rotation) {
                std::vector<std::unique_ptr<IPolicy>> pol;
                // 席位 A/B 为 2.0 与 1.0 轮换，C/D 为普通基线
                if (rotation == 0) {
                    pol.push_back(std::make_unique<Bot2Policy>(2, 500));
                    pol.push_back(make_legacy_bot(2, 500, 'B'));
                } else {
                    pol.push_back(make_legacy_bot(2, 500, 'A'));
                    pol.push_back(std::make_unique<Bot2Policy>(2, 500));
                }
                if (robots >= 4) {
                    pol.push_back(std::make_unique<GreedyBot>());
                    pol.push_back(std::make_unique<GreedyBot>());
                }
                std::vector<std::vector<double>> times(4);
                const MatchResult r = run_match(map, 500, pol, &times);
                const int i2 = rotation == 0 ? 0 : 1;
                const int i1 = rotation == 0 ? 1 : 0;
                sa.values.push_back(r.score[i2]);
                sra.values.push_back(r.resource[i2]);
                sda.values.push_back(r.damage[i2]);
                sma.values.push_back(r.missions[i2]);
                ska.values.push_back(r.kills[i2]);
                sxa.values.push_back(r.deaths[i2]);
                sb.values.push_back(r.score[i1]);
                srb.values.push_back(r.resource[i1]);
                sdb.values.push_back(r.damage[i1]);
                smb.values.push_back(r.missions[i1]);
                skb.values.push_back(r.kills[i1]);
                sxb.values.push_back(r.deaths[i1]);
                cra.values.push_back(r.compRes[i2]);
                cda.values.push_back(r.compDmg[i2]);
                cma.values.push_back(r.compMis[i2]);
                crb.values.push_back(r.compRes[i1]);
                cdb.values.push_back(r.compDmg[i1]);
                cmb.values.push_back(r.compMis[i1]);
                for (int k = 0; k < 4; ++k) {
                    misA[k] += r.missionFlags[i2][k];
                    misB[k] += r.missionFlags[i1][k];
                }
                ++misTotal;
                for (double v : times[static_cast<std::size_t>(i2)]) us.values.push_back(v);
                for (double v : times[static_cast<std::size_t>(i1)]) usLegacy.values.push_back(v);
                if (r.score[i2] > r.score[i1] + 1e-9) ++wins;
                else if (r.score[i1] > r.score[i2] + 1e-9) ++losses;
                else ++draws;
            }
        }
    }

    if (stage == 1) {
        for (std::size_t i = 0; i < sa.values.size(); ++i) {
            if (sa.values[i] > sb.values[i] + 1e-9) ++wins;
            else if (sb.values[i] > sa.values[i] + 1e-9) ++losses;
            else ++draws;
        }
        report("STAGE 1 (independent collection, identical maps)", sa, sb, sra, srb, sda, sdb, sma, smb, ska,
               skb, sxa, sxb, wins, draws, losses, us, usLegacy, cra, cda, cma, crb, cdb, cmb);
    } else {
        std::printf("  mission completion: bot2 [normal %.0f%% high %.0f%% reach %.0f%%]  legacy [normal %.0f%% high %.0f%% reach %.0f%%]\n",
                    100.0 * misA[0] / std::max(1, misTotal), 100.0 * misA[1] / std::max(1, misTotal),
                    100.0 * misA[2] / std::max(1, misTotal), 100.0 * misB[0] / std::max(1, misTotal),
                    100.0 * misB[1] / std::max(1, misTotal), 100.0 * misB[2] / std::max(1, misTotal));
        report(robots >= 4 ? "STAGE 2 (4-team match: A/B = bot2 & legacy, C/D = greedy baseline)"
                            : "STAGE 2 (1v1 duel: A = bot2, B = legacy)", sa, sb, sra, srb, sda, sdb,
               sma, smb, ska, skb, sxa, sxb, wins, draws, losses, us, usLegacy, cra, cda, cma, crb, cdb, cmb);
    }
    return 0;
}
