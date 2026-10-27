#include "rmtc9/events.hpp"
#include <algorithm>
#include <set>
#include <tuple>
namespace rmtc9 {
Plan merge_plans(const Plan& previous,const Plan& next,int tick) {
 Plan result=next;
 const bool same=previous.valid&&previous.session==next.session;
 for(const auto& e:(same?previous.events:std::vector<Event>{})) {
   bool replaced=false;for(const auto& n:next.events)if(n.id==e.id)replaced=true;
   if(!replaced&&std::max(e.end,e.kind=="heal"?e.end+e.duration:(e.kind=="inversion"?e.start+e.duration:e.end))>tick&&result.events.size()<64)result.events.push_back(e);
 }
 for(const auto& h:(same?previous.hazards:std::vector<Hazard>{}))if(h.end>tick&&result.hazards.size()<64)result.hazards.push_back(h);
 std::set<std::tuple<bool,int,int,int,int,int,int,int,bool,bool,bool>> keys;
 std::vector<Hazard> distinct;
 for(const auto& h:result.hazards)if(h.end>tick&&keys.insert({h.cross,h.a.x,h.a.y,h.b.x,h.b.y,h.start,h.end,h.damage,h.lethal,h.wall,h.respectsProtection}).second) distinct.push_back(h);
 result.hazards=distinct;
 if(same&&result.inverted.end<=tick && previous.inverted.end>tick)result.inverted=previous.inverted;
 if(same&&result.vulnerable.end<=tick && previous.vulnerable.end>tick)result.vulnerable=previous.vulnerable;
 if(same&&result.reflect.end<=tick && previous.reflect.end>tick){result.reflect=previous.reflect;result.reflectDamage=previous.reflectDamage;}
 return result;
}
Plan EventTracker::apply(const Plan& input,const Perception& p) {
 if(session_!=p.session){reset();session_=p.session;}
 // Minimal public history, bounded to one standard match and no hidden fields.
 Perception frame=p;frame.entities.clear();frame.eventText.clear();frame.missionText.clear();frame.missions.clear();
 history_[p.tick]=frame;
 while(history_.size()>1024)history_.erase(history_.begin());
 Plan out=input;
 std::optional<Point> spawn;
 for(const auto& item:history_)if(item.second.positionInitial){spawn=item.second.self;break;}
 for(const auto& e:input.events) {
   if(e.kind=="explosion") {
     Hazard h;h.a=e.a;h.b=e.b;h.start=e.start;h.end=e.start+1;h.damage=e.damage;h.respectsProtection=true;out.hazards.push_back(h);
   }else if(e.kind=="split") {
     // Split death only at activation; the wall persists. Own initial spawn is excluded.
     for(int y=0;y<p.mapH;++y)for(int x=0;x<p.mapW;++x)if((x==e.a.x||y==e.a.y)&&(!spawn||Point{x,y}!=*spawn)) {
       Hazard wall;wall.a=wall.b={x,y};wall.start=e.start;wall.end=e.end;wall.wall=true;out.hazards.push_back(wall);
       Hazard death=wall;death.end=e.start+1;death.wall=false;death.lethal=true;out.hazards.push_back(death);
     }
   }else if(e.kind=="inversion") {
     auto activation=history_.find(e.start);
     if(activation!=history_.end()&&activation->second.alive&&e.contains(activation->second.self))out.inverted={e.start,e.start+e.duration};
   }else if(e.kind=="reflect" && e.active(p.tick)) {out.reflect={e.start,e.end};out.reflectDamage=e.damage;
   }else if(e.kind=="vulnerability"||e.kind=="heal") {
     for(const auto& item:history_) {
       const auto& f=item.second;
       if(!f.lastMoved||f.lastActionTick!=f.tick-1)continue; // movement can be followed by chili death; buffs survive
       if(e.kind=="vulnerability" && e.start<=f.lastActionTick && f.lastActionTick<e.triggerEnd)
         out.vulnerable={f.tick,e.end};
       if(e.kind=="heal"&&e.active(f.lastActionTick)&&e.contains(f.self)&&f.lastActionTick+e.duration>out.healUntil){
         // 只有“从点外成功移动进入点内”才算一次入点；在点内来回移动不应刷新。
         const auto prev=history_.find(f.tick-1);
         if(prev==history_.end()||!e.contains(prev->second.self)){
           out.healUntil=f.lastActionTick+e.duration;out.healRate=e.heal;
         }
       }
     }
   }else if(e.kind=="fortress") {
     int dwell=0;bool granted=false;
     for(int t=e.start;t<std::min(p.tick,e.end);++t) {
       auto before=history_.find(t),after=history_.find(t+1);
       if(before==history_.end()||after==history_.end()||!before->second.alive||!after->second.alive||after->second.positionRespawn||!e.contains(after->second.self))dwell=0;
       else if(++dwell>=e.dwell)granted=true;
     }
     if(granted){out.fury={e.start,e.end};out.furyDamage=e.damage;out.furyDefendDamage=e.defendDamage;}
   }
 }
 return out;
}
}
