#pragma once
#ifndef RMTC_PHASE_ALPHA_PERCENT
#define RMTC_PHASE_ALPHA_PERCENT 100
#endif
#ifndef RMTC_PHASE_RESTORE_MASK
#define RMTC_PHASE_RESTORE_MASK 0
#endif
static_assert(RMTC_PHASE_ALPHA_PERCENT==0 || RMTC_PHASE_ALPHA_PERCENT==50 || RMTC_PHASE_ALPHA_PERCENT==100,"phase alpha must be 0/50/100");
static_assert(RMTC_PHASE_RESTORE_MASK>=0 && RMTC_PHASE_RESTORE_MASK<=7,"invalid restore mask");
namespace rmtc9 {
enum class PhaseComponent { Resource=1,Pursuit=2,Mission=4 };
struct PhaseWeights {
    double alpha=RMTC_PHASE_ALPHA_PERCENT/100.0;
    unsigned restoreMask=RMTC_PHASE_RESTORE_MASK;
};
inline double phase_multiplier(double raw,PhaseComponent part,const PhaseWeights& w) {
    if(w.alpha==1 || (w.restoreMask&static_cast<unsigned>(part)))return raw;
    if(w.alpha==0)return 1;
    return 1+w.alpha*(raw-1);
}
} // namespace rmtc9
