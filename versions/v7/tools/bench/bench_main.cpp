#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "opponent_adapter.hpp"
#include "referee.hpp"
#include "rmtc7/bot.hpp"
using Json=nlohmann::json;
namespace {
std::map<std::string,int> reasons;
long long allInvalid=0;
std::ofstream trace;int traceBudget=550;
class V7Policy final:public IPolicy {
 rmtc7::Bot bot;
 public: V7Policy(int stage,int ticks){bot.configure({stage,ticks});}
 rmtc7::Action act(const rmtc7::Perception& p) override {auto d=bot.decide(p);++reasons[d.reason];
 if(trace && traceBudget-->0){Json es=Json::array();for(const auto& e:p.entities)if(e.type==rmtc7::EntityType::Robot)es.push_back({{"team",e.team},{"position",{e.pos.x,e.pos.y}},{"hp",e.hp}});
 trace<<Json{{"tick",p.tick},{"self",{p.self.x,p.self.y}},{"hp",p.hp},{"damage",p.effectiveDamage},{"resource",p.resourceValue},{"reason",d.reason},{"action",static_cast<int>(d.action.kind)},{"dir",{d.action.dx,d.action.dy}},{"enemies",es}}.dump()<<'\n';}
 return d.action;}
 const char* name()const override{return "v7";}
};
class GreedyBot final : public IPolicy {
public:
    rmtc7::Action act(const rmtc7::Perception& p) override {
        mem_.update(p);
        mask_.reset(mem_.width(), mem_.height());
        for (const auto& w : mem_.walls()) mask_.set(w, true);
        for (const auto& e : p.entities) {
            if (e.type == rmtc7::EntityType::Robot && e.hp > 0) mask_.set(e.pos, true);
        }
        field_ = rmtc7::bfs(mask_, p.self, 600);
        if (!p.alive) return rmtc7::Action::wait();
        for (int dir = 0; !p.protectedSpawn && dir < 4; ++dir) {
            for (const auto& e : p.entities) {
                if (e.type != rmtc7::EntityType::Robot || e.hp <= 0) continue;
                if (e.pos.x - p.self.x == rmtc7::kDirX[dir] && e.pos.y - p.self.y == rmtc7::kDirY[dir]) {
                    return rmtc7::Action::attack(rmtc7::kDirX[dir], rmtc7::kDirY[dir]);
                }
            }
        }
        int bestDist = 1 << 20;
        rmtc7::Point goal{0, 0};
        bool found = false;
        for (const auto& kv : mem_.resources()) {
            const int d = field_.at(kv.second.pos);
            if (d > 0 && d < bestDist) {
                bestDist = d;
                goal = kv.second.pos;
                found = true;
            }
        }
        if (!found) {
            for (const auto& c : mem_.frontiers()) {
                const int d = field_.at(c);
                if (d > 0 && d < bestDist) {
                    bestDist = d;
                    goal = c;
                    found = true;
                }
            }
        }
        if (!found) return rmtc7::Action::wait();
        const int8_t dir = field_.firstAt(goal);
        if (dir < 0) return rmtc7::Action::wait();
        return rmtc7::Action::move(rmtc7::kDirX[dir], rmtc7::kDirY[dir]);
    }
    const char* name() const override { return "greedy"; }

private:
    rmtc7::WorldMap mem_;
    rmtc7::BlockMask mask_;
    rmtc7::Field field_;
};


std::unique_ptr<IPolicy> make(char version,int stage,int ticks){
 if(version=='7')return std::make_unique<V7Policy>(stage,ticks);
 if(version=='6')return make_v6_bot(stage,ticks);
  if(version=='g')return std::make_unique<GreedyBot>();
 throw std::runtime_error("policy must be 6,7,g");
}
struct Options {int stage=2,maps=60,robots=4,w=24,h=24,vision=4,rotations=12;unsigned seed=71000;char a='7',b='6',other='g';std::string output;bool legacy=false;};
Options parse(int argc,char** argv){
 Options o;
 for(int i=1;i<argc;++i){std::string key=argv[i];if(key=="--legacy"){o.legacy=true;continue;}
  if(i+1>=argc)throw std::runtime_error("missing argument");std::string value=argv[++i];
  if(key=="--stage")o.stage=std::stoi(value);else if(key=="--matches")o.maps=std::stoi(value);
  else if(key=="--robots")o.robots=std::stoi(value);else if(key=="--width")o.w=std::stoi(value);else if(key=="--height")o.h=std::stoi(value);
  else if(key=="--vision")o.vision=std::stoi(value);else if(key=="--rotations")o.rotations=std::stoi(value);
  else if(key=="--seed")o.seed=static_cast<unsigned>(std::stoul(value));else if(key=="--output")o.output=value;
  else if(key=="--trace"){trace.open(value);if(!trace)throw std::runtime_error("trace open");}
  else if(key=="--a")o.a=value.at(0);else if(key=="--b")o.b=value.at(0);else if(key=="--others")o.other=value.at(0);
  else throw std::runtime_error("unknown argument: "+key);
 }
 if((o.stage!=1&&o.stage!=2)||(o.robots!=2&&o.robots!=4)||o.maps<1||o.maps>2000||o.w<8||o.w>64||o.h<8||o.h>64||o.vision<0||o.vision>4||(o.rotations!=2&&o.rotations!=12))throw std::runtime_error("argument out of bounds");
 make(o.a,o.stage,100);make(o.b,o.stage,100);make(o.other,2,500);return o;
}
struct Sample {double score=0,resource=0,damage=0;int missions=0,deaths=0,kills=0,invalid=0;};
Json encode(const Sample& s){return {{"score",s.score},{"resource",s.resource},{"damage",s.damage},{"missions",s.missions},{"deaths",s.deaths},{"kills",s.kills},{"invalid",s.invalid}};}
double mean(const std::vector<double>& a){return a.empty()?0:std::accumulate(a.begin(),a.end(),0.0)/a.size();}
double quantile(std::vector<double> a,double q){if(a.empty())return 0;std::sort(a.begin(),a.end());return a[static_cast<std::size_t>(q*(a.size()-1))];}
std::vector<Sample> match(const bench::MapDef& map,int ticks,const std::vector<char>& versions,bool legacy,std::vector<std::vector<double>>& times){
 bench::Referee ref;ref.reset(map,ticks);std::vector<std::unique_ptr<IPolicy>> bots;
 for(auto v:versions)bots.push_back(make(v,map.missions.empty()?1:2,ticks));
 for(int t=0;t<ticks;++t){for(int i=0;i<ref.robot_count();++i){auto p=ref.perceive(i);
   if(legacy){p.lastActionTick=-1;p.lastMoved=false;p.positionRespawn=false;}
   auto begin=std::chrono::steady_clock::now();auto action=bots[i]->act(p);
   times[i].push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-begin).count());
   ref.submit(i,action);
  }ref.step();}
 std::vector<Sample> result;
 for(int i=0;i<ref.robot_count();++i){auto r=ref.robots()[i];allInvalid+=r.invalid;result.push_back({ref.score(i),r.resourceValue,r.damage,r.missionsDone,r.deaths,r.kills,r.invalid});}
 return result;
}
Json summarize(const std::vector<Sample>& ss,const std::vector<double>& times){
 Json j;for(auto key:{"score","resource","damage","missions","deaths","kills","invalid"}){std::vector<double> values;for(const auto& s:ss)values.push_back(encode(s)[key].get<double>());j[key]=mean(values);}
 j["decision_us"]={{"p50",quantile(times,0.5)},{"p99",quantile(times,0.99)},{"max",times.empty()?0:*std::max_element(times.begin(),times.end())}};return j;
}
}
int main(int argc,char** argv){try{
 auto o=parse(argc,argv);std::ofstream log;if(!o.output.empty()){log.open(o.output,std::ios::binary);if(!log)throw std::runtime_error("cannot open output");}
 std::vector<Sample> as,bs;std::vector<double> ta,tb,mapDiff;int wins=0,draws=0,losses=0;
 for(int m=0;m<o.maps;++m){auto map=bench::generate_map(o.w,o.h,o.stage==1?1:o.robots,o.seed+m,o.stage);map.vision=o.vision;std::vector<double> thisDiff;
  std::vector<std::pair<int,int>> seats;
  if(o.stage==1)seats={{0,0}};
  else if(o.robots==2||o.rotations==2)seats={{0,1},{1,0}};
  else for(int a=0;a<4;++a)for(int b=0;b<4;++b)if(a!=b)seats.push_back({a,b});
  for(auto pair:seats){std::vector<std::vector<double>> times(o.robots);Sample a,b;
   if(o.stage==1){auto r=match(map,100,{o.a},o.legacy,times);a=r[0];ta.insert(ta.end(),times[0].begin(),times[0].end());times.assign(o.robots,{});r=match(map,100,{o.b},o.legacy,times);b=r[0];tb.insert(tb.end(),times[0].begin(),times[0].end());}
   else {std::vector<char> versions(o.robots,o.other);versions[pair.first]=o.a;versions[pair.second]=o.b;auto r=match(map,500,versions,o.legacy,times);a=r[pair.first];b=r[pair.second];ta.insert(ta.end(),times[pair.first].begin(),times[pair.first].end());tb.insert(tb.end(),times[pair.second].begin(),times[pair.second].end());}
   as.push_back(a);bs.push_back(b);double diff=a.score-b.score;thisDiff.push_back(diff);
   if(diff>1e-8)++wins;else if(diff< -1e-8)++losses;else ++draws;
   if(log)log<<Json{{"seed",o.seed+m},{"a_seat",pair.first},{"b_seat",pair.second},{"a",encode(a)},{"b",encode(b)},{"difference",diff}}.dump()<<'\n';
  }mapDiff.push_back(mean(thisDiff));
 }
 // Cluster bootstrap over maps, never treating rotations as independent maps.
 std::mt19937 random(932001);std::uniform_int_distribution<std::size_t> pick(0,mapDiff.size()-1);std::vector<double> bootstrap;
 for(int sample=0;sample<2000;++sample){double sum=0;for(std::size_t i=0;i<mapDiff.size();++i)sum+=mapDiff[pick(random)];bootstrap.push_back(sum/mapDiff.size());}
 Json out={{"all_policy_invalid_total",allInvalid},{"stage",o.stage},{"seed",o.seed},{"maps",o.maps},{"games",as.size()},{"robots",o.robots},{"width",o.w},{"height",o.h},{"vision",o.vision},{"other_policy",std::string(1,o.other)},{"a_version",std::string(1,o.a)},{"b_version",std::string(1,o.b)},{"legacy_receipts",o.legacy},{"a",summarize(as,ta)},{"b",summarize(bs,tb)},{"wins",wins},{"draws",draws},{"losses",losses},{"win_rate",double(wins)/as.size()},{"mean_difference",mean(mapDiff)},{"map_bootstrap_95",{quantile(bootstrap,.025),quantile(bootstrap,.975)}},{"v6_reasons",reasons}};
 std::cout<<out.dump(2)<<'\n';return 0;
 }catch(const std::exception& e){std::cerr<<"arena: "<<e.what()<<'\n';return 2;}}
