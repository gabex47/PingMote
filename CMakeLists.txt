cmake_minimum_required(VERSION 3.24)

project(PingMote VERSION 0.2.0 LANGUAGES C CXX)

include(FetchContent)
include(CTest)
find_package(Threads REQUIRED)

# cJSON 1.7.18 supports older CMake releases whose policy floor CMake 4 no longer assumes.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

option(PINGMOTE_FETCH_DEPENDENCIES "Fetch missing third-party dependencies" ON)
option(PINGMOTE_NETWORK_TESTS "Enable live network smoke tests" OFF)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "" FORCE)
set(WHISPER_SDL2 OFF CACHE BOOL "" FORCE)
set(WHISPER_ALL_WARNINGS_3RD_PARTY OFF CACHE BOOL "" FORCE)
set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
if(APPLE)
    set(GGML_METAL ON CACHE BOOL "" FORCE)
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
endif()
FetchContent_Declare(
    whisper
    URL https://github.com/ggml-org/whisper.cpp/archive/refs/tags/v1.8.6.tar.gz
    URL_HASH SHA256=f8e632016ceae556f3132a16c7f704be1e7715595041f474fa81a2b64c1abf7c
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(whisper)

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
    src/assistant.c
    src/audio.c
    src/cache.c
    src/microphone.c
    src/network.c
    src/reply.c
    src/secure_store.c
    src/sha256.c
    src/speech.c
)

target_include_directories(pingmote_core PUBLIC include)
target_link_libraries(
    pingmote_core
    PUBLIC CURL::libcurl ${PINGMOTE_CJSON_TARGET} miniaudio Threads::Threads whisper
)

add_executable(pingmote
    src/main.c
    src/app.c
    src/resources.c
    src/sprite.c
)

target_link_libraries(pingmote PRIVATE pingmote_core raylib)
target_compile_definitions(
    pingmote
    PRIVATE PINGMOTE_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/sprites"
)

if(APPLE)
    set(PINGMOTE_SPRITE_RESOURCES
        assets/sprites/Idle.png
        assets/sprites/Talking.png
        assets/sprites/Thinking.png
    )
    target_sources(pingmote PRIVATE ${PINGMOTE_SPRITE_RESOURCES})
    set_source_files_properties(
        ${PINGMOTE_SPRITE_RESOURCES}
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets/sprites"
    )
    set_target_properties(pingmote PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
        OUTPUT_NAME "PingMote"
    )
    target_link_libraries(pingmote_core PUBLIC
        "-framework CoreFoundation"
        "-framework Security"
    )
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

    add_executable(cache_tests tests/cache_tests.c src/cache.c)
    target_include_directories(cache_tests PRIVATE include)
    pingmote_enable_warnings(cache_tests)
    add_test(NAME cache_expiration COMMAND cache_tests)

    add_executable(sha256_tests tests/sha256_tests.c src/sha256.c)
    target_include_directories(sha256_tests PRIVATE include)
    pingmote_enable_warnings(sha256_tests)
    add_test(NAME sha256_integrity COMMAND sha256_tests)

    if(PINGMOTE_NETWORK_TESTS)
        add_executable(network_smoke tests/network_smoke.c)
        target_link_libraries(network_smoke PRIVATE pingmote_core)
        pingmote_enable_warnings(network_smoke)
        add_test(NAME live_tetyys_audio COMMAND network_smoke)
        set_tests_properties(live_tetyys_audio PROPERTIES LABELS "network")

        add_executable(speech_smoke tests/speech_smoke.c)
        target_link_libraries(speech_smoke PRIVATE pingmote_core)
        target_compile_definitions(
            speech_smoke
            PRIVATE PINGMOTE_WHISPER_SAMPLE_PATH="${whisper_SOURCE_DIR}/samples/jfk.wav"
        )
        pingmote_enable_warnings(speech_smoke)
        add_test(NAME live_whisper_transcription COMMAND speech_smoke)
        set_tests_properties(live_whisper_transcription PROPERTIES LABELS "network")
    endif()
endif()

install(TARGETS pingmote
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)
install(DIRECTORY assets/ DESTINATION share/pingmote/assets)
