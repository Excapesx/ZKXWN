// Local stage-1 closed-loop test environment. Hidden map stays here; BotRuntime
// receives only the public JSON perception, exactly as in the ROS adapter.
#include "rmtc_bot/runtime.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <queue>
#include <set>
#include <stdexcept>

using J = nlohmann::json;
using Point = std::pair<int,int>;

static std::string relative(Point delta) {
    std::string text;
    for(int y=-4;y<=4;++y) {
        if(y!=-4) text+='\n';
        for(int x=-4;x<=4;++x) text+=(x==0&&y==0)?'1':(Point{x,y}==delta?'2':'0');
    }
    return text;
}
static Point point(const J& p) { return {p.at(0).get<int>(),p.at(1).get<int>()}; }
static J run(const J& entry, int ticks, rmtc_bot::BotRuntime& bot) {
    const auto& cfg=entry.at("config"); const auto& map=cfg.at("map");
    const int w=map.at("width"), h=map.at("height"), radius=cfg.at("vision_radius");
    const std::string session="multimap-"+entry.at("name").get<std::string>();
    Point pos=point(cfg.at("robots").at(0).at("spawn"));
    const auto inside=[&](Point p){return p.first>=0&&p.second>=0&&p.first<w&&p.second<h;};
    const auto index=[&](Point p){return p.second*w+p.first;};
    std::set<Point> walls, visited;
    for(const auto& p:map.at("obstacles"))walls.insert(point(p));
    auto resources=map.at("resources");
    // Test-only upper bound: sum values of resources individually reachable in
    // <= ticks from spawn. Not an optimal collection-tour score.
    std::vector<int> distances(w*h,-1);distances[index(pos)]=0;
    std::queue<Point> q;q.push(pos);
    constexpr Point deltas[]={{0,-1},{1,0},{0,1},{-1,0}};
    while(!q.empty()) {
        auto p=q.front();q.pop();
        for(auto d:deltas) {
            Point n{p.first+d.first,p.second+d.second};
            if(!inside(n)||walls.count(n)||distances[index(n)]>=0)continue;
            distances[index(n)]=distances[index(p)]+1;q.push(n);
        }
    }
    double upper=0,total=0;
    for(const auto& r:resources) {
        double v=r.at("value");total+=v;const auto d=distances[index(point(r.at("position")))];
        if(d>=0&&d<=ticks)upper+=v;
    }
    double value=0; int blocked=0,invalid=0,missing=0,bad_context=0,receipt_errors=0,waits=0;
    J receipt=nullptr,trace=J::array(); std::vector<double> times;
    for(int tick=0;tick<=ticks;++tick) {
        visited.insert(pos);
        J entities=J::array();
        const auto visible=[&](Point p){return std::abs(p.first-pos.first)<=radius&&std::abs(p.second-pos.second)<=radius;};
        for(auto p:walls)if(visible(p))entities.push_back({{"type","obstacle"},{"position",relative({p.first-pos.first,p.second-pos.second})}});
        for(auto r:resources) {
            auto p=point(r.at("position"));if(!visible(p))continue;
            r["type"]=r["value"].get<double>()>=20?"resource_high":"resource_normal";
            r["position"]=relative({p.first-pos.first,p.second-pos.second});entities.push_back(r);
        }
        J self={{"team","A"},{"position",{pos.first,pos.second}},{"hp",100},{"alive",true},
                {"protected",false},{"respawn_in",0},{"last_action",receipt}};
        if(tick==0)self["position_reason"]="initial";
        J perception={{"tick",tick},{"session_id",session},{"vision_radius",radius},{"self",self},
                      {"visible_entities",entities},{"score",{{"resource_value",value},{"effective_damage",0},
                      {"mission_completed",0},{"mission_completed_ids",J::array()},{"invalid_actions",invalid}}},
                      {"missions",J::array()},{"mission_text","练习采集：自主收集资源。"},{"event_text",""},
                      {"map",{{"width",w},{"height",h}}}};
        const auto start=std::chrono::steady_clock::now();auto command=bot.handle(perception.dump());
        const double us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
        if(tick==ticks) {if(command)++bad_context;break;}
        times.push_back(us);
        if(tick>0 && (!bot.last_receipt()||bot.last_receipt()->tick!=tick-1||bot.last_receipt()->moved!=receipt.value("moved",false)))++receipt_errors;
        J action={{"action","wait"},{"tick",tick},{"session_id",session}};
        if(command)action=J::parse(*command);else ++missing;
        if(action.value("tick",-1)!=tick||action.value("session_id",std::string{})!=session)++bad_context;
        receipt={{"tick",tick},{"action",action.value("action",std::string{})},{"moved",false}};
        const auto before=pos; const auto kind=action.value("action",std::string{});
        if(kind=="move") {
            const auto dir=action.value("direction",std::string{});Point d;
            if(dir=="up")d={0,-1};else if(dir=="right")d={1,0};else if(dir=="down")d={0,1};else if(dir=="left")d={-1,0};
            else {++invalid;receipt["error"]="E_SCHEMA";}
            if(!receipt.contains("error")) {
                Point n{pos.first+d.first,pos.second+d.second};
                if(!inside(n)||walls.count(n)){++blocked;receipt["error"]=inside(n)?"E_BLOCKED":"E_OOB";}
                else {pos=n;receipt["moved"]=true;receipt["effective_direction"]=dir;}
            }
        } else if(kind=="wait")++waits;else {++invalid;receipt["error"]="E_STATE";}
        for(auto it=resources.begin();it!=resources.end();) {
            if(point(it->at("position"))==pos){value+=it->at("value").get<double>();it=resources.erase(it);}else ++it;
        }
        trace.push_back({{"tick",tick},{"from",{before.first,before.second}},{"to",{pos.first,pos.second}},
                         {"action",action},{"reason",bot.reason()},{"resource_value",value},{"callback_us",us}});
    }
    auto sorted=times;std::sort(sorted.begin(),sorted.end());
    const bool met=entry.at("minimum_value").is_null()||value>=entry.at("minimum_value").get<double>();
    const bool pass=met&&invalid==0&&missing==0&&bad_context==0&&receipt_errors==0;
    return {{"name",entry.at("name")},{"pass",pass},{"minimum_met",met},{"minimum_value",entry.at("minimum_value")},
            {"width",w},{"height",h},{"vision_radius",radius},{"value",value},{"total_value",total},
            {"individually_reachable_value_upper_bound",upper},{"invalid_actions",invalid},{"blocked_moves",blocked},
            {"missing_actions",missing},{"bad_context",bad_context},{"receipt_errors",receipt_errors},
            {"visited_cells",visited.size()},{"waits",waits},
            {"callback_us_p95",sorted.at(sorted.size()*95/100)},{"callback_us_max",sorted.back()},{"trace",trace}};
}
int main(int argc,char** argv) {
    if(argc<2||argc>3){std::cerr<<"Usage: rmtc_multimap suite.json [report.json]\n";return 2;}
    try {
        std::ifstream input(argv[1]);J suite;input>>suite;J results=J::array();
        rmtc_bot::Config config;config.stage=1;config.max_ticks=suite.at("max_ticks");
        rmtc_bot::BotRuntime bot(config);int failed=0;
        for(const auto& entry:suite.at("cases")) {
            auto result=run(entry,config.max_ticks,bot);if(!result.at("pass").get<bool>())++failed;
            std::cout<<(result.at("pass").get<bool>()?"PASS ":"FAIL ")<<entry.at("name").get<std::string>()
                     <<" value="<<result.at("value")<<'/'<<result.at("total_value")<<" blocked="<<result.at("blocked_moves")<<'\n';
            results.push_back(std::move(result));
        }
        J report={{"kind","local_closed_loop_not_official_referee"},{"cases",results},{"failed",failed}};
        if(argc==3){std::ofstream output(argv[2]);if(!output)throw std::runtime_error("Cannot write report");output<<report.dump(2)<<'\n';}
        std::cout<<results.size()<<" maps, "<<failed<<" failed\n";return failed?1:0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 2;}
}
