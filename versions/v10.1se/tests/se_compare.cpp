// Deterministic 24x24, 8-event, three-task comparison harness.
//
// It runs four bots on the same generated map. In "normal" mode all bots see
// the official structured missions. In "fallback" mode seat A's structured
// missions are hidden; this simulates a missing/malformed missions field while
// the referee still scores the real tasks. On v10.1se seat A receives the
// mission text fallback; v10.1.1 can only use the remaining local heuristics.
//
// Output is one JSON object per seat:
//   {"map":N,"seat":i,"score":...,"resource":...,"damage":...,"missions":...,"deaths":...,"invalid":...}
#include <iostream>
#include <string>

#include "rmtc9/bot.hpp"
#include "rmtc9/parse.hpp"
#include "referee.hpp"

using namespace rmtc9;

int main(int argc, char** argv) {
    int maps = argc > 1 ? std::stoi(argv[1]) : 8;
    unsigned seed = argc > 2 ? static_cast<unsigned>(std::stoul(argv[2])) : 20261030u;
    const std::string mode = argc > 3 ? argv[3] : "normal";
    const bool fallback = mode == "fallback";
    const int maxTicks = 500;

    for (int m = 0; m < maps; ++m) {
        const unsigned mapSeed = seed + static_cast<unsigned>(m) * 7919u;
        bench::MapDef map = bench::generate_map(24, 24, 4, mapSeed, 2);
        map.events = bench::generate_events(map, mapSeed, "standard");
        bench::Referee ref;
        ref.reset(map, maxTicks);

        Bot bots[4];
        for (auto& b : bots) b.configure({2, maxTicks});
#ifdef SE_HAS_FALLBACK
        if (fallback) bots[0].set_fallback_missions(map.missions);
#endif

        for (int tick = 0; tick < maxTicks; ++tick) {
            bool announce = bench::has_announcement(map, tick);
            for (int i = 0; i < 4; ++i) {
                Perception p = ref.perceive(i);
                if (fallback && i == 0) p.missions.clear();
                if (announce) {
                    Plan plan;
                    if (parse_plan(bench::oracle_plan(map, tick, 0), p.session, tick,
                                   maxTicks, map.w, map.h, &plan)) {
                        bots[i].set_plan(plan);
                    }
                }
                Decision d = bots[i].decide(p);
                ref.submit(i, d.action);
            }
            ref.step();
        }

        for (int i = 0; i < 4; ++i) {
            const auto& r = ref.robots()[i];
            std::cout << "{\"map\":" << m
                      << ",\"seat\":" << i
                      << ",\"score\":" << ref.score(i)
                      << ",\"resource\":" << r.resourceValue
                      << ",\"damage\":" << r.damage
                      << ",\"missions\":" << r.missionsDone
                      << ",\"deaths\":" << r.deaths
                      << ",\"invalid\":" << r.invalid
                      << "}\n";
        }
    }
    return 0;
}
