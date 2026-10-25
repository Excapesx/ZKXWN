#include "rmtc6/battle.hpp"
#include <algorithm>
#include <cmath>
namespace rmtc6 {
namespace {
int direction(Point a,Point b){
 if(a.x==b.x&&b.y<a.y)return 0;if(a.y==b.y&&b.x>a.x)return 1;
 if(a.x==b.x&&b.y>a.y)return 2;if(a.y==b.y&&b.x<a.x)return 3;return -1;
}
struct Actor{Entity e;int hp;bool defending;int dir=-1;};
int nearest(const std::vector<Actor>& a,int from,int dir){
 int nearest=-1,dist=3;
 for(int j=0;j<static_cast<int>(a.size());++j)if(j!=from&&a[j].hp>0&&a[from].e.team!=a[j].e.team){
  int d=manhattan(a[from].e.pos,a[j].e.pos);
  if(d>0&&d<dist&&direction(a[from].e.pos,a[j].e.pos)==dir){nearest=j;dist=d;}
 }return nearest;
}
}
BattleForecast forecast_battle(const Perception& p,const Plan& plan,const HazardField& hz,
                               const std::vector<const Entity*>& targets,BattleMode mode){
 std::vector<Actor> actors;Entity self;self.type=EntityType::Robot;self.team=p.team;self.pos=p.self;self.hp=p.hp;self.protectedSpawn=p.protectedSpawn;
 auto health=[&](const Entity& e){return hz.lethalAt(e.pos,p.tick,e.protectedSpawn)?0:std::max(0,e.hp-hz.damageAt(e.pos,p.tick,e.protectedSpawn));};
 actors.push_back({self,health(self),false});
 for(const auto& e:p.entities)if(e.type==EntityType::Robot&&e.team!=p.team&&e.hp>0&&actors.size()<4){
  bool duplicate=false;for(const auto& a:actors)if(a.e.team==e.team)duplicate=true;
  if(!duplicate)actors.push_back({e,health(e),e.defending});
 }
 // Each enemy chooses ONE action in a scenario. It cannot attack us and a
 // shared victim at the same time, unlike the previous incompatible bounds.
 if(mode!=BattleMode::Hold)for(int i=1;i<static_cast<int>(actors.size());++i){
  auto& actor=actors[i];if(actor.hp<=0||actor.e.protectedSpawn)continue;
  int best=-1;double value=-1;
  for(int dir=0;dir<4;++dir){int j=nearest(actors,i,dir);if(j<0||actors[j].e.protectedSpawn)continue;
   if(mode==BattleMode::FocusSelf){if(j==0){best=dir;break;}continue;}
   double profit=std::min(20,actors[j].hp)+(actors[j].hp<=20?60:0);
   if(profit>value){value=profit;best=dir;}
  }
  if(best>=0){actor.dir=best;actor.defending=false;}
 }
 std::vector<std::vector<DamageClaim>> claims(actors.size());
 auto hit=[&](int from,int victim,int base){
  if(actors[from].hp<=0||actors[from].e.protectedSpawn||actors[victim].hp<=0||actors[victim].e.protectedSpawn)return;
  if(plan.reflect.active(p.tick)&&actors[victim].defending){
   claims[from].push_back({actors[victim].e.team,static_cast<double>(plan.reflectDamage),false});return;
  }
  int damage=base;if(victim==0&&plan.vulnerable.active(p.tick))damage*=2;
  claims[victim].push_back({actors[from].e.team,static_cast<double>(damage),true});
 };
 if(actors[0].hp>0&&!p.protectedSpawn)for(const Entity* target:targets){
  for(int j=1;j<static_cast<int>(actors.size());++j)if(actors[j].e.team==target->team){
   const bool fury=plan.fury.active(p.tick);int damage=fury?(actors[j].defending?plan.furyDefendDamage:plan.furyDamage):(actors[j].defending?10:20);
   hit(0,j,damage);
  }
 }
 for(int i=1;i<static_cast<int>(actors.size());++i)if(actors[i].dir>=0){
  int j=nearest(actors,i,actors[i].dir);if(j>=0)hit(i,j,actors[j].defending?10:20);
 }
 BattleForecast out;out.fatal=actors[0].hp<=0;
 for(int j=0;j<static_cast<int>(actors.size());++j){
  if(actors[j].hp<=0)continue;const auto result=resolve_damage_batch(actors[j].hp,claims[j]);
  if(j==0){out.fatal=result.killed;out.hpAfter=result.hp_after;}
  auto reward=result.awards.find(p.team);if(reward!=result.awards.end()){
   out.credit+=reward->second.attack_credit+reward->second.kill_credit;
   if(result.killed&&j!=0)out.kills+=1;
  }
 }
 return out;
}
}

