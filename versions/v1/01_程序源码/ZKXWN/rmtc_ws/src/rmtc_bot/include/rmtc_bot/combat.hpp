// OWNER: B
#pragma once
#include "rmtc_bot/types.hpp"
namespace rmtc_bot {
// Diagnostic helper only; action evaluation is centralized in DecisionEngine.
bool same_attack_ray(Vec2 from, Vec2 target);
}
