#pragma once
#include "rmtc9/types.hpp"
#include "rmtc9/grid.hpp"
#include "rmtc9/combat.hpp"
#include <set>
#include <string>
namespace rmtc9 {
enum class BattleMode { Hold, FocusSelf, Opportunistic };
struct BattleForecast{double credit=0,hpAfter=0;bool fatal=false;int kills=0;};
// Legal public observations only. Scenario predictions are not enemy truth.
// Known pre-move hazards are applied. Unknown enemy buffs remain a limitation.
// passive is a soft hypothesis for Hold/Opportunistic only. FocusSelf always
// retains possible enemy fire, even when quiet history is supplied.
BattleForecast forecast_battle(const Perception&,const Plan&,const HazardField&,
                               const std::vector<const Entity*>& targets,BattleMode,
                               const std::set<std::string>* passive=nullptr);
}
