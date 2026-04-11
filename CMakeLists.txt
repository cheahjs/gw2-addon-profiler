cmake_minimum_required(VERSION 3.20)
project(nexus_profiler VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── Tracy Profiler ────────────────────────────────────────────
set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
set(TRACY_NO_EXIT ON CACHE BOOL "" FORCE)
set(TRACY_DELAYED_INIT ON CACHE BOOL "" FORCE)
set(TRACY_NO_SYSTEM_TRACING ON CACHE BOOL "" FORCE)
set(TRACY_NO_SAMPLING ON CACHE BOOL "" FORCE)
set(TRACY_NO_VSYNC_CAPTURE ON CACHE BOOL "" FORCE)
set(TRACY_NO_FRAME_IMAGE ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy.git
    GIT_TAG v0.11.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(tracy)

# ── ImGui 1.80 (core only, matches Nexus) ────────────────────
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.80
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
)
target_include_directories(imgui_lib PUBLIC "${imgui_SOURCE_DIR}")

# ── Nexus API (header-only) ──────────────────────────────────
add_library(nexus_api INTERFACE)
target_include_directories(nexus_api INTERFACE "${CMAKE_SOURCE_DIR}/extern/nexus-api")

# ── arcdps API (header-only, for future use) ──────────────────
add_library(arcdps_api INTERFACE)
target_include_directories(arcdps_api INTERFACE "${CMAKE_SOURCE_DIR}/extern/arcdps")

# ── Main DLL ─────────────────────────────────────────────────
add_library(nexus_profiler SHARED
    src/dllmain.cpp
    src/addon.cpp
    src/profiler.cpp
    src/ui.cpp
)

target_include_directories(nexus_profiler PRIVATE src)

target_compile_definitions(nexus_profiler PRIVATE
    _WIN32_WINNT=0x0601
    IMGUI_DISABLE_DEMO_WINDOWS
    IMGUI_DISABLE_METRICS_WINDOW
)

target_link_libraries(nexus_profiler PRIVATE
    Tracy::TracyClient
    imgui_lib
    nexus_api
    arcdps_api
)

if(WIN32)
    target_link_libraries(nexus_profiler PRIVATE ws2_32 dbghelp)
endif()

if(MINGW)
    target_link_options(nexus_profiler PRIVATE
        -static-libgcc -static-libstdc++
        -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()

set_target_properties(nexus_profiler PROPERTIES
    PREFIX ""
    OUTPUT_NAME "nexus_profiler"
    SUFFIX ".dll"
)
