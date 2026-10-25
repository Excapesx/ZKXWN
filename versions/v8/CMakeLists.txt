cmake_minimum_required(VERSION 3.16)
project(rmtc8_core LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 8.0 决策核心（纯 C++17，无 ROS 依赖）
add_library(rmtc8_core src/rmtc8/types.cpp src/rmtc8/grid.cpp src/rmtc8/parse.cpp src/rmtc8/bot.cpp src/rmtc8/events.cpp src/rmtc8/battle.cpp)
target_include_directories(rmtc8_core PUBLIC src third_party/nlohmann/include)
if(MSVC)
 target_compile_options(rmtc8_core PUBLIC /utf-8 /W4)
else()
 target_compile_options(rmtc8_core PUBLIC -Wall -Wextra)
endif()

# 冻结的 7.0 决策核心（baseline_v7/src/rmtc7，与校内7.0 源码逐字相同），只用于对比测评
add_library(baseline7 STATIC
  baseline_v7/src/rmtc7/types.cpp baseline_v7/src/rmtc7/grid.cpp baseline_v7/src/rmtc7/parse.cpp
  baseline_v7/src/rmtc7/bot.cpp baseline_v7/src/rmtc7/events.cpp baseline_v7/src/rmtc7/battle.cpp)
target_include_directories(baseline7 PUBLIC baseline_v7/src third_party/nlohmann/include)
if(MSVC)
 target_compile_options(baseline7 PUBLIC /utf-8)
endif()

enable_testing()
if(NOT WIN32)
 find_package(Threads REQUIRED)
endif()

add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests rmtc8_core)
add_test(NAME core_regression COMMAND core_tests)

add_executable(llm_worker_tests tests/llm_worker_tests.cpp ros2_ws/src/rmtc_bot8/src/llm.cpp)
target_include_directories(llm_worker_tests PRIVATE ros2_ws/src/rmtc_bot8/include third_party/nlohmann/include)
if(NOT WIN32)
 target_link_libraries(llm_worker_tests PRIVATE Threads::Threads)
endif()
if(MSVC)
 target_compile_options(llm_worker_tests PRIVATE /utf-8 /W4)
endif()
add_test(NAME llm_session_lifecycle COMMAND llm_worker_tests)

add_executable(v4_tests tests/v4_tests.cpp tools/bench/referee.cpp)
target_include_directories(v4_tests PRIVATE tools/bench)
target_link_libraries(v4_tests rmtc8_core)
add_test(NAME v4_rules_boundaries COMMAND v4_tests)

add_executable(v6_tests tests/v6_tests.cpp tools/bench/referee.cpp)
target_include_directories(v6_tests PRIVATE tools/bench)
target_link_libraries(v6_tests rmtc8_core)
add_test(NAME v6_physical_scenarios COMMAND v6_tests)

add_executable(v7_tests tests/v7_tests.cpp)
target_link_libraries(v7_tests rmtc8_core)
add_test(NAME v7_contracts COMMAND v7_tests)

add_executable(v8_tests tests/v8_tests.cpp tools/bench/referee.cpp)
target_include_directories(v8_tests PRIVATE tools/bench)
target_link_libraries(v8_tests rmtc8_core)
add_test(NAME v8_new_contracts COMMAND v8_tests)
set_tests_properties(core_regression llm_session_lifecycle v4_rules_boundaries v6_physical_scenarios v7_contracts v8_new_contracts
                     PROPERTIES TIMEOUT 60)

add_executable(rmtc_replay tools/replay_main.cpp)
target_link_libraries(rmtc_replay PRIVATE rmtc8_core)

add_executable(rmtc_smoke tools/smoke_main.cpp tools/bench/referee.cpp)
target_include_directories(rmtc_smoke PRIVATE tools/bench)
target_link_libraries(rmtc_smoke rmtc8_core)

# 测评工具：8.0 与冻结 7.0（或贪心、冲锋、只采集、掉线等对手）在同一离线裁判中比较
add_executable(rmtc_arena tools/bench/bench_main.cpp tools/bench/referee.cpp tools/bench/v7_adapter.cpp)
target_include_directories(rmtc_arena PRIVATE tools/bench)
target_link_libraries(rmtc_arena rmtc8_core baseline7)
if(NOT WIN32)
 target_link_libraries(rmtc_arena Threads::Threads)
endif()

option(RMTC_BUILD_HTTP "Compile the production libcurl transport (requires CURL development package)" OFF)
if(RMTC_BUILD_HTTP)
 find_package(CURL REQUIRED)
 add_library(rmtc8_http ros2_ws/src/rmtc_bot8/src/curl_transport.cpp)
 target_include_directories(rmtc8_http PUBLIC ros2_ws/src/rmtc_bot8/include)
 target_link_libraries(rmtc8_http PUBLIC CURL::libcurl Threads::Threads)
endif()
