#pragma once
#include "rmtc7/types.hpp"
#include "rmtc7/grid.hpp"
#include "rmtc7/combat.hpp"
namespace rmtc7 {
enum class BattleMode { Hold, FocusSelf, Opportunistic };
struct BattleForecast{double credit=0,hpAfter=0;bool fatal=false;int kills=0;};
// Legal public observations only. Scenario predictions are not enemy truth.
// Known pre-move hazards are applied. Unknown enemy buffs remain a limitation.
BattleForecast forecast_battle(const Perception&,const Plan&,const HazardField&,
                               const std::vector<const Entity*>& targets,BattleMode);
}

