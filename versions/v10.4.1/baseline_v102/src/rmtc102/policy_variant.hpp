#pragma once
// Compile-time ablations for offline experiments. Production ROS uses the
// sealed default. This is not an environment variable or runtime parameter.
#ifndef RMTC_ABLATION_LEVEL
#define RMTC_ABLATION_LEVEL 2
#endif
static_assert(RMTC_ABLATION_LEVEL>=1 && RMTC_ABLATION_LEVEL<=3,"invalid offline ablation");
namespace rmtc102 {
inline constexpr int kAblationLevel=RMTC_ABLATION_LEVEL;
inline constexpr bool kUseOpportunities=kAblationLevel>=2;
inline constexpr bool kUseFixedPhases=kAblationLevel<3;
}
