#include <iostream>
#include "rmtc9/bot.hpp"
#if RMTC_ENABLE_PHASE_PROBES
#error "Production-scope test must not compile offline phase probes"
#endif
int main(){using namespace rmtc9;Bot bot;bot.configure({2,500});
 Perception p;p.session="production-scope";p.team="A";p.mapW=p.mapH=12;p.self={4,4};p.hp=100;p.alive=true;p.vision=4;
 Entity r;r.type=EntityType::ResNormal;r.id="n";r.pos={5,4};r.value=10;p.entities={r};
 auto d=bot.decide(p);if(!d.action.legal_shape()||!bot.diagnostics().phaseCandidates.empty()||bot.diagnostics().fullPhaseAudit)return 1;
 if(d.action.kind!=Act::Move||d.action.dx!=1||d.action.dy!=0)return 2;
 std::cout<<"Production-scope core compiles without probe header; ordinary decision is legal; full phase probe audit stays disabled\n";return 0;
}
