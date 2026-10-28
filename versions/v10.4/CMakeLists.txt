cmake_minimum_required(VERSION 3.16)
project(rmtc9_core VERSION 10.4.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 9.0 decision core (C++17, no ROS dependency).
add_library(rmtc9_core src/rmtc9/types.cpp src/rmtc9/grid.cpp src/rmtc9/parse.cpp src/rmtc9/bot.cpp src/rmtc9/events.cpp src/rmtc9/battle.cpp)
target_include_directories(rmtc9_core PUBLIC src third_party/nlohmann/include)
set(RMTC_ABLATION_LEVEL "2" CACHE STRING "Offline compile-time ablation: 1=A scores, 2=B opportunities, 3=C opportunity-driven time")
if(NOT RMTC_ABLATION_LEVEL MATCHES "^[123]$")
 message(FATAL_ERROR "RMTC_ABLATION_LEVEL must be 1, 2 or 3")
endif()
target_compile_definitions(rmtc9_core PUBLIC RMTC_ABLATION_LEVEL=${RMTC_ABLATION_LEVEL})
if(MSVC)
 target_compile_options(rmtc9_core PUBLIC /utf-8 /W4)
else()
 target_compile_options(rmtc9_core PUBLIC -Wall -Wextra)
endif()

# 冻结的 7.0 决策核心（baseline_v7/src/rmtc7，与校内7.0 源码逐字相同），只用于对比测评
add_library(baseline7 STATIC
  baseline_v7/src/rmtc7/types.cpp baseline_v7/src/rmtc7/grid.cpp baseline_v7/src/rmtc7/parse.cpp
  baseline_v7/src/rmtc7/bot.cpp baseline_v7/src/rmtc7/events.cpp baseline_v7/src/rmtc7/battle.cpp)
target_include_directories(baseline7 PUBLIC baseline_v7/src third_party/nlohmann/include)
if(MSVC)
 target_compile_options(baseline7 PUBLIC /utf-8)
endif()

add_library(baseline93 STATIC baseline_v93/src/rmtc93/types.cpp baseline_v93/src/rmtc93/grid.cpp baseline_v93/src/rmtc93/parse.cpp baseline_v93/src/rmtc93/bot.cpp baseline_v93/src/rmtc93/events.cpp baseline_v93/src/rmtc93/battle.cpp)
target_include_directories(baseline93 PUBLIC baseline_v93/src third_party/nlohmann/include)
if(MSVC)
 target_compile_options(baseline93 PUBLIC /utf-8)
endif()

enable_testing()
find_package(Threads REQUIRED)

# Frozen, byte-identical 8.0 algorithm for comparisons.
add_library(baseline8 STATIC
  baseline_v8/src/rmtc8/types.cpp baseline_v8/src/rmtc8/grid.cpp baseline_v8/src/rmtc8/parse.cpp
  baseline_v8/src/rmtc8/bot.cpp baseline_v8/src/rmtc8/events.cpp baseline_v8/src/rmtc8/battle.cpp)
target_include_directories(baseline8 PUBLIC baseline_v8/src third_party/nlohmann/include)
if(MSVC)
 target_compile_options(baseline8 PUBLIC /utf-8)
endif()

# Frozen 9.1, with namespace-only transformation to allow a shared arena.
add_library(baseline91 STATIC baseline_v91/src/rmtc91/types.cpp baseline_v91/src/rmtc91/grid.cpp
 baseline_v91/src/rmtc91/parse.cpp baseline_v91/src/rmtc91/bot.cpp
 baseline_v91/src/rmtc91/events.cpp baseline_v91/src/rmtc91/battle.cpp)
target_include_directories(baseline91 PUBLIC baseline_v91/src third_party/nlohmann/include)
if(MSVC)
 target_compile_options(baseline91 PUBLIC /utf-8)
endif()
add_executable(rmtc_replay91 tools/replay91_main.cpp)
target_link_libraries(rmtc_replay91 PRIVATE baseline91)

add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests rmtc9_core)
add_test(NAME core_regression COMMAND core_tests)

add_executable(llm_worker_tests tests/llm_worker_tests.cpp ros2_ws/src/rmtc_bot9/src/llm.cpp)
target_include_directories(llm_worker_tests PRIVATE ros2_ws/src/rmtc_bot9/include third_party/nlohmann/include)
if(NOT WIN32)
 target_link_libraries(llm_worker_tests PRIVATE Threads::Threads)
endif()
if(MSVC)
 target_compile_options(llm_worker_tests PRIVATE /utf-8 /W4)
endif()
add_test(NAME llm_session_lifecycle COMMAND llm_worker_tests)

