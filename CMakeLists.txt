cmake_minimum_required(VERSION 3.25)

project(EtherBeat
    VERSION 0.2.14
    DESCRIPTION "ETHERBEAT private Windows-native generative music workstation"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

option(ETHERBEAT_BUILD_CLI "Build the portable command-line harness" ON)
option(ETHERBEAT_BUILD_WIN32 "Build the native Windows shell" ON)

add_library(etherbeat_core STATIC
    src/core/ModelRouter.cpp
    src/core/EtherComposer.cpp
    src/core/EtherControl.cpp
    src/core/EtherControlUi.cpp
    src/core/EtherVersions.cpp
    src/core/EtherCompare.cpp
    src/core/EtherTimeline.cpp
    src/core/EtherDNA.cpp
    src/core/EtherDraft.cpp
    src/core/EtherCritic.cpp
    src/core/EtherSearch.cpp
    src/core/EtherPromote.cpp
    src/core/AceStepRequestCodec.cpp
    src/backends/MockWaveBackend.cpp
)

if(WIN32)
    target_sources(etherbeat_core PRIVATE
        src/win32/AceStepApiBackend.cpp
        src/win32/AceStepEngineManager.cpp
        src/win32/ManagedAceStepBackend.cpp
    )
    target_link_libraries(etherbeat_core PUBLIC winhttp shell32 ole32)
endif()

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
    add_executable(EtherBeat WIN32
        src/win32/EtherBeatTabbedWinTimeline.cpp
        src/win32/AudioAnalysisWin.cpp
    )
    target_include_directories(EtherBeat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(EtherBeat PRIVATE
        etherbeat_core
        ole32
        mfplat
        mfreadwrite
        mfuuid
        comdlg32
        shell32
        gdiplus
        dwmapi
        winmm
    )
    target_compile_definitions(EtherBeat PRIVATE UNICODE _UNICODE NOMINMAX)
    set_target_properties(EtherBeat PROPERTIES OUTPUT_NAME "ETHERBEAT")

    if(MSVC)
        target_compile_options(EtherBeat PRIVATE
            /W4
            /permissive-
            /utf-8
            /FIwindowsx.h
            /FI${CMAKE_CURRENT_SOURCE_DIR}/src/win32/WinShellPreamble.hpp
        )
    endif()
endif()

if(BUILD_TESTING)
    add_executable(etherbeat_core_smoke tests/CoreSmoke.cpp)
    target_link_libraries(etherbeat_core_smoke PRIVATE etherbeat_core)
    add_test(NAME etherbeat_core_smoke COMMAND etherbeat_core_smoke)

    add_executable(etherbeat_search_smoke tests/SearchSmoke.cpp)
    target_link_libraries(etherbeat_search_smoke PRIVATE etherbeat_core)
    add_test(NAME etherbeat_search_smoke COMMAND etherbeat_search_smoke)

    add_executable(etherbeat_auto_promote_smoke tests/AutoPromoteSmoke.cpp)
    target_link_libraries(etherbeat_auto_promote_smoke PRIVATE etherbeat_core)
    add_test(NAME etherbeat_auto_promote_smoke COMMAND etherbeat_auto_promote_smoke)

    add_executable(etherbeat_control_smoke tests/ControlSmoke.cpp)
    target_link_libraries(etherbeat_control_smoke PRIVATE etherbeat_core)
    add_test(NAME etherbeat_control_smoke COMMAND etherbeat_control_smoke)

    add_executable(etherbeat_ace_control_smoke tests/AceControlSmoke.cpp)
    target_link_libraries(etherbeat_ace_control_smoke PRIVATE etherbeat_core)
    add_test(NAME etherbeat_ace_control_smoke COMMAND etherbeat_ace_control_smoke)

    add_executable(etherbeat_control_ui_smoke tests/ControlUiSmoke.cpp)
    target_link_libraries(etherbeat_control_ui_smoke PRIVATE etherbeat_core)
    add_test(NAME etherbeat_control_ui_smoke COMMAND etherbeat_control_ui_smoke)

    add_executable(etherbeat_versions_smoke tests/VersionsSmoke.cpp)
    target_link_libraries(etherbeat_versions_smoke PRIVATE etherbeat_core)
    add_test(NAME etherbeat_versions_smoke COMMAND etherbeat_versions_smoke)

    add_executable(etherbeat_compare_smoke tests/CompareSmoke.cpp)
    target_link_libraries(etherbeat_compare_smoke PRIVATE etherbeat_core)
    add_test(NAME etherbeat_compare_smoke COMMAND etherbeat_compare_smoke)

    add_executable(etherbeat_timeline_smoke tests/TimelineSmoke.cpp)
    target_link_libraries(etherbeat_timeline_smoke PRIVATE etherbeat_core)
    add_test(NAME etherbeat_timeline_smoke COMMAND etherbeat_timeline_smoke)

    if(MSVC)
        target_compile_options(etherbeat_core_smoke PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(etherbeat_search_smoke PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(etherbeat_auto_promote_smoke PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(etherbeat_control_smoke PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(etherbeat_ace_control_smoke PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(etherbeat_control_ui_smoke PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(etherbeat_versions_smoke PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(etherbeat_compare_smoke PRIVATE /W4 /permissive- /utf-8)
        target_compile_options(etherbeat_timeline_smoke PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(etherbeat_core_smoke PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(etherbeat_search_smoke PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(etherbeat_auto_promote_smoke PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(etherbeat_control_smoke PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(etherbeat_ace_control_smoke PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(etherbeat_control_ui_smoke PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(etherbeat_versions_smoke PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(etherbeat_compare_smoke PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(etherbeat_timeline_smoke PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()
