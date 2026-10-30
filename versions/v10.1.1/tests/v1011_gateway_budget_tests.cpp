// Actual production node/worker with ROS callbacks and HTTP replaced by test doubles.
// This tests request budgets and action continuity, never actual LLM comprehension.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>
#include "rclcpp/rclcpp.hpp"
#include "rmtc_bot9/node.hpp"
#include "rmtc_bot9/llm.hpp"
using J=nlohmann::json;
static std::mutex guard;
static std::vector<J> envelopes;
static int mode=0;
namespace rmtc_bot9 {
bool http_post(const std::string&,const std::string&,const std::string& body,int,
               std::string* response,long* status,const std::atomic<bool>&) {
    {std::lock_guard<std::mutex> lock(guard);envelopes.push_back(J::parse(body));}
    if(mode==2){*status=401;*response=R"({"error":{"code":"invalid_token"}})";return true;}
    *status=200;
    *response=mode==1 ? R"({"choices":[{"message":{"content":"broken-json"}}]})"
                      : R"({"choices":[{"finish_reason":"stop","message":{"content":"{\"events\":[]}"}}]})";
    return true;
}
}
int main() {
    setenv("RMTC_TEAM","A",1);setenv("RMTC_STAGE","2",1);setenv("RMTC_MAX_TICKS","500",1);
    setenv("RMTC_AI_GATEWAY","http://injected.invalid",1);setenv("RMTC_TEAM_TOKEN","test-only",1);
    setenv("RMTC_MODEL","",1);setenv("RMTC_MAX_TOKENS","512",1);setenv("RMTC_DECISION_CONFIG","",1);
    setenv("STUB_LOG","",1);
    J results=J::array();bool ok=true;
    for(mode=0;mode<3;++mode) {
        envelopes.clear();rclcpp::Bus::get().subs.clear();rclcpp::Bus::get().pubs.clear();rclcpp::Bus::get().qos.clear();
        auto node=rmtc_bot9::make_bot9_node();double maxCallbackMs=0;
        for(int t=0;t<500;++t){
            J p={{"tick",t},{"session_id","budget-test"},{"map",{{"width",12},{"height",12}}},
                 {"vision_radius",4},{"self",{{"team","A"},{"position",{5,5}},{"hp",100},{"alive",true}}},
                 {"visible_entities",J::array()},{"score",{{"resource_value",0},{"effective_damage",0}}},
                 {"event_text","test announcement "+std::to_string(t)},{"mission_text","test"}};
            const auto start=std::chrono::steady_clock::now();
            for(const auto& sub:rclcpp::Bus::get().subs)if(sub.first=="/team_A/perception")sub.second(p.dump());
            maxCallbackMs=std::max(maxCallbackMs,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        node.reset();
        auto pub=std::static_pointer_cast<rclcpp::Publisher<std_msgs::msg::String>>(rclcpp::Bus::get().pubs.at("/team_A/action")[0]);
        const bool flags=std::all_of(envelopes.begin(),envelopes.end(),[](const J& b){return b.value("thinking",J())==J{{"type","disabled"}} && b.value("response_format",J())==J{{"type","json_object"}};});
        const bool tokens=std::all_of(envelopes.begin(),envelopes.end(),[](const J& b){return b.at("max_tokens")==512 && !b.contains("model") && b.at("stream")==false;});
        bool ordered=pub->sent.size()==500;
        for(std::size_t i=0;i<pub->sent.size();++i){const auto a=J::parse(pub->sent[i].data);ordered=ordered && a.at("tick")==i && a.at("session_id")=="budget-test";}
        const bool passed=ordered && flags && tokens && !envelopes.empty() && envelopes.size()<=100;
        ok=ok && passed;
        results.push_back({{"response_mode",mode},{"requests",envelopes.size()},{"actions",pub->sent.size()},
            {"request_fields_512_nonstream_default_model",tokens},{"thinking_disabled_and_json_mode",flags},
            {"max_callback_ms_observed",maxCallbackMs},{"ordered_actions",ordered},{"passed",passed}});
    }
    std::cout<<J{{"tests",results},{"passed",ok},{"real_network",false}}.dump(2)<<'\n';return ok?0:1;
}