add_executable(v4_tests tests/v4_tests.cpp tools/bench/referee.cpp)
target_include_directories(v4_tests PRIVATE tools/bench)
target_link_libraries(v4_tests rmtc9_core)
add_test(NAME v4_rules_boundaries COMMAND v4_tests)

add_executable(v6_tests tests/v6_tests.cpp tools/bench/referee.cpp)
target_include_directories(v6_tests PRIVATE tools/bench)
target_link_libraries(v6_tests rmtc9_core)
add_test(NAME v6_physical_scenarios COMMAND v6_tests)

add_executable(v7_tests tests/v7_tests.cpp)
target_link_libraries(v7_tests rmtc9_core)
add_test(NAME v7_contracts COMMAND v7_tests)

add_executable(v8_tests tests/v8_tests.cpp tools/bench/referee.cpp)
target_include_directories(v8_tests PRIVATE tools/bench)
target_link_libraries(v8_tests rmtc9_core)
add_test(NAME v8_new_contracts COMMAND v8_tests)

add_executable(v9_tests tests/v9_tests.cpp)
target_link_libraries(v9_tests PRIVATE rmtc9_core)
add_test(NAME v9_review_regressions COMMAND v9_tests)

add_executable(v92_tests tests/v92_tests.cpp tools/bench/referee.cpp)
target_include_directories(v92_tests PRIVATE tools/bench)
target_link_libraries(v92_tests PRIVATE rmtc9_core)
add_test(NAME v92_score_buffer_and_offset_fortress COMMAND v92_tests)

add_executable(v93_tests tests/v93_tests.cpp)
target_link_libraries(v93_tests PRIVATE rmtc9_core)
add_test(NAME v93_shortest_path_exploration COMMAND v93_tests)
set_tests_properties(v93_shortest_path_exploration PROPERTIES TIMEOUT 60)

# Compile the production node with a ROS stand-in and an injected HTTP transport.
# This validates callback/session behavior, not real DDS or real AI qualification.
add_executable(node_scope_tests tests/node_scope_tests.cpp
  ros2_ws/src/rmtc_bot9/src/node.cpp ros2_ws/src/rmtc_bot9/src/llm.cpp)
target_include_directories(node_scope_tests PRIVATE tools/node_sim/stub ros2_ws/src/rmtc_bot9/include)
target_link_libraries(node_scope_tests PRIVATE rmtc9_core Threads::Threads)
add_test(NAME node_announcement_session COMMAND node_scope_tests "${CMAKE_CURRENT_SOURCE_DIR}/ros2_ws/src/rmtc_bot9/config/decision_default.json")
set_tests_properties(core_regression llm_session_lifecycle v4_rules_boundaries v6_physical_scenarios v7_contracts v8_new_contracts
                     PROPERTIES TIMEOUT 60)
set_tests_properties(v9_review_regressions node_announcement_session PROPERTIES TIMEOUT 60)
set_tests_properties(v92_score_buffer_and_offset_fortress PROPERTIES TIMEOUT 60)

add_executable(rmtc_replay tools/replay_main.cpp)
target_link_libraries(rmtc_replay PRIVATE rmtc9_core)

add_executable(rmtc_smoke tools/smoke_main.cpp tools/bench/referee.cpp)
target_include_directories(rmtc_smoke PRIVATE tools/bench)
target_link_libraries(rmtc_smoke rmtc9_core)

# Offline arena: 9.0 vs frozen 8.0/7.0 and scripted opponents.
add_library(baseline100 STATIC baseline_v100/src/rmtc100/types.cpp baseline_v100/src/rmtc100/grid.cpp
 baseline_v100/src/rmtc100/parse.cpp baseline_v100/src/rmtc100/bot.cpp
 baseline_v100/src/rmtc100/events.cpp baseline_v100/src/rmtc100/battle.cpp)
target_include_directories(baseline100 PUBLIC baseline_v100/src third_party/nlohmann/include)
if(MSVC)
 target_compile_options(baseline100 PUBLIC /utf-8)
endif()
add_library(baseline101 STATIC baseline_v101/src/rmtc101/types.cpp baseline_v101/src/rmtc101/grid.cpp
 baseline_v101/src/rmtc101/parse.cpp baseline_v101/src/rmtc101/bot.cpp
 baseline_v101/src/rmtc101/events.cpp baseline_v101/src/rmtc101/battle.cpp)
target_include_directories(baseline101 PUBLIC baseline_v101/src third_party/nlohmann/include)
if(MSVC)
 target_compile_options(baseline101 PUBLIC /utf-8)
