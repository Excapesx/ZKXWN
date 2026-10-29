#pragma once

#include <algorithm>

namespace rmtc102 {

// Frozen, public-state-driven priorities inside the registered decision_type.
// No runtime tuning channel and no estimate of hidden resource truth.
struct PhasePolicy {
    const char* name = "adaptive";
    int openingEnd = 100;
    int endgameStart = 350;
    bool opening = false;
    bool endgame = false;
    double resourceScale = 1.0;
    double denialScale = 1.0;
};

inline PhasePolicy phase_policy(int tick, int maxTicks, double resourceValue, int hp) {
    PhasePolicy p;
    const int total = std::max(1, maxTicks);
    p.openingEnd = std::min(100, std::max(1, total / 5));
    const int closingTicks = std::min(150, std::max(1, static_cast<int>(3LL * total / 10)));
    p.endgameStart = std::max(p.openingEnd, total - closingTicks);
    p.opening = tick < p.openingEnd;
    p.endgame = tick >= p.endgameStart;
    p.name = p.endgame ? "finish" : p.opening ? "opening" : "adaptive";
    if (p.opening && resourceValue < 120 && hp >= 60) p.resourceScale = 1.20;
    // Once own resource score is full, denial remains useful but cannot be
    // valued as highly as a still-scoreable objective. Mission bonuses are separate.
    if (resourceValue >= 120) p.denialScale = .25;
    return p;
}

}  // namespace rmtc102
