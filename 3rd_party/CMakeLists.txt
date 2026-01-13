# sokol
add_library(sokol INTERFACE)
target_include_directories(sokol INTERFACE sokol)

# imgui
add_library(imgui
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp)
target_include_directories(imgui PUBLIC imgui)

# nfd
add_subdirectory(nativefiledialog-extended)