endif()
add_executable(rmtc_arena tools/bench/bench_main.cpp tools/bench/referee.cpp tools/bench/v7_adapter.cpp tools/bench/v8_adapter.cpp tools/bench/v91_adapter.cpp tools/bench/v93_adapter.cpp tools/bench/v100_adapter.cpp tools/bench/v101_adapter.cpp)
target_include_directories(rmtc_arena PRIVATE tools/bench)
target_link_libraries(rmtc_arena rmtc9_core baseline7 baseline8 baseline91 baseline93 baseline100 baseline101)
if(NOT WIN32)
 target_link_libraries(rmtc_arena Threads::Threads)
endif()

option(RMTC_BUILD_HTTP "Compile the production libcurl transport (requires CURL development package)" OFF)
if(RMTC_BUILD_HTTP)
 find_package(CURL REQUIRED)
 add_library(rmtc9_http ros2_ws/src/rmtc_bot9/src/curl_transport.cpp)
 target_include_directories(rmtc9_http PUBLIC ros2_ws/src/rmtc_bot9/include)
 target_link_libraries(rmtc9_http PUBLIC CURL::libcurl Threads::Threads)
endif()

add_executable(v10_tests tests/v10_tests.cpp)
target_link_libraries(v10_tests PRIVATE rmtc9_core)
add_test(NAME v10_arrival_tail_receipt COMMAND v10_tests)
set_tests_properties(v10_arrival_tail_receipt PROPERTIES TIMEOUT 60)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME v10_recorded_bag_audit COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audit_bag.py")
set_tests_properties(v10_recorded_bag_audit PROPERTIES TIMEOUT 30)

add_executable(v101_tests tests/v101_tests.cpp)
target_link_libraries(v101_tests PRIVATE baseline101)
add_test(NAME v101_phase_safety_and_deadlines COMMAND v101_tests)
set_tests_properties(v101_phase_safety_and_deadlines PROPERTIES TIMEOUT 60)

add_executable(v102_tests tests/v102_tests.cpp)
target_link_libraries(v102_tests PRIVATE rmtc9_core)
add_test(NAME v102_scores_opportunities_and_risk COMMAND v102_tests)
set_tests_properties(v102_scores_opportunities_and_risk PROPERTIES TIMEOUT 60)

add_executable(rmtc_trace tools/trace_main.cpp tools/bench/referee.cpp tools/bench/v7_adapter.cpp tools/bench/v8_adapter.cpp tools/bench/v101_adapter.cpp)
target_include_directories(rmtc_trace PRIVATE tools/bench)
target_link_libraries(rmtc_trace PRIVATE rmtc9_core baseline7 baseline8 baseline101)

set(RMTC_ROUND2_LEVEL "3" CACHE STRING "0=B0 frozen behavior; 1=B1 race; 2=B2 route; 3=B3 opportunity cost")
if(NOT RMTC_ROUND2_LEVEL MATCHES "^[0123]$")
 message(FATAL_ERROR "RMTC_ROUND2_LEVEL must be 0..3")
endif()
target_compile_definitions(rmtc9_core PUBLIC RMTC_ROUND2_LEVEL=${RMTC_ROUND2_LEVEL})
add_library(baseline102 STATIC baseline_v102/src/rmtc102/types.cpp baseline_v102/src/rmtc102/grid.cpp baseline_v102/src/rmtc102/parse.cpp baseline_v102/src/rmtc102/bot.cpp baseline_v102/src/rmtc102/events.cpp baseline_v102/src/rmtc102/battle.cpp)
target_include_directories(baseline102 PUBLIC baseline_v102/src third_party/nlohmann/include)
if(MSVC)
 target_compile_options(baseline102 PUBLIC /utf-8)
endif()
target_sources(rmtc_arena PRIVATE tools/bench/v102_adapter.cpp)
target_link_libraries(rmtc_arena baseline102)
add_executable(rmtc_round2_trace tools/round2_trace.cpp tools/bench/referee.cpp tools/bench/v7_adapter.cpp tools/bench/v8_adapter.cpp tools/bench/v91_adapter.cpp tools/bench/v93_adapter.cpp tools/bench/v100_adapter.cpp tools/bench/v101_adapter.cpp tools/bench/v102_adapter.cpp)
target_include_directories(rmtc_round2_trace PRIVATE tools tools/bench)
target_link_libraries(rmtc_round2_trace PRIVATE rmtc9_core baseline7 baseline8 baseline91 baseline93 baseline100 baseline101 baseline102 Threads::Threads)
add_executable(v103_tests tests/v103_tests.cpp)
target_link_libraries(v103_tests PRIVATE rmtc9_core)
add_test(NAME v103_resource_choice_route_outcomes COMMAND v103_tests)
set_tests_properties(v103_resource_choice_route_outcomes PROPERTIES TIMEOUT 60)
option(RMTC_ENABLE_OPPORTUNITY_COST "Use explicit irreversible resource loss in B3" ON)
target_compile_definitions(rmtc9_core PUBLIC RMTC_ENABLE_OPPORTUNITY_COST=$<BOOL:${RMTC_ENABLE_OPPORTUNITY_COST}>)

