#pragma once
#include <map>
#include "rmtc6/types.hpp"
namespace rmtc6 {
class EventTracker {
public:
 void reset(){ history_.clear();session_.clear(); }
 Plan apply(const Plan& input,const Perception& p);
private:
 std::string session_;
 std::map<int,Perception> history_;
};
Plan merge_plans(const Plan& previous,const Plan& next,int tick);
}
