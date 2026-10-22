// SHARED: offline public-perception replay, no ROS and no network.
#include "rmtc_bot/runtime.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
int main(int argc,char** argv) {
    if(argc<2 || argc>3) {std::cerr<<"Usage: rmtc_replay perception.json [1|2]\n";return 1;}
    try {
        std::ifstream input(argv[1]); if(!input)throw std::runtime_error("file");
        const std::string text((std::istreambuf_iterator<char>(input)),{});
        rmtc_bot::Config c;
        if(argc==3) {const std::string stage=argv[2];if(stage!="1"&&stage!="2")throw std::runtime_error("stage");c.stage=stage=="1"?1:2;}
        c.max_ticks=c.stage==1?100:500;
        rmtc_bot::BotRuntime runtime(c);
        if(auto result=runtime.handle(text)) {std::cout<<*result<<'\n';return 0;}
        std::cerr<<runtime.reason()<<'\n';return 2;
    }catch(const std::exception&){std::cerr<<"Invalid replay file or stage\n";return 1;}
}
