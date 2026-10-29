#pragma once

#include <algorithm>
#include <cmath>
#include <vector>
#include "rmtc101/grid.hpp"

namespace rmtc101 {

// A local-observation heuristic, not a probability learned from hidden truth.
// ETA is measured in moves from the current frame for every competitor.
struct ResourceRace {
    int ourEta = -1;
    int rivalEta = -1;
    double availability = 1.0;
    const char* reason = "no_visible_rival";
};

inline ResourceRace resource_race(int ours, int rival) {
    ResourceRace r; r.ourEta = ours; r.rivalEta = rival;
    if (ours < 0) { r.availability = 0; r.reason = "unreachable"; return r; }
    if (rival < 0) return r;
    const int lead = rival - ours;
    if (lead >= 3) { r.availability = 1.0; r.reason = "ahead"; }
    else if (lead >= 1) { r.availability = .9; r.reason = "contested_lead"; }
    else if (lead == 0) { r.availability = .55; r.reason = "same_tick"; }
    else if (lead == -1) { r.availability = .35; r.reason = "one_tick_late"; }
    else { r.availability = .2; r.reason = "rival_faster"; }
    return r;
}

struct ArrivalTour { std::vector<int> order; double value = 0; int length = 0; };

// Bounded beam search: a resource at stop 3 is discounted at its arrival time,
// not at the direct distance from the starting position. It is not an optimizer
// with a global-optimality guarantee. Negative rival ETA denotes unknown.
inline ArrivalTour solve_arrival_tour(const std::vector<int>& start,
    const std::vector<std::vector<int>>& between, const std::vector<double>& value,
    const std::vector<int>& rivalEta, int budget) {
    struct State { unsigned mask = 0; int last = -1, time = 0; double score = 0;
                   std::vector<int> order; };
    const int n = static_cast<int>(value.size());
    if (n == 0 || n > 16 || start.size() != value.size() ||
        rivalEta.size() != value.size() || between.size() != value.size() || budget <= 0) return {};
    for (const auto& row : between) if (row.size() != value.size()) return {};
    std::vector<State> beam(1); ArrivalTour best;
    for (int depth = 0; depth < n && !beam.empty(); ++depth) {
        std::vector<State> next;
        for (const auto& s : beam) for (int j = 0; j < n; ++j) {
            if (s.mask & (1u << j)) continue;
            const int step = s.last < 0 ? start[j] : between[s.last][j];
            if (step < 0 || step > budget - s.time) continue;
            State t = s; t.mask |= 1u << j; t.last = j; t.time += step;
            t.score += std::max(0.0, value[j]) * resource_race(t.time, rivalEta[j]).availability;
            t.order.push_back(j);
            if (t.score > best.value + 1e-9 ||
                (std::abs(t.score - best.value) <= 1e-9 && t.time < best.length))
                best = {t.order, t.score, t.time};
            next.push_back(std::move(t));
        }
        // Stable ordering gives repeatable choices on identical observations.
        std::stable_sort(next.begin(), next.end(), [](const State& a, const State& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.time < b.time;
        });
        if (next.size() > 64) next.resize(64);
        beam = std::move(next);
    }
    return best;
}
}  // namespace rmtc101
