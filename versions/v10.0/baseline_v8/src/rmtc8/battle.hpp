#pragma once
#include "rmtc8/types.hpp"
#include "rmtc8/grid.hpp"
#include "rmtc8/combat.hpp"
#include <set>
#include <string>
namespace rmtc8 {
enum class BattleMode { Hold, FocusSelf, Opportunistic };
struct BattleForecast{double credit=0,hpAfter=0;bool fatal=false;int kills=0;};
// Legal public observations only. Scenario predictions are not enemy truth.
// Known pre-move hazards are applied. Unknown enemy buffs remain a limitation.
// passive：在线识别为“对齐也不开火”的对手队伍，情景里它们不选攻击方向（8.0）。
BattleForecast forecast_battle(const Perception&,const Plan&,const HazardField&,
                               const std::vector<const Entity*>& targets,BattleMode,
                               const std::set<std::string>* passive=nullptr);
}

