#include "rmtc_bot3/llm.hpp"
#include <cstdio>
#include <mutex>
#include <condition_variable>
using namespace rmtc_bot3;
int main() {
    int checks=0,failed=0;
    auto check=[&](bool b,const char* text){++checks;if(!b){++failed;std::printf("FAIL %s\n",text);}};
    std::mutex mutex; std::condition_variable cv;
    bool entered=false,release=false;int calls=0;
    LlmWorker worker([&](const std::string& endpoint,const std::string&,const std::string&,int,
                         std::string* response,long* status,const std::atomic<bool>&)->bool {
        std::unique_lock<std::mutex> lock(mutex);
        ++calls;
        if(calls==1){entered=true;cv.notify_all();cv.wait(lock,[&]{return release;});}
        if(endpoint!="http://localhost:8080/v1/chat/completions") return false;
        *status=200;*response=R"({"choices":[{"message":{"content":"{\"hazards\":[]}"}}]})";
        return true;
    });
    worker.configure("http://localhost:8080/","fixture-token","",0,500);
    worker.start();worker.reset_session("old");worker.submit("x",400,"old");
    {std::unique_lock<std::mutex> lock(mutex);
     check(cv.wait_for(lock,std::chrono::seconds(2),[&]{return entered;}),"old request starts");}
    worker.reset_session("new");worker.submit("x",0,"new");
    {std::lock_guard<std::mutex> lock(mutex);release=true;}cv.notify_all();
    LlmResult result;bool got=false;
    for(int i=0;i<2000 && !got;++i){got=worker.poll(&result);if(!got)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(got && result.session=="new" && result.sourceTick==0,"old in-flight result discarded");
    check(got && result.httpSuccess && result.valid,"new session response accepted");
    check(worker.successes()==1,"only new session increments counter");
    worker.reset_session("third");check(worker.successes()==0,"success count resets per match");
    worker.stop();
    LlmWorker blocked([](const std::string&,const std::string&,const std::string&,int,std::string* r,long* s,const std::atomic<bool>&){*s=401;*r="{}";return true;});
    blocked.configure("http://localhost","fixture-token","",0,500);blocked.start();blocked.reset_session("s1");blocked.submit("x",0,"s1");
    for(int i=0;i<2000 && !blocked.hard_blocked();++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(blocked.hard_blocked(),"401 stops further requests");
    blocked.reset_session("s2");check(!blocked.hard_blocked(),"new match releases old authentication block");blocked.stop();
    LlmWorker invalid([](const std::string&,const std::string&,const std::string&,int,std::string*,long*,const std::atomic<bool>&)->bool{throw std::runtime_error("injected transport failure");});
    invalid.configure("http://localhost/v1","fixture-token","",0,500);invalid.start();invalid.reset_session("s");invalid.submit(std::string(1,static_cast<char>(0xff)),0,"s");
    got=false;for(int i=0;i<2000&&!got;++i){got=invalid.poll(&result);if(!got)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(got&&!result.valid&&invalid.status()=="invalid_payload","invalid UTF8 cannot terminate worker");
    invalid.submit("valid",1,"s");got=false;
    for(int i=0;i<3000&&!got;++i){got=invalid.poll(&result);if(!got)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(got&&!result.httpSuccess,"transport exception bounded and reported");invalid.stop();
    std::atomic<bool> started{false};
    LlmWorker cancel([&](const std::string&,const std::string&,const std::string&,int,std::string*,long*,const std::atomic<bool>& stop)->bool{started=true;while(!stop.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));return false;});
    cancel.configure("http://localhost","fixture-token","",0,500);cancel.start();cancel.reset_session("old");cancel.submit("x",0,"old");
    for(int i=0;i<2000&&!started;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(started,"cancellable request starts");auto begin=std::chrono::steady_clock::now();cancel.stop();
    check(std::chrono::steady_clock::now()-begin<std::chrono::seconds(1),"stop cancels and joins active transfer");
    std::printf("%d checks, %d failures (mock transport, no real model)\n",checks,failed);
    return failed?1:0;
}