target_compile_definitions(baseline102 PRIVATE RMTC_ABLATION_LEVEL=2)


set(RMTC_PHASE_ALPHA_PERCENT "100" CACHE STRING "Third-round numeric multiplier alpha: 100/50/0")
set(RMTC_PHASE_RESTORE_MASK "0" CACHE STRING "Restore original resource=1 pursuit=2 mission=4")
if(NOT RMTC_ABLATION_LEVEL STREQUAL "2" OR NOT RMTC_ROUND2_LEVEL STREQUAL "3" OR NOT RMTC_ENABLE_OPPORTUNITY_COST)
 message(FATAL_ERROR "Third-round comparison fixes first/second-round policy to 2/3/ON")
endif()
target_compile_definitions(rmtc9_core PUBLIC RMTC_PHASE_ALPHA_PERCENT=${RMTC_PHASE_ALPHA_PERCENT} RMTC_PHASE_RESTORE_MASK=${RMTC_PHASE_RESTORE_MASK} RMTC_ENABLE_PHASE_PROBES=1)
add_library(baseline103 STATIC baseline_v103/src/rmtc103/types.cpp baseline_v103/src/rmtc103/grid.cpp baseline_v103/src/rmtc103/parse.cpp baseline_v103/src/rmtc103/bot.cpp baseline_v103/src/rmtc103/events.cpp baseline_v103/src/rmtc103/battle.cpp)
target_include_directories(baseline103 PUBLIC baseline_v103/src third_party/nlohmann/include)
target_compile_definitions(baseline103 PRIVATE RMTC_ABLATION_LEVEL=2 RMTC_ROUND2_LEVEL=3 RMTC_ENABLE_OPPORTUNITY_COST=1)
if(MSVC)
 target_compile_options(baseline103 PUBLIC /utf-8)
endif()
target_sources(rmtc_arena PRIVATE tools/bench/v103_adapter.cpp)
target_link_libraries(rmtc_arena baseline103)
target_sources(rmtc_round2_trace PRIVATE tools/bench/v103_adapter.cpp)
target_link_libraries(rmtc_round2_trace PRIVATE baseline103)
add_executable(rmtc_phase_trace tools/phase_trace.cpp tools/bench/referee.cpp tools/bench/v7_adapter.cpp tools/bench/v8_adapter.cpp tools/bench/v91_adapter.cpp tools/bench/v93_adapter.cpp tools/bench/v100_adapter.cpp tools/bench/v101_adapter.cpp tools/bench/v102_adapter.cpp tools/bench/v103_adapter.cpp)
target_include_directories(rmtc_phase_trace PRIVATE tools tools/bench)
target_link_libraries(rmtc_phase_trace PRIVATE rmtc9_core baseline7 baseline8 baseline91 baseline93 baseline100 baseline101 baseline102 baseline103 Threads::Threads)

add_executable(v104_tests tests/v104_tests.cpp)
target_link_libraries(v104_tests PRIVATE rmtc9_core)
add_test(NAME v104_numeric_phase_scope_and_safety COMMAND v104_tests)
set_tests_properties(v104_numeric_phase_scope_and_safety PROPERTIES TIMEOUT 60)
add_library(production_scope_core STATIC src/rmtc9/types.cpp src/rmtc9/grid.cpp src/rmtc9/parse.cpp src/rmtc9/bot.cpp src/rmtc9/events.cpp src/rmtc9/battle.cpp)
target_include_directories(production_scope_core PUBLIC src third_party/nlohmann/include)
target_compile_definitions(production_scope_core PUBLIC RMTC_ABLATION_LEVEL=2 RMTC_ROUND2_LEVEL=3 RMTC_ENABLE_OPPORTUNITY_COST=1 RMTC_PHASE_ALPHA_PERCENT=${RMTC_PHASE_ALPHA_PERCENT} RMTC_PHASE_RESTORE_MASK=${RMTC_PHASE_RESTORE_MASK} RMTC_ENABLE_PHASE_PROBES=0)
if(MSVC)
 target_compile_options(production_scope_core PUBLIC /utf-8)
endif()
add_executable(v104_production_scope tests/v104_production_scope.cpp)
target_link_libraries(v104_production_scope PRIVATE production_scope_core)
add_test(NAME v104_production_without_offline_probes COMMAND v104_production_scope)
set_tests_properties(v104_production_without_offline_probes PROPERTIES TIMEOUT 60)
