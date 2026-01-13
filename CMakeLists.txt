cmake_minimum_required(VERSION 4.0)
project(gigatron_emu)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(3rd_party)

add_executable(imgui_test main.cpp)
target_link_libraries(imgui_test PRIVATE sokol imgui nfd)
