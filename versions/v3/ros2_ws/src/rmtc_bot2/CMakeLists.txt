cmake_minimum_required(VERSION 3.16)
project(rmtc_bot2 VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 决策核心在仓库根的 src/rmtc2，与 ros2_ws 保持相对位置
get_filename_component(RMTC2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
if(NOT EXISTS "${RMTC2_ROOT}/src/rmtc2/bot.cpp")
  message(FATAL_ERROR "找不到决策核心：请保持 src/rmtc2 与 ros2_ws 同级")
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_library(rmtc2_core STATIC
  "${RMTC2_ROOT}/src/rmtc2/types.cpp"
  "${RMTC2_ROOT}/src/rmtc2/grid.cpp"
  "${RMTC2_ROOT}/src/rmtc2/parse.cpp"
  "${RMTC2_ROOT}/src/rmtc2/bot.cpp"
)
target_include_directories(rmtc2_core PUBLIC
  "${RMTC2_ROOT}/src"
  "${RMTC2_ROOT}/third_party/nlohmann/include"
)
target_link_libraries(rmtc2_core PUBLIC CURL::libcurl)
if(MSVC)
  target_compile_options(rmtc2_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(rmtc2_core PRIVATE -Wall -Wextra)
endif()

add_executable(rmtc_bot2
  src/main.cpp
  src/node.cpp
  src/llm.cpp
  src/curl_transport.cpp
)
target_include_directories(rmtc_bot2 PRIVATE include)
target_link_libraries(rmtc_bot2 rmtc2_core Threads::Threads)
ament_target_dependencies(rmtc_bot2 rclcpp std_msgs)

install(TARGETS rmtc_bot2 DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})

ament_package()
