# Adding Raylib
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE) # don't build the supplied examples
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE) # don't build the supplied example games

FetchContent_Declare(
     raylib
     GIT_REPOSITORY "https://github.com/raysan5/raylib.git"
     GIT_TAG "5.5"
     GIT_PROGRESS TRUE
     GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(gigatron_raylib main.c)
target_link_libraries(gigatron_raylib PRIVATE gigatron_core raylib)

# Copy ROM files to build directory
add_custom_command(TARGET gigatron_raylib POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:gigatron_raylib>/roms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/roms/gigatron.rom"
        "$<TARGET_FILE_DIR:gigatron_raylib>/roms/gigatron.rom"
    COMMENT "Copying ROM files..."
)