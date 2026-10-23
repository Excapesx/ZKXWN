#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace rmtc::decision {
// One victim in one simultaneous damage batch. The caller supplies a scenario,
// not a claim that opponents' next actions are known.
struct DamageClaim {
    std::string team;
    double nominal = 0;
    bool ordinary_attack = true; // Reflection/chili may earn kills, but not attack credit.
};
struct BattleAward { double attack_credit = 0, kill_credit = 0; };
struct DamageBatch {
    bool valid = false, killed = false;
    double hp_after = 0;
    std::map<std::string, BattleAward> awards;
};
inline DamageBatch resolve_damage_batch(double hp, const std::vector<DamageClaim>& claims) {
    DamageBatch result;
    if (!std::isfinite(hp) || hp <= 0) return result;
    double total = 0;
    for (const auto& claim : claims) {
        if (claim.team.empty() || !std::isfinite(claim.nominal) || claim.nominal < 0) return result;
        total += claim.nominal;
    }
    if (!std::isfinite(total)) return result;
    result.valid = true;
    result.hp_after = std::max(0.0, hp-total);
    result.killed = total >= hp;
    const double factor = total > 0 ? std::min(1.0, hp/total) : 0;
    for (const auto& claim : claims) {
        if (claim.nominal <= 0) continue;
        auto& award = result.awards[claim.team]; // Deduplicate contributors by team.
        if (claim.ordinary_attack) award.attack_credit += claim.nominal*factor;
    }
    if (result.killed && !result.awards.empty())
        for (auto& item : result.awards) item.second.kill_credit = 60.0/result.awards.size();
    return result;
}
inline double combat_points(double adjusted_damage) {
    return 15.0*std::clamp(adjusted_damage/500.0, 0.0, 1.0);
}
inline double forecast_adjusted_damage(double current, double attack, double kills, int deaths) {
    // Do not cap at 500: excess credit matters for ties and later death deductions.
    return std::max(0.0, current+attack+kills-60.0*std::max(0,deaths));
}
} // namespace rmtc::decision
