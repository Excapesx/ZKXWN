#pragma once
// Offline compile-time ablation. Release selection is sealed in this file.
#ifndef RMTC_ROUND2_LEVEL
#define RMTC_ROUND2_LEVEL 3
#endif
static_assert(RMTC_ROUND2_LEVEL>=0 && RMTC_ROUND2_LEVEL<=3,"invalid round2 level");
#ifndef RMTC_ENABLE_OPPORTUNITY_COST
#define RMTC_ENABLE_OPPORTUNITY_COST 1
#endif
namespace rmtc103 { inline constexpr int kRound2Level=RMTC_ROUND2_LEVEL; }

