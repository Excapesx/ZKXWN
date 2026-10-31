#include "rmtc_bot9/llm.hpp"
#include <cstdlib>
#include <iostream>
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    std::atomic<bool> stop{false};std::string response;long status=0;
    bool ok=rmtc_bot9::http_post(argv[1],"probe-token",R"({"probe":true})",std::atoi(argv[2]),&response,&status,stop);
    std::cout<<(ok?1:0)<<' '<<status<<'\n';
}
