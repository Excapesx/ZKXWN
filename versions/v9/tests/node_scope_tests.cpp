// Production node.cpp + llm.cpp; fake ROS callbacks and in-process HTTP only.
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
static std::mutex mutex;
static std::vector<J> requests;
static int checks=0,failures=0;
static void check(bool yes,const char* name){++checks;if(!yes){++failures;std::cout<<"FAIL "<<name<<'\n';}}
namespace rmtc_bot9 {
bool http_post(const std::string&,const std::string&,const std::string& body,int,std::string* response,long* status,const std::atomic<bool>&){
 auto envelope=J::parse(body);auto user=J::parse(envelope["messages"][1]["content"].get<std::string>());
 {std::lock_guard<std::mutex> lock(mutex);requests.push_back(user.at("request"));}
 *status=200;*response=R"({"choices":[{"message":{"content":"{\"events\":[]}"}}]})";return true;
}
}
static void env(const char* key,const char* value){
#ifdef _WIN32
 _putenv_s(key,value);
#else
 setenv(key,value,1);
#endif
}
static void emit(const std::string& topic,const std::string& data){
 for(const auto& sub:rclcpp::Bus::get().subs)if(sub.first==topic)sub.second(data);
 std::this_thread::sleep_for(std::chrono::milliseconds(15));
}
static void perception(const char* sid,int tick,const std::string& events="",const std::string& missions=""){
 J p={{"tick",tick},{"session_id",sid},{"map",{{"width",12},{"height",12}}},{"vision_radius",4},
 {"self",{{"team","A"},{"position",{5,5}},{"hp",100},{"alive",true}}},{"visible_entities",J::array()},
 {"score",{{"resource_value",0},{"effective_damage",0}}},{"event_text",events},{"mission_text",missions}};
 emit("/team_A/perception",p.dump());
}
static bool await_request(const std::string& sid){
 for(int attempt=0;attempt<200;++attempt){
  {std::lock_guard<std::mutex> lock(mutex);for(const auto& q:requests)if(q.value("session_id","")==sid)return true;}
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
 }return false;
}
static void reset_bus(){rclcpp::Bus::get().subs.clear();rclcpp::Bus::get().pubs.clear();rclcpp::Bus::get().qos.clear();requests.clear();}
static void trial(bool late){
 reset_bus();auto node=rmtc_bot9::make_bot9_node();
 perception("old",0,"OLD-EVENT","OLD-MISSION");check(await_request("old"),"old session mock request completes");
 perception("old",1,"OLD-EVENT","OLD-MISSION");
 perception("new",0);check(await_request("new"),"new session has its own mock request");
 if(late){emit("/referee/event_text","OLD-EVENT");emit("/referee/mission","OLD-MISSION");}
 for(int t=1;t<=12;++t){
  if(t==3)perception("old",2,"OLD-LATE-PERCEPTION","OLD-MISSION");
  perception("new",t,t>=4?"NEW-EVENT":"",t>=4?"NEW-MISSION":"");
 }
 node.reset(); // joins the worker before inspecting the final captured requests
 bool sawNewEvent=false,sawNewMission=false;int newCount=0;
 for(const auto& q:requests)if(q.value("session_id","")=="new"){
  ++newCount;check(q.dump().find("OLD-")==std::string::npos,"old topic/perception text never enters the new request");
  check(q.value("stage",0)==2,"request carries stage");
  check(q.value("announcement_source","")=="accepted_perception.event_text","request identifies authoritative text source");
  sawNewEvent|=q["announcements"].dump().find("NEW-EVENT")!=std::string::npos;
  sawNewMission|=q["mission_text"]=="NEW-MISSION";
 }
 check(newCount>=2,"new session plans after an accepted new announcement");
 check(sawNewEvent&&sawNewMission,"legitimate current-session event and mission remain usable");
 auto& pubs=rclcpp::Bus::get().pubs.at("/team_A/action");
 auto pub=std::static_pointer_cast<rclcpp::Publisher<std_msgs::msg::String>>(pubs[0]);
 bool enteredNew=false;for(const auto& msg:pub->sent){auto a=J::parse(msg.data);if(a["session_id"]=="new")enteredNew=true;
  if(enteredNew)check(a["session_id"]=="new","late old perception produces no stale-session action");}
 reset_bus();
}
int main(){
 env("RMTC_TEAM","A");env("RMTC_STAGE","2");env("RMTC_MAX_TICKS","500");env("RMTC_DECISION_CONFIG","");
 env("RMTC_TEAM_TOKEN","in-process-test-only");env("RMTC_AI_GATEWAY","http://injected.invalid");
 env("RMTC_MAX_TOKENS","0");env("RMTC_HTTP_TIMEOUT_MS","15000");env("RMTC_TEAM_NAME","HERO-9.0-test");
 env("RMTC_MODEL","");env("STUB_LOG","");
 trial(false);trial(true);
 std::cout<<checks<<" checks, "<<failures<<" failures (ROS stub, injected HTTP, no network)\n";
 return failures?1:0;
}
