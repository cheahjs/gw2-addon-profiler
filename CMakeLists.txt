cmake_minimum_required(VERSION 3.20)
project(gw2_addon_profiler VERSION 0.2.0 LANGUAGES C CXX)

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

# ── MinHook (we bundle our own since we're not a Nexus addon) ─
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG v1.3.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(minhook)

add_library(minhook_lib STATIC
    "${minhook_SOURCE_DIR}/src/buffer.c"
    "${minhook_SOURCE_DIR}/src/hook.c"
    "${minhook_SOURCE_DIR}/src/trampoline.c"
    "${minhook_SOURCE_DIR}/src/hde/hde64.c"
)
target_include_directories(minhook_lib PUBLIC "${minhook_SOURCE_DIR}/include")

# ── ImGui 1.80 with D3D11 + Win32 backends ───────────────────
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
    "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp"
)
target_include_directories(imgui_lib PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
target_compile_definitions(imgui_lib PRIVATE
    IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
)

# ── API headers ───────────────────────────────────────────────
add_library(nexus_api INTERFACE)
target_include_directories(nexus_api INTERFACE "${CMAKE_SOURCE_DIR}/extern/nexus-api")

add_library(arcdps_api INTERFACE)
target_include_directories(arcdps_api INTERFACE "${CMAKE_SOURCE_DIR}/extern/arcdps")

# ── Main DLL ─────────────────────────────────────────────────
add_library(gw2_addon_profiler SHARED
    src/dllmain.cpp
    src/proxy.cpp
    src/hooks.cpp
    src/profiler.cpp
    src/imgui_impl.cpp
    src/ui.cpp
    ${CMAKE_SOURCE_DIR}/def/gw2_addon_profiler.def
)

target_include_directories(gw2_addon_profiler PRIVATE src)

target_compile_definitions(gw2_addon_profiler PRIVATE
    _WIN32_WINNT=0x0601
    IMGUI_DISABLE_DEMO_WINDOWS
    IMGUI_DISABLE_METRICS_WINDOW
)

target_link_libraries(gw2_addon_profiler PRIVATE
    Tracy::TracyClient
    minhook_lib
    imgui_lib
    nexus_api
    arcdps_api
    d3d11 dxgi d3dcompiler
    ws2_32 dbghelp dwmapi
)

if(MINGW)
    target_link_options(gw2_addon_profiler PRIVATE
        -static-libgcc -static-libstdc++
        -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()

set_target_properties(gw2_addon_profiler PROPERTIES
    PREFIX ""
    OUTPUT_NAME "gw2_addon_profiler"
    SUFFIX ".dll"
)
