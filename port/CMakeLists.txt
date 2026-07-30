cmake_minimum_required(VERSION 3.16)
project(moraffs_world VERSION 1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
option(MORAFF_COPY_ORIGINAL_DATA
    "Copy the developer's locally owned original Moraff's World files into the build directory"
    ON)

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
    src/mw_audio.c
    src/mw_wilderness.c
    src/mw_model_viewer.c
    src/mw_game.c
    src/mw_combat.c
    src/mw_trainer.c
    src/mw_integrity.c
)

# WIN32 selects the GUI subsystem and prevents a separate loader/status console
# from appearing beside the actual game window.
add_executable(moraffs_world WIN32 ${SOURCES})

target_include_directories(moraffs_world PRIVATE include)
target_link_libraries(moraffs_world PRIVATE SDL2::SDL2-static SDL2::SDL2main)

if(WIN32)
    target_link_libraries(moraffs_world PRIVATE winmm imm32 setupapi version)
endif()

# Keep the native build self-contained.  The original game data directory is
# two levels above this source tree; the running program opens only local,
# relative filenames beside its executable.
set(MORAFF_DATA_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(MORAFF_RUNTIME_FILES
    DUNG.BIN
    WORLDMAP.BIN
    H.BIN
    WORLD.PIC
    WALL.PIC
    360X480.FNT
    320X200.FNT
    ROLL.TXT
    MW.EXE
    WORLD.EXE
)

if(MORAFF_COPY_ORIGINAL_DATA)
    foreach(MORAFF_DATA_FILE IN LISTS MORAFF_RUNTIME_FILES)
        if(EXISTS "${MORAFF_DATA_SOURCE_DIR}/${MORAFF_DATA_FILE}")
            add_custom_command(TARGET moraffs_world POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${MORAFF_DATA_SOURCE_DIR}/${MORAFF_DATA_FILE}"
                    "$<TARGET_FILE_DIR:moraffs_world>/${MORAFF_DATA_FILE}"
                VERBATIM
            )
        else()
            message(WARNING "Missing Moraff runtime file: ${MORAFF_DATA_FILE}")
        endif()
    endforeach()
endif()

add_custom_command(TARGET moraffs_world POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/README_RELEASE.md"
        "$<TARGET_FILE_DIR:moraffs_world>/README.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/ORIGINAL_FILES_REQUIRED.md"
        "$<TARGET_FILE_DIR:moraffs_world>/ORIGINAL_FILES_REQUIRED.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/CREDITS.md"
        "$<TARGET_FILE_DIR:moraffs_world>/CREDITS.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        "$<TARGET_FILE_DIR:moraffs_world>/THIRD_PARTY_NOTICES.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE_PORT.txt"
        "$<TARGET_FILE_DIR:moraffs_world>/LICENSE_PORT.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/RELEASE_NOTES.md"
        "$<TARGET_FILE_DIR:moraffs_world>/RELEASE_NOTES.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/HOTKEYS.md"
        "$<TARGET_FILE_DIR:moraffs_world>/HOTKEYS.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/DEEP_DUNGEON.md"
        "$<TARGET_FILE_DIR:moraffs_world>/DEEP_DUNGEON.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/EXPERIENCE_MODES.md"
        "$<TARGET_FILE_DIR:moraffs_world>/EXPERIENCE_MODES.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/DEEP_SPELLS.md"
        "$<TARGET_FILE_DIR:moraffs_world>/DEEP_SPELLS.md"
    COMMAND ${CMAKE_COMMAND} -E echo
        "Build complete. Run moraffs_world with its local game data files."
)
