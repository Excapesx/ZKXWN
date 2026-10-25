#include <iostream>
#include <chrono>
#include <algorithm>
#include "referee.hpp"
#include "rmtc7/bot.hpp"
#include "rmtc7/frame_gate.hpp"
int main(){
 using namespace rmtc7;long decisions=0;int invalid=0;std::vector<double> times;
 for(int stage=1;stage<=2;++stage){double score=0;int count=stage==1?1:4;
   for(int seed=0;seed<12;++seed){auto map=bench::generate_map(stage==1?20:24,stage==1?20:24,count,20261024U+seed,stage);bench::Referee ref;ref.reset(map,stage==1?100:500);
     std::vector<Bot> bots(count);for(auto& b:bots)b.configure({stage,stage==1?100:500});
     for(int tick=0;tick<(stage==1?100:500);++tick){
       for(int i=0;i<count;++i){auto p=ref.perceive(i);auto start=std::chrono::steady_clock::now();auto d=bots[i].decide(p);times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());++decisions;
         auto action=sanitize_action(d.action,p,stage);if(!action.legal_shape())++invalid;ref.submit(i,action);}
       ref.step();
     }
     for(int i=0;i<count;++i){invalid+=ref.robots()[i].invalid;score+=ref.score(i);}
   }
   std::cout<<"stage="<<stage<<" maps=12 self_play_robots="<<count<<" mean_score="<<score/(12*count)<<"\n";
 }
 std::sort(times.begin(),times.end());
 std::cout<<"decisions="<<decisions<<" invalid="<<invalid<<" p99_ms="<<times[static_cast<std::size_t>(times.size()*0.99)]<<" max_ms="<<times.back()<<"\n";
 std::cout<<"PRIVATE NO-EVENT SIMULATOR. NOT official referee, win-rate proof, real-time ROS, or AI qualification.\n";
 return invalid?1:0;
}
