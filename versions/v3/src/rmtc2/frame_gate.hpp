#pragma once
#include <set>
#include <string>
namespace rmtc2 {
// A retired session cannot replace the current match because of a delayed DDS frame.
class FrameGate {
public:
 enum class Result { Reject, NewSession, NewTick, Duplicate };
 Result accept(const std::string& session, int tick) {
   if(session.empty() || tick < 0 || retired_.count(session)) return Result::Reject;
   if(session != session_) {
     if(!session_.empty()) retired_.insert(session_);
     session_=session; tick_=tick; return Result::NewSession;
   }
   if(tick < tick_) return Result::Reject;
   if(tick == tick_) return Result::Duplicate;
   tick_=tick; return Result::NewTick;
 }
private:
 std::string session_; int tick_=-1; std::set<std::string> retired_;
};
}
