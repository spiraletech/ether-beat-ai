cmake_minimum_required(VERSION 3.25)

project(EtherBeat
    VERSION 0.1.0
    DESCRIPTION "ETHERBEAT private Windows-native generative music workstation"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ETHERBEAT_BUILD_CLI "Build the portable command-line harness" ON)
option(ETHERBEAT_BUILD_WIN32 "Build the native Windows shell" ON)

add_library(etherbeat_core STATIC
    src/core/ModelRouter.cpp
    src/backends/MockWaveBackend.cpp
)

target_include_directories(etherbeat_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_definitions(etherbeat_core
    PUBLIC
        ETHERBEAT_VERSION="${PROJECT_VERSION}"
)

if(MSVC)
    target_compile_options(etherbeat_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(etherbeat_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(ETHERBEAT_BUILD_CLI)
    add_executable(etherbeat src/main.cpp)
    target_link_libraries(etherbeat PRIVATE etherbeat_core)

    if(MSVC)
        target_compile_options(etherbeat PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(etherbeat PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(WIN32 AND ETHERBEAT_BUILD_WIN32)
    add_executable(EtherBeat WIN32 src/win32/EtherBeatWin.cpp)
    target_link_libraries(EtherBeat PRIVATE etherbeat_core comdlg32 shell32)
    target_compile_definitions(EtherBeat PRIVATE UNICODE _UNICODE NOMINMAX)
    set_target_properties(EtherBeat PROPERTIES OUTPUT_NAME "ETHERBEAT")

    if(MSVC)
        target_compile_options(EtherBeat PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()
