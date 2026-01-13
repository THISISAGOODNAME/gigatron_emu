cmake_minimum_required(VERSION 3.16)
project(gigatron_emu C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

# Add third party libraries
add_subdirectory(3rd_party)

# Gigatron emulator core library (pure C)
add_library(gigatron_core STATIC
    src/gigatron.c
    src/vga.c
    src/audio.c
    src/loader.c
)
target_include_directories(gigatron_core PUBLIC src)

# Main emulator application
add_executable(gigatron_emu src/main.cpp)
target_link_libraries(gigatron_emu PRIVATE 
    gigatron_core
    sokol 
    imgui 
    nfd
)

# Platform-specific settings
if(WIN32)
    # Windows settings
    target_compile_definitions(gigatron_emu PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(gigatron_emu PUBLIC $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<NOT:$<COMPILE_LANGUAGE:CUDA>>>:/wd4819>)
elseif(APPLE)
    # macOS settings
    target_link_libraries(gigatron_emu PRIVATE
        "-framework Cocoa"
        "-framework QuartzCore"
        "-framework AudioToolbox"
        "-framework Metal"
        "-framework MetalKit"
    )
elseif(UNIX)
    # Linux settings
    find_package(Threads REQUIRED)
    find_package(X11 REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(ALSA REQUIRED)
    target_link_libraries(gigatron_emu PRIVATE
        Threads::Threads
        ${X11_LIBRARIES}
        ${X11_Xi_LIB}
        ${X11_Xcursor_LIB}
        ${OPENGL_LIBRARIES}
        ${ALSA_LIBRARIES}
        dl
        m
    )
endif()

# Copy ROM files to build directory
file(COPY ${CMAKE_SOURCE_DIR}/roms DESTINATION ${CMAKE_BINARY_DIR})

# Install target
install(TARGETS gigatron_emu RUNTIME DESTINATION bin)
install(DIRECTORY roms DESTINATION share/gigatron_emu)
