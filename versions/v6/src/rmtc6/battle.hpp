#pragma once
#include "rmtc6/types.hpp"
#include "rmtc6/grid.hpp"
#include "rmtc6/combat.hpp"
namespace rmtc6 {
enum class BattleMode { Hold, FocusSelf, Opportunistic };
struct BattleForecast{double credit=0,hpAfter=0;bool fatal=false;int kills=0;};
// Legal public observations only. Scenario predictions are not enemy truth.
// Known pre-move hazards are applied. Unknown enemy buffs remain a limitation.
BattleForecast forecast_battle(const Perception&,const Plan&,const HazardField&,
                               const std::vector<const Entity*>& targets,BattleMode);
}

