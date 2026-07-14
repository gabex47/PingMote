cmake_minimum_required(VERSION 3.24)

project(PingMote VERSION 0.1.0 LANGUAGES C)

include(FetchContent)
include(CTest)

# cJSON 1.7.18 supports older CMake releases whose policy floor CMake 4 no longer assumes.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

option(PINGMOTE_FETCH_DEPENDENCIES "Fetch missing third-party dependencies" ON)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(CURL 8.0 REQUIRED)

set(MINIAUDIO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_INSTALL OFF CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_EXTRA_NODES ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBOPUS ON CACHE BOOL "" FORCE)
set(MINIAUDIO_NO_LIBVORBIS ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    miniaudio
    URL https://github.com/mackron/miniaudio/archive/refs/tags/0.11.25.tar.gz
    URL_HASH SHA256=b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(miniaudio)

find_package(raylib 5.5 QUIET)
if(NOT TARGET raylib)
    if(NOT PINGMOTE_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "raylib 5.5+ was not found")
    endif()

    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
        URL_HASH SHA256=aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(raylib)
endif()

if(TARGET raylib AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(raylib PRIVATE -Wno-tautological-compare)
endif()

find_package(cJSON 1.7.18 CONFIG QUIET)
if(NOT TARGET cjson AND NOT TARGET cjson-static)
    if(NOT PINGMOTE_FETCH_DEPENDENCIES)
        message(FATAL_ERROR "cJSON 1.7.18+ was not found")
    endif()

    set(ENABLE_CJSON_TEST OFF CACHE BOOL "" FORCE)
    set(ENABLE_CJSON_UTILS OFF CACHE BOOL "" FORCE)
    set(ENABLE_CJSON_UNINSTALL OFF CACHE BOOL "" FORCE)
    set(ENABLE_TARGET_EXPORT OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        cjson
        URL https://github.com/DaveGamble/cJSON/archive/refs/tags/v1.7.18.tar.gz
        URL_HASH SHA256=3aa806844a03442c00769b83e99970be70fbef03735ff898f4811dd03b9f5ee5
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    FetchContent_MakeAvailable(cjson)

    # Upstream's build target does not expose its source include directory.
    target_include_directories(cjson INTERFACE "${cjson_SOURCE_DIR}")
endif()

if(TARGET cjson)
    set(PINGMOTE_CJSON_TARGET cjson)
elseif(TARGET cjson-static)
    set(PINGMOTE_CJSON_TARGET cjson-static)
else()
    message(FATAL_ERROR "No supported cJSON target is available")
endif()

add_library(pingmote_core STATIC
    src/animation.c
    src/audio.c
    src/network.c
    src/reply.c
)

target_include_directories(pingmote_core PUBLIC include)
target_link_libraries(
    pingmote_core
    PUBLIC CURL::libcurl ${PINGMOTE_CJSON_TARGET} miniaudio
)

add_executable(pingmote
    src/main.c
    src/app.c
    src/sprite.c
)

target_link_libraries(pingmote PRIVATE pingmote_core raylib)
target_compile_definitions(
    pingmote
    PRIVATE PINGMOTE_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites"
)

if(APPLE)
    target_link_libraries(pingmote PRIVATE
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreVideo"
    )
endif()

function(pingmote_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -Wconversion
            -Wshadow
            -Wstrict-prototypes
        )
    endif()
endfunction()

pingmote_enable_warnings(pingmote_core)
pingmote_enable_warnings(pingmote)

if(BUILD_TESTING)
    add_executable(animation_tests tests/animation_tests.c src/animation.c)
    target_include_directories(animation_tests PRIVATE include)
    pingmote_enable_warnings(animation_tests)
    add_test(NAME animation_state_machine COMMAND animation_tests)

    add_executable(reply_tests tests/reply_tests.c src/reply.c)
    target_include_directories(reply_tests PRIVATE include)
    pingmote_enable_warnings(reply_tests)
    add_test(NAME reply_sanitization COMMAND reply_tests)
endif()

install(TARGETS pingmote RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/pingmote/assets)
