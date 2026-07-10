cmake_minimum_required(VERSION 3.16)
project(moraffs_world C)

set(CMAKE_C_STANDARD 11)

# ── Fetch SDL2 automatically if not found ──
include(FetchContent)
FetchContent_Declare(
    SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz
    URL_HASH SHA256=ac356ea55e8b9dd0b2d1fa27da40ef7e238267ccf9324704850d5d47375b48ea
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# ── Source files ──
set(SOURCES
    src/main.c
    src/mw_video.c
    src/mw_input.c
    src/mw_game.c
)

add_executable(moraffs_world ${SOURCES})

target_include_directories(moraffs_world PRIVATE include)
target_link_libraries(moraffs_world PRIVATE SDL2::SDL2-static SDL2::SDL2main)

if(WIN32)
    target_link_libraries(moraffs_world PRIVATE winmm imm32 setupapi version)
endif()

# Copy game data files to build dir for convenience
add_custom_command(TARGET moraffs_world POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Build complete. Run from the game data directory."
)
