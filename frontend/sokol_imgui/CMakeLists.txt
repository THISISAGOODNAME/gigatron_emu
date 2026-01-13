# Main emulator application
add_executable(gigatron_sokol_imgui main.cpp)
target_link_libraries(gigatron_sokol_imgui PRIVATE
    gigatron_core
    sokol
    imgui
    nfd
)

# Platform-specific settings
if(WIN32)
    # Windows settings
    target_compile_definitions(gigatron_sokol_imgui PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(gigatron_sokol_imgui PUBLIC $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<NOT:$<COMPILE_LANGUAGE:CUDA>>>:/wd4819>)
elseif(APPLE)
    # macOS settings
    target_link_libraries(gigatron_sokol_imgui PRIVATE
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
    target_link_libraries(gigatron_sokol_imgui PRIVATE
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
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/../../roms DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
