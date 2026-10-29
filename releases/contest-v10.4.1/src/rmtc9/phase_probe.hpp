#pragma once
#if !defined(RMTC_ENABLE_PHASE_PROBES) || !RMTC_ENABLE_PHASE_PROBES
#error "Phase probes are offline-only and cannot be included by the ROS production build"
#endif
#include <stdexcept>
#include "rmtc9/bot.hpp"
namespace rmtc9 {
struct PhaseProbeResult { Decision decision;DecisionDiagnostics diagnostics; };
// Deep-copy the state BEFORE this observation. No probe mutates the live bot,
// sees future frames, or supplies game truth. No production parameter channel.
struct PhaseProbe {
    static void configure_offline(Bot& bot,double alpha,unsigned restore=0) {
        if((alpha!=0&&alpha!=.5&&alpha!=1)||restore>7)throw std::invalid_argument("invalid phase probe");
        bot.phaseWeights_={alpha,restore};
    }
    static PhaseProbeResult evaluate(const Bot& before,const Perception& p,double alpha,unsigned restore=0) {
        Bot copy=before;configure_offline(copy,alpha,restore);copy.detailedPhaseAudit_=true;
        auto decision=copy.decide(p);return {decision,copy.diagnostics_};
    }
};
} // namespace rmtc9
