cmake_minimum_required(VERSION 3.16)
project(rmtc_bot9 VERSION 10.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 决策核心在仓库根的 src/rmtc9，与 ros2_ws 保持相对位置
get_filename_component(RMTC2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
if(NOT EXISTS "${RMTC2_ROOT}/src/rmtc9/bot.cpp")
  message(FATAL_ERROR "找不到决策核心：请保持 src/rmtc9 与 ros2_ws 同级")
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_library(rmtc9_core STATIC
  "${RMTC2_ROOT}/src/rmtc9/types.cpp"
  "${RMTC2_ROOT}/src/rmtc9/grid.cpp"
  "${RMTC2_ROOT}/src/rmtc9/parse.cpp"
  "${RMTC2_ROOT}/src/rmtc9/bot.cpp"
  "${RMTC2_ROOT}/src/rmtc9/events.cpp"
  "${RMTC2_ROOT}/src/rmtc9/battle.cpp"
)
target_include_directories(rmtc9_core PUBLIC
  "${RMTC2_ROOT}/src"
  "${RMTC2_ROOT}/third_party/nlohmann/include"
)
target_link_libraries(rmtc9_core PUBLIC CURL::libcurl)
if(MSVC)
  target_compile_options(rmtc9_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(rmtc9_core PRIVATE -Wall -Wextra)
endif()

add_executable(rmtc_bot9
  src/main.cpp
  src/node.cpp
  src/llm.cpp
  src/curl_transport.cpp
)
target_include_directories(rmtc_bot9 PRIVATE include)
target_link_libraries(rmtc_bot9 rmtc9_core Threads::Threads)
ament_target_dependencies(rmtc_bot9 rclcpp std_msgs)

install(TARGETS rmtc_bot9 DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

ament_package()
