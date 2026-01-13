cmake_minimum_required(VERSION 3.16)
project(gigatron_emu C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# Add third party libraries
add_subdirectory(3rd_party)

# Gigatron emulator core library (pure C)
add_library(gigatron_core STATIC
    core/gigatron.c
    core/vga.c
    core/audio.c
    core/loader.c
)
target_include_directories(gigatron_core PUBLIC core)

# frontends
add_subdirectory(frontend/sokol_imgui)
