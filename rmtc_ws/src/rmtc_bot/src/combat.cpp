// OWNER: B
#include "rmtc_bot/combat.hpp"
#include <cstdlib>
namespace rmtc_bot {
bool same_attack_ray(Vec2 a, Vec2 b) {
    const int d = std::abs(a.x-b.x)+std::abs(a.y-b.y);
    return d >= 1 && d <= 2 && (a.x == b.x || a.y == b.y);
}
}
